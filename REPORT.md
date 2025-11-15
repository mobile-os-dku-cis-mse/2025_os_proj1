# Simple Scheduling Simulator --- Round-Robin CPU Scheduler

### Operating Systems & Advanced Mobile Project

**Author:** Otcheskii Leonid (32239236)\
**Course:** Operation Systems

------------------------------------------------------------------------

## Overview

This project implements a simplified **Round-Robin CPU scheduling
simulator** using: - Unix processes (`fork`) - Timer interrupts
(`SIGALRM`, `setitimer`) - System V IPC message queues (`msgsnd`,
`msgrcv`) - Run Queue / Wait Queue process management - Simulation of
CPU bursts and I/O bursts - Logging of all scheduling decisions

The parent process acts as the **scheduler**, and 10 child processes
simulate **user tasks**.

------------------------------------------------------------------------

## Features

### Timer-Based Scheduling

The system uses `setitimer()` to deliver periodic `SIGALRM` ticks.\
Each tick corresponds to a **scheduler time unit**.

### Round-Robin Policy

-   Fixed time quantum \
-   Preemption and context switching\
-   Ready/Waiting state transitions

### IPC-Based Process Communication

Children report: - Remaining CPU burst\
- I/O burst request\
- Execution state

Parent sends: - RUN commands\
- New CPU burst after I/O completion

------------------------------------------------------------------------

## Architecture

### Parent (Scheduler)

Maintains: - Run Queue (ready tasks) - Wait Queue (I/O tasks) -
Per-process metadata:
`cpu_remaining   io_remaining   quantum_rem   state   waiting_time`

Timer interrupt triggers scheduling decisions: 1. Collect child reports\
2. Update I/O queue\
3. Preempt if needed\
4. Dispatch next process\
5. Log state

------------------------------------------------------------------------

### Child Processes

Each child: - Waits for a RUN message - Executes one CPU tick -
Decrements CPU burst - Signals WAITING when CPU burst ends - Generates a
new I/O burst - Waits for next RUN command after I/O

Children **do not** make scheduling decisions.

------------------------------------------------------------------------

## Timer Subsystem

Uses:

``` c
setitimer(ITIMER_REAL, &itv, NULL)
```

to generate periodic ticks (10ms default).\
Interrupts are handled via:

``` c
static void sigalrm_handler(int signo)
```

The scheduler loop uses `sigsuspend()` to sleep until the next timer
tick.

------------------------------------------------------------------------

## IPC Messaging

Messages are exchanged via System V queues using the structure:

``` c
struct sched_msg {
    long reciever;
    pid_t pid;
    int remaining;
    int io_time;
    int state;
};
```

### Parent → Child

-   RUN command with updated CPU burst

### Child → Parent

-   CPU progress
-   I/O request
-   Execution state

------------------------------------------------------------------------

## Problems Encountered & Solutions

### CPU burst desynchronization

Children originally generated their own CPU bursts → inconsistent
state.\
**Solution:** parent became the single source of truth.
- Child no longer generates CPU bursts.
- Parent sends the correct cpu_remaining to the child in every RUN message.
- Child assigns cpu_burst = remaining only if its local burst is currently 0.
- Child decrements this value and reports back.


###  Queue corruption due to misuse of READY state

READY was overloaded and caused duplicate entries.\
**Solution:** distinct states for RUNNING / WAITING.

###  Slow PID lookups

**Solution:** introduced `pid_to_index[]` mapping instead of nested loop searching
- (more optimal solution in case of many children processes, but takes additional memory).

### Child overwrote new CPU bursts

Solved by applying new burst only when `cpu_burst == 0`.

------------------------------------------------------------------------

## Logging

All scheduling activity is written to:

    schedule_dump.txt

including: - selected process per tick - run queue contents - wait queue
contents - remaining CPU bursts

------------------------------------------------------------------------

##  Running the Program

``` bash
gcc -o scheduler schedulerV2.c
./scheduler
```

Ensure the program has permission to create:

    schedule_dump.txt


