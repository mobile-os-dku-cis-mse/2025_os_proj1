CC = gcc
CFLAGS = -Wall -g
TARGET = scheduler
SOURCE = scheduler.c
all: $(TARGET)
$(TARGET): $(SOURCE) 
	$(CC) $(CFLAGS) -o $(TARGET) $(SOURCE)
clean: 
	rm -f $(TARGET)