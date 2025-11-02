#include <errno.h>
#include <unistd.h>
#include <sys/msg.h>
#include "scheduler.h"

int create_message_queue(void) {
    int msgqid = msgget(IPC_PRIVATE, IPC_CREAT | 0666);
    if (msgqid == -1) {
        perror("msgget");
        return -1;
    }

    return msgqid;
}

int send_time_slice(int msgqid, pid_t target_pid) {
    message msg;

    msg.mtype = target_pid;
    msg.data.command = MSG_TIME_SLICE;
    msg.data.value = 1;
    msg.data.sender_pid = getpid();

    if (msgsnd(msgqid, &msg, sizeof(msg.data), IPC_NOWAIT) == -1) {
        if (errno != EAGAIN) {
            perror("msgsnd (time_slice)");
        }
        return -1;
    }

    return 0;
}

int send_terminate(int msgqid, pid_t target_pid) {
    message msg;

    msg.mtype = target_pid;
    msg.data.command = MSG_TERMINATE;
    msg.data.value = 0;
    msg.data.sender_pid = getpid();

    if (msgsnd(msgqid, &msg, sizeof(msg.data), 0) == -1) {
        perror("msgsnd (terminate)");
        return -1;
    }

    return 0;
}

int receive_io_request(int msgqid, message *msg) {
    ssize_t ret = msgrcv(msgqid, msg, sizeof(msg->data),
                         MSG_IO_REQUEST, IPC_NOWAIT);

    if (ret == -1) {
        if (errno != ENOMSG) {
            perror("msgrcv (io_request)");
        }
        return -1;
    }

    return 0;
}

int child_receive_message(int msgqid, pid_t my_pid, message *msg) {
    ssize_t ret = msgrcv(msgqid, msg, sizeof(msg->data),
                         my_pid, IPC_NOWAIT);

    if (ret == -1) {
        if (errno != ENOMSG) {
            perror("msgrcv (child)");
        }
        return -1;
    }

    return 0;
}

int child_send_io_request(int msgqid, int io_burst, pid_t my_pid) {
    message msg;

    msg.mtype = MSG_IO_REQUEST;
    msg.data.command = MSG_IO_REQUEST;
    msg.data.value = io_burst;
    msg.data.sender_pid = my_pid;

    if (msgsnd(msgqid, &msg, sizeof(msg.data), IPC_NOWAIT) == -1) {
        if (errno != EAGAIN) {
            perror("msgsnd (io_request)");
        }
        return -1;
    }

    return 0;
}