# Simple scheduling
This project is implements a simple Round-Robin CPU scheduling simulation using multiple child processes in C. The parent process creates 10 child processes and allocates CPU time according to a configurable time quantum. It demonstrates CPU burst, I/O burst, run queue, wait queue, timer interrupts, and inter-process communication via System V message queues.

## Feature
- Create multiple child processes and manage them with a Round-Robin scheduler.
- Handle CPU burst and I/O requests from child processes.
- Log scheduling events including run queue and wait queue states to `log.txt.`
- Timer-based tick interrupt to simulate time quantum expiration.
- Analyze performance with time quantum tuning and log statistics.

## Project Structure
```
project-root/
├── src/ # This directory is source files directory.
│ ├── sched.c
│ ├── child.c
│ ├── msg.h
│ ├── Makefile
│ ├── test.py
│ └── graph.py
├── README.md
├── LICENSE
└── Assignment3 - Simple scheduling (Yoo, J. H., 32212808, Department of Mobile Systems Engineering)
```

## Installation
1. Clone the repository
	```
	gh repo clone YooJunHyuk123/[repository]
	```

2. Navigate to the project directory
	```
	cd [project]
	```

## Usage
1. Build the project
	```
	make
	```

2. Run the program
	```
	./sched [time_quantum]
	```

## Test
- `make` generates object files `.o`.
- `make clean` removes object files.
- Running `./sched` without argument or with invalid quantum prints an error message.
- Running `./sched [time_quantum]` creates child processes and correctly logs CPU/I-O scheduling events in `log.txt`.

## Contributing
1. Create a new branch.
	```
	git checkout -b your_branch
	```

2. Commit your changes.
	```
	git add .
	git commit -m "Commit message"
	```

3. Push to the branch.
	```
	git push origin your_branch
	```

4. Open a pull request.

## License
This project is licensed under the MIT License.

## Authors
- Yoo, J. H. ([Yoo, J. H.](https://github.com/YooJunHyuk123))
- Email: a01091040305@gmail.com