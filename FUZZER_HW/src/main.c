#include "../include/fuzzer.h"
#include "../include/global.h"
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char *argv[]) {
    size_t jobs = DEFAULT_RUNNER_COUNT;
    size_t inputs = DEFAULT_INPUT_TOTAL;
    size_t tlimit = DEFAULT_TIMEOUT_SEC;
    char *seed_path = NULL;

    size_t i = 1;
    for (; i < argc && argv[i][0] == '-' && argv[i][1] != '\0'; i++) {
        char flag = argv[i][1];
        if (flag == 'h') {
            PRINT_USAGE(stdout, argv[0]);
            return EXIT_SUCCESS;
        } else if (flag == 'j') {
            if (++i >= argc) { PRINT_USAGE(stderr, argv[0]); return EXIT_FAILURE; }
            jobs = atoi(argv[i]);
        } else if (flag == 'n') {
            if (++i >= argc) { PRINT_USAGE(stderr, argv[0]); return EXIT_FAILURE; }
            inputs = atoi(argv[i]);
        } else if (flag == 's') {
            if (++i >= argc) { PRINT_USAGE(stderr, argv[0]); return EXIT_FAILURE; }
            seed_path = argv[i];
        } else if (flag == 't') {
            if (++i >= argc) { PRINT_USAGE(stderr, argv[0]); return EXIT_FAILURE; }
            tlimit = atoi(argv[i]);
        } else {
            break;
        }
    }

    if (!seed_path || i >= argc) {
        PRINT_USAGE(stderr, argv[0]);
        return EXIT_FAILURE;
    }

    FILE *seed_file = fopen(seed_path, "r");
    if (!seed_file) {
        PRINT_USAGE(stderr, argv[0]);
        return EXIT_FAILURE;
    }

    int ret = run_fuzzer(seed_file, jobs, inputs, tlimit, argv + i);
    fclose(seed_file);
    return ret ? EXIT_FAILURE : EXIT_SUCCESS;
}
