#include "../include/input_queue.h"
#include <stdint.h>
#include <stdlib.h>

struct node {
    INPUT input;
    struct node* next;
    struct node* prev;
};

struct queue {
    struct node* head; //empty node (prev and input are NULL)
    struct node* tail; //empty node (next and input are NULL)
};

struct input_queue {
    struct queue high_priority;
    struct queue low_priority;
    uint8_t count; // 9H1L
};

static struct queue init_queue() {
    struct queue qu;
    qu.head = malloc(sizeof(struct node));
    qu.tail = malloc(sizeof(struct node));

    qu.head->input = NULL;
    qu.tail->input = NULL;

    qu.head->next = qu.tail;
    qu.head->prev = NULL;
    qu.tail->next = NULL;
    qu.tail->prev = qu.head;

    return qu;
}

static void kill_queue(struct queue qu) {
    struct node* now = qu.head->next;
    struct node* next;

    while (now != qu.tail) {
        next = now->next;
        if (now->input != NULL) free_input(now->input);
        next->prev = now->prev;
        free(now);
        now = next;
    }

    free(qu.head);
    free(qu.tail);
}

INPUT_QUEUE input_queue_init() {
    INPUT_QUEUE queue = malloc(sizeof(struct input_queue));
    queue->high_priority = init_queue();
    queue->low_priority = init_queue();
    queue->count = 0;
    return queue;
}

void input_queue_fini(INPUT_QUEUE queue) {
    kill_queue(queue->high_priority);
    kill_queue(queue->low_priority);
    free(queue);
}

void enqueue_high_prio_input(INPUT_QUEUE queue, INPUT input) {
    struct node* tail = queue->high_priority.tail;
    struct node* cur = queue->high_priority.head;
    while (cur != tail) {
        if (cur->input == input) return;
        cur = cur->next;
    }

    struct node* taol = queue->low_priority.tail;
    cur = queue->low_priority.head;
    while (cur != taol) {
        if (cur->input == input) return;
        cur = cur->next;
    }

    struct node* recent = tail->prev;
    struct node* new = malloc(sizeof(struct node));
    new->input = input;
    new->next = tail;
    new->prev = recent;
    tail->prev = new;
    recent->next = new;
}

void enqueue_low_prio_input(INPUT_QUEUE queue, INPUT input) {
    struct node* tail = queue->low_priority.tail;
    struct node* cur = queue->low_priority.head;
    while (cur != tail) {
        if (cur->input == input) return;
        cur = cur->next;
    }

    struct node* taol = queue->high_priority.tail;
    cur = queue->high_priority.head;
    while (cur != taol) {
        if (cur->input == input) return;
        cur = cur->next;
    }

    struct node* recent = tail->prev;
    struct node* new = malloc(sizeof(struct node));
    new->input = input;
    new->next = tail;
    new->prev = recent;
    tail->prev = new;
    recent->next = new;
}

INPUT dequeue_input(INPUT_QUEUE queue) {
    size_t pattern = queue->count;
    struct queue* target;
    if (pattern == 9) {
        target = &queue->low_priority;
        if (target->head->next == target->tail) target = &queue->high_priority;
    }
    else {
        target = &queue->high_priority;
        if (target->head->next == target->tail) target = &queue->low_priority;
    }

    struct node* no_get = target->head->next;
    INPUT to_get = no_get->input;
    if (no_get->next != NULL) {
        struct node* next = no_get->next;
        next->prev = target->head;
        target->head->next = next;

        struct node* last = target->tail->prev;
        last->next = no_get;
        no_get->prev = last;
        no_get->next = target->tail;
        target->tail->prev = no_get;
    }

    pattern = (pattern+1) % 10;
    queue->count = pattern;
    return to_get;
}
