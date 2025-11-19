CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -D_POSIX_C_SOURCE=200809L -D_GNU_SOURCE
LDFLAGS = -lrt

TARGET = scheduler
SOURCES = rr_main.c rr_core.c rr_ready_queue.c rr_wait_queue.c rr_msg.c rr_log.c rr_child.c
HEADER = scheduler.h

.PHONY: all clean run help

all: $(TARGET)

$(TARGET): $(SOURCES) $(HEADER)
	$(CC) $(CFLAGS) -o $@ $(SOURCES) $(LDFLAGS)

clean:
	rm -f $(TARGET) schedule_dump.txt
	ipcs -q | grep `whoami` | awk '{print $$2}' | xargs -r ipcrm -q 2>/dev/null || true

run: $(TARGET)
	./$(TARGET)

help:
	@echo "Available targets:"
	@echo "  all    - Build the scheduler (default)"
	@echo "  clean  - Remove build artifacts and log files"
	@echo "  run    - Build and run the scheduler"