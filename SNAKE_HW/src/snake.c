#include "../include/snake.h"
#include "../include/game_board.h"
// #include "../include/server.h"
#include <stdlib.h>

int snake_set_direction(snake_t *snake, direction_t dir) {
    if (snake == NULL) return -1;
    if (dir != DIR_UP && dir != DIR_DOWN && dir != DIR_LEFT && dir != DIR_RIGHT) return -1;

    direction_t cur = snake->direction;
    if ((cur == DIR_UP    && dir == DIR_DOWN)  ||
        (cur == DIR_DOWN  && dir == DIR_UP)    ||
        (cur == DIR_LEFT  && dir == DIR_RIGHT) ||
        (cur == DIR_RIGHT && dir == DIR_LEFT)) return 0;

    snake->next_direction = dir;
	return 0;
}

int snake_advance(struct game_board *board, int snake_id) {
    if (board == NULL) return -1;
    if (snake_id < 0 || snake_id >= MAX_PLAYERS) return -1;
    snake_t* snake = &board->snakes[snake_id];
    if (!snake->alive) return -1;

    snake->direction = snake->next_direction;
    int size = board->size;
    cell_t* cells = board->cells;

    int hx = snake->body[0].x, hy = snake->body[0].y;
    int nx = hx, ny = hy;
    if (snake->direction == DIR_UP)         ny = hy - 1;
    else if (snake->direction == DIR_DOWN)  ny = hy + 1;
    else if (snake->direction == DIR_LEFT)  nx = hx - 1;
    else if (snake->direction == DIR_RIGHT) nx = hx + 1;
    else return -1;

    cell_t target = cells[ny*size + nx];

    if (target == CELL_WALL || (target >= CELL_SNAKE_0 && target <= CELL_SNAKE_7)) {
        board_remove_snake(board, snake_id);
        return 1;
    }

    if (target == CELL_APPLE) {
        if (snake->length < MAX_SNAKE_LENGTH) {
            for (int i = snake->length; i > 0; i--) snake->body[i] = snake->body[i-1];
            snake->length++;
        } else {
            position_t tail = snake->body[snake->length - 1];
            cells[tail.y * size + tail.x] = CELL_EMPTY;
            for (int i = snake->length - 1; i > 0; i--) snake->body[i] = snake->body[i-1];
        }
        snake->body[0] = (position_t){.x = nx, .y = ny};
        cells[ny*size + nx] = CELL_SNAKE_0 + snake_id;
        board_place_apple(board);
        return 2;
    }

    position_t tail = snake->body[snake->length - 1];
    cells[tail.y * size + tail.x] = CELL_EMPTY;
    for (int i = snake->length - 1; i > 0; i--) snake->body[i] = snake->body[i-1];
    snake->body[0] = (position_t){.x = nx, .y = ny};
    cells[ny*size + nx] = CELL_SNAKE_0 + snake_id;
	return 0;
}
