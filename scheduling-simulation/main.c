#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <fcntl.h>
#include <string.h>
#include <time.h>
#include "ipc.h"
#include "msg.h"

#define NUM_CHILDREN 10
#define TIME_QUANTUM 100 // ms
#define MAX_TICKS 10000
#define MIN_SECONDS 60

typedef struct {
    pid_t pid;
    int cpu_burst;
    int io_burst;
    int remaining_cpu;
    int remaining_io;
    int state; // 0: ready, 1: running, 2: waiting
} PCB;

PCB run_queue[NUM_CHILDREN];
PCB wait_queue[NUM_CHILDREN];
int run_count = 0, wait_count = 0;
int curr_idx = 0;
int ticks = 0;
int fd_dump;
int msgq;
struct itimerval timer;
time_t start_time;

void print_queues(int t, pid_t pid, int cpu_left) {
    char buf[1024];
    int len = 0;
    len += sprintf(buf+len, "(at time %d, process %d gets cpu time, remaining cpu-burst %d) ", t, pid, cpu_left);
    len += sprintf(buf+len, "run-queue: [");
    for(int i=0;i<run_count;i++) len += sprintf(buf+len, "%d ", run_queue[i].pid);
    len += sprintf(buf+len, "] wait-queue: [");
    for(int i=0;i<wait_count;i++) len += sprintf(buf+len, "%d ", wait_queue[i].pid);
    len += sprintf(buf+len, "]\n");
    write(fd_dump, buf, len);
}

void move_to_wait_queue(int idx, int io_burst) {
    run_queue[idx].remaining_io = io_burst;
    wait_queue[wait_count++] = run_queue[idx];
    for(int i=idx;i<run_count-1;i++) run_queue[i]=run_queue[i+1];
    run_count--;
    if(curr_idx >= run_count) curr_idx = 0;
}

void move_to_run_queue(int idx, int cpu_burst, int io_burst) {
    wait_queue[idx].cpu_burst = cpu_burst;
    wait_queue[idx].io_burst = io_burst;
    wait_queue[idx].remaining_cpu = cpu_burst;
    wait_queue[idx].remaining_io = 0;
    run_queue[run_count++] = wait_queue[idx];
    for(int i=idx;i<wait_count-1;i++) wait_queue[i]=wait_queue[i+1];
    wait_count--;
}

void handle_io_requests() {
    struct msgbuf msg;
    // Non-blocking receive for IO requests (mtype=1)
    while (msgrcv(msgq, &msg, sizeof(msg)-sizeof(long), 1, IPC_NOWAIT) > 0) {
        // Find the process in run_queue and move to wait_queue
        for (int i = 0; i < run_count; i++) {
            if (run_queue[i].pid == msg.pid) {
                move_to_wait_queue(i, msg.io_time);
                break;
            }
        }
    }
}

void alarm_handler(int signo) {
    handle_io_requests();
    if(run_count == 0) return;
    ticks++;
    if(ticks > MAX_TICKS || (time(NULL)-start_time) >= MIN_SECONDS) {
        timer.it_value.tv_sec = 0; timer.it_value.tv_usec = 0;
        setitimer(ITIMER_REAL, &timer, NULL);
        return;
    }
    // Decrement IO burst for waiting processes
    for(int i=0;i<wait_count;i++) {
        if(--wait_queue[i].remaining_io <= 0) {
            // After IO, generate new bursts and move back to run_queue
            int cpu_burst = rand()%500+500;
            int io_burst = rand()%300+200;
            move_to_run_queue(i--, cpu_burst, io_burst);
        }
    }
    // Schedule next process in run_queue
    PCB *proc = &run_queue[curr_idx];
    struct msgbuf msg;
    msg.mtype = proc->pid;
    msg.pid = proc->pid;
    msg.cpu_time = (proc->remaining_cpu > TIME_QUANTUM) ? TIME_QUANTUM : proc->remaining_cpu;
    msg.io_time = 0;
    send_msg(msgq, &msg);
    print_queues(ticks, proc->pid, proc->remaining_cpu);
    proc->remaining_cpu -= msg.cpu_time;
    if(proc->remaining_cpu <= 0) {
        // Wait for IO request from child, will be handled in handle_io_requests
        curr_idx = (curr_idx+1)%run_count;
    } else {
        curr_idx = (curr_idx+1)%run_count;
    }
}

int main() {
    signal(SIGCHLD, SIG_IGN);
    struct sigaction sa;
    sa.sa_handler = alarm_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGALRM, &sa, NULL);

    fd_dump = open("schedule_dump.txt", O_WRONLY|O_CREAT|O_TRUNC, 0644);
    msgq = create_msg_queue(0x12345);

    // Create children and initialize run_queue
    for(int i=0;i<NUM_CHILDREN;i++) {
        int cpu_burst = rand()%500+500; // 500-999 ms
        int io_burst = rand()%300+200;  // 200-499 ms
        pid_t pid = fork();
        if(pid==0) {
            // Child process
            execl("./process", "process", NULL);
            exit(1);
        }
        run_queue[i].pid = pid;
        run_queue[i].cpu_burst = cpu_burst;
        run_queue[i].io_burst = io_burst;
        run_queue[i].remaining_cpu = cpu_burst;
        run_queue[i].remaining_io = 0;
        run_queue[i].state = 0;
    }
    run_count = NUM_CHILDREN;
    wait_count = 0;
    curr_idx = 0;
    ticks = 0;
    start_time = time(NULL);

    // Start periodic timer
    timer.it_interval.tv_sec = 0;
    timer.it_interval.tv_usec = TIME_QUANTUM*1000;
    timer.it_value.tv_sec = 0;
    timer.it_value.tv_usec = TIME_QUANTUM*1000;
    setitimer(ITIMER_REAL, &timer, NULL);

    // Wait for all children
    while(ticks <= MAX_TICKS && (time(NULL)-start_time) < MIN_SECONDS) {
        pause();
    }
    close(fd_dump);
    return 0;
}