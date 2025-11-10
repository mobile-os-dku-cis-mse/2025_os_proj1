#include "scheduler.h"
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

scheduler global_scheduler;
volatile sig_atomic_t global_timeout = false;

void timer_signal_handler(int signo) {
    global_timeout = true;
}

void setup_signal_handlers(void) {
    struct sigaction sa = {0};
    
    sa.sa_handler = timer_signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    
    if (sigaction(SIGALRM, &sa, NULL) == -1) {
        perror("sigaction");
        exit(1);
    }
    
    signal(SIGCHLD, SIG_IGN);
}

void setup_timer(void) {
    struct itimerval timer;

    timer.it_value.tv_sec = 0;
    timer.it_value.tv_usec = TICK_INTERVAL_US;
    timer.it_interval.tv_sec = 0;
    timer.it_interval.tv_usec = TICK_INTERVAL_US;

    if (setitimer(ITIMER_REAL, &timer, NULL) == -1) {
        perror("setitimer");
        exit(1);
    }
}

void create_child_processes(scheduler *sched) {
    printf("Creating %d child processes...\n", NUM_CHILDREN);
    
    for (int i = 0; i < NUM_CHILDREN; i++) {
        process_control_block *pcb = allocate_pcb(sched);
        if (pcb == NULL) {
            fprintf(stderr, "[ERROR] Failed to allocate PCB for child %d\n", i);
            exit(1);
        }
        
        pid_t pid = fork();
        
        if (pid == -1) {
            perror("fork");
            exit(1);
        } else if (pid == 0) {
            child_process_main(sched->msgqid, getpid());
            exit(0);
        } else {
            pcb->pid = pid;
            pcb->state = PROCESS_READY;
            pcb->cpu_burst = generate_random_burst(MIN_CPU_BURST, MAX_CPU_BURST);
            pcb->io_burst = 0;
            pcb->remaining_quantum = TIME_QUANTUM;
            pcb->waiting_time = 0;
            pcb->total_cpu_time = 0;
            pcb->total_io_time = 0;
            pcb->arrival_time = sched->current_tick;
            pcb->first_run_time = -1;
            pcb->completion_time = -1;
            pcb->response_time = -1;

            if (enqueue_ready(&sched->ready_queue, pcb) != 0) {
                fprintf(stderr, "[ERROR] Failed to enqueue child process %d\n", pid);
                exit(1);
            }

            printf("  Created child process: PID=%d (PCB index=%d), Initial CPU burst=%d\n",
                   pid, pcb->pcb_index, pcb->cpu_burst);
        }
    }
    
    printf("All child processes created successfully.\n\n");
}

int main(int argc, char *argv[]) {
    srand(time(NULL));
    
    init_scheduler(&global_scheduler);
    
    global_scheduler.msgqid = create_message_queue();
    if (global_scheduler.msgqid == -1) {
        fprintf(stderr, "Failed to create message queue\n");
        exit(1);
    }
    printf("Message queue created: ID=%d\n\n", global_scheduler.msgqid);

    open_log_file(&global_scheduler);
    printf("Log file opened: %s\n\n", LOG_FILE);

    setup_signal_handlers();
    printf("Signal handlers configured.\n\n");

    create_child_processes(&global_scheduler);

    setup_timer();
    printf("Timer started: %d microseconds per tick\n\n", TICK_INTERVAL_US);

    schedule_next_process(&global_scheduler);

    printf("Starting simulation...\n");

    int min_ticks = 60 * 1000000 / TICK_INTERVAL_US;
    
    while (global_scheduler.current_tick < min_ticks) {
        if (global_timeout) {
            global_timeout = 0;
            process_timer_tick(&global_scheduler);

            if (global_scheduler.current_tick % 1000 == 0) {
                printf("Progress: %d ticks (%.1f seconds)\n",
                       global_scheduler.current_tick,
                       global_scheduler.current_tick * TICK_INTERVAL_US / 1000000.0);
            }
        }
        
        usleep(100);
    }

    printf("\n");
    printf("Simulation completed after %d ticks (%.2f seconds)\n",
           global_scheduler.current_tick,
           global_scheduler.current_tick * TICK_INTERVAL_US / 1000000.0);
    printf("\n");

    print_final_statistics(&global_scheduler);

    printf("\nCleaning up...\n");
    cleanup_scheduler(&global_scheduler);
    printf("Cleanup completed.\n");

    printf("\n=================================================\n");
    printf("   Simulation Finished Successfully\n");
    printf("=================================================\n");
    
    return 0;
}