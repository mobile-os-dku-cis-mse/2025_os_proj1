#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/time.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>

#define MAX_CHILD 10

enum {
    CMD_TICK = 1,
    CMD_START = 2,
    CMD_WAKE = 3,
    CMD_TERMINATE = 4
};

struct buffmsg {
    long mtype;
    int cmd;
    pid_t pid;
    int cpu;
    int io;
};

volatile sig_atomic_t tick_flag = 0;
volatile sig_atomic_t tick_count = 0;

void tick_handler(int sig) {
    (void)sig;
    tick_flag = 1;
    tick_count++;
}

typedef struct {
    pid_t buf[32];
    int head, tail, size;
} runq_t;

void runq_init(runq_t *q) { q->head = q->tail = q->size = 0; }
int runq_empty(runq_t *q) { return q->size == 0; }

void runq_push(runq_t *q, pid_t p) {
    q->buf[q->tail] = p;
    q->tail = (q->tail + 1) % 32;
    q->size++;
}

pid_t runq_pop(runq_t *q) {
    if (q->size == 0) return -1;
    pid_t p = q->buf[q->head];
    q->head = (q->head + 1) % 32;
    q->size--;
    return p;
}

void runq_dump(runq_t *q, char *out, size_t n) {
    size_t pos = 0;
    pos += snprintf(out+pos, n-pos, "[");
    for (int i = 0; i < q->size; i++) {
        int idx = (q->head + i) % 32;
        pos += snprintf(out+pos, n-pos, "%d%s",
            q->buf[idx], (i+1==q->size?"":" "));
    }
    snprintf(out+pos, n-pos, "]");
}

typedef struct {
    pid_t pid;
    int remaining_io;
} wq_entry;

int main() {
    int tick_usecs = 10000;       
    int quantum_ticks = 50;       
    int max_ticks = 10000;        
    key_t msg_key = 0x12345;
    int msgid = msgget(msg_key, IPC_CREAT | 0666);

    int logfd = open("schedule_dump.txt", O_CREAT | O_WRONLY | O_TRUNC, 0644);

    struct sigaction sa;
    memset(&sa,0,sizeof(sa));
    sa.sa_handler = tick_handler;
    sigaction(SIGALRM,&sa,NULL);

    struct itimerval itv;
    itv.it_interval.tv_sec = 0;
    itv.it_interval.tv_usec = tick_usecs;
    itv.it_value = itv.it_interval;
    setitimer(ITIMER_REAL,&itv,NULL);

    srand(time(NULL) ^ getpid());

    pid_t child[MAX_CHILD];
    int idx_by_pid[65536];
    memset(idx_by_pid,-1,sizeof(idx_by_pid));

    int cpu_rem[MAX_CHILD];
    int io_next[MAX_CHILD];
    int in_wait[MAX_CHILD];
    int quantum[MAX_CHILD];

    for (int i=0; i<MAX_CHILD; i++) {
        pid_t pid = fork();
        if (pid < 0) { perror("fork"); exit(1); }

        if (pid == 0) {
            pid_t mypid = getpid();
            struct buffmsg msg;

            msgrcv(msgid,&msg,sizeof(msg)-sizeof(long),mypid,0);
            int cpu = msg.cpu;
            int io  = msg.io;

            while (1) {
                msgrcv(msgid,&msg,sizeof(msg)-sizeof(long),mypid,0);

                if (msg.cmd == CMD_TICK) {
                    if (cpu > 0) cpu--;

                    if (cpu == 0) {
                        struct buffmsg out = {0};
                        out.mtype = 1;
                        out.cmd = 0;        
                        out.pid = mypid;
                        out.io  = io;
                        msgsnd(msgid,&out,sizeof(out)-sizeof(long),0);

                        while (1) {
                            msgrcv(msgid,&msg,sizeof(msg)-sizeof(long),mypid,0);
                            if (msg.cmd == CMD_WAKE) {
                                cpu = msg.cpu;
                                io  = msg.io;
                                break;
                            }
                            if (msg.cmd == CMD_TERMINATE)
                                _exit(0);
                        }
                    }
                }
                else if (msg.cmd == CMD_TERMINATE) {
                    _exit(0);
                }
            }
            _exit(0);
        }

        child[i] = pid;
        idx_by_pid[pid] = i;
    }

    for (int i=0;i<MAX_CHILD;i++) {
        cpu_rem[i] = (rand()%181)+20;
        io_next[i] = (rand()%91)+10;
        in_wait[i] = 0;
        quantum[i] = quantum_ticks;

        struct buffmsg s = {0};
        s.mtype = child[i];
        s.cmd = CMD_START;
        s.cpu = cpu_rem[i];
        s.io  = io_next[i];
        msgsnd(msgid,&s,sizeof(s)-sizeof(long),0);
    }

    runq_t runq;
    runq_init(&runq);
    for (int i=0;i<MAX_CHILD;i++) runq_push(&runq,child[i]);

    wq_entry wq[MAX_CHILD];
    int wq_size = 0;

    pid_t current = -1;
    int current_idx = -1;

    char line[512];

    while (tick_count <= max_ticks) {
        while (!tick_flag) pause();
        tick_flag = 0;
        int t = tick_count - 1;

        if (current == -1 && !runq_empty(&runq)) {
            current = runq_pop(&runq);
            current_idx = idx_by_pid[current];
            quantum[current_idx] = quantum_ticks;
        }

        if (current != -1 && !in_wait[current_idx]) {
            struct buffmsg tick = {0};
            tick.mtype = current;
            tick.cmd = CMD_TICK;
            msgsnd(msgid,&tick,sizeof(tick)-sizeof(long),0);

            cpu_rem[current_idx]--;
            if (cpu_rem[current_idx] < 0) cpu_rem[current_idx] = 0;

            quantum[current_idx]--;
        }

        for (int i=0;i<wq_size;i++) {
            wq[i].remaining_io--;
            if (wq[i].remaining_io < 0) wq[i].remaining_io = 0;
        }

        for (int i=0;i<wq_size;) {
            if (wq[i].remaining_io == 0) {
                pid_t p = wq[i].pid;
                int idx = idx_by_pid[p];

                in_wait[idx] = 0;

                cpu_rem[idx] = (rand()%181)+20;
                io_next[idx] = (rand()%91)+10;

                struct buffmsg wake = {0};
                wake.mtype = p;
                wake.cmd = CMD_WAKE;
                wake.cpu = cpu_rem[idx];
                wake.io  = io_next[idx];
                msgsnd(msgid,&wake,sizeof(wake)-sizeof(long),0);

                runq_push(&runq,p);

                wq[i] = wq[wq_size-1];
                wq_size--;
                continue;
            }
            i++;
        }

        while (1) {
            struct buffmsg in;
            ssize_t r = msgrcv(msgid,&in,sizeof(in)-sizeof(long),1,IPC_NOWAIT);
            if (r<0) break;

            int idx = idx_by_pid[in.pid];
            in_wait[idx] = 1;

            wq[wq_size].pid = in.pid;
            wq[wq_size].remaining_io = in.io;
            wq_size++;

            if (current == in.pid) {
                current = -1;
                current_idx = -1;
            }
        }

        if (current != -1 && quantum[current_idx] == 0) {
            if (!in_wait[current_idx] && cpu_rem[current_idx] > 0)
                runq_push(&runq, current);

            current = -1;
            current_idx = -1;
        }

        char rqstr[128], wqstr[128];
        runq_dump(&runq, rqstr, sizeof(rqstr));

        int pos = 0;
        pos += snprintf(wqstr+pos,sizeof(wqstr)-pos,"[");
        for (int i=0;i<wq_size;i++)
            pos += snprintf(wqstr+pos,sizeof(wqstr)-pos,"%d:%d%s",
                wq[i].pid, wq[i].remaining_io,
                (i+1==wq_size?"":" "));
        snprintf(wqstr+pos,sizeof(wqstr)-pos,"]");

        if (current != -1)
            snprintf(line,sizeof(line),
                "(at time %d, process %d gets cpu time, remaining cpu-burst %d) run-queue: %s wait-queue: %s\n",
                t, current, cpu_rem[current_idx], rqstr, wqstr);
        else
            snprintf(line,sizeof(line),
                "(at time %d, idle) run-queue: %s wait-queue: %s\n",
                t, rqstr, wqstr);

        write(logfd, line, strlen(line));
    }

    for (int i=0;i<MAX_CHILD;i++) {
        struct buffmsg term = {0};
        term.mtype = child[i];
        term.cmd = CMD_TERMINATE;
        msgsnd(msgid,&term,sizeof(term)-sizeof(long),0);
    }
    for (int i=0;i<MAX_CHILD;i++) wait(NULL);

    close(logfd);
    msgctl(msgid, IPC_RMID, NULL);

    printf("Done! Check schedule_dump.txt\n");
    return 0;
}
