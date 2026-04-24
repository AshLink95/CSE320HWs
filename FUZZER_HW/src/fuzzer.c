#include "../include/fuzzer.h"
#include "../include/global.h"
#include "../include/input.h"
#include "../include/input_queue.h"
#include "../include/coverage_map.h"
#include "../include/mutator.h"
#include "../include/runner.h"
#include <signal.h>
#include <stdlib.h>
#include <string.h>

int runner_get_id(RUNNER runner);

static volatile sig_atomic_t TERM = 0;
static void handle_term(int signo) { TERM = signo; }

static volatile sig_atomic_t CHLD = 0;
static void handle_chld(int signo) { (void)signo; CHLD = 1; }

static void handle_usr1(int signo) { (void)signo; }

int run_fuzzer(FILE *seed_file, int job_count, int input_count, int time_limit, char *target_program[]) {
    TERM = 0;
    CHLD = 0;

    fzl_init(NULL);

    cmd = target_program[0];
    args = target_program + 1;
    size_t argc = 0;
    for (char **p = target_program; *p; ++p) argc++;
    program_argc = argc - 1;
    timeout = time_limit;

    struct sigaction sa;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;

    sa.sa_handler = handle_term;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGHUP, &sa, NULL);

    sa.sa_handler = SIG_IGN;
    sigaction(SIGPIPE, &sa, NULL);

    sa.sa_handler = handle_usr1;
    sigaction(SIGUSR1, &sa, NULL);

    sa.sa_handler = handle_chld;
    sigaction(SIGCHLD, &sa, NULL);

    sigset_t block;
    sigemptyset(&block);
    sigaddset(&block, SIGUSR1);
    sigaddset(&block, SIGCHLD);
    sigprocmask(SIG_BLOCK, &block, NULL);

    sigset_t wait_mask;
    sigfillset(&wait_mask);
    sigdelset(&wait_mask, SIGUSR1);
    sigdelset(&wait_mask, SIGCHLD);
    sigdelset(&wait_mask, SIGINT);
    sigdelset(&wait_mask, SIGTERM);
    sigdelset(&wait_mask, SIGHUP);

    INPUT_QUEUE queue = input_queue_init();
    COVERAGE_MAP map = coverage_map_init();
    RUNNERS runners = runners_init(job_count);

    char line[COVERAGE_MAP_SIZE];
    while (fgets(line, sizeof(line), seed_file)) {
        line[strcspn(line, "\n")] = '\0';
        if (line[0] == '\0') continue;
        INPUT seed = make_input(line);
        enqueue_high_prio_input(queue, seed);
    }

    int mutated = 0;

    while (!TERM) {
        if (CHLD) { runners_reap(runners); CHLD = 0; }

        runners_check_if_jobs_done(runners);

        while (runners_has_done_jobs(runners)) {
            RUNNER_STATE state;
            int data;
            RUNNER r = runners_process_result(runners, &state, &data);
            if (!r) break;

            fzl_received_status(runner_get_id(r), state, data, NULL);

            if (state == VALID) {
                COVERAGE_PRIORITY prio = coverage_map_add(map, runner_coverage_map(r));
                if (prio == COV_HIGH_PRIO)
                    enqueue_high_prio_input(queue, runner_get_active_input(r));
                else if (prio == COV_LOW_PRIO)
                    enqueue_low_prio_input(queue, runner_get_active_input(r));
            }
        }

        int dispatched = 0;
        while (runners_has_ready_jobs(runners)) {
            INPUT inp = dequeue_input(queue);
            if (!inp) break;

            INPUT to_submit;
            if (input_mutator_state(inp) == 0) {
                to_submit = make_input(input_str(inp));
                input_state_step(inp);
            } else {
                if (mutated >= input_count) break;
                to_submit = mutate(inp);
                mutated++;
            }

            runners_submit_input(runners, to_submit);
            free_input(to_submit);
            dispatched++;
        }

        if (!runners_has_active_jobs(runners) && !dispatched)
            break;

        if (runners_has_active_jobs(runners))
            sigsuspend(&wait_mask);
    }

    runners_fini(runners);
    input_queue_fini(queue);
    coverage_map_fini(map);

    fzl_fini(NULL);

    if (TERM) exit(EXIT_SUCCESS);
    return 0;
}
