# Round-Robin Scheduler with I/O Simulation

## Overview

This program implements a **Round-Robin CPU scheduler** that simulates process scheduling with dynamic CPU bursts and I/O operations. The parent process acts as the operating system scheduler, managing 10 child processes that simulate user programs executing in an infinite loop of CPU and I/O phases.

## Architecture

### Components

- **Parent Process (Scheduler)**: Manages process scheduling, maintains queues, handles timer interrupts
- **Child Processes (10)**: Simulate user programs with alternating CPU-burst and I/O-burst cycles
- **IPC Mechanism**: POSIX message queues for inter-process communication
- **Timer**: `SIGALRM` signals triggered every 10ms to drive scheduling decisions

### Data Structures

```c
process_t {
    pid_t pid;              // Process ID
    int cpu_burst;          // Remaining CPU burst time
    int io_burst;           // Remaining I/O burst time
    int remaining_quantum;  // Time quantum remaining
    int waiting_time;       // Total time spent waiting
    int state;              // 0=init, 1=ready, 2=waiting
}
```

**Queues**:
- `run_queue`: Ready processes eligible for CPU
- `wait_queue`: Processes blocked on I/O

## Scheduling Algorithm

### Round-Robin Policy

- **Time Quantum**: 5 ticks
- **Preemption**: Process is preempted when quantum expires
- **Fairness**: Equal CPU time distribution among ready processes

### State Transitions

```
         ┌──────────┐
         │  READY   │◄─────┐
         │ (RUN_Q)  │      │
         └────┬─────┘      │
              │            │
    ┌─────────▼─────────┐  │
    │    RUNNING        │  │
    │  (current_proc)   │  │
    └─────┬───────┬─────┘  │
          │       │        │
cpu_burst │       │ quantum│
= 0       │       │expired │
          │       └────────┘
          │
    ┌─────▼──────┐
    │  WAITING   │
    │ (WAIT_Q)   │
    └─────┬──────┘
          │
   io_burst = 0
          │
          └────────────────┘
```

## Implementation Details

### Timer-Driven Scheduling

Every 10ms (TIMER_TICK), the scheduler:
1. Decrements the current process's quantum
2. Checks for I/O completion in wait_queue
3. Receives CPU burst updates from children (mtype=2)
4. Handles I/O requests from children (mtype=1)
5. Selects next process if current quantum expired

### IPC Protocol

**Message Types**:
- `mtype = 999`: Initial process registration
- `mtype = 1`: I/O request from child (cpu_burst completed)
- `mtype = 2`: CPU burst update after each tick
- `mtype = PID`: Time slice grant from parent to specific child

**Message Structure**:
```c
struct msgbuf {
    long mtype;
    int pid;
    int io_time;
    int cpu_remaining;
}
```

### Child Process Behavior

1. Generate random `cpu_burst` (10-30 ticks) and `io_burst` (5-20 ticks)
2. Send initial registration message to parent
3. **Main loop**:
   - Wait for time slice from parent (blocking `msgrcv`)
   - Decrement `cpu_burst`
   - Send update to parent (mtype=2)
   - If `cpu_burst == 0`:
     - Send I/O request (mtype=1)
     - Wait for I/O completion signal
     - Generate new `cpu_burst`

### Parent Process (Scheduler)

**Initialization**:
1. Create message queue
2. Fork 10 child processes
3. Receive initial registration from all children
4. Setup timer interrupt handler

**Scheduling Loop** (triggered by SIGALRM):
1. Update current process quantum
2. Process CPU burst updates (mtype=2)
3. Handle I/O requests (mtype=1) → move to wait_queue
4. Decrement I/O burst for all waiting processes
5. Move completed I/O processes back to run_queue
6. Context switch if quantum expired
7. Send time slice to new current process
8. Log scheduling event

## Output Format

Each scheduling event is logged to `schedule_dump.txt`:

```
[Tick   145] PID=49668 CPU_burst=27 Quantum=5 Wait_time=129 | 
             RQ(size=7)=[49670,49669,...] | 
             WQ(size=2)=[49676(io=4),49677(io=11)]
```

**Fields**:
- `Tick`: Current time tick
- `PID`: Currently running process
- `CPU_burst`: Remaining CPU burst
- `Quantum`: Remaining time quantum
- `Wait_time`: Total accumulated waiting time
- `RQ`: List of PIDs in run queue
- `WQ`: List of PIDs with remaining I/O time

## Key Features

### Dynamic Workload
- CPU bursts: 10-30 ticks (random)
- I/O bursts: 5-20 ticks (random)
- Infinite loop simulating realistic process behavior

### Synchronization
- Message queues handle asynchronous communication
- No race conditions due to single-threaded scheduler
- Children block on `msgrcv` when not scheduled

### Statistics Tracking
- **Waiting time**: Accumulated for each process
- **Queue sizes**: Monitored in real-time
- **I/O overlap**: Multiple processes can perform I/O concurrently

## Compilation and Execution

#### Create a build folder and navigate to it
```shell
mkdir build && cd build
```
#### Run cmake
```shell
cmake ..
```
#### Compile the project
```shell
make
```
#### Run the program
```shell
./scheduler
```


**Runtime**: 60 seconds  
**Output**: First 10,000 ticks logged to `schedule_dump.txt`

## Technical Parameters

| Parameter | Value | Description |
|-----------|-------|-------------|
| NUM_PROCESSES | 10 | Number of child processes |
| TIME_QUANTUM | 5 | Ticks per time slice |
| TIMER_TICK | 10000 μs | Scheduler interrupt interval |
| MAX_TICKS | 10000 | Logging duration |
| CPU_BURST_RANGE | 10-30 | Random CPU burst duration |
| IO_BURST_RANGE | 5-20 | Random I/O burst duration |

## Limitations and Assumptions

1. **No priority scheduling**: All processes have equal priority
2. **Cooperative I/O**: Children voluntarily yield CPU on I/O
3. **Single CPU**: No parallel execution simulation
4. **Infinite workload**: Processes never terminate naturally
5. **Synchronization delay**: 1-tick lag in CPU burst updates (acceptable for simulation)

## Cleanup

The program gracefully terminates after 60 seconds:
1. Stops timer interrupts
2. Sends `SIGTERM` then `SIGKILL` to all children
3. Removes message queue (`IPC_RMID`)
4. Closes output file