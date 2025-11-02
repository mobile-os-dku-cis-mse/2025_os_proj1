#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include "scheduler.h"

void init_wait_queue(wait_queue *queue) {
    queue->head = NULL;
    queue->tail = NULL;
    queue->count = 0;
}

bool is_wait_queue_empty(const wait_queue *queue) {
    return queue->count == 0;
}

int enqueue_wait(wait_queue *queue, process_control_block *pcb) {
    if (pcb == NULL) {
        fprintf(stderr, "[ERROR] Cannot enqueue NULL PCB to wait queue\n");
        return -1;
    }
    
    wait_node *node = (wait_node *)malloc(sizeof(wait_node));
    if (node == NULL) {
        perror("malloc");
        return -1;
    }
    
    node->pcb = pcb;
    node->next = NULL;
    
    if (queue->tail == NULL) {
        queue->head = queue->tail = node;
    } else {
        queue->tail->next = node;
        queue->tail = node;
    }
    
    queue->count++;

    return 0;
}

process_control_block* dequeue_wait(wait_queue *queue) {
    if (is_wait_queue_empty(queue)) {
        return NULL;
    }
    
    wait_node *node = queue->head;
    process_control_block *pcb = node->pcb;
    
    queue->head = node->next;
    if (queue->head == NULL) {
        queue->tail = NULL;
    }
    
    free(node);
    queue->count--;
    
    return pcb;
}

process_control_block* remove_from_wait_queue(wait_queue *queue, pid_t pid) {
    if (is_wait_queue_empty(queue)) {
        return NULL;
    }
    
    wait_node *current = queue->head;
    wait_node *prev = NULL;
    
    while (current != NULL) {
        if (current->pcb != NULL && current->pcb->pid == pid) {
            process_control_block *pcb = current->pcb;
            
            if (prev == NULL) {
                queue->head = current->next;
                if (queue->head == NULL) {
                    queue->tail = NULL;
                }
            } else {
                prev->next = current->next;
                if (current == queue->tail) {
                    queue->tail = prev;
                }
            }
            
            free(current);
            queue->count--;
            return pcb;
        }
        
        prev = current;
        current = current->next;
    }
    
    return NULL;
}
