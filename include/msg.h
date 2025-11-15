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

// ======================= PARAMETRES =========================

#define NCHILD          10
#define TIME_QUANTUM    5         // en ticks
#define TICK_USEC       10000     // 10 ms par tick
#define MAX_TICKS_LOG   10000
#define LOG_FILENAME    "schedule_dump.txt"

// taille utile des messages (sans mtype)
#define MSGSZ (sizeof(msgbuf_perso) - sizeof(long))

// ======================= STRUCTURES =========================

typedef struct {
    pid_t pid;
    int in_io;                  // 0 = prêt, 1 = en I/O
    int remaining_io;           // temps I/O restant (en ticks)
    int remaining_quantum;      // quantum CPU restant
    int waiting_time;           // temps passé en run-queue (optionnel)
} pcb_t;

typedef struct {
    int items[NCHILD];
    int head;
    int tail;
    int size;
} queue_t;

typedef struct msgbuf_perso {
	long mtype;

	// pid will sleep for io_time
	int pid;
	int io_time;
} msgbuf_perso ;
