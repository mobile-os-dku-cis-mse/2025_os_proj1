#include "msg.h"

int msgq = -1;
int log_fd = -1;

pcb_t pcbs[NCHILD];
queue_t runq;
queue_t waitq;

volatile sig_atomic_t tick_count = 0;
volatile sig_atomic_t pending_ticks = 0;

int current_idx = -1;
pid_t parent_pid;


void queue_init(queue_t *q) {
    q->head = q->tail = q->size = 0;
}

int queue_is_empty(queue_t *q) {
    return q->size == 0;
}

int queue_enqueue(queue_t *q, int idx) {
    if (q->size >= NCHILD) return -1;
    q->items[q->tail] = idx;
    q->tail = (q->tail + 1) % NCHILD;
    q->size++;
    return 0;
}

int queue_dequeue(queue_t *q) {
    if (q->size == 0) return -1;
    int idx = q->items[q->head];
    q->head = (q->head + 1) % NCHILD;
    q->size--;
    return idx;
}

void queue_remove(queue_t *q, int idx) {
    if (q->size == 0) return;
    int new_items[NCHILD];
    int new_size = 0;

    for (int i = 0; i < q->size; ++i) {
        int pos = (q->head + i) % NCHILD;
        if (q->items[pos] != idx) {
            new_items[new_size++] = q->items[pos];
        }
    }

    q->head = 0;
    q->tail = new_size;
    q->size = new_size;
    for (int i = 0; i < new_size; ++i)
        q->items[i] = new_items[i];
}

int find_pcb_index(pid_t pid) {
    for (int i = 0; i < NCHILD; ++i) {
        if (pcbs[i].pid == pid)
            return i;
    }
    return -1;
}

void log_line(const char *line) {
    if (log_fd < 0) return;
    size_t len = strlen(line);
    if (len == 0) return;
    write(log_fd, line, len);
}

void dump_queues() {
    char buffer[512];

    int offset = 0;
    offset += snprintf(buffer + offset, sizeof(buffer) - offset, "  run-queue: [");
    for (int i = 0; i < runq.size; ++i) {
        int pos = (runq.head + i) % NCHILD;
        int idx = runq.items[pos];
        offset += snprintf(buffer + offset, sizeof(buffer) - offset, "%d(pid=%d)%s", idx, pcbs[idx].pid, (i == runq.size - 1) ? "" : ", ");
    }
    offset += snprintf(buffer + offset, sizeof(buffer) - offset, "]\n");
    log_line(buffer);

    offset = 0;
    offset += snprintf(buffer + offset, sizeof(buffer) - offset, "  wait-queue: [");
    for (int i = 0; i < waitq.size; ++i) {
        int pos = (waitq.head + i) % NCHILD;
        int idx = waitq.items[pos];
        offset += snprintf(buffer + offset, sizeof(buffer) - offset, "%d(pid=%d, io=%d)%s",
            idx, pcbs[idx].pid, pcbs[idx].remaining_io, (i == waitq.size - 1) ? "" : ", ");
    }
    offset += snprintf(buffer + offset, sizeof(buffer) - offset, "]\n\n");
    log_line(buffer);
}

void timer_handler(int signo) {
    (void)signo;
    tick_count++;
    pending_ticks++;
}

void scheduler_tick() {
    for (int i = 0; i < waitq.size; ++i) {
        int pos = (waitq.head + i) % NCHILD;
        int idx = waitq.items[pos];
        if (pcbs[idx].remaining_io > 0)
            pcbs[idx].remaining_io--;
    }

    int finished[NCHILD];
    int finished_count = 0;

    for (int i = 0; i < waitq.size; ++i) {
        int pos = (waitq.head + i) % NCHILD;
        int idx = waitq.items[pos];
        if (pcbs[idx].remaining_io <= 0) {
            finished[finished_count++] = idx;
        }
    }

    for (int i = 0; i < finished_count; ++i) {
        int idx = finished[i];
        pcbs[idx].in_io = 0;
        pcbs[idx].remaining_io = 0;
        queue_remove(&waitq, idx);
        queue_enqueue(&runq, idx);
    }

    for (int i = 0; i < runq.size; ++i) {
        int pos = (runq.head + i) % NCHILD;
        int idx = runq.items[pos];
        pcbs[idx].waiting_time++;
    }

    if (current_idx != -1) {
        if (pcbs[current_idx].in_io) {
            current_idx = -1;
        } else {
            pcbs[current_idx].remaining_quantum--;
            if (pcbs[current_idx].remaining_quantum <= 0) {
                queue_enqueue(&runq, current_idx);
                current_idx = -1;
            }
        }
    }

    if (current_idx == -1 && !queue_is_empty(&runq)) {
        current_idx = queue_dequeue(&runq);
        pcbs[current_idx].remaining_quantum = TIME_QUANTUM;
    }

    if (current_idx != -1) {
        msgbuf_perso msg;
        memset(&msg, 0, sizeof(msg));
        msg.mtype = pcbs[current_idx].pid;
        msg.pid = pcbs[current_idx].pid;
        msg.io_time = 0;

        while (1) {
            if (msgsnd(msgq, &msg, MSGSZ, 0) == -1) {
                if (errno == EINTR) {
                    perror("msgsnd (CPU tick) EINTR");
                    continue;
                } 
                perror("msgsnd (CPU tick)");
                break;
            }
            break;
        }
    }
    while (1) {
        msgbuf_perso m;
        ssize_t ret = msgrcv(msgq, &m, MSGSZ, 1, IPC_NOWAIT);
        if (ret < 0) {
            if (errno == ENOMSG)
                break;
            if (errno == EINTR)
                continue;
            break;
        }

        int idx = find_pcb_index(m.pid);
        if (idx >= 0) {
            pcbs[idx].in_io = 1;
            pcbs[idx].remaining_io = m.io_time;

            if (idx == current_idx) {
                current_idx = -1;
            } else {
                queue_remove(&runq, idx);
            }
            queue_enqueue(&waitq, idx);
        }
    }

    if (tick_count <= MAX_TICKS_LOG) {
        char buffer[256];
        if (current_idx != -1) {
            snprintf(buffer, sizeof(buffer), "(time %d) process pid=%d gets cpu time, remaining time-quantum=%d\n",
                tick_count, pcbs[current_idx].pid, pcbs[current_idx].remaining_quantum);
        } else {
            snprintf(buffer, sizeof(buffer), "(time %d) CPU idle\n", tick_count);
        }
        log_line(buffer);
        dump_queues();
    }
}

void child_loop() {
     srand(getpid());

     int cpu_burst = rand() % 10 + 5;
     int io_burst = rand() % 20 + 5;
     fflush(stdout);
     while (1) {
         msgbuf_perso msg;

         if (msgrcv(msgq, &msg, MSGSZ, getpid(), 0) == -1) {
             perror("msgrcv child");
             exit(1);
         }
         cpu_burst--;

         if (cpu_burst <= 0) {
             fflush(stdout);
             msgbuf_perso reply;
             memset(&reply, 0, sizeof(reply));
             reply.mtype = 1;
             reply.pid = getpid();
             reply.io_time = io_burst;

             while (1) {
                 if (msgsnd(msgq, &reply, MSGSZ, 0) == -1) {
                     perror("msgsnd child IO");
                     if (errno == EINTR)
                         continue;
                     break;
                 }
                 break;
             }

             cpu_burst = rand() % 10 + 5;
             io_burst = rand() % 20 + 5;
             fflush(stdout);
         }
     }
}

void parent_loop() {
    log_fd = open(LOG_FILENAME, O_CREAT | O_TRUNC | O_WRONLY, 0644);
    if (log_fd < 0) {
        perror("open log file");
        exit(1);
    }

    queue_init(&runq);
    queue_init(&waitq);

    for (int i = 0; i < NCHILD; ++i) {
        pcbs[i].in_io = 0;
        pcbs[i].remaining_io = 0;
        pcbs[i].remaining_quantum = TIME_QUANTUM;
        pcbs[i].waiting_time = 0;
        queue_enqueue(&runq, i);
    }
    current_idx = -1;

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = timer_handler;
    sa.sa_flags = SA_RESTART;
    sigaction(SIGALRM, &sa, NULL);

    struct itimerval it;
    it.it_interval.tv_sec = 0;
    it.it_interval.tv_usec = TICK_USEC;
    it.it_value.tv_sec = 0;
    it.it_value.tv_usec = TICK_USEC;
    setitimer(ITIMER_REAL, &it, NULL);

    while (tick_count < MAX_TICKS_USAGE) {
        pause();

        while (pending_ticks > 0) {
            pending_ticks--;
            scheduler_tick();
        }
    }

    for (int i = 0; i < NCHILD; ++i) {
        kill(pcbs[i].pid, SIGTERM);
        printf("[SUB-PROCESS %d] Kill\n", pcbs[i].pid);
    }
    for (int i = 0; i < NCHILD; ++i) {
        wait(NULL);
    }

    msgctl(msgq, IPC_RMID, NULL);

    close(log_fd);
}


int main(int argc, char *argv[]) {
    (void)argc; (void)argv;

    parent_pid = getpid();

    key_t key = 0x12345;
    int old = msgget(key, 0666);
    if (old != -1) {
        msgctl(old, IPC_RMID, NULL);
    }
    msgq = msgget(key, IPC_CREAT | 0666);
    if (msgq < 0) {
        perror("msgget");
        exit(1);
    }

    for (int i = 0; i < NCHILD; ++i) {
        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            exit(1);
        } else if (pid == 0) {
            child_loop();
            exit(0);
        } else {
            pcbs[i].pid = pid;
            printf("[SUB-PROCESS %d] Create\n", pcbs[i].pid);
        }
    }
    usleep(1000);
    printf("[INFO] Running ...\n[INFO] For more information check log file : schedule_dump.txt\n");

    if (getpid() == parent_pid) {
        parent_loop();
    }

    printf("[INFO] End");
    return 0;
}