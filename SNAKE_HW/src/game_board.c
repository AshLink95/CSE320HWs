#include "../include/game_board.h"
// #include "../include/debug.h"
#include "../include/global.h"
#include <stdlib.h>
#include <string.h>

int board_init(game_board_t *board, int size, int max_snakes, unsigned int seed) {
    if (board == NULL) return -1;
    if (size < BOARD_SIZE_MIN || size > BOARD_SIZE_MAX) return -1;
    if (max_snakes < MAX_PLAYERS_MIN || max_snakes > MAX_PLAYERS_MAX) return -1;

    board->cells = malloc(size * size * sizeof(cell_t));
    if (board->cells == NULL) return -1;
    board->size = size;
    board->max_snakes = max_snakes;
    board->num_snakes = 0;
    board->rng_state = seed;

    for (int i = 0; i < size; i++) {
        for (int j = 1; j < size-1; j++) board->cells[i*size + j] = CELL_EMPTY;
        board->cells[i] = CELL_WALL;
        board->cells[(size-1) * size + i] = CELL_WALL;
        board->cells[i * size] = CELL_WALL;
        board->cells[i * size + size-1] = CELL_WALL;
    }

    memset(board->snakes, 0, sizeof(board->snakes));
    for (int i = 0; i < MAX_PLAYERS; i++) board->snakes[i].id = i;

    if (board_place_apple(board) < 0) {
        free(board->cells); board->cells = NULL;
        return -1;
    }

	return 0;
}

void board_free(game_board_t *board) {
    if (board == NULL) return;
    if (board->cells != NULL) {
        free(board->cells);
        board->cells = NULL;
    }
}

unsigned int board_random(game_board_t *board) {
    board->rng_state = board->rng_state * 1103515245 + 12345;
    return (board->rng_state / 65536) % 32768;
}

int board_place_apple(game_board_t *board) {
    int size = board->size;
    int count = 0;
    for (int i = 1; i < size-1; i++) {
        for (int j = 1; j < size-1; j++) {
            if (board->cells[i*size + j] == CELL_EMPTY) count++;
        }
    }
    if (!count) return -1;

    unsigned int rand = board_random(board);
    unsigned int idx = rand % count;
    for (int i = 1; i < size-1; i++) {
        for (int j = 1; j < size-1; j++) {
            if (board->cells[i*size + j] == CELL_EMPTY) {
                if (!idx) {
                    board->cells[i*size + j] = CELL_APPLE;
                    board->apple = (position_t){.x = j, .y = i};
                    return 0;
                }
                idx--;
            }
        }
    }

	return -1;
}

int board_add_snake(game_board_t *board, int *out_id) {
    if (board == NULL || out_id == NULL) return -1;
    int max = board->max_snakes;
    int size = board->size;
    cell_t* cells = board->cells;

    int id = -1;
    for (int i = 0; i < max; i++) {
        if (!board->snakes[i].alive) { id = i; break; }
    }
    if (id < 0) return -1;

    int sx, sy;
    if (id % 4 == 0)      { sx = size/4;   sy = size/4; }
    else if (id % 4 == 1) { sx = 3*size/4; sy = size/4; }
    else if (id % 4 == 2) { sx = size/4;   sy = 3*size/4; }
    else                  { sx = 3*size/4; sy = 3*size/4; }

    int x = sx, y = sy;
    int found = 0;
    while (1) {
        if (cells[y*size + x] == CELL_EMPTY) { found = 1; break; }
        x++;
        if (x >= size-1) { x = 1; y++; }
        if (y >= size-1) { y = 1; }
        if (x == sx && y == sy) break;
    }
    if (!found) return -1;

    snake_t* snake = &board->snakes[id];
    snake->id = id;
    snake->body[0] = (position_t){.x = x, .y = y};
    snake->length = 1;
    snake->direction = DIR_RIGHT;
    snake->next_direction = DIR_RIGHT;
    snake->alive = 1;
    cells[y*size + x] = CELL_SNAKE_0 + id;
    *out_id = id;
    board->num_snakes++;
	return 0;
}

int board_remove_snake(game_board_t *board, int snake_id) {
    if (board->max_snakes <= snake_id) return -1;
    snake_t* snake = &board->snakes[snake_id];
    if (snake == NULL) return -1;
    int len = snake->length;
    if (!len || !snake->alive) return -1;
    position_t* pos = snake->body;
    int size = board->size;
    cell_t* cells = board->cells;
    for (int i = 0; i < len; i++) {
        int x = (pos+i)->x, y = (pos+i)->y;
        cells[y*size + x] = CELL_EMPTY;
    }
    snake->length = 0;
    snake->alive = 0;
    board->num_snakes--;
	return 0;
}

int board_tick(game_board_t *board) {
    if (board == NULL) return -1;
    for (int id = 0; id < MAX_PLAYERS; id++) {
        if (board->snakes[id].alive && snake_advance(board, id) < 0) return -1;
    }
	return 0;
}
