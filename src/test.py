# Test.py

# Main function
def main():
    process_gets_cpu_log = 0             # Number of 'ㆍProcess N gets CPU' log
    process_time_quantum_expired_log = 0 # Number of 'ㆍProcess N time quantum expired' log
    is_run_queue = False                 # Distinguish between run queue and wait queue
    run_queue = 0                        # Number of run queue
    size_of_run_queue = 0                # Size of run queue
    size_of_run_queue_mean = 0           # Size of run queue mean

    # Open log.txt file
    with open('log.txt', 'r') as file:
        lines = file.readlines()

    for x in lines:
        # Count 'ㆍProcess N gets CPU' log
        if 'gets CPU' in x:
            process_gets_cpu_log += 1

        # Count 'ㆍProcess N time quantum expired' log
        if 'time quantum expired' in x:
            process_time_quantum_expired_log += 1

        # Distinguish between run queue and wait queue
        if 'ㆍRun queue (Index, Remaining CPU burst, Remaining time quantum)' in x:
            is_run_queue = True
            continue
        
        # Count number of run queue & count size of run queue
        if is_run_queue:
            run_queue += 1
            size_of_run_queue += x.count('(')
            is_run_queue = False

    # Count size of run queue mean
    size_of_run_queue_mean = size_of_run_queue / run_queue

    print(f"Number of 'ㆍProcess N gets CPU' log is {process_gets_cpu_log}")
    print(f"Number of 'ㆍProcess N time quantum expired' log is {process_time_quantum_expired_log}")
    print(f"Size of run queue mean is {size_of_run_queue_mean}")

if __name__ == '__main__':
    main()