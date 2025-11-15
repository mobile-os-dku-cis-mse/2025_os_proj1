#include "msg.h"


// ======================= VARIABLES GLOBALES =================

int msgq = -1;
int log_fd = -1;

pcb_t pcbs[NCHILD];
queue_t runq;
queue_t waitq;

volatile sig_atomic_t tick_count = 0;
volatile sig_atomic_t pending_ticks = 0;   // nombre de ticks à traiter

int current_idx = -1;   // index dans pcbs[] du processus actuellement sur le CPU

pid_t parent_pid;

// ======================= QUEUE HELPER =======================

void queue_init(queue_t *q) {
    q->head = q->tail = q->size = 0;
}

int queue_is_empty(queue_t *q) {
    return q->size == 0;
}

int queue_enqueue(queue_t *q, int idx) {
    if (q->size >= NCHILD) return -1;
    q->items[q->tail] = idx;
    q->tail = (q->tail + 1) % NCHILD;
    q->size++;
    return 0;
}

int queue_dequeue(queue_t *q) {
    if (q->size == 0) return -1;
    int idx = q->items[q->head];
    q->head = (q->head + 1) % NCHILD;
    q->size--;
    return idx;
}

// retire idx de la queue (si présent)
void queue_remove(queue_t *q, int idx) {
    if (q->size == 0) return;
    int new_items[NCHILD];
    int new_size = 0;

    for (int i = 0; i < q->size; ++i) {
        int pos = (q->head + i) % NCHILD;
        if (q->items[pos] != idx) {
            new_items[new_size++] = q->items[pos];
        }
    }

    q->head = 0;
    q->tail = new_size;
    q->size = new_size;
    for (int i = 0; i < new_size; ++i)
        q->items[i] = new_items[i];
}

// trouver index de pcb à partir du pid
int find_pcb_index(pid_t pid) {
    for (int i = 0; i < NCHILD; ++i) {
        if (pcbs[i].pid == pid) return i;
    }
    return -1;
}

// ======================= LOGGING ============================

void log_line(const char *line) {
    if (log_fd < 0) return;
    size_t len = strlen(line);
    if (len == 0) return;
    write(log_fd, line, len);
}

void dump_queues() {
    char buffer[512];

    // run-queue
    int offset = 0;
    offset += snprintf(buffer + offset, sizeof(buffer) - offset,
                       "  run-queue: [");
    for (int i = 0; i < runq.size; ++i) {
        int pos = (runq.head + i) % NCHILD;
        int idx = runq.items[pos];
        offset += snprintf(buffer + offset, sizeof(buffer) - offset,
                           "%d(pid=%d)%s",
                           idx, pcbs[idx].pid,
                           (i == runq.size - 1) ? "" : ", ");
    }
    offset += snprintf(buffer + offset, sizeof(buffer) - offset, "]\n");
    log_line(buffer);

    // wait-queue
    offset = 0;
    offset += snprintf(buffer + offset, sizeof(buffer) - offset,
                       "  wait-queue: [");
    for (int i = 0; i < waitq.size; ++i) {
        int pos = (waitq.head + i) % NCHILD;
        int idx = waitq.items[pos];
        offset += snprintf(buffer + offset, sizeof(buffer) - offset,
                           "%d(pid=%d, io=%d)%s",
                           idx, pcbs[idx].pid, pcbs[idx].remaining_io,
                           (i == waitq.size - 1) ? "" : ", ");
    }
    offset += snprintf(buffer + offset, sizeof(buffer) - offset, "]\n\n");
    log_line(buffer);
}

// ======================= TIMER / SIGNAL =====================

void timer_handler(int signo) {
    // Un tick s'est produit
    tick_count++;
    pending_ticks++;
}

// ======================= SCHEDULER TICK =====================

void scheduler_tick() {
    // 1) Mettre à jour I/O des processus en wait-queue
    for (int i = 0; i < waitq.size; ++i) {
        int pos = (waitq.head + i) % NCHILD;
        int idx = waitq.items[pos];
        if (pcbs[idx].remaining_io > 0) {
            pcbs[idx].remaining_io--;
        }
    }

    // 2) Si certains ont fini leur I/O, les remettre en run-queue
    int finished[NCHILD];
    int finished_count = 0;
    for (int i = 0; i < waitq.size; ++i) {
        int pos = (waitq.head + i) % NCHILD;
        int idx = waitq.items[pos];
        if (pcbs[idx].remaining_io <= 0) {
            finished[finished_count++] = idx;
        }
    }
    for (int i = 0; i < finished_count; ++i) {
        int idx = finished[i];
        pcbs[idx].in_io = 0;
        pcbs[idx].remaining_io = 0;
        queue_remove(&waitq, idx);
        queue_enqueue(&runq, idx);
    }

    // 3) Incrémenter le temps d'attente de tous les process en run-queue
    for (int i = 0; i < runq.size; ++i) {
        int pos = (runq.head + i) % NCHILD;
        int idx = runq.items[pos];
        pcbs[idx].waiting_time++;
    }

    // 4) Vérifier si le process courant a encore du quantum
    if (current_idx != -1) {
        if (pcbs[current_idx].in_io) {
            // par sécurité : si déjà en I/O, on le vire du CPU
            current_idx = -1;
        } else {
            pcbs[current_idx].remaining_quantum--;
            if (pcbs[current_idx].remaining_quantum <= 0) {
                // fin de quantum, Round-Robin : remettre à la fin de la run-queue
                queue_enqueue(&runq, current_idx);
                current_idx = -1;
            }
        }
    }

    // 5) Si aucun process ne tourne, en élire un nouveau
    if (current_idx == -1) {
        if (!queue_is_empty(&runq)) {
            current_idx = queue_dequeue(&runq);
            pcbs[current_idx].remaining_quantum = TIME_QUANTUM;
        }
    }

    // 6) Envoyer un tick CPU au process courant + gérer éventuellement
    //    les messages d'I/O des enfants
    if (current_idx != -1) {
        msgbuf_perso msg;
        memset(&msg, 0, sizeof(msg));
        msg.mtype = pcbs[current_idx].pid;  // l'enfant écoute sur son pid
        msg.pid = pcbs[current_idx].pid;
        msg.io_time = 0; // 0 => simple tick CPU

        if (msgsnd(msgq, &msg, sizeof(msg), 0) == -1) {
            // erreur possible si l'enfant est déjà mort, etc.
        }

        // Lire les messages des enfants qui finissent leur CPU-burst
        while (1) {
            msgbuf_perso m;
            ssize_t ret = msgrcv(msgq, &m, sizeof(m),
                                 1,  // mtype = 1 => messages pour le parent
                                 IPC_NOWAIT);
            if (ret < 0) {
                if (errno == ENOMSG) break; // plus de message
                else break;
            }
            int idx = find_pcb_index(m.pid);
            if (idx >= 0) {
                // L'enfant m.pid demande une I/O de durée m.io_time
                pcbs[idx].in_io = 1;
                pcbs[idx].remaining_io = m.io_time;
                // S'il était courant, on enlève le CPU
                if (idx == current_idx) {
                    current_idx = -1;
                } else {
                    queue_remove(&runq, idx);
                }
                queue_enqueue(&waitq, idx);
            }
        }
    }

    // 7) Logging
    if (tick_count <= MAX_TICKS_LOG) {
        char buffer[256];
        if (current_idx != -1) {
            snprintf(buffer, sizeof(buffer),
                     "(time %d) process pid=%d gets cpu time, remaining time-quantum=%d\n",
                     tick_count, pcbs[current_idx].pid, pcbs[current_idx].remaining_quantum);
        } else {
            snprintf(buffer, sizeof(buffer),
                     "(time %d) CPU idle\n", tick_count);
        }
        log_line(buffer);
        dump_queues();
    }
}

// ======================= CODE DES ENFANTS ===================

void child_loop() {
    // Chaque enfant simule: CPU-burst -> I/O-burst -> CPU-burst -> ...
    srand(getpid());

    int cpu_burst = rand() % 10 + 5;   // entre 5 et 14 ticks
    int io_burst  = rand() % 20 + 5;   // entre 5 et 24 ticks

    while (1) {
        msgbuf_perso msg;
        // Attendre un tick CPU pour ce processus (mtype = pid)
        if (msgrcv(msgq, &msg, sizeof(msg), getpid(), 0) == -1) {
            // Erreur ou arrêt, on quitte
            exit(0);
        }

        // On a reçu un "time slice" (1 tick CPU)
        cpu_burst--;

        if (cpu_burst <= 0) {
            // On a fini notre CPU-burst, on demande de l'I/O au parent
            msgbuf_perso reply;
            memset(&reply, 0, sizeof(reply));
            reply.mtype = 1;         // messages vers le parent
            reply.pid = getpid();
            reply.io_time = io_burst;
            msgsnd(msgq, &reply, sizeof(reply), 0);

            // On génère le prochain couple (cpu_burst, io_burst)
            cpu_burst = rand() % 10 + 5;
            io_burst  = rand() % 20 + 5;

            // Ensuite, on va simplement continuer à attendre des ticks CPU.
            // Le parent ne nous enverra plus de messages tant que notre I/O n'est pas terminée.
        }
    }
}

// ======================= CODE DU PARENT =====================

void parent_loop() {
    // Ouverture du fichier de log
    log_fd = open(LOG_FILENAME, O_CREAT | O_TRUNC | O_WRONLY, 0644);
    if (log_fd < 0) {
        perror("open log file");
        exit(1);
    }

    // Initialiser les queues
    queue_init(&runq);
    queue_init(&waitq);

    // Mettre tous les enfants dans la run-queue au début
    for (int i = 0; i < NCHILD; ++i) {
        pcbs[i].in_io = 0;
        pcbs[i].remaining_io = 0;
        pcbs[i].remaining_quantum = TIME_QUANTUM;
        pcbs[i].waiting_time = 0;
        queue_enqueue(&runq, i);
    }
    current_idx = -1;

    // Installer le handler SIGALRM
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = timer_handler;
    sigaction(SIGALRM, &sa, NULL);

    // Configurer le timer périodique
    struct itimerval it;
    it.it_interval.tv_sec = 0;
    it.it_interval.tv_usec = TICK_USEC; // 10ms
    it.it_value.tv_sec = 0;
    it.it_value.tv_usec = TICK_USEC;
    setitimer(ITIMER_REAL, &it, NULL);

    // Le parent va tourner au moins ~1 minute.
    // 10ms par tick => 6000 ticks ≈ 60s, donc on arrête après 6000-7000 ticks.
    while (tick_count < 7000) {
        // Attendre qu'un tick soit signalé
        pause();

        // Traiter tous les ticks en attente (au cas où plusieurs signaux sont arrivés)
        while (pending_ticks > 0) {
            pending_ticks--;
            scheduler_tick();
        }
    }

    // Arrêt : tuer les enfants proprement
    for (int i = 0; i < NCHILD; ++i) {
        kill(pcbs[i].pid, SIGTERM);
    }
    for (int i = 0; i < NCHILD; ++i) {
        wait(NULL);
    }

    close(log_fd);
}

// ======================= main ===============================

int main(int argc, char *argv[]) {
    parent_pid = getpid();

    // Créer la file de messages IPC
    int key = 0x12345; // même clé que dans tes exemples msgq.c/msgrcv.c
    msgq = msgget(key, IPC_CREAT | 0666);
    if (msgq < 0) {
        perror("msgget");
        exit(1);
    }

    // Créer 10 enfants
    for (int i = 0; i < NCHILD; ++i) {
        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            exit(1);
        } else if (pid == 0) {
            // Enfant
            child_loop();
            exit(0);
        } else {
            // Parent : stocker le pid
            pcbs[i].pid = pid;
        }
    }

    // Le processus original devient le "parent/ordonnanceur"
    if (getpid() == parent_pid) {
        parent_loop();
    }

    return 0;
}
