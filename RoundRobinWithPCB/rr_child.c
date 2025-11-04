#include <stdlib.h>
#include <time.h>
#include "scheduler.h"
#include <unistd.h>

volatile sig_atomic_t child_should_terminate = 0;

void child_signal_handler(int signo) {
}

void child_process_main(int msgqid, pid_t my_pid) {
    signal(SIGUSR1, child_signal_handler);

    srand(time(NULL) ^ my_pid);

    int cpu_burst = generate_random_burst(MIN_CPU_BURST, MAX_CPU_BURST);
    int io_burst = 0;

    message msg;

    while (1) {
        if (child_receive_message(msgqid, my_pid, &msg) == 0) {
            if (msg.data.command == MSG_TERMINATE) {
                break;
            } else if (msg.data.command == MSG_TIME_SLICE) {
                if (cpu_burst > 0) {
                    cpu_burst--;

                    if (cpu_burst == 0) {
                        io_burst = generate_random_burst(MIN_IO_BURST, MAX_IO_BURST);

                        if (child_send_io_request(msgqid, io_burst, my_pid) == 0) {
                            cpu_burst = generate_random_burst(MIN_CPU_BURST, MAX_CPU_BURST);
                        }
                    }
                }
            }
        }

        usleep(100);
    }

    exit(0);
}