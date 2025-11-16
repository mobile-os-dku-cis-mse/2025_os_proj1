// sched.c
#define _XOPEN_SOURCE 700
#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/msg.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include "msg.h"
#define LOGFILE "log.txt"
#define NUM_CHILD 10
#define MSG_KEY 0x100
#define TICK_USEC 10000

// ―――――――――― Child process handler ――――――――――
typedef struct proc{
    pid_t pid;        // Child process PID
    int rem_quantum;  // Remaining time quantum
    int cpu_burst;    // Remaining CPU burst
    int io_burst;     // Remaining I/O burst
    int in_wait;      // If child process is in run queue then 0, if child process is in wait queue then 1
    int waiting_time; // Waiting time in run queue (ticks)
}proc_t;

proc_t procs[NUM_CHILD];

// Run queue (circular queue)
int run_q[NUM_CHILD];
int run_head = 0, run_tail = 0, run_count = 0;

// Wait queue
int wait_q[NUM_CHILD];
int wait_count = 0;

// System V message queue ID
int msgq = -1;

// Run queue operations
void enqueue_run(int idx){
    if(run_count < NUM_CHILD){
        run_q[run_tail] = idx;
        run_tail = (run_tail + 1) % NUM_CHILD;
        run_count++;
    }
}

int dequeue_run(){
    if(run_count == 0){
        return -1;
    }
    int idx = run_q[run_head];
    run_head = (run_head + 1) % NUM_CHILD;
    run_count--;
    return idx;
}

// Remove child process from run queue by index
void remove_from_run_by_idx(int idx){
    if(run_count == 0){
        return;
    }
    int tmp[NUM_CHILD], t = 0;
    for(int i = 0; i < run_count; i++){
        int ii = (run_head + i) % NUM_CHILD;
        if(run_q[ii] != idx){
            tmp[t++] = run_q[ii];
        }
    }
    run_head = run_tail = run_count = 0;
    for(int i = 0; i < t; i++){
        enqueue_run(tmp[i]);
    }
}

// Wait queue operations
void enqueue_wait(int idx){
    if(wait_count < NUM_CHILD){
        wait_q[wait_count++] = idx;
        procs[idx].in_wait = 1;
    }
}

// Remove child process from wait queue by index
void remove_from_wait_by_pos(int pos){
    if(pos < 0 || pos >= wait_count){
        return;
    }
    for(int i = pos; i < wait_count - 1; i++){
        wait_q[i] = wait_q[i + 1];
    }
    wait_count--;
}

// ―――――――――― Timer interrupt handler ――――――――――
volatile sig_atomic_t tick_flag = 0;

void alarm_handler(int sig){
    (void)sig;
    tick_flag = 1;
}

// ―――――――――― Log handler ――――――――――
FILE *logfp = NULL;

// Log event format of log.txt file
void log_event(const char *fmt, ...){
    va_list ap;
    va_start(ap, fmt);
    if(logfp){
        vfprintf(logfp, fmt, ap);
        fflush(logfp);
    }
    va_end(ap);
}

// Dump run queue and wait queue state
void dump_queues(){
    fprintf(logfp, "ㆍRun queue (Index, Remaining CPU burst, Remaining time quantum)\n[");
    for(int i = 0; i < run_count; i++){
        int idx = run_q[(run_head + i) % NUM_CHILD];
        fprintf(logfp, "(%d, %d, %d)%s", idx , procs[idx].cpu_burst, procs[idx].rem_quantum, (i == run_count - 1) ? "" : ", ");
    }
    fprintf(logfp, "]\nㆍWait queue (Index, Remaining I/O burst, Remaining time quantum)\n[");
    for(int i = 0; i < wait_count; i++){
        int idx = wait_q[i];
        fprintf(logfp, "(%d, %d, %d)%s", idx, procs[idx].io_burst, procs[idx].rem_quantum, (i == wait_count - 1) ? "" : ", ");
    }
    fprintf(logfp, "]\n");
    fflush(logfp);
}

// ―――――――――― Main function ――――――――――
int main(int argc, char *argv[]){
    printf("―――――――――― Simple scheduling ――――――――――\n");

    // ―――――――――― Command line argument handling ――――――――――
    // Check command line argument
    if(argc < 2){
        fprintf(stderr, "―――――――――― sched usage ――――――――――\nㆍ./sched [time_quantum]\n");
        exit(1);
    }

    int quantum = atoi(argv[1]);
    if(quantum <= 0){
        fprintf(stderr, "―――――――――― sched usage ――――――――――\nㆍTime quantum must be positive integer\n");
        exit(1);
    }

    // ―――――――――― Child process handling ――――――――――
    // Create message queue
    msgq = msgget(MSG_KEY, IPC_CREAT | 0666);
    if(msgq == -1){
        perror("msgget");
        exit(1);
    }
    fprintf(stdout, "ㆍMessage queue id is %d\n", msgq);

    // Create child processes
    for(int i = 0; i < NUM_CHILD; i++){
        pid_t pid = fork();
        if(pid < 0){
            perror("fork");
            exit(1);
        }else if(pid == 0){
            char key_str[32], idx_str[8];
            snprintf(key_str, sizeof(key_str), "%d", MSG_KEY);
            snprintf(idx_str, sizeof(idx_str), "%d", i);
            execlp("./child", "./child", key_str, idx_str, NULL);
            perror("execlp child");
            _exit(1);
        }else{
            procs[i].pid = pid;
            procs[i].rem_quantum = quantum;
            procs[i].cpu_burst = 0;
            procs[i].io_burst = 0;
            procs[i].in_wait = 0;
            procs[i].waiting_time = 0;
            enqueue_run(i);

            // Receive CPU burst from child process
            struct msgbuf_s rcv;
            if(msgrcv(msgq, &rcv, sizeof(rcv) - sizeof(long), 1, 0) == -1){
                perror("msgrcv init");
                exit(1);
            }
            procs[i].cpu_burst = rcv.io_time;
        }
    }

    // ―――――――――― Timer interrupt handling ――――――――――
    // Setup timer interrupt
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = alarm_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if(sigaction(SIGALRM, &sa, NULL) == -1){
        perror("sigaction");
        exit(1);
    }

    struct itimerval it;
    it.it_interval.tv_sec = 0;
    it.it_interval.tv_usec = TICK_USEC;
    it.it_value = it.it_interval;
    if(setitimer(ITIMER_REAL, &it, NULL) == -1){
        perror("setitimer");
        exit(1);
    }

    // ―――――――――― Log handling ――――――――――
    // Open log file
    logfp = fopen(LOGFILE, "w");
    if(!logfp){
        perror("fopen log");
        exit(1);
    }

    int current_tick = 0, running_idx = -1, ticks_to_log_limit = 10000;
    time_t start_time = time(NULL);

    // ―――――――――― Scheduling loop ――――――――――
    while(1){
        while (!tick_flag){
            pause(); // Wait for next timer tick
        }
        tick_flag = 0;
        current_tick++;

        // Handle messages from children process by IO requests
        struct msgbuf_s rcv;
        ssize_t r;
        while(1){
            memset(&rcv, 0, sizeof(rcv));
            r = msgrcv(msgq, &rcv, sizeof(rcv) - sizeof(long), 1, IPC_NOWAIT);
            if(r == -1){
                if(errno == ENOMSG){
                    break;
                }else{
                    perror("msgrcv parent");
                    break;
                }
            }

            if(rcv.cmd == MSG_CMD_IOREQ){
                int idx = -1;
                for(int k = 0; k < NUM_CHILD; k++){
                    if(procs[k].pid == rcv.pid){
                        idx = k;
                        break;
                    }
                }

                if(idx == -1){
                    continue;
                }

                // Move child process to wait queue
                remove_from_run_by_idx(idx);
                procs[idx].io_burst = rcv.io_time;
                procs[idx].in_wait = 1;
                enqueue_wait(idx);

                // Log
                log_event("―――――――――― Time tick %d ――――――――――\nㆍProcess %d requests I/O and moved to wait-queue\n", current_tick, idx);
                dump_queues(current_tick);

                if(running_idx == idx){
                    running_idx = -1;
                }
            }
        }

        // Decrement I/O burst for waiting processes
        for(int i = 0; i < wait_count; i++){
            int idx = wait_q[i];
            if(procs[idx].io_burst > 0){
                procs[idx].io_burst--;
            }
        }

        // Move finished I/O back to run queue
        for(int i = wait_count - 1; i >= 0; i--){
            int idx = wait_q[i];
            if(procs[idx].io_burst <= 0){
                procs[idx].in_wait = 0;
                enqueue_run(idx);

                // Log
                log_event("―――――――――― Time tick %d ――――――――――\nㆍProcess %d finished I/O and moved to run-queue\n", current_tick, idx);
                remove_from_wait_by_pos(i);
                dump_queues(current_tick);
            }
        }

        // Increase waiting time for child processes in run queue
        for(int i = 0; i < run_count; i++){
            int idx = run_q[(run_head + i) % NUM_CHILD];
            if(idx != running_idx){
                procs[idx].waiting_time++;
            }
        }

        // Schedule next child process if CPU idle
        if(running_idx == -1){
            int next = dequeue_run();
            if(next != -1){
                running_idx = next;
                procs[running_idx].rem_quantum = quantum;
                struct msgbuf_s snd;
                memset(&snd, 0, sizeof(snd));
                snd.mtype = procs[running_idx].pid;
                snd.cmd = MSG_CMD_RUN;
                snd.pid = getpid();
                snd.cpu_decr = 0;
                if(msgsnd(msgq, &snd, sizeof(snd) - sizeof(long), 0) == -1){
                    perror("msgsnd run");
                }else{
                    // Log
                    log_event("―――――――――― Time tick %d ――――――――――\nㆍProcess %d gets CPU\n", current_tick, running_idx);
                    dump_queues(current_tick);
                }
            }
        }else{
            // Continue running current child process
            struct msgbuf_s snd;
            memset(&snd, 0, sizeof(snd));
            snd.mtype = procs[running_idx].pid;
            snd.cmd = MSG_CMD_RUN;
            snd.pid = getpid();
            snd.cpu_decr = 0;
            if(msgsnd(msgq, &snd, sizeof(snd) - sizeof(long), 0) == -1){
                perror("msgsnd run");
            }
        }

        // Decrease remaining quantum, check expiration
        if(running_idx != -1){
            procs[running_idx].rem_quantum--;
            if (procs[running_idx].rem_quantum <= 0){
                int idx = running_idx;
                enqueue_run(idx);

                // Log
                log_event("―――――――――― Time tick %d ――――――――――\nㆍProcess %d time quantum expired\n", current_tick, idx);
                dump_queues(current_tick);
                running_idx = -1;
            }
        }

        // Exit condition by time limit
        if(current_tick >= ticks_to_log_limit && (time(NULL) - start_time) >= 60){
            break;
        }
    }

    // Terminate children process
    for(int i = 0; i < NUM_CHILD; i++){
        kill(procs[i].pid, SIGTERM);

    }
    while(wait(NULL) > 0 || errno == EINTR){}

    if(msgctl(msgq, IPC_RMID, NULL) == -1){
        perror("msgctl remove");
    }

    if(logfp){
        fclose(logfp);
    }

    printf("ㆍSimple scheduling is finished. Log is in %s\n", LOGFILE);
    return 0;
}