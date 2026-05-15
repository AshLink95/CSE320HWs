#include "../include/protocol.h"
// #include "../include/debug.h"
#include <arpa/inet.h>
#include <string.h>

int protocol_serialize_welcome(uint8_t *buf, size_t buf_len, int player_id, int board_size, int max_players) {
    if (buf == NULL || buf_len < 4) return -1;
    if (player_id < 0 || player_id > 7) return -1;

    buf[0] = MSG_WELCOME;
    buf[1] = (uint8_t)player_id;
    buf[2] = (uint8_t)board_size;
    buf[3] = (uint8_t)max_players;
	return 4;
}

int protocol_serialize_game_state(uint8_t *buf, size_t buf_len, const game_board_t *board) {
    if (buf == NULL || board == NULL) return -1;
    if (buf_len < 6) return -1;

    int n_alive = 0;
    for (int i = 0; i < MAX_PLAYERS; i++) if (board->snakes[i].alive) n_alive++;

    size_t off = 0;
    buf[off++] = MSG_GAME_STATE;
    buf[off++] = (uint8_t)n_alive;
    uint16_t ax = htons((uint16_t)board->apple.x);
    uint16_t ay = htons((uint16_t)board->apple.y);
    memcpy(buf + off, &ax, 2); off += 2;
    memcpy(buf + off, &ay, 2); off += 2;

    for (int i = 0; i < MAX_PLAYERS; i++) {
        const snake_t* s = &board->snakes[i];
        if (!s->alive) continue;
        size_t need = 4 + (size_t)s->length * 4;
        if (off + need > buf_len) return -1;

        buf[off++] = (uint8_t)s->id;
        uint16_t len = htons((uint16_t)s->length);
        memcpy(buf + off, &len, 2); off += 2;
        buf[off++] = (uint8_t)s->direction;
        for (int k = 0; k < s->length; k++) {
            uint16_t bx = htons((uint16_t)s->body[k].x);
            uint16_t by = htons((uint16_t)s->body[k].y);
            memcpy(buf + off, &bx, 2); off += 2;
            memcpy(buf + off, &by, 2); off += 2;
        }
    }
	return (int)off;
}

int protocol_serialize_dead(uint8_t *buf, size_t buf_len, int player_id) {
    if (buf == NULL || buf_len < 2) return -1;
    if (player_id < 0 || player_id > 7) return -1;

    buf[0] = MSG_PLAYER_DEAD;
    buf[1] = (uint8_t)player_id;
	return 2;
}

int protocol_serialize_game_over(uint8_t *buf, size_t buf_len, int winner_id) {
    if (buf == NULL || buf_len < 2) return -1;

    buf[0] = MSG_GAME_OVER;
    buf[1] = (uint8_t)winner_id;
	return 2;
}

int protocol_serialize_error(uint8_t *buf, size_t buf_len, uint8_t error_code) {
    if (buf == NULL || buf_len < 2) return -1;

    buf[0] = MSG_ERROR;
    buf[1] = error_code;
	return 2;
}

int protocol_deserialize_client_msg(const uint8_t *buf, size_t buf_len, uint8_t *out_type, uint8_t *out_payload) {
    if (buf == NULL || out_type == NULL || out_payload == NULL) return -1;
    if (buf_len < CLIENT_MSG_SIZE) return -1;

    uint8_t type = buf[0];
    if (type != MSG_JOIN && type != MSG_DIRECTION && type != MSG_LEAVE) return -1;

    *out_type = type;
    *out_payload = buf[1];
	return 0;
}
