# Graph.py

import matplotlib.pyplot as plt

# Main function
def main():
    # Data
    time_quantum = [10, 20, 40, 80, 160]
    log = [2007, 996, 509, 271, 184]
    size_of_run_queue_mean = [8.468830059777968, 8.346686088856519, 8.20852534562212, 7.983948635634029, 8.065573770491802]

    # Draw graph
    fig, ax1 = plt.subplots()

    # Title
    plt.title('Effect of Time Quantum on Number of Log and Size of Run Queue Mean')

    # Left y-axis is number of log
    ax1.set_xticks(time_quantum)
    ax1.set_xlabel('Time quantum')
    ax1.set_ylabel('Number of log')
    ax1.plot(time_quantum, log, color = '#ff0000', marker = 'o', linestyle = '-', label = 'Number of log')
    ax1.tick_params(axis='y')

    # Right y-axis is size of run queue mean
    ax2 = ax1.twinx()
    ax2.set_ylabel('Size of run queue mean')
    ax2.plot(time_quantum, size_of_run_queue_mean, color = '#0000ff', marker = 'o', linestyle = '-', label='Size of run queue mean')
    ax2.tick_params(axis='y')

    # Grid
    ax1.grid(True, linestyle='--', alpha=0.5)

    # Show graph
    plt.show()
    
if __name__ == '__main__':
    main()