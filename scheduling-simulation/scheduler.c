#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include "scheduler.h"
#include "ipc.h"
#include "msg.h"

#define NUM_PROCESSES 10

struct process_info {
    pid_t pid;
    int remaining_time;
};

void round_robin_schedule(int msgq) {
    struct process_info processes[NUM_PROCESSES];
    int i, time_slice;
    int completed = 0;

    for (i = 0; i < NUM_PROCESSES; i++) {
        processes[i].pid = fork();
        if (processes[i].pid == 0) {
            // Child process execution
            execlp("./process", "process", NULL);
            exit(0);
        }
        processes[i].remaining_time = rand() % 10 + 1; // Random remaining time
    }

    while (completed < NUM_PROCESSES) {
        for (i = 0; i < NUM_PROCESSES; i++) {
            if (processes[i].remaining_time > 0) {
                time_slice = (processes[i].remaining_time > TIME_QUANTUM) ? TIME_QUANTUM : processes[i].remaining_time;
                processes[i].remaining_time -= time_slice;
                // Prepare and send message
                struct msgbuf msg;
                msg.mtype = processes[i].pid;
                msg.pid = processes[i].pid;
                msg.cpu_time = time_slice;
                msg.io_time = 0; // set as needed
                // Send time slice to child process
                send_msg(msgq, &msg);

                if (processes[i].remaining_time == 0) {
                    completed++;
                }
                sleep(1); // Simulate context switch
            }
        }
    }
}

int run_scheduler() {
    key_t key = 0x12345;
    int msgq = create_msg_queue(key);
    if (msgq == -1) {
        perror("Failed to create message queue");
        exit(EXIT_FAILURE);
    }

    round_robin_schedule(msgq);

    return 0;
}