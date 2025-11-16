COMPILER = gcc
OPTION = -Wall -Wextra -std=gnu11 -O2
FILE = sched child

all: $(FILE)

sched: sched.c msg.h
	$(COMPILER) $(OPTION) -o $@ sched.c

child: child.c msg.h
	$(COMPILER) $(OPTION) -o $@ child.c

clean:
	rm -f $(FILE) log.txt
