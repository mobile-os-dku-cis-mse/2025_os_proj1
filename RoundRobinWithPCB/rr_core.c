#include <stdlib.h>
#include <sys/msg.h>
#include <sys/wait.h>
#include "scheduler.h"

void init_pcb_table(scheduler *scheduler) {
    for (int i = 0; i < NUM_CHILDREN; i++) {
        process_control_block *pcb = &scheduler->pcb_table[i];
        pcb->pcb_index = i;
        pcb->pid = 0;
        pcb->state = PROCESS_NEW;
        pcb->cpu_burst = 0;
        pcb->io_burst = 0;
        pcb->remaining_quantum = 0;
        pcb->waiting_time = 0;
        pcb->total_cpu_time = 0;
        pcb->total_io_time = 0;
        pcb->response_time = -1;
        pcb->arrival_time = 0;
        pcb->first_run_time = -1;
        pcb->completion_time = -1;
    }
}

process_control_block* find_pcb_by_pid(scheduler *scheduler, pid_t pid) {
    for (int i = 0; i < NUM_CHILDREN; i++) {
        if (scheduler->pcb_table[i].pid == pid) {
            return &scheduler->pcb_table[i];
        }
    }
    return NULL;
}

process_control_block* allocate_pcb(scheduler *scheduler) {
    for (int i = 0; i < NUM_CHILDREN; i++) {
        if (scheduler->pcb_table[i].state == PROCESS_NEW &&
            scheduler->pcb_table[i].pid == 0) {
            return &scheduler->pcb_table[i];
        }
    }
    return NULL;
}

void update_pcb_state(process_control_block *pcb, process_state new_state) {
    if (pcb != NULL) {
        pcb->state = new_state;
    }
}

void init_scheduler(scheduler *scheduler) {
    init_pcb_table(scheduler);

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