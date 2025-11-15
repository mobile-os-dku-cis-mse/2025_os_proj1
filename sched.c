#include <pthread.h>
#include <sched.h>
#include <stdio.h>

#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>

#include <stdlib.h> 
#include <time.h>

#include "msg.h"
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/time.h>
#include <signal.h>


const int tick = 100; // µs 마다s
const int tq = 10;// tq틱 마다 
// pcb 저장
typedef struct PCB {
	int pid;
	int cpu_burst;
	int io_burst;
	int waiting_time;
	int enter_time;
	struct PCB *nextNode;
} pcb;
typedef struct Queue{
	pcb *head;
	pcb *tail;
	int size;
} queue;

typedef struct pcb_msg {
	long mtype;
	int pid;
	int cpu_burst;
	int io_burst;
} msg;

typedef struct option{
	long mtype;
	int op;
} cmd;

typedef struct {
	int pid;
	int waiting;
} Stat;

int now_time = 0;

// ---------- 로그 유틸리티 --------------

static int logfd = -1;

static void dump_queues_tick(int fd, int t,
                             int running_pid, int running_rem_cpu,
                             queue *run_q, queue *wait_q)
{
    char buf[4096];
    int off = 0;

    if (running_pid >= 0)
        off += snprintf(buf+off, sizeof(buf)-off,
            "(at time %d, process %d gets cpu time, remaining cpu-burst=%d)\n",
            t, running_pid, running_rem_cpu);
    else
        off += snprintf(buf+off, sizeof(buf)-off,
            "(at time %d, IDLE)\n", t);

    // run-queue dump
    off += snprintf(buf+off, sizeof(buf)-off, "run-queue: ");
    for (pcb *p = run_q->head; p; p = p->nextNode) {
        off += snprintf(buf+off, sizeof(buf)-off,
            "[pid=%d cpu=%d io=%d wt=%d] ",
            p->pid, p->cpu_burst, p->io_burst, p->waiting_time);
        if (off >= (int)sizeof(buf)) break;
    }
    off += snprintf(buf+off, sizeof(buf)-off, "\n");

    // wait-queue dump
    off += snprintf(buf+off, sizeof(buf)-off, "wait-queue: ");
    for (pcb *p = wait_q->head; p; p = p->nextNode) {
        off += snprintf(buf+off, sizeof(buf)-off,
            "[pid=%d cpu=%d io=%d wt=%d] ",
            p->pid, p->cpu_burst, p->io_burst, p->waiting_time);
        if (off >= (int)sizeof(buf)) break;
    }
    off += snprintf(buf+off, sizeof(buf)-off, "\n\n");

    if (off > 0) write(fd, buf, off);
}

Stat stats[10];
int stat_i = 0;
void end_process(pcb *p){
	stats[stat_i].pid = p->pid;
	stats[stat_i].waiting = p->waiting_time;
	stat_i++;
}
void dump_stats(void){
	for(int i = 0; i < 10; i++){
		printf("PID: %d, waiting time: %d\n", stats[i].pid, stats[i].waiting);
	}
}
static void dump_stats2(int fd){
    char buf[4096];
    int off = 0;
    long sum = 0;

    off += snprintf(buf+off, sizeof(buf)-off, "==== Waiting Time Summary ====\n");
    for (int i = 0; i < stat_i; i++){
        off += snprintf(buf+off, sizeof(buf)-off, "pid=%d waiting=%d\n", stats[i].pid, stats[i].waiting);
        sum += stats[i].waiting;
        if (off > (int)sizeof(buf)-128) { write(fd, buf, off); off = 0; }
    }
    double avg = (stat_i > 0) ? (double)sum / stat_i : 0.0;
    off += snprintf(buf+off, sizeof(buf)-off, "Average waiting time = %.2f\n\n", avg);
    if (off > 0) write(fd, buf, off);
}

// ----------------- 큐 유틸 함수 -------------------

void init(queue *q){
	q->head = q->tail = NULL;
	q->size = 0;
}

int isEmpty(queue *q){
	return q->size == 0;
}

void push(queue *q, pcb *p){
	p->nextNode = NULL;
	if(!isEmpty(q)){
		q->tail->nextNode = p;
		q->tail = p;
	}else{
		q->head = q->tail = p;
	}
	q->size++;
}

pcb* pop(queue *q){
	if(isEmpty(q)) return NULL;
	pcb *p = q->head;
	q->head = p->nextNode;
	if(q->head == NULL) q->tail = NULL;
	p->nextNode = NULL;
	q->size--;
	return p;
}

// wait_q의 모든 노드 처리: io_burst 감소 → 완료시 이동/해제 
void reduce(queue *wait_q, queue *run_q)
{
    if (!wait_q || !wait_q->head) return;
    pcb *prev = NULL;
    pcb *cur  = wait_q->head;
    while (cur) {
        if (cur->io_burst > 0) cur->io_burst--;
        // 2) I/O 완료 처리
        if (cur->io_burst == 0) {
            pcb *done = cur;
            pcb *next = cur->nextNode;

            // wait_q에서 제거
            if (prev) prev->nextNode = next;
            else{
				wait_q->head   = next;
			}   
            if (done == wait_q->tail) wait_q->tail = prev;
            wait_q->size--;
            done->nextNode = NULL;

            // run_q로 이동 또는 해제
            if (done->cpu_burst > 0) {
				done->enter_time = now_time;
                push(run_q, done);
            } else {
				end_process(done);
                free(done);
            }
            // 다음 노드로 진행 (prev 그대로)
            cur = next;
        } else {
            // 아직 I/O 남음 → 계속 대기
            prev = cur;
            cur  = cur->nextNode;
        }
    }
}

// ------------- 기타 함수 -------------------------

sig_atomic_t ticks = 0;
static void on_alarm(int signo){
	ticks++;
}
void send_msg(int msgid, int cpu, int io){
	msg r;
	r.mtype = 2;
	r.cpu_burst = cpu;
	r.io_burst = io;
	if (msgsnd(msgid, &r, sizeof(r)-sizeof(long), 0) == -1) perror("child msgsnd");
}


int main(int argc, char * arg[])
{
	logfd = open("schedule_dump.txt", O_CREAT|O_TRUNC|O_WRONLY, 0644);
	
	if (logfd < 0) { perror("open log"); exit(1); }

	int msgid = msgget(IPC_PRIVATE, 0600);
	if(msgid == -1) {
		perror("msgget");
		exit(1);
	}
	
	// 타이머 tick 설정
	struct sigaction sa = {0};
	sa.sa_handler = on_alarm;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART;
	if (sigaction(SIGALRM, &sa, NULL) == -1){ perror("sigaction"); exit(1); }


	struct itimerval it = {0};
	// 마이크로초 -> 1,000,000 마이크로초 = 1 초
    it.it_value.tv_usec = tick;
    it.it_interval = it.it_value;
	if (setitimer(ITIMER_REAL, &it, NULL) == -1){ perror("setitimer"); exit(1); }
		printf("[parent] timer started: %ld µs per tick\n",
       it.it_value.tv_usec);
	
	// 큐 초기화
	queue run_queue;
	queue wait_queue;
	init(&run_queue);
	init(&wait_queue);
	
    srand((unsigned)time(NULL));

	// 프로세스 cpu, io burst 초기화 10개
	for(int i = 0; i < 10; i++){
		pcb *p = malloc(sizeof(*p));
		p->pid = 1000 + i;
		p->cpu_burst = (rand()%1000)+100; 
		p->io_burst = (rand()%1000)+300;
		p->waiting_time = 0;
		p->enter_time = now_time;
        push(&run_queue, p);
	}
 
	// 시그널 대기용 빈 마스크
    sigset_t empty; 
	sigemptyset(&empty);

	while(!isEmpty(&run_queue) || !isEmpty(&wait_queue)){
		if (isEmpty(&run_queue)) {
			// I/O 완료를 기다림
			int used_tick = 0;
			while (isEmpty(&run_queue) && !isEmpty(&wait_queue)) {
				while (ticks == 0) sigsuspend(&empty);
				ticks--;
				now_time++;
				reduce(&wait_queue, &run_queue);
				if (++used_tick == tq) {
					dump_queues_tick(logfd, now_time, -1, 0, &run_queue, &wait_queue);
					used_tick = 0;
				}
			}
			if (isEmpty(&run_queue)) break; // 모두 끝난 경우
		}
		
		// 실행 pcb 꺼내오기
		pcb *cur = pop(&run_queue);
		// 대기시간 계산
		cur->waiting_time += (now_time - cur->enter_time);

		// 자식 부모 분리
		pid_t pid = fork();
		if(pid < 0){
			perror("fork error");
			cur->enter_time = now_time;
			push(&run_queue, cur);
			continue;

		}else if(pid > 0){
			// parent

			// 자식에게 tick 보내기 -> cpu 감소
			cmd ticksnd;
			ticksnd.mtype = 1;
			ticksnd.op = 1;

			int shadow_cpu = cur->cpu_burst;
			// 매 틱마다 실행되는 코드 tick
			for(int i = 0; i < tq; i++){ 
				while (ticks == 0) sigsuspend(&empty);  // ← 틱 기다리기
   				ticks--;
				now_time++;
				if(msgsnd(msgid, &ticksnd, sizeof(cmd) - sizeof(long), 0) == -1 ){
					perror("parent INIT msgsnd");
				}

				// 자식과 동일하게 1틱 감소 반영(로그용)
				if (shadow_cpu > 0) shadow_cpu--;
				reduce(&wait_queue, &run_queue);
				if(shadow_cpu == 0) break;
			}
			 // ── 로그 ──
			dump_queues_tick(logfd, now_time, cur->pid, shadow_cpu, &run_queue, &wait_queue);
			
			ticksnd.op = 0;
			if(msgsnd(msgid, &ticksnd, sizeof(cmd) - sizeof(long), 0) == -1 ){
				perror("parent INIT msgsnd");
			}
				
			msg m;
			while (1) {
				int n = msgrcv(msgid, &m, sizeof(m)-sizeof(long), 2, 0);
				if (n >= 0) break;
				if (errno == EINTR) continue;
				perror("parent msgrcv"); break;
			}

			waitpid(pid, NULL, 0);

			if(m.io_burst > 0){ 
				cur->io_burst = m.io_burst;
				push(&wait_queue, cur);
			}else if(m.cpu_burst > 0){ 
				cur->cpu_burst = m.cpu_burst;
				cur->enter_time = now_time;
				push(&run_queue, cur);
			}else{ // io_burst == 0, cpu_burst == 0
				end_process(cur);
				free(cur);
			}

		}else{
			// child

			// 자식 타이머 해제
			struct itimerval zero = {0};
			setitimer(ITIMER_REAL, &zero, NULL);

			int cpu = cur->cpu_burst;
			int io = cur->io_burst;
		
			while(1){
				cmd c;
				if (msgrcv(msgid, &c, sizeof(c)-sizeof(long), 1, 0) == -1) {
					if (errno == EINTR) continue;
					perror("child msgrcv"); _exit(1);
				}
				
				switch(c.op){
					case 0:
						send_msg(msgid, cpu, io);
            			_exit(0);
					case 1:
						if(cpu == 0){
							send_msg(msgid, cpu, io);
							_exit(0);
						}
						cpu--;
						break;
					default:
						break;
				}
			}
			_exit(0);
		}
	}
	msgctl(msgid, IPC_RMID, NULL);
	dump_stats();
	dump_stats2(logfd);
	// main() 종료 직전
	if (logfd >= 0) close(logfd);
	return 0;
}

