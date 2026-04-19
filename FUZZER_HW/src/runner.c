#include "../include/runner.h"
#include "../include/global.h"
#include <bits/types/sigset_t.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/select.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <string.h>
#include <wait.h>

static int SIZE_ID = 0;
struct runner {
    int id;
    INPUT input;

    int pipe2runnr[2]; // runner reads from 0, fuzzer writes to 1
    int pipe2fuzzr[2]; // reverse of prev (fuzzer reads & runner writes)

    pid_t parent;
    pid_t child;

    int shm_obj;    // current shared memory object aka fd (shm_open)
    char name[32];  // (shm_unlink)
    char* shm_map;  // of size COVERAGE_MAP_SIZE (mmap)
};

RUNNER runner_init() {
    RUNNER runner = malloc(sizeof(struct runner));
    runner->id = SIZE_ID++;
    runner->parent = getpid();
    runner->child = 0;
    runner->input = NULL;

    pipe(runner->pipe2runnr);
    pipe(runner->pipe2fuzzr);
    fcntl(runner->pipe2fuzzr[0], F_SETFL, O_NONBLOCK);

    snprintf(runner->name, sizeof(runner->name), "/fz_rnr_%d", runner->id);
    runner->shm_obj = shm_open(runner->name, O_CREAT | O_RDWR, 0600); //perms
    ftruncate(runner->shm_obj, COVERAGE_MAP_SIZE);
    runner->shm_map = mmap(NULL, COVERAGE_MAP_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, runner->shm_obj, 0);
    return runner;
}

void runner_fini(RUNNER runner) {
    if (runner->input != NULL) free_input(runner->input);

    if (runner->child > 0) {
        kill(runner->child, SIGTERM);
        waitpid(runner->child, NULL, 0);  // reap to prevent zombie
    }

    close(runner->pipe2runnr[0]);
    close(runner->pipe2runnr[1]);
    close(runner->pipe2fuzzr[0]);
    close(runner->pipe2fuzzr[1]);

    shm_unlink(runner->name);
    close(runner->shm_obj);
    munmap(runner->shm_map, COVERAGE_MAP_SIZE);
    free(runner);
}

char *runner_coverage_map(RUNNER runner) {
    return runner->shm_map;
}

INPUT runner_get_active_input(RUNNER runner) {
    return runner->input;
}

int fuzzer_send_runner_input(RUNNER runner, INPUT input) {
    ssize_t ret = write(runner->pipe2runnr[1], input_str(input), input_len(input)+1);
    if (ret < 0) return ret;
    return 0;
}

char * runner_receive_fuzzer_input(RUNNER runner) {
    sigset_t* sig_mask = malloc(sizeof(sigset_t));
    sigfillset(sig_mask);
    sigdelset(sig_mask, SIGINT);
    sigdelset(sig_mask, SIGTERM);
    sigdelset(sig_mask, SIGHUP);

    fd_set* readfds = malloc(sizeof(fd_set));
    FD_ZERO(readfds);
    FD_SET(runner->pipe2runnr[0], readfds);

    int ret = pselect(runner->pipe2runnr[0]+1, readfds, NULL, NULL, NULL, sig_mask);
    free(sig_mask); free(readfds);
    if (ret < 0) return NULL;

    size_t mb = 1048576;
    char* buf = malloc(1048576);
    ret = read(runner->pipe2runnr[0], buf, mb);
    if (ret < 0) {free(buf); return NULL;}

    size_t len = 0;
    while (buf[len] != '\0') {
        len++;
    }
    buf = realloc(buf, len+1);
    return buf;
}

int runner_alert_fuzzer(RUNNER runner, RUNNER_STATE state, int data) {
    if (write(runner->pipe2fuzzr[1], &state, sizeof(state)) == -1) return -1;
    if (write(runner->pipe2fuzzr[1], &data, sizeof(data)) == -1) return -1;

    kill(runner->parent, SIGUSR1);
    return 0;
}

RUNNER_STATE fuzzer_attempt_receive_status(RUNNER runner, int *data) {
    RUNNER_STATE state;
    if (read(runner->pipe2fuzzr[0], &state, sizeof(state)) <= 0) return NO_STATE;

    int tada;
    if (read(runner->pipe2fuzzr[0], &tada, sizeof(tada)) <= 0) return NO_STATE;
    if (data && state != TIMEOUT) *data = tada;
    return state;
}

static volatile sig_atomic_t TERM = 0;
static void handle_term(int signo) { TERM = signo; };

static volatile sig_atomic_t CHILD = 0;
static void handle_child(int signo) { CHILD = signo; };

static volatile sig_atomic_t ALARM = 0;
static void handle_alarm(int signo) { ALARM = signo; };

int runner_launch(RUNNER runner) {
    runner->parent = getpid();
    pid_t pid = fork();
    if (pid > 0) runner->child = pid;
    else if (!pid) {
        TERM = 0;
        CHILD = 0;
        ALARM = 0;

        struct sigaction* sa = malloc(sizeof(struct sigaction));
        sa->sa_handler = handle_term;
        sigemptyset(&sa->sa_mask);
        sa->sa_flags = SA_RESTART;
        if (sigaction(SIGINT, sa, NULL) == -1) _exit(1); // pervent running parent code
        if (sigaction(SIGTERM, sa, NULL) == -1) _exit(1);
        if (sigaction(SIGHUP, sa, NULL) == -1) _exit(1);
        sa->sa_handler = SIG_IGN;
        if (sigaction(SIGPIPE, sa, NULL) == -1) _exit(1);
        sa->sa_handler = handle_child;
        if (sigaction(SIGCHLD, sa, NULL) == -1) _exit(1);
        sa->sa_handler = handle_alarm;
        if (sigaction(SIGALRM, sa, NULL) == -1) _exit(1);
        free(sa);

        close(runner->pipe2runnr[1]);
        close(runner->pipe2fuzzr[0]);
        dup2(runner->shm_obj, COVERAGE_MAP_FD);

        while (!TERM) {
            if (runner->input == NULL) {
                char *recv = runner_receive_fuzzer_input(runner);
                if (!recv) break;
                runner->input = make_input(recv);
                free(recv);
            }

            pid_t target = fork();
            if (target == 0) {
                int devnull = open("/dev/null", O_RDWR);
                dup2(devnull, STDIN_FILENO);
                dup2(devnull, STDOUT_FILENO);
                close(devnull);

                const char *input_str_val = input_str(runner->input);
                char *exec_args[program_argc + 2];
                exec_args[0] = cmd; // the program to run
                for (size_t i = 0; i < program_argc; i++) {
                    if (args[i] && strcmp(args[i], PROGRAM_ARGUMENT_PLACEHOLDER) == 0)
                        exec_args[i + 1] = (char *)input_str_val;
                    else exec_args[i + 1] = args[i];
                }
                exec_args[program_argc + 1] = NULL;

                execvp(cmd, exec_args);
                _exit(127);
            }
            if (target == -1) _exit(1);

            ALARM = 0;
            CHILD = 0;
            alarm(timeout);

            sigset_t wait_mask;
            sigfillset(&wait_mask);
            sigdelset(&wait_mask, SIGCHLD);
            sigdelset(&wait_mask, SIGALRM);
            sigdelset(&wait_mask, SIGINT);
            sigdelset(&wait_mask, SIGTERM);
            sigdelset(&wait_mask, SIGHUP);

            while (!CHILD && !ALARM && !TERM)
                sigsuspend(&wait_mask);

            if (TERM) {
                kill(target, SIGKILL);
                waitpid(target, NULL, 0);
                break;
            }

            int status;
            if (ALARM) {
                kill(target, SIGKILL);
                waitpid(target, &status, 0);
                runner_alert_fuzzer(runner, TIMEOUT, 0);
            } else if (CHILD) {
                alarm(0);
                waitpid(target, &status, 0);
                if (WIFSIGNALED(status)) {
                    runner_alert_fuzzer(runner, CRASH, WTERMSIG(status));
                } else {
                    runner_alert_fuzzer(runner, VALID, WEXITSTATUS(status));
                }
            }

            free_input(runner->input);
            runner->input = NULL;
            ALARM = 0;
            CHILD = 0;
        }
        _exit(0);
    }
    else return -1;

    close(runner->pipe2runnr[0]);
    close(runner->pipe2fuzzr[1]);

    return 0;
}


struct node {
    RUNNER runner;
    RUNNER_STATE state;
    int cached_data; //preserves pipes
    struct node* next;
    struct node* prev;
};

struct queue {
    struct node* head;
    struct node* tail;
};

struct runners {
    struct queue ready;
    struct queue active;
    struct queue done;
};

static struct queue rq_init() {
    struct queue qu;
    qu.head = malloc(sizeof(struct node));
    qu.tail = malloc(sizeof(struct node));

    qu.head->runner = NULL;
    qu.tail->runner = NULL;

    qu.head->next = qu.tail;
    qu.head->prev = NULL;
    qu.tail->next = NULL;
    qu.tail->prev = qu.head;

    return qu;
}

static void rq_kill(struct queue qu) {
    struct node* now = qu.head->next;
    struct node* next;

    while (now != qu.tail) {
        next = now->next;
        if (now->runner != NULL) runner_fini(now->runner);
        free(now);
        now = next;
    }

    free(qu.head);
    free(qu.tail);
}

static int rq_empty(struct queue *qu) {
    return qu->head->next == qu->tail;
}

static void rq_enqueue(struct queue *qu, RUNNER runner) {
    struct node* last = qu->tail->prev;
    struct node* new = malloc(sizeof(struct node));
    new->runner = runner;
    new->next = qu->tail;
    new->prev = last;
    qu->tail->prev = new;
    last->next = new;
}

static RUNNER rq_dequeue(struct queue *qu) {
    if (rq_empty(qu)) return NULL;
    struct node* first = qu->head->next;
    RUNNER runner = first->runner;

    qu->head->next = first->next;
    first->next->prev = qu->head;
    free(first);

    return runner;
}

static void rq_remove_node(struct node *n) {
    n->prev->next = n->next;
    n->next->prev = n->prev;
    free(n);
}

RUNNERS runners_init(int job_count) {
    RUNNERS runners = malloc(sizeof(struct runners));
    runners->ready = rq_init();
    runners->active = rq_init();
    runners->done = rq_init();

    for (int i = 0; i < job_count; i++) {
        RUNNER r = runner_init();
        runner_launch(r);
        rq_enqueue(&runners->ready, r);
    }

    return runners;
}

void runners_fini(RUNNERS runners) {
    rq_kill(runners->ready);
    rq_kill(runners->active);
    rq_kill(runners->done);
    free(runners);
}

int runners_submit_input(RUNNERS runners, INPUT input) {
    RUNNER r = rq_dequeue(&runners->ready);
    if (!r) return -1;

    int ret = fuzzer_send_runner_input(r, input);
    if (ret == -1) {
        rq_enqueue(&runners->ready, r);
        return -1;
    }

    rq_enqueue(&runners->active, r);
    return 0;
}

int runners_has_jobs(RUNNERS runners) {
    return !rq_empty(&runners->ready) || !rq_empty(&runners->active) || !rq_empty(&runners->done);
}

int runners_has_active_jobs(RUNNERS runners) {
    return !rq_empty(&runners->active);
}

int runners_has_done_jobs(RUNNERS runners) {
    return !rq_empty(&runners->done);
}

int runners_has_ready_jobs(RUNNERS runners) {
    return !rq_empty(&runners->ready);
}

void runners_check_if_jobs_done(RUNNERS runners) {
    struct node* cur = runners->active.head->next;
    while (cur != runners->active.tail) {
        struct node* next = cur->next;
        int data = 0;
        RUNNER_STATE state = fuzzer_attempt_receive_status(cur->runner, &data);
        if (state != NO_STATE) {
            RUNNER r = cur->runner;
            RUNNER_STATE s = state;
            int d = data;
            rq_remove_node(cur);
            struct node* last = runners->done.tail->prev;
            struct node* new = malloc(sizeof(struct node));
            new->runner = r;
            new->state = s;
            new->cached_data = d;
            new->next = runners->done.tail;
            new->prev = last;
            runners->done.tail->prev = new;
            last->next = new;
        }
        cur = next;
    }
}

RUNNER runners_process_result(RUNNERS runners, RUNNER_STATE *state, int *data) {
    if (rq_empty(&runners->done)) return NULL;

    struct node* first = runners->done.head->next;
    RUNNER r = first->runner;
    if (state) *state = first->state;
    if (data) *data = first->cached_data;

    runners->done.head->next = first->next;
    first->next->prev = runners->done.head;
    free(first);

    rq_enqueue(&runners->ready, r);
    return r;
}

int runners_reap(RUNNERS runners) {
    struct queue *queues[] = { &runners->ready, &runners->active, &runners->done };
    for (int q = 0; q < 3; q++) {
        struct node* cur = queues[q]->head->next;
        while (cur != queues[q]->tail) {
            waitpid(cur->runner->child, NULL, WNOHANG);
            struct node* next = cur->next;
            cur = next;
        }
    }
    return 0;
}
