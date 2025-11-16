#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/msg.h>
#include <sys/types.h>
#include <time.h>
#include "msg.h"
#include "ipc.h"

int main() {
    key_t key = 0x12345;
    int msgq = create_msg_queue(key);
    struct msgbuf msg;
    pid_t mypid = getpid();
    srand(time(NULL) ^ (mypid << 16));

    int cpu_burst = rand() % 500 + 500; // ms
    int io_burst = rand() % 300 + 200;  // ms
    int remaining_cpu = cpu_burst;

    while(1) {
        // Wait for time slice from parent
        receive_msg(msgq, &msg, mypid);
        int slice = msg.cpu_time;
        if (remaining_cpu > slice) {
            usleep(slice * 1000);
            remaining_cpu -= slice;
        } else {
            usleep(remaining_cpu * 1000);
            // Notify parent that CPU burst is done, send IO burst
            struct msgbuf notify;
            notify.mtype = 1; // Special type for parent to listen for IO requests
            notify.pid = mypid;
            notify.cpu_time = 0;
            notify.io_time = io_burst;
            send_msg(msgq, &notify);
            // Simulate I/O burst
            usleep(io_burst * 1000);
            // Generate new bursts
            cpu_burst = rand() % 500 + 500;
            io_burst = rand() % 300 + 200;
            remaining_cpu = cpu_burst;
        }
    }
    return 0;
}