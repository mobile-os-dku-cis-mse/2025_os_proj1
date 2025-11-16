// child.c
#define _XOPEN_SOURCE 700
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/msg.h>
#include <time.h>
#include <unistd.h>
#include "msg.h"

int main(int argc, char *argv[]){
    // ――――――――――  Check command line argument ――――――――――
    if(argc < 3){
        fprintf(stderr, "―――――――――― child usage ――――――――――\nㆍ./child [msg_key] [index]\n");
        exit(1);
    }

    int key = atoi(argv[1]);
    int msgq = msgget(key, 0666);
    if(msgq == -1){
        perror("child msgget");
        exit(1);
    }

    // ―――――――――― CPU burst handling ――――――――――
    // Create random seed with PID & generate CPU burst time (10 ~ ticks)
    struct msgbuf_s snd, rcv;
    srand((unsigned int)(getpid() ^ time(NULL)));
    int cpu_burst = (rand() % 91) + 10;

    // Send CPU burst to parent process
    memset(&snd, 0, sizeof(snd));
    snd.mtype = 1;
    snd.cmd = MSG_CMD_IOREQ;
    snd.pid = getpid();
    snd.io_time = cpu_burst;
    if(msgsnd(msgq, &snd, sizeof(snd) - sizeof(long), 0) == -1){
        perror("child msgsnd init cpu_burst");
        exit(1);
    }

    // ―――――――――― Main child loop ――――――――――
    while(1){
        memset(&rcv, 0, sizeof(rcv));
        if(msgrcv(msgq, &rcv, sizeof(rcv) - sizeof(long), getpid(), 0) == -1){
            perror("child msgrcv");
            exit(1);
        }

        // If parent allows to run
        if(rcv.cmd == MSG_CMD_RUN){
            cpu_burst--; // Decrease CPU burst by 1 tick

            // If CPU burst is finished then request I/O
            if(cpu_burst <= 0){
                // Generate I/O burst time (10 ~ ticks)
                int next_io = (rand() % 91) + 10;

                // Send I/O burst to parent process
                memset(&snd, 0, sizeof(snd));
                snd.mtype = 1;
                snd.cmd = MSG_CMD_IOREQ;
                snd.pid = getpid();
                snd.io_time = next_io;
                if(msgsnd(msgq, &snd, sizeof(snd) - sizeof(long), 0) == -1){
                    perror("child msgsnd ioreq");
                }

                // Generate new CPU burst time (10 ~ ticks)
                cpu_burst = (rand() % 91) + 10;
            }
        }
    }
    return 0;
}
