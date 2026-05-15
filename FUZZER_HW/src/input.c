#include "../include/input.h"
#include <stddef.h>
#include <string.h>
#include <stdlib.h>

struct input {
    char* str;
    size_t len;
    MUTATOR_STATE state;
};

INPUT make_input(const char *input_str) {
    if (!input_str) return NULL;
    INPUT input = malloc(sizeof(struct input));
    input->len = strlen(input_str);
    input->state = 0;
    input->str = malloc(input->len + 1);
    strcpy(input->str, input_str);
    return input;
}

void free_input(INPUT input) {
    if (!input) return;
    free(input->str);
    free(input);
}

size_t input_len(INPUT input) {
    if (!input) return 0;
    return input->len;
}

const char *input_str(INPUT input) {
    if (!input) return NULL;
    return input->str;
}

MUTATOR_STATE input_mutator_state(INPUT input) {
    if (!input) return 0;
    return input->state;
}

MUTATOR_STATE input_set_state(INPUT input, MUTATOR_STATE state) {
    if (!input) return 0;
    MUTATOR_STATE old_state = input->state;
    input->state = state;
    return old_state;
}

MUTATOR_STATE input_state_step(INPUT input) {
    if (!input) return 0;
    input->state++;
    return input->state;
}
