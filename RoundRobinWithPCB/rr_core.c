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

void schedule_next_process(scheduler *scheduler) {
    process_control_block *next_pcb = dequeue_ready(&scheduler->ready_queue);

    if (next_pcb != NULL) {
        scheduler->current_cpu_process = next_pcb;
        scheduler->current_cpu_process->remaining_quantum = TIME_QUANTUM;
        update_pcb_state(scheduler->current_cpu_process, PROCESS_RUNNING);

        if (scheduler->current_cpu_process->first_run_time == -1) {
            scheduler->current_cpu_process->first_run_time = scheduler->current_tick;
            scheduler->current_cpu_process->response_time =
                scheduler->current_tick - scheduler->current_cpu_process->arrival_time;
        }

        scheduler->total_context_switches++;

        send_time_slice(scheduler->msgqid, scheduler->current_cpu_process->pid);
    } else {
        scheduler->current_cpu_process = NULL;
    }
}

void handle_time_slice_complete(scheduler *scheduler) {
    if (scheduler->current_cpu_process == NULL) {
        return;
    }

    if (scheduler->current_cpu_process->cpu_burst > 0) {
        update_pcb_state(scheduler->current_cpu_process, PROCESS_READY);
        enqueue_ready(&scheduler->ready_queue, scheduler->current_cpu_process);
    } else if (scheduler->current_cpu_process->state == PROCESS_TERMINATED) {
        scheduler->current_cpu_process->completion_time = scheduler->current_tick;
    }

    scheduler->current_cpu_process = NULL;
    schedule_next_process(scheduler);
}

void handle_io_request(scheduler *scheduler, const message *msg) {
    pid_t requesting_pid = msg->data.sender_pid;
    int io_burst = msg->data.value;

    process_control_block *pcb = find_pcb_by_pid(scheduler, requesting_pid);
    if (pcb == NULL) {
        fprintf(stderr, "[ERROR] I/O request from unknown process: %d\n", requesting_pid);
        return;
    }

    pcb->io_burst = io_burst;

    if (scheduler->current_cpu_process == pcb) {
        update_pcb_state(pcb, PROCESS_WAITING);
        enqueue_wait(&scheduler->wait_queue, pcb);
        scheduler->current_cpu_process = NULL;
        schedule_next_process(scheduler);
    }else if (pcb->state == PROCESS_READY) {
        process_control_block *removed_pcb = remove_from_ready_queue(&scheduler->ready_queue, requesting_pid);
        if (removed_pcb != NULL) {
            update_pcb_state(removed_pcb, PROCESS_WAITING);
            enqueue_wait(&scheduler->wait_queue, removed_pcb);
        }
    }
}

void process_timer_tick(scheduler *scheduler) {
    scheduler->current_tick++;

    update_wait_queue_io_bursts(&scheduler->wait_queue, &scheduler->ready_queue);
    update_ready_queue_waiting_times(&scheduler->ready_queue);

    message msg;
    while (receive_io_request(scheduler->msgqid, &msg) == 0) {
        handle_io_request(scheduler, &msg);
    }

    if (scheduler->current_cpu_process != NULL) {
        send_time_slice(scheduler->msgqid, scheduler->current_cpu_process->pid);

        scheduler->current_cpu_process->remaining_quantum--;
        scheduler->current_cpu_process->total_cpu_time++;

        if (scheduler->current_cpu_process->cpu_burst > 0) {
            scheduler->current_cpu_process->cpu_burst--;
        }

        if (scheduler->current_cpu_process->remaining_quantum <= 0) {
            handle_time_slice_complete(scheduler);
        }
    } else {
        schedule_next_process(scheduler);
    }
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