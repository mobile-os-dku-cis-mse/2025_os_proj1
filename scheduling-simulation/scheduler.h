#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <sys/types.h>

#define MAX_PROCESSES 10
#define TIME_QUANTUM 100 // Time quantum in milliseconds

typedef struct {
    pid_t pid;          // Process ID
    int remaining_time; // Remaining time for the process
    int waiting_time;   // Waiting time for the process
} Process;

void initialize_processes(Process processes[], int num_processes);
void round_robin_scheduling(Process processes[], int num_processes);
void update_waiting_time(Process processes[], int num_processes, int time_slice);

#endif // SCHEDULER_H