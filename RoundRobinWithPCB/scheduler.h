#ifndef INC_2025_OS_PROJ1_SCHEDULER_H
#define INC_2025_OS_PROJ1_SCHEDULER_H

#include <stdio.h>
#include <signal.h>
#include <stdbool.h>

#define TIME_QUANTUM        3
#define TICK_INTERVAL_US    10000
#define MAX_TICKS           10000
#define MIN_CPU_BURST       5
#define MAX_CPU_BURST       20
#define MIN_IO_BURST        3
#define MAX_IO_BURST        15
#define NUM_CHILDREN        10

#define MSG_TIME_SLICE      1
#define MSG_IO_REQUEST      2
#define MSG_TERMINATE       3

#define LOG_FILE            "schedule_dump.txt"

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

typedef struct {
    process_control_block pcb_table[NUM_CHILDREN];

    ready_queue ready_queue;
    wait_queue wait_queue;

    process_control_block *current_cpu_process;

    int current_tick;
    int msgqid;
    bool running;
    FILE *log_file;

    int total_context_switches;
} scheduler;

void init_pcb_table(scheduler *scheduler);
process_control_block* find_pcb_by_pid(scheduler *scheduler, pid_t pid);
process_control_block* allocate_pcb(scheduler *scheduler);
void update_pcb_state(process_control_block *pcb, process_state new_state);

void init_ready_queue(ready_queue *queue);
bool is_ready_queue_empty(const ready_queue *queue);
bool is_ready_queue_empty(const ready_queue *queue);
bool is_ready_queue_full(const ready_queue *queue);
int enqueue_ready(ready_queue *queue, process_control_block *pcb);
process_control_block* dequeue_ready(ready_queue *queue);
void update_ready_queue_waiting_times(ready_queue *queue);
process_control_block* remove_from_ready_queue(ready_queue *queue, pid_t pid);

void init_wait_queue(wait_queue *queue);
bool is_wait_queue_empty(const wait_queue *queue);
int enqueue_wait(wait_queue *queue, process_control_block *pcb);
process_control_block* dequeue_wait(wait_queue *queue);
process_control_block* remove_from_wait_queue(wait_queue *queue, pid_t pid);
void update_wait_queue_io_bursts(wait_queue *queue, ready_queue *ready_queue);

void init_scheduler(scheduler *scheduler);
void cleanup_scheduler(scheduler *scheduler);
int generate_random_burst(int min, int max);
void schedule_next_process(scheduler *scheduler);
void handle_time_slice_complete(scheduler *scheduler);
void handle_io_request(scheduler *scheduler, const message *msg);
void process_timer_tick(scheduler *scheduler);
void create_child_processes(scheduler *sched);

int create_message_queue(void);
int send_time_slice(int msgqid, pid_t target_pid);
int send_terminate(int msgqid, pid_t target_pid);
int receive_io_request(int msgqid, message *msg);
int child_receive_message(int msgqid, pid_t my_pid, message *msg);
int child_send_io_request(int msgqid, int io_burst, pid_t my_pid);
int generate_random_burst(int min, int max);

void child_process_main(int msgqid, pid_t my_pid);

void open_log_file(scheduler *sched);
void close_log_file(scheduler *scheduler);
void log_scheduling_event(scheduler *scheduler);
void dump_ready_queue(FILE *fp, const ready_queue *queue);
void dump_wait_queue(FILE *fp, const wait_queue *queue);
void print_final_statistics(const scheduler *sched);

void setup_signal_handlers(void);
void timer_signal_handler(int signo);
void child_signal_handler(int signo);

extern volatile sig_atomic_t global_timeout;
extern scheduler global_scheduler;

#endif