#include "scheduler.h"
#include <unistd.h>
#include <stdlib.h>

void open_log_file(scheduler *sched) {
    sched->log_file = fopen(LOG_FILE, "w");
    if (sched->log_file == NULL) {
        perror("fopen");
        exit(1);
    }
    
    fprintf(sched->log_file, "=== Round Robin Scheduling Simulation ===\n");
    fprintf(sched->log_file, "Configuration:\n");
    fprintf(sched->log_file, "  - Number of Processes: %d\n", NUM_CHILDREN);
    fprintf(sched->log_file, "  - Time Quantum: %d ticks\n", TIME_QUANTUM);
    fprintf(sched->log_file, "  - Tick Interval: %d microseconds\n", TICK_INTERVAL_US);
    fprintf(sched->log_file, "  - CPU Burst Range: [%d, %d]\n", MIN_CPU_BURST, MAX_CPU_BURST);
    fprintf(sched->log_file, "  - I/O Burst Range: [%d, %d]\n", MIN_IO_BURST, MAX_IO_BURST);
    fprintf(sched->log_file, "==========================================\n\n");
    
    fflush(sched->log_file);
}

void close_log_file(scheduler *scheduler) {
    if (scheduler->log_file != NULL) {
        fprintf(scheduler->log_file, "\n=== End of Simulation ===\n");
        fclose(scheduler->log_file);
        scheduler->log_file = NULL;
    }
}

void log_scheduling_event(scheduler *scheduler) {
    if (scheduler->log_file == NULL) {
        return;
    }
    
    char buffer[512];
    int offset = 0;
    
    if (scheduler->current_cpu_process != NULL) {
        offset += snprintf(buffer + offset, sizeof(buffer) - offset,
                          "(at time %d, process %d gets cpu time, remaining cpu-burst %d) ",
                          scheduler->current_tick,
                          scheduler->current_cpu_process->pid,
                          scheduler->current_cpu_process->cpu_burst);
    } else {
        offset += snprintf(buffer + offset, sizeof(buffer) - offset,
                          "(at time %d, IDLE) ",
                          scheduler->current_tick);
    }
    
    write(fileno(scheduler->log_file), buffer, offset);
    dump_ready_queue(scheduler->log_file, &scheduler->ready_queue);
    dump_wait_queue(scheduler->log_file, &scheduler->wait_queue);
    write(fileno(scheduler->log_file), "\n", 1);
    
    if (scheduler->current_tick % 100 == 0) {
        fflush(scheduler->log_file);
    }
}

void dump_ready_queue(FILE *fp, const ready_queue *queue) {
    char buffer[256];
    int offset = 0;
    
    offset += snprintf(buffer + offset, sizeof(buffer) - offset,
                      "run-queue[%d]: ", queue->count);
    
    if (is_ready_queue_empty(queue)) {
        offset += snprintf(buffer + offset, sizeof(buffer) - offset, "(empty) ");
    } else {
        for (int i = 0; i < queue->count; i++) {
            int idx = (queue->front + i) % NUM_CHILDREN;
            const process_control_block *pcb = queue->processes[idx];
            
            if (pcb != NULL) {
                offset += snprintf(buffer + offset, sizeof(buffer) - offset,
                                 "P%d(burst=%d,wait=%d) ",
                                 pcb->pid, pcb->cpu_burst, pcb->waiting_time);
            }
            
            if (offset >= sizeof(buffer) - 50) {
                break;
            }
        }
    }
    
    write(fileno(fp), buffer, offset);
}

void dump_wait_queue(FILE *fp, const wait_queue *queue) {
    char buffer[256];
    int offset = 0;
    
    offset += snprintf(buffer + offset, sizeof(buffer) - offset,
                      "wait-queue[%d]: ", queue->count);
    
    if (is_wait_queue_empty(queue)) {
        offset += snprintf(buffer + offset, sizeof(buffer) - offset, "(empty)");
    } else {
        wait_node *current = queue->head;
        while (current != NULL) {
            const process_control_block *pcb = current->pcb;
            
            if (pcb != NULL) {
                offset += snprintf(buffer + offset, sizeof(buffer) - offset,
                                 "P%d(io=%d) ",
                                 pcb->pid, pcb->io_burst);
            }
            
            current = current->next;
            
            if (offset >= sizeof(buffer) - 50) {
                break;
            }
        }
    }
    
    write(fileno(fp), buffer, offset);
}

void print_final_statistics(const scheduler *sched) {
    printf("\n");
    printf("=================================================\n");
    printf("         Final Statistics\n");
    printf("=================================================\n");
    printf("Total Ticks: %d\n", sched->current_tick);
    printf("Total Runtime: %.2f seconds\n",
           sched->current_tick * TICK_INTERVAL_US / 1000000.0);
    printf("Total Context Switches: %d\n", sched->total_context_switches);
    printf("\n");
    
    printf("Process Statistics:\n");
    printf("%-8s %-10s %-10s %-10s %-10s %-10s %-12s\n",
           "PID", "State", "CPU Time", "I/O Time", "Wait Time", "Response", "CPU Burst");
    printf("%-8s %-10s %-10s %-10s %-10s %-10s %-12s\n",
           "---", "-----", "--------", "--------", "---------", "--------", "---------");
    
    int total_cpu_time = 0;
    int total_io_time = 0;
    int total_wait_time = 0;
    int total_response_time = 0;
    int processes_with_response = 0;
    
    for (int i = 0; i < NUM_CHILDREN; i++) {
        const process_control_block *pcb = &sched->pcb_table[i];
        if (pcb->pid == 0) continue;
        
        const char *state_str;
        switch (pcb->state) {
            case PROCESS_NEW: state_str = "NEW"; break;
            case PROCESS_READY: state_str = "READY"; break;
            case PROCESS_RUNNING: state_str = "RUNNING"; break;
            case PROCESS_WAITING: state_str = "WAITING"; break;
            case PROCESS_TERMINATED: state_str = "TERMINATED"; break;
            default: state_str = "UNKNOWN"; break;
        }
        
        printf("%-8d %-10s %-10d %-10d %-10d ",
               pcb->pid, state_str, pcb->total_cpu_time, 
               pcb->total_io_time, pcb->waiting_time);
        
        if (pcb->response_time >= 0) {
            printf("%-10d ", pcb->response_time);
            total_response_time += pcb->response_time;
            processes_with_response++;
        } else {
            printf("%-10s ", "N/A");
        }

        printf("%-12d\n", pcb->cpu_burst);
        
        total_cpu_time += pcb->total_cpu_time;
        total_io_time += pcb->total_io_time;
        total_wait_time += pcb->waiting_time;
    }
    
    printf("\n");
    printf("Average Statistics:\n");
    printf("  - Average CPU Time: %.2f ticks\n", 
           (double)total_cpu_time / NUM_CHILDREN);
    printf("  - Average I/O Time: %.2f ticks\n", 
           (double)total_io_time / NUM_CHILDREN);
    printf("  - Average Wait Time: %.2f ticks\n", 
           (double)total_wait_time / NUM_CHILDREN);
    
    if (processes_with_response > 0) {
        printf("  - Average Response Time: %.2f ticks\n", 
               (double)total_response_time / processes_with_response);
    }
    
    printf("\n");
    printf("Queue Status:\n");
    printf("  - Ready Queue: %d processes\n", sched->ready_queue.count);
    printf("  - Wait Queue: %d processes\n", sched->wait_queue.count);
    printf("  - Current Process: %s\n",
           sched->current_cpu_process ? "Yes" : "No");

    printf("\n");
    printf("Output written to: %s\n", LOG_FILE);
    printf("=================================================\n");
}