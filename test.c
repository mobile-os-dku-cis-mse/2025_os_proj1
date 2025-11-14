#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <string.h>
#include <time.h>
#include <fcntl.h>

#define NUM_PROCESSES 10
#define TIME_QUANTUM 5
#define TIMER_TICK 10000
#define MAX_TICKS 10000
#define MSG_KEY 0x12345

struct msgbuf {
    long mtype;
    int pid;
    int io_time;
};

typedef struct {
    pid_t pid;
    int cpu_burst;
    int io_burst;
    int remaining_quantum;
    int waiting_time;
    int state;
} process_t;

typedef struct node {
    int index;
    struct node *next;
} node_t;

typedef struct {
    node_t *head;
    node_t *tail;
    int size;
} queue_t;

process_t processes[NUM_PROCESSES];
queue_t run_queue;
queue_t wait_queue;
int msgq_id;
volatile int current_tick = 0;
int current_process = -1;
int output_fd;
volatile sig_atomic_t running = 1;

void init_queue(queue_t *q) {
    q->head = NULL;
    q->tail = NULL;
    q->size = 0;
}

void enqueue(queue_t *q, int index) {
    node_t *new_node = (node_t *)malloc(sizeof(node_t));
    new_node->index = index;
    new_node->next = NULL;
    
    if (q->tail == NULL) {
        q->head = q->tail = new_node;
    } else {
        q->tail->next = new_node;
        q->tail = new_node;
    }
    q->size++;
}

int dequeue(queue_t *q) {
    if (q->head == NULL) return -1;
    
    node_t *temp = q->head;
    int index = temp->index;
    q->head = q->head->next;
    
    if (q->head == NULL) q->tail = NULL;
    
    free(temp);
    q->size--;
    return index;
}

void remove_from_queue(queue_t *q, int index) {
    if (q->head == NULL) return;
    
    if (q->head->index == index) {
        dequeue(q);
        return;
    }
    
    node_t *curr = q->head;
    while (curr->next != NULL) {
        if (curr->next->index == index) {
            node_t *temp = curr->next;
            curr->next = curr->next->next;
            if (temp == q->tail) q->tail = curr;
            free(temp);
            q->size--;
            return;
        }
        curr = curr->next;
    }
}

void dump_queue(char *buffer, queue_t *q) {
    node_t *curr = q->head;
    strcat(buffer, "[");
    while (curr != NULL) {
        char temp[32];
        sprintf(temp, "%d", processes[curr->index].pid);
        strcat(buffer, temp);
        if (curr->next != NULL) strcat(buffer, ",");
        curr = curr->next;
    }
    strcat(buffer, "]");
}

void log_output(const char *msg) {
    write(output_fd, msg, strlen(msg));
}

void child_process(int index) {
    struct msgbuf msg;
    memset(&msg, 0, sizeof(msg));
    
    srand(time(NULL) ^ (getpid() << 16));
    int cpu_burst = (rand() % 20) + 10;
    int io_burst = (rand() % 15) + 5;
    
    msg.mtype = 999;
    msg.pid = getpid();
    msg.io_time = io_burst;
    msgsnd(msgq_id, &msg, sizeof(msg) - sizeof(long), 0);
    
    while (1) {
        memset(&msg, 0, sizeof(msg));
        ssize_t ret = msgrcv(msgq_id, &msg, sizeof(msg) - sizeof(long), getpid(), 0);
        if (ret < 0) break;
        
        cpu_burst--;
        
        if (cpu_burst <= 0) {
            io_burst = (rand() % 15) + 5;
            memset(&msg, 0, sizeof(msg));
            msg.mtype = 1;
            msg.pid = getpid();
            msg.io_time = io_burst;
            msgsnd(msgq_id, &msg, sizeof(msg) - sizeof(long), 0);
            
            memset(&msg, 0, sizeof(msg));
            ret = msgrcv(msgq_id, &msg, sizeof(msg) - sizeof(long), getpid(), 0);
            if (ret < 0) break;
            
            cpu_burst = (rand() % 20) + 10;
        }
    }
    exit(0);
}

void schedule() {
    if (current_process != -1) {
        processes[current_process].remaining_quantum--;
        
        if (processes[current_process].remaining_quantum <= 0) {
            if (processes[current_process].state == 1) {
                enqueue(&run_queue, current_process);
                processes[current_process].remaining_quantum = TIME_QUANTUM;
            }
            current_process = -1;
        }
    }
    
    struct msgbuf msg;
    memset(&msg, 0, sizeof(msg));
    while (msgrcv(msgq_id, &msg, sizeof(msg) - sizeof(long), 1, IPC_NOWAIT) > 0) {
        for (int i = 0; i < NUM_PROCESSES; i++) {
            if (processes[i].pid == msg.pid && processes[i].state == 1) {
                processes[i].io_burst = msg.io_time;
                processes[i].state = 2;
                remove_from_queue(&run_queue, i);
                enqueue(&wait_queue, i);
                if (current_process == i) current_process = -1;
                break;
            }
        }
        memset(&msg, 0, sizeof(msg));
    }
    
    node_t *curr = wait_queue.head;
    node_t *prev = NULL;
    while (curr != NULL) {
        int idx = curr->index;
        processes[idx].io_burst--;
        
        if (processes[idx].io_burst <= 0) {
            node_t *next = curr->next;
            
            processes[idx].state = 1;
            processes[idx].remaining_quantum = TIME_QUANTUM;
            enqueue(&run_queue, idx);
            
            if (prev == NULL) {
                wait_queue.head = next;
            } else {
                prev->next = next;
            }
            
            if (curr == wait_queue.tail) wait_queue.tail = prev;
            
            free(curr);
            wait_queue.size--;
            curr = next;
            
            memset(&msg, 0, sizeof(msg));
            msg.mtype = processes[idx].pid;
            msg.pid = processes[idx].pid;
            msg.io_time = 0;
            msgsnd(msgq_id, &msg, sizeof(msg) - sizeof(long), IPC_NOWAIT);
        } else {
            prev = curr;
            curr = curr->next;
        }
    }
    
    if (current_process == -1 && run_queue.size > 0) {
        current_process = dequeue(&run_queue);
        processes[current_process].remaining_quantum = TIME_QUANTUM;
    }
    
    if (current_process != -1) {
        memset(&msg, 0, sizeof(msg));
        msg.mtype = processes[current_process].pid;
        msg.pid = processes[current_process].pid;
        msg.io_time = 0;
        msgsnd(msgq_id, &msg, sizeof(msg) - sizeof(long), IPC_NOWAIT);
        
        processes[current_process].cpu_burst--;
        
        if (current_tick <= MAX_TICKS) {
            char buffer[1024];
            memset(buffer, 0, sizeof(buffer));
            sprintf(buffer, "[%d] Process %d gets CPU, burst=%d, quantum=%d | RUN=", 
                    current_tick, 
                    processes[current_process].pid,
                    processes[current_process].cpu_burst,
                    processes[current_process].remaining_quantum);
            dump_queue(buffer, &run_queue);
            strcat(buffer, " WAIT=");
            dump_queue(buffer, &wait_queue);
            strcat(buffer, "\n");
            log_output(buffer);
        }
    }
    
    for (int i = 0; i < NUM_PROCESSES; i++) {
        if (processes[i].state == 1 && i != current_process) {
            processes[i].waiting_time++;
        }
    }
}

void timer_handler(int signo) {
    current_tick++;
    if (current_tick > MAX_TICKS + 100) {
        running = 0;
    }
    schedule();
}

int main() {
    printf("Starting scheduler...\n");
    
    output_fd = open("schedule_dump.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (output_fd < 0) {
        perror("open");
        exit(1);
    }
    
    msgq_id = msgget(MSG_KEY, IPC_CREAT | 0666);
    if (msgq_id < 0) {
        perror("msgget");
        exit(1);
    }
    
    msgctl(msgq_id, IPC_RMID, NULL);
    msgq_id = msgget(MSG_KEY, IPC_CREAT | 0666);
    
    init_queue(&run_queue);
    init_queue(&wait_queue);
    
    srand(time(NULL));
    
    printf("Creating %d child processes...\n", NUM_PROCESSES);
    for (int i = 0; i < NUM_PROCESSES; i++) {
        pid_t pid = fork();
        
        if (pid == 0) {
            child_process(i);
            exit(0);
        } else if (pid > 0) {
            processes[i].pid = pid;
            processes[i].state = 0;
            processes[i].remaining_quantum = TIME_QUANTUM;
            processes[i].waiting_time = 0;
            printf("Created child %d with PID %d\n", i, pid);
        } else {
            perror("fork");
            exit(1);
        }
    }
    
    usleep(50000);
    
    printf("Receiving initial messages...\n");
    for (int i = 0; i < NUM_PROCESSES; i++) {
        struct msgbuf msg;
        memset(&msg, 0, sizeof(msg));
        if (msgrcv(msgq_id, &msg, sizeof(msg) - sizeof(long), 999, 0) > 0) {
            for (int j = 0; j < NUM_PROCESSES; j++) {
                if (processes[j].pid == msg.pid) {
                    processes[j].cpu_burst = (rand() % 20) + 10;
                    processes[j].io_burst = msg.io_time;
                    processes[j].state = 1;
                    enqueue(&run_queue, j);
                    printf("Process %d initialized\n", msg.pid);
                    break;
                }
            }
        }
    }
    
    printf("Starting timer...\n");
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = timer_handler;
    sa.sa_flags = SA_RESTART;
    sigaction(SIGALRM, &sa, NULL);
    
    struct itimerval timer;
    timer.it_interval.tv_sec = 0;
    timer.it_interval.tv_usec = TIMER_TICK;
    timer.it_value.tv_sec = 0;
    timer.it_value.tv_usec = TIMER_TICK;
    setitimer(ITIMER_REAL, &timer, NULL);
    
    printf("Scheduler running ...\n");
    while (running) {
        pause();
    }
    
    printf("Stopping scheduler...\n");
    timer.it_interval.tv_sec = 0;
    timer.it_interval.tv_usec = 0;
    timer.it_value.tv_sec = 0;
    timer.it_value.tv_usec = 0;
    setitimer(ITIMER_REAL, &timer, NULL);
    
    for (int i = 0; i < NUM_PROCESSES; i++) {
        kill(processes[i].pid, SIGTERM);
    }
    
    usleep(100000);
    
    for (int i = 0; i < NUM_PROCESSES; i++) {
        kill(processes[i].pid, SIGKILL);
        waitpid(processes[i].pid, NULL, 0);
    }
    
    msgctl(msgq_id, IPC_RMID, NULL);
    close(output_fd);
    
    printf("Scheduler terminated. Check schedule_dump.txt\n");
    return 0;
}