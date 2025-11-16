#ifndef PROCESS_H
#define PROCESS_H

#include <sys/types.h>

#define MAX_CHILD_PROCESSES 10

struct process {
    pid_t pid;
    int cpu_burst;
    int io_burst;
    int remaining_time;
    int state; // 0: ready, 1: running, 2: waiting, 3: terminated
};

void create_processes(struct process *proc_array, int num_processes);
void simulate_process(struct process *proc);

#endif // PROCESS_H