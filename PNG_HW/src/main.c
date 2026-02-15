#include <stdint.h>
#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include "global.h"
#include "png_reader.h"
#include "png_chunks.h"
#include "png_steg.h"
#include "png_overlay.h"

char* USAGE = "Usage: bin/png -f png_file [options]\n"
"Options:\n"
"  -f png_file        Input PNG file (required)\n"
"  -h                    Print this help message\n"
"  -s                    Print chunk summary\n"
"  -p                    Print palette summary\n"
"  -i                    Print IHDR fields\n"
"  -e message -o out_file Encode message and write to output file\n"
"  -d                    Decode and print hidden message\n"
"  -m file2 -o out_file [-w width] [-g height]  Overlay file2 (smaller) over input and write to output\n";

int main(int argc, char **argv)
{
    if (argc == 1) {PRINT_ERROR_MISSING_F_FLAG(); return EXIT_FAILURE;}

    uint8_t name = 0;
    char* fname = NULL;
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] != '-' && name == 0) {PRINT_ERROR_UNKNOWN_OPTION(argv[i]); return EXIT_FAILURE;}
        if (argv[i][0] != '-' && name == 1) {
            fname = malloc(strlen(argv[i] + 1));
            strcpy(fname, argv[i]);
            break;
        }
        char arg[strlen(argv[i]) - 1];
        strncpy(arg, argv[i] + 1, strlen(argv[i]) - 1);
        for (int j = 0; j < strlen(arg); j++) {
            if (arg[j] == 'h') {PRINT_USAGE(argv[0]); return EXIT_SUCCESS;}
            if (arg[j] == 'f' && name == 0) name = 1;
        }
    }
    if (fname == NULL) {PRINT_ERROR_OPEN_FILE(fname); return EXIT_FAILURE;}
    FILE* fp = png_open(fname);
    if (fp == NULL) {PRINT_ERROR_OPEN_FILE(fname); return EXIT_FAILURE;}
    fclose(fp); fp = NULL;

    uint8_t s = 1; uint8_t p = 1; uint8_t ii = 1; uint8_t d = 1;
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] != '-' && strcmp(argv[i], fname) != 0)
        {PRINT_ERROR_UNKNOWN_OPTION(argv[i]); return EXIT_FAILURE;}
        else if (argv[i][0] != '-' && strcmp(argv[i], fname) == 0) continue;
        char arg[strlen(argv[i]) - 1];
        strncpy(arg, argv[i] + 1, strlen(argv[i]) - 1);
        for (int j = 0; j < strlen(arg); j++) {
            if (arg[j] == 'e') {/*encode (needs extra work)*/; return EXIT_SUCCESS;}
            if (arg[j] == 'm') {/*overlay (needs extra work)*/; return EXIT_SUCCESS;}

            if (arg[j] == 's' && s) {
                png_chunk_t* c_summary = NULL;
                if (png_summary(fname, &c_summary) < 0) {PRINT_ERROR_READ_CHUNKS(); return EXIT_FAILURE;}
                PRINT_CHUNK_SUMMARY_HEADER(fname);
                for (int k = 0; strcmp(c_summary[k].type, "IEND") !=0 ; k++) PRINT_CHUNK_INFO(k, c_summary[k]);
                s--;
            }
            if (arg[j] == 'p' && p) {
                png_color_t* p_summary = NULL;
                size_t p_count = 0;
                FILE* fp = png_open(fname);
                if (fp == NULL) {PRINT_ERROR_OPEN_FILE(fname); return EXIT_FAILURE;}
                if (png_extract_plte(fp, &p_summary, &p_count) < 0)
                {fclose(fp); PRINT_ERROR_PLTE_NOT_FOUND(); return EXIT_FAILURE;}
                PRINT_PALETTE_HEADER(fname);
                PRINT_PALETTE_COUNT(p_count);
                for (size_t k=0;k<p_count;k++) PRINT_PALETTE_COLOR(k, p_summary[k].r, p_summary[k].g, p_summary[k].b);
                p--;
            }
            if (arg[j] == 'i' && i) {
                png_ihdr_t i_summary;
                FILE* fp = png_open(fname);
                if (fp == NULL) {PRINT_ERROR_OPEN_FILE(fname); return EXIT_FAILURE;}
                if (png_extract_ihdr(fp, &i_summary) < 0)
                {fclose(fp); PRINT_ERROR_READ_IHDR(); return EXIT_FAILURE;}
                PRINT_IHDR(fname, i_summary);
                ii--;
            }
            if (arg[j] == 'd' && d) {
                FILE* fp = png_open(fname);
                if (fp == NULL) {PRINT_ERROR_OPEN_FILE(fname); return EXIT_FAILURE;}
                png_ihdr_t ihdr;
                if (png_extract_ihdr(fp, &ihdr) < 0)
                {fclose(fp); PRINT_ERROR_EXTRACT_FAILED(); return EXIT_FAILURE;}
                fclose(fp); fp = NULL;
                size_t max_len = (ihdr.width * ihdr.height * 3) / 8;
                char* msg = malloc(max_len);
                if (msg == NULL) {PRINT_ERROR_OPEN_FILE(fname); return EXIT_FAILURE;}
                if (png_extract_lsb(fname, msg, max_len) < 0)
                {free(msg); PRINT_ERROR_EXTRACT_FAILED(); return EXIT_FAILURE;}
                PRINT_HIDDEN_MESSAGE(msg);
                d--;
            }
        }
    }

    return EXIT_SUCCESS;
}
