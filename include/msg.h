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
#include <sys/msg.h>

#define NCHILD          10
#define TIME_QUANTUM    5      // en nombre de ticks
#define TICK_USEC       10000  // 10 ms par tick (ITIMER_REAL)
#define MAX_TICKS_LOG   10000  // on ne log que 0..10000
#define TICK_USAGE		7000
#define LOG_FILENAME    "schedule_dump.txt"

typedef struct {
	int mtype;

	// pid will sleep for io_time
	int pid;
	int io_time;
} msgbuf_perso ;

typedef struct {
	pid_t pid;
	int in_io;                  // 0 = prêt, 1 = en I/O
	int remaining_io;           // temps I/O restant (en ticks)
	int remaining_quantum;      // temps CPU restant dans la time slice courante
	int waiting_time;           // temps passé à attendre en run-queue (optionnel)
} pcb_t;

typedef struct {
	int items[NCHILD];
	int head;
	int tail;
	int size;
} queue_t;
