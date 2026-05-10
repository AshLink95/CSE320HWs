#include <stdint.h>
#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../include/global.h"
#include "../include/server.h"

int main(int argc, char **argv) {
    if (argc == 1) {ERR_PORT_REQUIRED(); return EXIT_FAILURE;}

    uint8_t pflag = 0;
    char* pstr = NULL;
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] != '-' && pflag == 0) {ERR_MSG("Unknown option %s", argv[i]); return EXIT_FAILURE;}
        else if (argv[i][0] != '-' && pflag == 1) {
            pstr = malloc(strlen(argv[i]) + 1);
            strcpy(pstr, argv[i]); break;
        }
        else if (argv[i][0] != '-') break;
        size_t arg_len = strlen(argv[i]) - 1;
        char arg[arg_len + 1];
        strncpy(arg, argv[i] + 1, arg_len);
        arg[arg_len] = '\0';
        for (size_t j = 0; j < arg_len; j++) {
            if (arg[j] == 'h') {PRINT_USAGE(); return EXIT_SUCCESS;}
            else if (arg[j] == 'p' && pflag == 0) pflag = 1;
        }
    }
    if (pflag == 0) {ERR_PORT_REQUIRED(); return EXIT_FAILURE;}
    else if (pstr == NULL) {ERR_PORT_REQUIRED(); return EXIT_FAILURE;}

    char* endptr = NULL;
    long port = strtol(pstr, &endptr, 10);
    if (endptr == pstr || *endptr != '\0' || port <= 0 || port > 65535)
    {ERR_INVALID_PORT(pstr); free(pstr); return EXIT_FAILURE;}

    uint8_t b = 0; uint8_t s = 0; uint8_t m = 0;
    long bval = BOARD_SIZE_DEFAULT;
    long mval = MAX_PLAYERS_DEFAULT;
    unsigned int sval = (unsigned int)time(NULL);
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] != '-' && strcmp(argv[i], pstr) != 0 && !b && !s && !m)
        {ERR_MSG("Unknown option %s", argv[i]); free(pstr); return EXIT_FAILURE;}
        else if (argv[i][0] != '-' && strcmp(argv[i], pstr) == 0) continue;
        else if (argv[i][0] != '-' && b == 1) {
            char* ep = NULL; b++;
            bval = strtol(argv[i], &ep, 10); continue;
        }
        else if (argv[i][0] != '-' && s == 1) {
            char* ep = NULL; s++;
            sval = (unsigned int)strtoul(argv[i], &ep, 10); continue;
        }
        else if (argv[i][0] != '-' && m == 1) {
            char* ep = NULL; m++;
            mval = strtol(argv[i], &ep, 10); continue;
        }
        else if (argv[i][0] != '-') break;
        size_t arg_len = strlen(argv[i]) - 1;
        char arg[arg_len + 1];
        strncpy(arg, argv[i] + 1, arg_len);
        arg[arg_len] = '\0';
        for (size_t j = 0; j < arg_len; j++) {
            if (arg[j] == 'b' && !b) b++;
            else if (arg[j] == 's' && !s) s++;
            else if (arg[j] == 'm' && !m) m++;
            else if (arg[j] != 'p' && arg[j] != 'h') {
                char matta[3];
                matta[0] = '-'; matta[1] = arg[j]; matta[2] = '\0';
                ERR_MSG("Unknown option %s", matta);
                free(pstr); return EXIT_FAILURE;
            }
        }
    }

    if (bval < BOARD_SIZE_MIN || bval > BOARD_SIZE_MAX)
    {ERR_INVALID_BOARD_SIZE((int)bval); free(pstr); return EXIT_FAILURE;}
    if (mval < MAX_PLAYERS_MIN || mval > MAX_PLAYERS_MAX)
    {ERR_INVALID_MAX_PLAYERS((int)mval); free(pstr); return EXIT_FAILURE;}

    free(pstr); pstr = NULL;

    server_t srv;
    if (server_init(&srv, (int)port, (int)bval, (int)mval, sval) < 0)
    {ERR_BIND_FAILED((int)port); return EXIT_FAILURE;}
    PRINT_SERVER_STARTED((int)port, (int)bval, (int)mval);
    if (server_start(&srv) < 0) {server_cleanup(&srv); return EXIT_FAILURE;}
    server_cleanup(&srv);
    return EXIT_SUCCESS;
}
