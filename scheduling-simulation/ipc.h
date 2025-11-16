#ifndef IPC_H
#define IPC_H

#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include "msg.h"

#define MSG_SIZE sizeof(struct msgbuf) - sizeof(long)

// Function to create a message queue
int create_msg_queue(key_t key);

// Function to send a message to the message queue
int send_msg(int msgq_id, struct msgbuf *msg);

// Function to receive a message from the message queue
int receive_msg(int msgq_id, struct msgbuf *msg, long msg_type);

void init_ipc(void);
#endif // IPC_H