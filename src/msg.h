// msg.h
#ifndef MSG_H
#define MSG_H
#include <sys/types.h>
#define MSG_CMD_RUN 1
#define MSG_CMD_IOREQ 2

// System V message buffer
struct msgbuf_s{
    long mtype;   // Message type (perent process to child process message is PID, child process to parent process message is 1)
    pid_t pid;    // Sender process PID
    int cmd;      // Command (MSG_CMD_RUN or MSG_CMD_IOREQ)
    int io_time;  // If command is MSG_CMD_IOREQ then use io_time for request I/O time
    int cpu_decr; // How many cpu ticks child progressed
};

#endif