#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <sys/time.h>
#include <sys/msg.h>
#include <sys/ipc.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>

#define NUM_CHILDREN 10
#define TIME_QUANTUM 100
#define PARENT_MTYPE 1L
#define LOG_MTYPE 2L

typedef struct run_queue{
    pid_t pid;
    struct run_queue *next;
} run_queue;

typedef struct wait_queue{
    pid_t pid;
    int io_burst;
    struct wait_queue *next;
} wait_queue;

struct msg_buffer{
    long mtype;
    char mtext[100];
};

key_t key = 1234; // 메시지 큐에 대한 키값
int msqid; // 메시지큐 id
int time_quantum_counter = 0; // 타임 퀀텀
pid_t child_pids[NUM_CHILDREN]; // 자식 프로세스의 pid 배열
run_queue *Run_queue_head = NULL;
run_queue *Run_queue_tail = NULL;
wait_queue *Wait_queue_head = NULL;
wait_queue *Wait_queue_tail = NULL;
int log_fd;
volatile int keep_running = 1;

void time_handler(int signum);
void parent_main(pid_t *child_pids);
void child_main(int index);
void enqueue_run_queue(pid_t pid);
void enqueue_wait_queue(pid_t pid, int io_burst);
run_queue *dequeue_run_queue_RR();
run_queue *dequeue_run_queue_io();
void dequeue_wait_queue();
void queue_log(char *log_buffer);

int main(){
    pid_t pid = -1;
    int i;
    // 기존 메시지큐 존재시 정리
    msqid = msgget(key, 0666);
    if (msqid != -1){
        if (msgctl(msqid, IPC_RMID, NULL) == -1){
            perror("msgctl(IPC_RMID) failed");
        }
    }
    msqid = msgget(key, 0666 | IPC_CREAT);
    if(msqid == -1){
        perror("msgget() failed");
        exit(1);
    }
    for(i = 0; i<NUM_CHILDREN; i++){
        pid = fork();
        if(pid == 0){
            break;
        }else if(pid > 0){
            child_pids[i] = pid;
        }else{
            perror("fork() failed");
            exit(1);
        }
    }
    if(pid == 0){
        child_main(i);
    }else{
        parent_main(child_pids);
    }
    return 0;
}

void time_handler(int signum)
{
    static long log_time_tick = 0;
    log_time_tick++;
    dequeue_wait_queue();
    if (Run_queue_head == NULL)
    {
        time_quantum_counter = 0;
        return;
    }
    pid_t target_pid = Run_queue_head->pid;
    struct msg_buffer msg;
    msg.mtype = target_pid;

    sprintf(msg.mtext, "%ld",log_time_tick);
    int msg_size = strlen(msg.mtext) + 1;

    if (msgsnd(msqid, &msg, msg_size, 0) == -1)
    {
        perror("time handler : msgsnd failed");
    }
    time_quantum_counter++;
    if (time_quantum_counter >= TIME_QUANTUM)
    {
        time_quantum_counter = 0;
        run_queue *remove_node = dequeue_run_queue_RR();
        if (remove_node != NULL)
        {
            enqueue_run_queue(remove_node->pid);
            free(remove_node);
        }
    }
    if (log_time_tick > 10000)
    {
        keep_running = 0;
        return;
    }
}

void child_main(int index){
    srand(time(NULL) + getpid()); // 시간단위 + pid로 고유의 시드 설정
    int cpu_burst;
    int io_burst;
    if (index < 3)
    {
        cpu_burst = (rand() % 51) + 10;  // 0, 1, 2번 인덱스는 타임퀀텀보다 작은 cpu작업
        io_burst = (rand() % 101) + 150; // 대신 긴 io 버스트
    }
    else if (index < 6)
    {
        cpu_burst = (rand() % 101) + 50; // 3,4,5 번 인덱스는 타임퀀텀보다 큰 cpu 작업
        io_burst = (rand() % 101) + 50; // 적당한 io 버스트
    }
    else
    {
        cpu_burst = (rand() % 301) + 200; // 6, 7, 8, 9번 인덱스는 타임퀀텀보다 매우 큰 cpu작업
        io_burst = (rand() % 51) + 10;    // 짧은 io버스트
    }
    struct msg_buffer recieve_msg;
    pid_t pid = getpid();
    while (1)
    {
        if (msgrcv(msqid, &recieve_msg, 100, pid, 0) == -1)
        {
            perror("child : msgrcv failed");
        }
        else
        {
            long current_tick;
            sscanf(recieve_msg.mtext, "%ld", &current_tick);
            cpu_burst--;
            struct msg_buffer log_msg; // 로그 출력을 위한 cpu burst 전달 메시지
            log_msg.mtype = LOG_MTYPE;
            sprintf(log_msg.mtext, "%d %d %ld", pid, cpu_burst,current_tick);
            int msg_size = strlen(log_msg.mtext) + 1;
            if (msgsnd(msqid, &log_msg, msg_size, 0) == -1)
            {
                perror("child : log msgsnd failed");
            }
            if (cpu_burst == 0)
            {
                struct msg_buffer msg_to_parent; // child process가 parent process에게 보내는 메시지
                msg_to_parent.mtype = PARENT_MTYPE;
                sprintf(msg_to_parent.mtext, "%d %d", getpid(), io_burst);
                int msg_size = strlen(msg_to_parent.mtext) + 1;

                if (msgsnd(msqid, &msg_to_parent, msg_size, 0) == -1)
                {
                    perror("child : msgsnd to parent failed");
                }
                // 다음 싸이클을 위한 새로운 burst값 생성
                if (index < 3)
                {
                    cpu_burst = (rand() % 51) + 10;
                    io_burst = (rand() % 101) + 150;
                }
                else if (index < 6)
                {
                    cpu_burst = (rand() % 101) + 50;
                    io_burst = (rand() % 101) + 50;
                }
                else
                {
                    cpu_burst = (rand() % 301) + 200;
                    io_burst = (rand() % 51) + 10;
                }
            }
        }
    }
}

void parent_main(pid_t *child_pids){
    log_fd = open("schedule_dump.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (log_fd == -1)
    {
        perror("open log file failed");
        exit(1);
    }
    signal(SIGALRM, time_handler);
    struct itimerval timer_val;
    timer_val.it_value.tv_sec = 0;
    timer_val.it_value.tv_usec = 10000;

    timer_val.it_interval.tv_sec = 0;
    timer_val.it_interval.tv_usec = 10000; // 타임 틱(0.01초)

    for (int i = 0; i < NUM_CHILDREN; i++)
    {
        enqueue_run_queue(child_pids[i]);
    }

    if (setitimer(ITIMER_REAL, &timer_val, NULL) == -1)
    { // 타이머 시작
        perror("settimer() failed");
        exit(1);
    }
    struct msg_buffer rcv_io;
    struct msg_buffer rcv_log;
    while (1){
        pause();
        if (msgrcv(msqid, &rcv_io, 100, PARENT_MTYPE, IPC_NOWAIT) != -1)
        {
            // 메시지 수신 (자식 프로세스가 cpu 버스트가 완료됨을 알림)
            pid_t request_pid;
            int request_io_burst;
            sscanf(rcv_io.mtext, "%d %d", &request_pid, &request_io_burst);
            int reset_quantum = 0;
            if (Run_queue_head != NULL && Run_queue_head->pid == request_pid){
                reset_quantum = 1;
            }
            run_queue *move_node = dequeue_run_queue_io(request_pid);
            if (move_node != NULL){
                enqueue_wait_queue(request_pid, request_io_burst);
                free(move_node);
                if(reset_quantum) time_quantum_counter = 0;
            }
        }
        else
        {
            // 메시지 수신 X
            if (errno != ENOMSG)
                perror("parent : msgrcv failed"); // 메시지 없음 이외의 다른 메시지일 경우 오류 발생
        }
        if (msgrcv(msqid, &rcv_log, 100, LOG_MTYPE, IPC_NOWAIT) != -1)
        {
            // 자식 프로세스가 cpu를 사용할때마다 로그 기록을 위한 메시지 전송
            pid_t send_pid;
            int remain_cpu_burst;
            long recieve_tick;
            sscanf(rcv_log.mtext, "%d %d %ld", &send_pid, &remain_cpu_burst, &recieve_tick);
            char log_buffer[400]; // 로그 출력 메시지
            sprintf(log_buffer, "(at time %ld, process %d gets cpu time, remaining cpu-burst %d)\n", recieve_tick, send_pid, remain_cpu_burst);
            queue_log(log_buffer);
            write(log_fd, log_buffer, strlen(log_buffer));
        }
        else
        {
            if (errno != ENOMSG)
                perror("parent : msgrcv(mtype 2) failed");
        }
        if (keep_running == 0) break;
    }
    for(int i = 0; i< NUM_CHILDREN; i++){
        kill(child_pids[i], SIGTERM);
        waitpid(child_pids[i], NULL, 0);
    }
    msgctl(msqid, IPC_RMID, NULL);
    close(log_fd);
}

void enqueue_run_queue(pid_t pid)
{
    run_queue *new_node = (run_queue *)malloc(sizeof(run_queue));
    if (new_node == NULL)
    {
        perror("malloc failed");
        return;
    }
    new_node->pid = pid;
    new_node->next = NULL;
    if (Run_queue_head == NULL)
    {
        // run-queue가 비었을 경우 연결 리스트의 첫 노드 채우기
        Run_queue_head = new_node;
        Run_queue_tail = new_node;
    }
    else
    {
        // 큐에 노드가 존재할경우, 기존의 마지막 노드가 새로운 노드를 가리키도록 변경 후 tail 포인터 갱신
        Run_queue_tail->next = new_node;
        Run_queue_tail = new_node;
    }
}

void enqueue_wait_queue(pid_t pid, int io_burst)
{
    wait_queue *new_node = (wait_queue *)malloc(sizeof(wait_queue));
    if (new_node == NULL)
    {
        perror("malloc failed");
        return;
    }
    new_node->pid = pid;
    new_node->io_burst = io_burst;
    new_node->next = NULL;
    if (Wait_queue_head == NULL)
    {
        Wait_queue_head = new_node;
        Wait_queue_tail = new_node;
    }
    else
    {
        Wait_queue_tail->next = new_node;
        Wait_queue_tail = new_node;
    }
}

run_queue *dequeue_run_queue_RR(){
    // Run queue의 헤드 노드의 연결을 끊고 반환하는 함수
    if (Run_queue_head == NULL)
    {
        return NULL;
    }
    run_queue *node_to_remove = Run_queue_head;
    Run_queue_head = Run_queue_head->next;
    if (Run_queue_head == NULL)
    {
        Run_queue_tail = NULL;
    }
    node_to_remove->next = NULL;
    return node_to_remove;
}

run_queue *dequeue_run_queue_io(pid_t remove_pid){
    // 경쟁상태로 인해 잘못된 프로세스를 제거할 수 있으므로 pid 순회를 통한 프로세스 제거
    run_queue *current = Run_queue_head;
    run_queue *previous = NULL;
    while(current != NULL){
        if(current->pid == remove_pid){
            if(previous == NULL){
                Run_queue_head = current->next; // head를 제거하는 경우
            }else{
                previous->next = current->next; // middle을 제거하는 경우
            }
            if(current == Run_queue_tail){
                Run_queue_tail = previous; // tail을 제거하는 경우
            }
            current->next = NULL; // 제거할 노드 분리
            return current;
        }
        previous = current;
        current = current->next;
    }
    return NULL;
}


void dequeue_wait_queue(){
    wait_queue *previous = NULL;
    wait_queue *nextNode = NULL;
    wait_queue *current = Wait_queue_head;
    while (current != NULL){
        nextNode = current->next;
        current->io_burst--;
        if (current->io_burst == 0){
            pid_t move_pid = current->pid;
            if (previous == NULL){
                Wait_queue_head = nextNode; // 제거하는 노드가 헤드노드인 경우
            }
            else{
                previous->next = nextNode; // 중간이나 테일인 경우
            }
            if (current == Wait_queue_tail){
                Wait_queue_tail = previous;
            }
            enqueue_run_queue(move_pid);
            free(current);
            current = nextNode;
        }
        else{
            previous = current;
            current = nextNode;
        }
    }
}

void queue_log(char *log_buffer)
{
    // 준비 큐랑 대기 큐를 출력하기 위한 함수
    sprintf(log_buffer + strlen(log_buffer), "RunQueue : [");
    run_queue *run_q = Run_queue_head;
    while (run_q != NULL)
    {
        sprintf(log_buffer + strlen(log_buffer), "%d, ", run_q->pid);
        run_q = run_q->next;
    }
    sprintf(log_buffer + strlen(log_buffer), "]\nWaitQueue : [");
    wait_queue *wait_q = Wait_queue_head;
    while (wait_q != NULL)
    {
        sprintf(log_buffer + strlen(log_buffer), "%d(%d), ", wait_q->pid, wait_q->io_burst);
        wait_q = wait_q->next;
    }
    sprintf(log_buffer + strlen(log_buffer), "]\n\n");
}