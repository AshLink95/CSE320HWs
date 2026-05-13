#include "../include/server.h"
// #include "../include/debug.h"
#include "../include/global.h"
#include "../include/protocol.h"
#include <unistd.h>
#include <netinet/in.h>
#include <pthread.h>
#include <sys/socket.h>
#include <errno.h>
#include <signal.h>
#include <stdlib.h>

static pthread_t* thread = NULL;
static volatile sig_atomic_t TERM = 0;

static pthread_mutex_t htids_mtx = PTHREAD_MUTEX_INITIALIZER;
static pthread_t* htids = NULL;
static size_t htids_n = 0, htids_cap = 0;

static void on_sigint(int signo) { TERM = signo; }

int server_init(server_t *server, int port, int board_size, int max_snakes, unsigned int seed) {
    if (server == NULL) return -1;
    server->listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server->listen_fd < 0) return -1;
    int opt = 1; //enables option (restart server without "address in use" errors)
    if (setsockopt(server->listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))<0) {
        close(server->listen_fd); return -1;
    }
    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(port), // fix endianness (portability)
        .sin_addr.s_addr = INADDR_ANY //0.0.0.0
    };
    if (bind(server->listen_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(server->listen_fd); return -1;
    }
    if (listen(server->listen_fd, 8) < 0) {
        close(server->listen_fd); return -1;
    }
    server->running = 1;

    if (board_init(&server->board, board_size, max_snakes, seed) < 0) {
        close(server->listen_fd); return -1;
    }
    if (pthread_mutex_init(&server->board_mutex, NULL) != 0) {
        close(server->listen_fd); board_free(&server->board); return -1;
    }
    for (int i=0; i<MAX_PLAYERS; i++) {
        server->client_fds[i] = -1;
        server->client_snake_ids[i] = -1;
    }
	return 0;
}

void *server_game_loop(void *arg) {
    server_t* server = (server_t*)arg;
    game_board_t* board = &server->board;
    snake_t* snakes = board->snakes;
    int alive[MAX_PLAYERS];
    int fds[MAX_PLAYERS];
    uint8_t buf_d[2];
    uint8_t buf_s[GAME_STATE_BUF_SIZE];
    int dead = 0;
    int dead_ids[MAX_PLAYERS];
    int dead_is[MAX_PLAYERS];
    for(;;) {
        usleep(TICK_INTERVAL_MS * 1000);
        pthread_mutex_lock(&server->board_mutex);
        if (!server->running) {
            pthread_mutex_unlock(&server->board_mutex);
            return NULL;
        }
        for (int i=0; i<MAX_PLAYERS; i++) {
            fds[i] = server->client_fds[i];
            int id = server->client_snake_ids[i];
            if (id<0) continue;
            alive[id] = snakes[id].alive;
        }
        board_tick(board);
        dead = 0;
        for (int i=0; i<MAX_PLAYERS; i++) {
            int id = server->client_snake_ids[i];
            if (id < 0) continue;
            int prev = alive[id];
            alive[id] = snakes[id].alive;
            if (prev && !alive[id]) {
                if (server->client_fds[i] < 0) continue;
                dead_ids[dead] = id;
                dead_is[dead] = i;
                dead++;
            }
        }
        int size = protocol_serialize_game_state(buf_s, GAME_STATE_BUF_SIZE, board);
        pthread_mutex_unlock(&server->board_mutex);
        if (size<0) continue;
        for (int i=0; i<dead; i++) {
            protocol_serialize_dead(buf_d, 2, dead_ids[i]);
            if (fds[dead_is[i]] < 0) continue;
            send_all(fds[dead_is[i]], buf_d, 2);
        }
        for (int i=0; i<MAX_PLAYERS; i++) {
            if (fds[i] < 0) continue;
            send_all(fds[i], buf_s, size);
        }
    }
}

void *server_client_handler(void *arg) {
    if (arg == NULL) return NULL;
    client_handler_arg_t* a = (client_handler_arg_t*)arg;
    server_t* server = a->server;
    int cfd = a->client_fd;
    free(a);

    uint8_t cmsg[CLIENT_MSG_SIZE];
    uint8_t type, payload;
    uint8_t err[2];

    if (recv_exact(cfd, cmsg, CLIENT_MSG_SIZE) < 0) { close(cfd); return NULL; }
    if (protocol_deserialize_client_msg(cmsg, CLIENT_MSG_SIZE, &type, &payload) < 0 || type != MSG_JOIN) {
        protocol_serialize_error(err, 2, ERR_INVALID_MSG);
        send_all(cfd, err, 2);
        close(cfd); return NULL;
    }

    int snake_id = -1;
    uint8_t wbuf[4];
    pthread_mutex_lock(&server->board_mutex);
    int rc = board_add_snake(&server->board, &snake_id);
    int wlen = protocol_serialize_welcome(wbuf, 4, snake_id, server->board.size, server->board.max_snakes);
    pthread_mutex_unlock(&server->board_mutex);
    if (rc < 0) {
        protocol_serialize_error(err, 2, ERR_GAME_FULL);
        send_all(cfd, err, 2);
        close(cfd); return NULL;
    }

    if (wlen < 0 || send_all(cfd, wbuf, (size_t)wlen) < 0) {
        pthread_mutex_lock(&server->board_mutex);
        board_remove_snake(&server->board, snake_id);
        pthread_mutex_unlock(&server->board_mutex);
        close(cfd); return NULL;
    }

    int slot = -1;
    pthread_mutex_lock(&server->board_mutex);
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (server->client_fds[i] < 0) {
            server->client_fds[i] = cfd;
            server->client_snake_ids[i] = snake_id;
            slot = i; break;
        }
    }
    pthread_mutex_unlock(&server->board_mutex);
    if (slot < 0) {
        pthread_mutex_lock(&server->board_mutex);
        board_remove_snake(&server->board, snake_id);
        pthread_mutex_unlock(&server->board_mutex);
        close(cfd); return NULL;
    }

    for (;;) {
        if (recv_exact(cfd, cmsg, CLIENT_MSG_SIZE) < 0) break;
        if (protocol_deserialize_client_msg(cmsg, CLIENT_MSG_SIZE, &type, &payload) < 0) {
            protocol_serialize_error(err, 2, ERR_INVALID_MSG);
            send_all(cfd, err, 2);
            continue;
        }
        if (type == MSG_LEAVE) break;
        if (type == MSG_DIRECTION) {
            pthread_mutex_lock(&server->board_mutex);
            int dir = snake_set_direction(&server->board.snakes[snake_id], (direction_t)payload);
            pthread_mutex_unlock(&server->board_mutex);
            if (dir<0) {
                protocol_serialize_error(err, 2, ERR_INVALID_MSG);
                send_all(cfd, err, 2);
            }
        }
    }

    pthread_mutex_lock(&server->board_mutex);
    board_remove_snake(&server->board, snake_id);
    server->client_fds[slot] = -1;
    server->client_snake_ids[slot] = -1;
    pthread_mutex_unlock(&server->board_mutex);
    close(cfd);
	return NULL;
}

int server_start(server_t *server) {
    if (server == NULL) return -1;
    struct sigaction sa = {0};
    sa.sa_handler = on_sigint;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);

    thread = malloc(sizeof(pthread_t));
    if (thread == NULL) return -1;
    if (pthread_create(thread, NULL, server_game_loop, (void*)server)) {
        free(thread);
        thread = NULL;
        return -1;
    }

    for (;;) {
        pthread_mutex_lock(&server->board_mutex);
        if (TERM || !server->running) {
            server->running = 0;
            pthread_mutex_unlock(&server->board_mutex);
            return 0;
        }
        pthread_mutex_unlock(&server->board_mutex);
        struct sockaddr_in caddr;
        socklen_t alen = sizeof(caddr);
        int cfd = accept(server->listen_fd, (struct sockaddr*)&caddr, &alen);
        if (cfd < 0) {
            if (errno == EINTR) continue;
            break;
        }
        client_handler_arg_t* arg = malloc(sizeof(client_handler_arg_t));
        if (arg == NULL) { close(cfd); continue; }
        arg->server = server;
        arg->client_fd = cfd;
        pthread_t tid;
        if (pthread_create(&tid, NULL, server_client_handler, arg)) {
            close(cfd); free(arg); continue;
        }
        pthread_mutex_lock(&htids_mtx);
        if (htids_n == htids_cap) {
            size_t nc = htids_cap ? htids_cap * 2 : 8;
            pthread_t* na = realloc(htids, nc * sizeof(pthread_t));
            if (na) { htids = na; htids_cap = nc; }
        }
        if (htids_n < htids_cap) htids[htids_n++] = tid;
        else pthread_detach(tid);
        pthread_mutex_unlock(&htids_mtx);
    }
	return 0;
}

void server_cleanup(server_t *server) {
    if (server == NULL) return;
    pthread_mutex_lock(&server->board_mutex);
    server->running = 0;
    for (int i=0; i<MAX_PLAYERS; i++) {
        if (server->client_fds[i] < 0) continue;
        shutdown(server->client_fds[i], SHUT_RDWR);
    }
    pthread_mutex_unlock(&server->board_mutex);
    shutdown(server->listen_fd, SHUT_RDWR);
    close(server->listen_fd);

    if (thread != NULL) {
        pthread_join(*thread, NULL);
        free(thread);
        thread = NULL;
    }

    pthread_mutex_lock(&htids_mtx);
    pthread_t* arr = htids; size_t n = htids_n;
    htids = NULL; htids_n = 0; htids_cap = 0;
    pthread_mutex_unlock(&htids_mtx);
    for (size_t i = 0; i < n; i++) pthread_join(arr[i], NULL);
    free(arr);

    board_free(&server->board);
    pthread_mutex_destroy(&server->board_mutex);
}

int recv_exact(int fd, uint8_t *buf, size_t len) {
    if (buf == NULL || fd < 0) return -1;
    size_t got = 0;
    while (got < len) {
        ssize_t n = recv(fd, buf + got, len - got, 0);
        if (n == 0) return -1;
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        got += (size_t)n;
    }
	return 0;
}

int send_all(int fd, const uint8_t *buf, size_t len) {
    if (buf == NULL || fd < 0) return -1;
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = send(fd, buf + sent, len - sent, MSG_NOSIGNAL);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        sent += (size_t)n;
    }
	return 0;
}
