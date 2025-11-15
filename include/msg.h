#include <stdio.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>

struct msgbuf {
	int mtype;

	// pid will sleep for io_time
	int pid;
	int io_time;
};
