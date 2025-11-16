#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <errno.h>

#define NCHILD          10
#define TIME_QUANTUM    5
#define TICK_USEC       10000
#define MAX_TICKS_LOG   10000
#define MAX_TICKS_USAGE 7000
#define LOG_FILENAME    "schedule_dump.txt"
#define MSGSZ (sizeof(msgbuf_perso) - sizeof(long))

typedef struct {
    pid_t pid;
    int in_io;
    int remaining_io;
    int remaining_quantum;
    int waiting_time;
} pcb_t;

typedef struct {
    int items[NCHILD];
    int head;
    int tail;
    int size;
} queue_t;

typedef struct msgbuf_perso {
	long mtype;
	int pid;
	int io_time;
} msgbuf_perso ;
