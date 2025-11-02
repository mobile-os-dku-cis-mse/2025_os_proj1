#ifndef INC_2025_OS_PROJ1_SCHEDULER_H
#define INC_2025_OS_PROJ1_SCHEDULER_H

#include <signal.h>

typedef enum {
    PROCESS_NEW,
    PROCESS_READY,
    PROCESS_RUNNING,
    PROCESS_WAITING,
    PROCESS_TERMINATED
} process_state;

typedef struct {
    int pcb_index;
    pid_t pid;
    process_state state;

    int cpu_burst;
    int io_burst;
    int remaining_quantum;

    int waiting_time;
    int total_cpu_time;
    int total_io_time;
    int response_time;

    int arrival_time;
    int first_run_time;
    int completion_time;
} process_control_block;

#endif