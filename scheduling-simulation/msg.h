#ifndef MSG_H
#define MSG_H

#include <sys/types.h>

struct msgbuf {
    long mtype;      // Message type
    pid_t pid;       // Process ID of the child process
    int io_time;     // I/O time required by the process
    int cpu_time;    // CPU time required by the process
};

/*struct sigaction {
    void (*sa_handler)(int);
    unsigned long sa_flags;
    //void (*sa_restorer)(void);
    unsigned long sa_mask;    
};*/

#endif // MSG_H