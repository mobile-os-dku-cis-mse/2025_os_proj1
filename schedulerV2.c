#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <stdarg.h>
#include <sys/time.h>
#include <signal.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/types.h>
#include <time.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/wait.h>

#define NCHILDREN 20      // number of child processes to create 
#define TIME_QUANTUM 20       // in ticks 
#define TICK_USEC 10000      // 10 ms tick 
#define MAX_TICKS 10000      // print schedule for ticks 0..10000 
#define LOGFILE "schedule_dump_v2.txt"
#define PID2IDX_SIZE 65536   // mapping table size (fast path for usual PIDs) 

/* process states */
#define STATE_READY 0
#define STATE_RUNNING 1
#define STATE_WAITING 2

// message struct (SysV)
struct sched_msg {
    long reciever;     // message reciever 
    pid_t pid;      // sender's pid 
    int remaining;  // remaining cpu_burst (child->parent) 
    int io_time;    // io_burst when reporting WAITING (child->parent) 
    int state;      // child's state (child->parent)
};

// process bookkeeping struct
typedef struct {
    pid_t pid;              // actual OS pid 
    int state;              // state: READY, RUNNING, WAITING
    int cpu_remaining;      // remaining cpu burst 
    int io_remaining;       // remaining io burst when waiting 
    int quantum_rem;        // remaining quantum (in ticks) 
    long waiting_time;      // time spent in ready queue 
} proc_t;

// simple index queue
typedef struct {
    int arr[NCHILDREN];
    int head, tail, count;
} idx_queue_t;

// global variables
int current_running_idx = -1;  // index of currently running process; -1 if none
static proc_t procs[NCHILDREN]; // process bookkeeping array
static idx_queue_t runq, waitq; // ready and waiting queues
static int msgq = -1; // message queue id
static pid_t parent_pid; // parent pid
static volatile sig_atomic_t tick_counter = 0;      // global tick counter
static volatile sig_atomic_t tick_pending = 0;      // flag: tick happened
static int logfile_fd = -1;
static int pid_to_index[PID2IDX_SIZE];             // pid -> index mapping table

// SIGALRM handler
static void sigalrm_handler(int signo) {
    (void)signo;
    tick_counter++;
    tick_pending = 1;
}

// index queue functions
static void q_init(idx_queue_t *q) {
    q->head = q->tail = q->count = 0;
}
static int q_empty(idx_queue_t *q) {
    return q->count == 0;
}
static void q_push(idx_queue_t *q, int idx) {
    if (q->count >= NCHILDREN) return;
    q->arr[q->tail] = idx;
    q->tail = (q->tail + 1) % NCHILDREN;
    q->count++;
}
static int q_pop(idx_queue_t *q) {
    if (q_empty(q)) return -1;
    int v = q->arr[q->head];
    q->head = (q->head + 1) % NCHILDREN;
    q->count--;
    return v;
}
static void q_remove(idx_queue_t *q, int idx) {
    if (q_empty(q)) return;
    int tmp[NCHILDREN];
    int tc = 0;
    while (!q_empty(q)) {
        int v = q_pop(q);
        if (v != idx) tmp[tc++] = v;
    }
    for (int i = 0; i < tc; ++i) q_push(q, tmp[i]);
}
static void q_dump(idx_queue_t *q, char *buf, size_t bufsz) {
    size_t off = 0;
    off += snprintf(buf + off, bufsz - off, "[");
    for (int i = 0, pos = q->head; i < q->count; ++i) {
        int idx = q->arr[pos];
        off += snprintf(buf + off, bufsz - off, "%d", idx);
        pos = (pos + 1) % NCHILDREN;
        if (i < q->count - 1) off += snprintf(buf + off, bufsz - off, " ");
    }
    off += snprintf(buf + off, bufsz - off, "]");
}

//logging function
static void log_printf(const char *fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n > 0) {
        if (logfile_fd >= 0) {
            ssize_t wr = write(logfile_fd, buf, (size_t)n);
            (void)wr;
        } else {
            write(STDOUT_FILENO, buf, (size_t)n);
        }
    }
}

// random number gen in [a,b]
static int rnd_range(int a, int b) {
    return a + (rand() % (b - a + 1));
}

// cleanup function (message queue, logfile, child processes)
static void cleanup_and_exit(int code) {
    if (msgq >= 0) {
        msgctl(msgq, IPC_RMID, NULL);
        msgq = -1;
    }
    if (logfile_fd >= 0) close(logfile_fd);
    for (int i = 0; i < NCHILDREN; ++i) {
        if (procs[i].pid > 0) kill(procs[i].pid, SIGTERM);
    }
    while (wait(NULL) > 0) {}
    exit(code);
}


// collect child reports 
static void collect_child_reports(pid_t parentpid) {
    struct sched_msg m;
    ssize_t r;

    while (1) {
        //parent will receive messages with reciever == parent_pid
        memset(&m, 0, sizeof(m));
        r = msgrcv(msgq, &m, sizeof(m) - sizeof(long), parentpid, IPC_NOWAIT);
        if (r == -1) {
            if (errno == ENOMSG) break;
            else break;
        }

        pid_t child_pid = m.pid;
        int idx = -1;
        if ((unsigned)child_pid < PID2IDX_SIZE) idx = pid_to_index[child_pid];
        /*
        if (idx < 0 || idx >= NCHILDREN) {
            
            for (int i = 0; i < NCHILDREN; ++i)
                if (procs[i].pid == child_pid) { idx = i; break; }
            if (idx < 0 || idx >= NCHILDREN) continue;
        }
        */
        //update bookkeeping
        procs[idx].cpu_remaining = m.remaining;
        procs[idx].state = m.state;
        if (m.state == STATE_WAITING) {//remove from runq, add to waitq
            procs[idx].io_remaining = m.io_time;
            procs[idx].state = STATE_WAITING;
            q_remove(&runq, idx);
            q_push(&waitq, idx);
            if (current_running_idx == idx) current_running_idx = -1;
            procs[idx].quantum_rem = TIME_QUANTUM;
        } else if (m.state == STATE_RUNNING) {
             procs[idx].state = STATE_RUNNING;
        } else if (m.state == STATE_READY) {
            //become ready; ensure in runq (if not running)
            if (current_running_idx != idx) {
                int found = 0;
                for (int i = 0; i < runq.count; ++i) {
                    int ridx = runq.arr[(runq.head + i) % NCHILDREN];
                    if (ridx == idx) {
                        found = 1;
                        break;
                    }
                }
                if (!found)
                q_push(&runq, idx);
            }
            procs[idx].state = STATE_READY;
        }
    }
}

// child process main function
static void child_main(int msgq_id) {
    pid_t mypid = getpid();
    pid_t parpid = parent_pid;
    srand((unsigned)(time(NULL) ^ (mypid << 8)));

    int cpu_burst = 0;
    int io_burst = rnd_range(5, 15);

    struct sched_msg mrecv;
    while (1) {
        memset(&mrecv, 0, sizeof(mrecv));
        //wait for run message from parent
        if (msgrcv(msgq_id, &mrecv, sizeof(mrecv) - sizeof(long), mypid, 0) == -1) {
            if (errno == EINTR) continue;
            _exit(0);
        }
        //get assigned cpu_burst
        if (mrecv.remaining > 0 && cpu_burst == 0) cpu_burst = mrecv.remaining;
    
        // simulate "progress" by decrementing cpu_burst by 1 tick
        if (cpu_burst > 0) cpu_burst--;
        
        struct sched_msg resp;
        memset(&resp, 0, sizeof(resp));
        resp.reciever = parpid;
        resp.pid = mypid;
        resp.remaining = cpu_burst;

        if (cpu_burst <= 0) {
            // moved to waiting and set io_burst
            resp.state = STATE_WAITING;
            resp.io_time = io_burst;
            //generate io burst (won't use until IO done)
            io_burst = rnd_range(5, 15);
        } else {
            resp.state = STATE_RUNNING; // still has CPU burst left and ready to run later
            resp.io_time = 0;
        }
        //send back report to parent
        msgsnd(msgq_id, &resp, sizeof(resp) - sizeof(long), 0);
    }
    _exit(0);
}
// parent process main function
int main(int argc, char **argv) {
    parent_pid = getpid();
    srand((unsigned)time(NULL) ^ parent_pid);

    // open logfile
    logfile_fd = open(LOGFILE, O_CREAT | O_TRUNC | O_WRONLY, 0644);
    if (logfile_fd < 0) {
        perror("open logfile");
        logfile_fd = -1;
    }

    // message queue
    key_t key = 0x12345;
    msgq = msgget(key, IPC_CREAT | 0666);
    if (msgq == -1) {
        perror("msgget");
        cleanup_and_exit(1);
    }

    // initialize pid->index mapping & queues
    for (int i = 0; i < PID2IDX_SIZE; ++i) pid_to_index[i] = -1;
    q_init(&runq);
    q_init(&waitq);

    // create child processes
    for (int i = 0; i < NCHILDREN; ++i) {
        pid_t c = fork();
        if (c < 0) {
            perror("fork");
            cleanup_and_exit(1);
        } else if (c == 0) {
            child_main(msgq);
            _exit(0);
        } else {
            // parent bookkeeping
            procs[i].pid = c;
            procs[i].state = STATE_READY;
            procs[i].cpu_remaining = rnd_range(5, 20);
            procs[i].io_remaining = 0;
            procs[i].quantum_rem = TIME_QUANTUM;
            procs[i].waiting_time = 0;
            //map pid to index
            if (c < PID2IDX_SIZE) pid_to_index[c] = i;
            q_push(&runq, i);
        }
    }

    // setup SIGALRM handler
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sigalrm_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0; 
    if (sigaction(SIGALRM, &sa, NULL) == -1) {
        perror("sigaction");
        cleanup_and_exit(1);
    }

    // setup periodic timer for SIGALRM
    struct itimerval itv;
    itv.it_interval.tv_sec = 0;
    itv.it_interval.tv_usec = TICK_USEC;
    itv.it_value.tv_sec = 0;
    itv.it_value.tv_usec = TICK_USEC;
    if (setitimer(ITIMER_REAL, &itv, NULL) == -1) {
        perror("setitimer");
        cleanup_and_exit(1);
    }

    //scheduling loop
    //int current_running_idx = -1; //index of currently running process; -1 if none
    int max_ticks = MAX_TICKS;

    // block SIGALRM initially
    sigset_t mask, oldmask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGALRM);
    sigprocmask(SIG_BLOCK, &mask, &oldmask);

    // main loop
    while (tick_counter < max_ticks) {
        tick_pending = 0;
        sigsuspend(&oldmask); // wait for next tick
        collect_child_reports(parent_pid); // collect any new child reports

        // IO wait queue update
        if (!q_empty(&waitq)) {
            for (int i=0, pos=waitq.head, steps=waitq.count; i<steps; ++i) {
                int idx = waitq.arr[pos];
                procs[idx].io_remaining--;
                if (procs[idx].io_remaining <= 0) {
                    // move to ready and assign new cpu burst
                    procs[idx].cpu_remaining = rnd_range(5, 20);
                    procs[idx].state = STATE_READY;
                }
                pos = (pos + 1) % NCHILDREN;
            }
            // rebuild waitq and move io-finished ones back to runq
            int tmp[NCHILDREN]; 
            int tmpc = 0;
            while (!q_empty(&waitq)) {
                int idx = q_pop(&waitq);
                if (procs[idx].io_remaining <= 0) {
                    // if finished IO, move to runq
                    q_push(&runq, idx);
                    procs[idx].state = STATE_READY;
                    procs[idx].quantum_rem = TIME_QUANTUM;
                } else {
                    tmp[tmpc++] = idx; // still waiting
                }
            }
            for (int i=0;i<tmpc;i++) q_push(&waitq, tmp[i]);
        }

        
        // increment waiting time for all in runq
        if (!q_empty(&runq)) {
            for (int i = 0, pos = runq.head; i < runq.count; ++i) {
                int idx = runq.arr[pos];
                procs[idx].waiting_time++;
                pos = (pos + 1) % NCHILDREN;
            }
        }

        // preemption check
        if (current_running_idx >= 0) {
            if (procs[current_running_idx].cpu_remaining <= 0) {
                current_running_idx = -1; // running process finished cpu burst
            } else {
                procs[current_running_idx].quantum_rem--;
                if (procs[current_running_idx].quantum_rem <= 0) {
                    procs[current_running_idx].quantum_rem = TIME_QUANTUM;
                    procs[current_running_idx].state = STATE_READY;
                    q_push(&runq, current_running_idx);
                    current_running_idx = -1;
                } // process preempted
            }
        }

        // pick next process if none running
        if (current_running_idx < 0 && !q_empty(&runq)) {
            current_running_idx = q_pop(&runq);
            procs[current_running_idx].state = STATE_RUNNING;
        }

        // send run message to current running process
        if (current_running_idx >= 0) {
            pid_t child_pid = procs[current_running_idx].pid;
            struct sched_msg m;
            memset(&m, 0, sizeof(m));
            m.reciever = child_pid;
            m.pid = child_pid;
            m.remaining = procs[current_running_idx].cpu_remaining;
            m.io_time = 0;
            m.state = STATE_RUNNING;
            if (msgsnd(msgq, &m, sizeof(m) - sizeof(long), 0) == -1) {
                //error handling
            }
        }

        // logging
        char rqbuf[256], wqbuf[256];
        q_dump(&runq, rqbuf, sizeof(rqbuf));
        {
            size_t off = 0;
            off += snprintf(wqbuf + off, sizeof(wqbuf) - off, "[");
            for (int i = 0, pos = waitq.head; i < waitq.count; ++i) {
                int idx = waitq.arr[pos];
                off += snprintf(wqbuf + off, sizeof(wqbuf) - off, "%d(io=%d)", idx, procs[idx].io_remaining);
                pos = (pos + 1) % NCHILDREN;
                if (i < waitq.count - 1) off += snprintf(wqbuf + off, sizeof(wqbuf) - off, " ");
            }
            off += snprintf(wqbuf + off, sizeof(wqbuf) - off, "]");
        }

        int rem = -1;
        //pid_t scheduled_pid = 0;
        if (current_running_idx >= 0) {
            rem = procs[current_running_idx].cpu_remaining;
            //scheduled_pid = procs[current_running_idx].pid;
            log_printf("(at time %d, process idx=%d , remaining cpu-burst=%d) run-queue %s, wait-queue %s\n",
                       tick_counter, current_running_idx, rem, rqbuf, wqbuf);
        } else {
            log_printf("(at time %d, no process scheduled) run-queue %s, wait-queue %s\n",
                       tick_counter, rqbuf, wqbuf);
        }
        // collect any new child reports after scheduling actions
        collect_child_reports(parent_pid);
    }

    //log prints
    log_printf("Simulation finished at tick %d\n", tick_counter);
    for (int i = 0; i < NCHILDREN; ++i) {
        log_printf("Process idx=%d pid=%d total waiting_time=%ld\n",
                   i, procs[i].pid, procs[i].waiting_time);
    }

    cleanup_and_exit(0);
    return 0;
}
