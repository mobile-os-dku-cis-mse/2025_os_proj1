# OS Project 1: Round-Robin Scheduling Simulation

## Description
This project simulates a simple **CPU scheduling system** using the **Round-Robin (RR) algorithm**.  
The program creates multiple child processes, schedules them using a time quantum, and handles both CPU and I/O bursts. It maintains **run-queue** and **wait-queue** states and logs all scheduling operations.

---

## Files in this repository
- `os_proj1_rr_perfect.c` : Main program file implementing the RR scheduler.  
- `schedule_dump.txt` : Output log of the program showing scheduling operations for 0–10,000 time ticks.  
- `Makefile` : Simplifies compilation.

---

## Requirements
- **Linux environment** (WSL Ubuntu, Ubuntu VM, or native Linux)  
- **C compiler** (`gcc`)  
- No additional libraries required.

---

## Compilation

### Using GCC:
```bash
gcc os_proj1_rr_perfect.c -o os_proj1_rr_perfect
