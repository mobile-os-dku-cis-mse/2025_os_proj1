#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include "scheduler.h"

void init_ready_queue(ready_queue *queue) {
    queue->front = 0;
    queue->rear = -1;
    queue->count = 0;

    for (int i = 0; i < NUM_CHILDREN; i++) {
        queue->processes[i] = NULL;
    }
}

bool is_ready_queue_empty(const ready_queue *queue) {
    return queue->count == 0;
}

bool is_ready_queue_full(const ready_queue *queue) {
    return queue->count == NUM_CHILDREN;
}

int enqueue_ready(ready_queue *queue, process_control_block *pcb) {
    if (pcb == NULL) {
        fprintf(stderr, "[ERROR] Cannot enqueue NULL PCB\n");
        return -1;
    }

    if (is_ready_queue_full(queue)) {
        fprintf(stderr, "[ERROR] Run queue is full (count=%d)\n", queue->count);
        return -1;
    }

    queue->rear = (queue->rear + 1) % NUM_CHILDREN;
    queue->processes[queue->rear] = pcb;
    queue->count++;

    return 0;
}

process_control_block* dequeue_ready(ready_queue *queue) {
    if (is_ready_queue_empty(queue)) {
        return NULL;
    }

    process_control_block *pcb = queue->processes[queue->front];
    queue->processes[queue->front] = NULL;  // Clear the pointer
    queue->front = (queue->front + 1) % NUM_CHILDREN;
    queue->count--;

    return pcb;
}

process_control_block* remove_from_ready_queue(ready_queue *queue, pid_t pid) {
    if (is_ready_queue_empty(queue)) {
        return NULL;
    }

    int found_index = -1;
    for (int i = 0; i < queue->count; i++) {
        int idx = (queue->front + i) % NUM_CHILDREN;
        if (queue->processes[idx] != NULL && queue->processes[idx]->pid == pid) {
            found_index = idx;
            break;
        }
    }

    if (found_index == -1) {
        return NULL;
    }

    process_control_block *removed_pcb = queue->processes[found_index];

    if (found_index == queue->front) {
        queue->processes[queue->front] = NULL;
        queue->front = (queue->front + 1) % NUM_CHILDREN;
    } else {
        int current = found_index;
        while (current != queue->rear) {
            int next = (current + 1) % NUM_CHILDREN;
            queue->processes[current] = queue->processes[next];
            current = next;
        }
        queue->processes[queue->rear] = NULL;
        queue->rear = (queue->rear - 1 + NUM_CHILDREN) % NUM_CHILDREN;
    }

    queue->count--;
    return removed_pcb;
}

void update_ready_queue_waiting_times(ready_queue *queue) {
    if (is_ready_queue_empty(queue)) {
        return;
    }

    for (int i = 0; i < queue->count; i++) {
        int idx = (queue->front + i) % NUM_CHILDREN;
        if (queue->processes[idx] != NULL) {
            queue->processes[idx]->waiting_time++;
        }
    }
}