/* os_proj1_rr_perfect.c
 *
 * Round-Robin Scheduling Simulation - Final / Submission-ready
 *
 * Requirements implemented:
 * - Parent creates 10 children
 * - Parent uses setitimer() for periodic ticks (SIGALRM)
 * - Parent schedules children in Round-Robin (configurable time quantum)
 * - Parent / child IPC via System V message queue (msgget, msgsnd, msgrcv)
 * - Children simulate CPU bursts and IO bursts; send IO request when CPU burst completes
 * - Parent maintains run-queue and wait-queue, updates IO timers each tick
 * - Logs scheduling events to schedule_dump.txt for ticks 0..MAX_TICKS
 * - Ensures simulation runs at least MIN_RUNTIME_SECONDS
 *
 * Build:
 *   gcc -O2 -std=c11 -Wall os_proj1_rr_perfect.c -o os_proj1_rr_perfect
 *
 * Run:
 *   ./os_proj1_rr_perfect
 *
 * Optional flags:
 *   -q <quantum_ticks>      (default 5)
 *   -u <tick_usec>          (tick in microseconds, default 100000 (0.1s))
 *   -m <max_ticks>          (default 10000)
 *   -s <min_seconds>        (min runtime seconds, default 60)
 *   -h                      (help)
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/time.h>
#include <signal.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <sys/wait.h>
#include <stdatomic.h>

#define NUM_CHILDREN 10
#define MSG_KEY 0x1234
#define LOG_FILENAME "schedule_dump.txt"

#define CPU_BURST_MIN 2
#define CPU_BURST_MAX 20
#define IO_BURST_MIN 5
#define IO_BURST_MAX 50

volatile sig_atomic_t tick_flag = 0;
volatile sig_atomic_t stop_flag = 0;

static void sigalrm_handler(int sig) { (void)sig; tick_flag = 1; }
static void sigint_handler(int sig) { (void)sig; stop_flag = 1; }

/* parent -> child grant message (mtype = child's pid) */
struct msg_parent_child {
    long mtype;   // child's pid as type
    int grant;    // 1 = grant, 0 = no-op
    int ticks;    // number of ticks granted (we use 1 typically)
};

/* child -> parent message (mtype = 1) */
struct msg_child_parent {
    long mtype;   // 1
    pid_t pid;    // child's pid
    int io_burst; // >0 when requesting IO; 0 otherwise
    int cpu_remaining; // child's remaining cpu-burst (reported at IO request)
};

typedef enum { STATE_READY, STATE_RUNNING, STATE_WAITING, STATE_TERMINATED } state_t;

typedef struct {
    pid_t pid;
    int init_cpu_burst;     // the CPU burst value assigned after last IO (in ticks)
    int cpu_burst_reported; // last value reported by child at IO request
    int grants_since_report; // parent-count: grants sent since last child report
    int io_burst_remaining;  // remaining io burst (in ticks) while waiting
    int rem_quantum;         // remaining quantum ticks for current run
    state_t state;
    int total_waiting;       // waiting time in ticks
} proc_t;

/* Run-queue and wait-queue helpers (stores indices 0..NUM_CHILDREN-1) */
typedef struct {
    int arr[NUM_CHILDREN + 5];
    int head, tail;
} q_t;

static void q_init(q_t *q) { q->head = q->tail = 0; }
static int q_empty(q_t *q) { return q->head == q->tail; }
static int q_size(q_t *q) { return q->tail - q->head; }
static void q_push(q_t *q, int v) { q->arr[q->tail++] = v; }
static int q_pop(q_t *q) { if (q_empty(q)) return -1; return q->arr[q->head++]; }
/* remove first occurrence; returns 1 if removed, 0 otherwise */
static int q_remove(q_t *q, int v) {
    int found = 0;
    int out[NUM_CHILDREN+5], oi=0;
    for (int i=q->head;i<q->tail;i++){
        if (!found && q->arr[i] == v) { found = 1; continue; }
        out[oi++] = q->arr[i];
    }
    if (!found) return 0;
    q->head = 0; q->tail = 0;
    for (int i=0;i<oi;i++) q->arr[q->tail++] = out[i];
    return 1;
}

static int randint_range(int a, int b) { return a + rand() % (b - a + 1); }

int main(int argc, char **argv) {
    /* default parameters */
    int TIME_QUANTUM_TICKS = 5;
    int TICK_USEC = 100000;
    int MAX_TICKS = 10000;
    int MIN_RUNTIME_SECONDS = 60;

    int opt;
    while ((opt = getopt(argc, argv, "q:u:m:s:h")) != -1) {
        switch(opt) {
            case 'q': TIME_QUANTUM_TICKS = atoi(optarg); break;
            case 'u': TICK_USEC = atoi(optarg); break;
            case 'm': MAX_TICKS = atoi(optarg); break;
            case 's': MIN_RUNTIME_SECONDS = atoi(optarg); break;
            case 'h':
            default:
                printf("Usage: %s [-q quantum_ticks] [-u tick_usec] [-m max_ticks] [-s min_seconds]\n", argv[0]);
                return 0;
        }
    }

    srand((unsigned)time(NULL) ^ getpid());

    signal(SIGALRM, sigalrm_handler);
    signal(SIGINT, sigint_handler);

    /* create message queue */
    int msqid = msgget(MSG_KEY, IPC_CREAT | 0666);
    if (msqid < 0) { perror("msgget"); exit(1); }

    /* open log file */
    int logfd = open(LOG_FILENAME, O_CREAT | O_TRUNC | O_WRONLY, 0644);
    if (logfd < 0) { perror("open log"); msgctl(msqid, IPC_RMID, NULL); exit(1); }

    proc_t procs[NUM_CHILDREN];
    pid_t children[NUM_CHILDREN];

    /* spawn children */
    for (int i=0;i<NUM_CHILDREN;i++) {
        pid_t pid = fork();
        if (pid < 0) { perror("fork"); exit(1); }
        if (pid == 0) {
            /* child process */
            struct msg_parent_child grant;
            struct msg_child_parent report;
            int cpu_burst = randint_range(CPU_BURST_MIN, CPU_BURST_MAX);
            int io_burst = randint_range(IO_BURST_MIN, IO_BURST_MAX);

            // child main loop: wait for grants (mtype = my pid)
            while (1) {
                ssize_t r = msgrcv(msqid, &grant, sizeof(grant) - sizeof(long), (long)getpid(), 0);
                if (r < 0) {
                    if (errno == EINTR) continue;
                    perror("child msgrcv");
                    break;
                }
                if (grant.grant == 1) {
                    int ticks = grant.ticks > 0 ? grant.ticks : 1;
                    for (int t=0;t<ticks;t++) {
                        // consume one tick of CPU
                        cpu_burst -= 1;
                        // simulate small work (not busy loop to avoid hogging)
                        // usleep(TICK_USEC/10);
                        if (cpu_burst <= 0) {
                            // send IO request to parent with io_burst and cpu_remaining=0
                            report.mtype = 1;
                            report.pid = getpid();
                            report.io_burst = io_burst;
                            report.cpu_remaining = 0;
                            if (msgsnd(msqid, &report, sizeof(report) - sizeof(long), 0) < 0) {
                                perror("child msgsnd io");
                            }
                            // after requesting IO, regenerate next CPU & IO bursts and block until parent schedules again
                            cpu_burst = randint_range(CPU_BURST_MIN, CPU_BURST_MAX);
                            io_burst = randint_range(IO_BURST_MIN, IO_BURST_MAX);
                            break; // stop consuming rest of granted ticks after IO request
                        }
                    }
                } else {
                    // grant==0 : ignore
                }
            }
            _exit(0);
        } else {
            /* parent stores child info */
            children[i] = pid;
            procs[i].pid = pid;
            procs[i].init_cpu_burst = randint_range(CPU_BURST_MIN, CPU_BURST_MAX);
            procs[i].cpu_burst_reported = procs[i].init_cpu_burst; // last reported known value
            procs[i].grants_since_report = 0;
            procs[i].io_burst_remaining = 0;
            procs[i].rem_quantum = 0;
            procs[i].state = STATE_READY;
            procs[i].total_waiting = 0;
        }
    }

    q_t runq, waitq;
    q_init(&runq); q_init(&waitq);
    for (int i=0;i<NUM_CHILDREN;i++) q_push(&runq, i);

    /* setup periodic timer */
    struct itimerval it;
    it.it_interval.tv_sec = 0;
    it.it_interval.tv_usec = TICK_USEC;
    it.it_value = it.it_interval;
    if (setitimer(ITIMER_REAL, &it, NULL) < 0) { perror("setitimer"); msgctl(msqid, IPC_RMID, NULL); close(logfd); exit(1); }

    char linebuf[1024];
    int tick = 0;
    int running_idx = -1;
    time_t start_time = time(NULL);

    struct msg_child_parent child_msg;
    struct msg_parent_child parent_msg;

    /* main loop */
    while (1) {
        if (stop_flag) break;
        if (tick >= MAX_TICKS && difftime(time(NULL), start_time) >= MIN_RUNTIME_SECONDS) break;

        if (!tick_flag) { usleep(1000); continue; }
        tick_flag = 0;
        tick++;

        /* handle child->parent messages (non-blocking) */
        while (1) {
            ssize_t r = msgrcv(msqid, &child_msg, sizeof(child_msg) - sizeof(long), 1L, IPC_NOWAIT);
            if (r < 0) {
                if (errno == ENOMSG) break;
                if (errno == EINTR) continue;
                perror("parent msgrcv child");
                break;
            }
            /* find child index */
            int idx = -1;
            for (int i=0;i<NUM_CHILDREN;i++) if (procs[i].pid == child_msg.pid) { idx = i; break; }
            if (idx < 0) continue;

            if (child_msg.io_burst > 0) {
                /* child requested IO: update parent knowledge */
                procs[idx].cpu_burst_reported = child_msg.cpu_remaining; // child reported remaining (here 0)
                procs[idx].grants_since_report = 0;
                procs[idx].io_burst_remaining = child_msg.io_burst;
                procs[idx].state = STATE_WAITING;
                procs[idx].rem_quantum = 0;
                /* remove from run-queue (if present) */
                q_remove(&runq, idx);
                /* ensure in wait-queue once */
                if (!q_remove(&waitq, idx)) q_push(&waitq, idx); else q_push(&waitq, idx);
                /* if it was running, clear running_idx */
                if (running_idx == idx) running_idx = -1;
                /* log */
                int len = snprintf(linebuf, sizeof(linebuf), "(at time %d) pid %d requests IO, io_burst=%d\n", tick, procs[idx].pid, procs[idx].io_burst_remaining);
                write(logfd, linebuf, len);
            }
        }

        /* decrement io bursts in wait-queue and move to run-queue when done */
        q_t new_wait; q_init(&new_wait);
        for (int i = waitq.head; i < waitq.tail; ++i) {
            int idx = waitq.arr[i];
            if (procs[idx].state != STATE_WAITING) continue;
            procs[idx].io_burst_remaining -= 1;
            if (procs[idx].io_burst_remaining <= 0) {
                procs[idx].io_burst_remaining = 0;
                procs[idx].state = STATE_READY;
                /* regenerate a cpu burst knowledge: when IO completes, assign a new cpu burst value consistent with child (we let parent set init randomly) */
                procs[idx].init_cpu_burst = randint_range(CPU_BURST_MIN, CPU_BURST_MAX);
                procs[idx].cpu_burst_reported = procs[idx].init_cpu_burst;
                procs[idx].grants_since_report = 0;
                q_push(&runq, idx);
                int len = snprintf(linebuf, sizeof(linebuf), "(at time %d) pid %d IO complete -> run-queue\n", tick, procs[idx].pid);
                write(logfd, linebuf, len);
            } else {
                q_push(&new_wait, idx);
            }
        }
        waitq = new_wait;

        /* if no running process, pick next from run-queue */
        if (running_idx == -1) {
            while (!q_empty(&runq)) {
                int idx = q_pop(&runq);
                if (procs[idx].state == STATE_READY) {
                    running_idx = idx;
                    procs[idx].state = STATE_RUNNING;
                    procs[idx].rem_quantum = TIME_QUANTUM_TICKS;
                    procs[idx].grants_since_report = 0; // start counting this quantum
                    break;
                }
            }
        }

        /* if have a running process, grant 1 tick */
        if (running_idx != -1) {
            parent_msg.mtype = (long)procs[running_idx].pid;
            parent_msg.grant = 1;
            parent_msg.ticks = 1;
            if (msgsnd(msqid, &parent_msg, sizeof(parent_msg) - sizeof(long), 0) < 0) {
                int len = snprintf(linebuf, sizeof(linebuf), "(at time %d) msgsnd to pid %d failed: %s\n", tick, procs[running_idx].pid, strerror(errno));
                write(logfd, linebuf, len);
            } else {
                procs[running_idx].grants_since_report += 1;
                procs[running_idx].rem_quantum -= 1;
                /* compute parent's view of remaining cpu-burst: */
                int parent_remaining = procs[running_idx].cpu_burst_reported - procs[running_idx].grants_since_report;
                if (parent_remaining < 0) parent_remaining = 0;
                /* build run-queue and wait-queue dumps as PIDs */
                char run_dump[256] = {0}, wait_dump[256] = {0};
                int pos = 0;
                for (int i=runq.head;i<runq.tail;i++) pos += snprintf(run_dump + pos, sizeof(run_dump)-pos, "%d ", procs[runq.arr[i]].pid);
                pos = 0;
                for (int i=waitq.head;i<waitq.tail;i++) pos += snprintf(wait_dump + pos, sizeof(wait_dump)-pos, "%d ", procs[waitq.arr[i]].pid);
                int len = snprintf(linebuf, sizeof(linebuf),
                    "(at time %d) proc %d gets cpu time, remaining cpu-burst=%d; run-queue: [%s]; wait-queue: [%s]\n",
                    tick, procs[running_idx].pid, parent_remaining, run_dump, wait_dump);
                write(logfd, linebuf, len);
            }

            /* if quantum expired, preempt and put back to run-queue (if still ready) */
            if (procs[running_idx].rem_quantum <= 0) {
                if (procs[running_idx].state == STATE_RUNNING) {
                    procs[running_idx].state = STATE_READY;
                    q_push(&runq, running_idx);
                }
                running_idx = -1;
            }
        } else {
            /* CPU idle log */
            int len = snprintf(linebuf, sizeof(linebuf), "(at time %d) CPU idle; run-queue size=%d wait-queue size=%d\n",
                               tick, q_size(&runq), q_size(&waitq));
            write(logfd, linebuf, len);
        }

        /* increment waiting counters */
        for (int i=0;i<NUM_CHILDREN;i++) if (procs[i].state == STATE_READY) procs[i].total_waiting += 1;
    } /* main loop */

    /* stop timer */
    it.it_interval.tv_sec = it.it_interval.tv_usec = 0;
    it.it_value.tv_sec = it.it_value.tv_usec = 0;
    setitimer(ITIMER_REAL, &it, NULL);

    dprintf(logfd, "Simulation finished at tick %d\n", tick);

    /* cleanup: kill children and wait */
    for (int i=0;i<NUM_CHILDREN;i++) kill(children[i], SIGTERM);
    for (int i=0;i<NUM_CHILDREN;i++) waitpid(children[i], NULL, 0);

    /* remove msg queue */
    if (msgctl(msqid, IPC_RMID, NULL) < 0) perror("msgctl(IPC_RMID)");

    close(logfd);
    return 0;
}
