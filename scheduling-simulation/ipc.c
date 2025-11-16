#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include "msg.h"

int create_msg_queue(int key) {
    int msgq = msgget(key, IPC_CREAT | 0666);
    if (msgq == -1) {
        perror("msgget failed");
        exit(EXIT_FAILURE);
    }
    return msgq;
}

void send_msg(int msgq, struct msgbuf *msg) {
    if (msgsnd(msgq, msg, sizeof(*msg) - sizeof(long), 0) == -1) {
        perror("msgsnd failed");
        exit(EXIT_FAILURE);
    }
}

void receive_msg(int msgq, struct msgbuf *msg, long msg_type) {
    if (msgrcv(msgq, msg, sizeof(*msg) - sizeof(long), msg_type, 0) == -1) {
        perror("msgrcv failed");
        exit(EXIT_FAILURE);
    }
}
void init_ipc(void) {
    // Stub: add any IPC initialization if needed
}