#include <stdlib.h>
#include <sys/msg.h>
#include <sys/wait.h>
#include "scheduler.h"

void init_scheduler(scheduler *scheduler) {

    init_ready_queue(&scheduler->ready_queue);
    init_wait_queue(&scheduler->wait_queue);

    scheduler->current_cpu_process = NULL;
    scheduler->current_tick = 0;
    scheduler->running = true;
    scheduler->log_file = NULL;
    scheduler->total_context_switches = 0;
}

void cleanup_scheduler(scheduler *scheduler) {
    for (int i = 0; i < NUM_CHILDREN; i++) {
        if (scheduler->pcb_table[i].pid > 0 &&
            scheduler->pcb_table[i].state != PROCESS_TERMINATED) {
            send_terminate(scheduler->msgqid, scheduler->pcb_table[i].pid);
        }
    }

    for (int i = 0; i < NUM_CHILDREN; i++) {
        if (scheduler->pcb_table[i].pid > 0) {
            waitpid(scheduler->pcb_table[i].pid, NULL, 0);
            scheduler->pcb_table[i].state = PROCESS_TERMINATED;
        }
    }

    wait_node *current = scheduler->wait_queue.head;
    while (current != NULL) {
        wait_node *next = current->next;
        free(current);
        current = next;
    }

    if (scheduler->msgqid >= 0) {
        msgctl(scheduler->msgqid, IPC_RMID, NULL);
    }
}

int generate_random_burst(int min, int max) {
    return min + (rand() % (max - min + 1));
}