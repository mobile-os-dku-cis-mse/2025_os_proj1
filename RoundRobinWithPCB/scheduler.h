#ifndef INC_2025_OS_PROJ1_SCHEDULER_H
#define INC_2025_OS_PROJ1_SCHEDULER_H

#include <stdio.h>
#include <signal.h>
#include <stdbool.h>

#define NUM_CHILDREN        10

typedef enum {
    PROCESS_NEW,
    PROCESS_READY,
    PROCESS_RUNNING,
    PROCESS_WAITING,
    PROCESS_TERMINATED
} process_state;

#define MSG_TIME_SLICE      1
#define MSG_IO_REQUEST      2
#define MSG_TERMINATE       3

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

typedef struct {
    process_control_block *processes[NUM_CHILDREN];
    int front;
    int rear;
    int count;
} ready_queue;

typedef struct wait_node {
    process_control_block *pcb;
    struct wait_node *next;
} wait_node;

typedef struct {
    wait_node *head;
    wait_node *tail;
    int count;
} wait_queue;

typedef struct {
    long mtype;
    struct {
        int command;
        int value;
        pid_t sender_pid;
    } data;
} message;

#endif