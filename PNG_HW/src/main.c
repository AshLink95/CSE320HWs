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
        else if (argv[i][0] != '-' && name == 1) {
            fname = malloc(strlen(argv[i]) + 1);
            strcpy(fname, argv[i]); break;
        }
        else if (argv[i][0] != '-') break;
        size_t arg_len = strlen(argv[i]) - 1;
        char arg[arg_len + 1];
        strncpy(arg, argv[i] + 1, arg_len);
        arg[arg_len] = '\0';
        for (int j = 0; j < arg_len; j++) {
            if (arg[j] == 'h') {PRINT_USAGE(argv[0]); return EXIT_SUCCESS;}
            else if (arg[j] == 'f' && name == 0) name = 1;
            // else if (arg[j])
        }
    }
    if (name == 0) {PRINT_ERROR_MISSING_F_FLAG(); return EXIT_FAILURE;}
    else if (fname == NULL) {PRINT_ERROR_F_REQUIRES_FILENAME(); return EXIT_FAILURE;}
    FILE* fp = png_open(fname);
    if (fp == NULL) {PRINT_ERROR_OPEN_FILE(fname); return EXIT_FAILURE;}
    fclose(fp); fp = NULL;

    uint8_t s = 1; uint8_t p = 1; uint8_t ii = 1; uint8_t d = 1;
    uint8_t e = 0; uint8_t eo = 0;
    char* emsg = NULL; char* eoname = NULL;
    uint8_t m = 0; uint8_t mo = 0; uint8_t mw = 0; uint8_t mg = 0;
    char* mname = NULL; char* moname = NULL; long mwval = 0; long mgval = 0;
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] != '-' && strcmp(argv[i], fname) != 0 && !e && !eo && !m && !mo && !mw && !mg)
        {PRINT_ERROR_UNKNOWN_OPTION(argv[i]); return EXIT_FAILURE;}
        else if (argv[i][0] != '-' && strcmp(argv[i], fname) == 0) continue;
        else if (argv[i][0] != '-' && e == 1) {
            emsg = malloc(strlen(argv[i]) + 1);
            strcpy(emsg, argv[i]); e++; continue;
        }
        else if (argv[i][0] != '-' && eo == 1) {
            eoname = malloc(strlen(argv[i]) + 1);
            strcpy(eoname, argv[i]); eo++; continue;
        }
        else if (argv[i][0] != '-' && m == 1) {
            mname = malloc(strlen(argv[i]) + 1);
            strcpy(mname, argv[i]); m++; continue;
        }
        else if (argv[i][0] != '-' && mo == 1) {
            moname = malloc(strlen(argv[i]) + 1);
            strcpy(moname, argv[i]); mo++; continue;
        }
        else if (argv[i][0] != '-' && mw == 1) {
            char* endptr = NULL; mw++;
            mwval = strtol(argv[i], &endptr, 10); continue;
        }
        else if (argv[i][0] != '-' && mg == 1) {
            char* endptr = NULL; mg++;
            mgval = strtol(argv[i], &endptr, 10); continue;
        }
        else if (argv[i][0] != '-') break;
        size_t arg_len = strlen(argv[i]) - 1;
        char arg[arg_len + 1];
        strncpy(arg, argv[i] + 1, arg_len);
        arg[arg_len] = '\0';
        for (int j = 0; j < arg_len; j++) {                   
            if (arg[j] == 'e' && !e) e++;

            else if (arg[j] == 'o' && e >= 1 && !eo) eo++;
            else if (arg[j] == 'o' && m >= 1 && !mo) mo++;

            else if (arg[j] == 'm' && !m) m++;
            else if (arg[j] == 'w' && m >= 1) mw++;
            else if (arg[j] == 'g' && m >= 1) mg++;

            else if (arg[j] == 's' && s) {
                png_chunk_t* c_summary = NULL;
                if (png_summary(fname, &c_summary) < 0) {PRINT_ERROR_READ_CHUNKS(); return EXIT_FAILURE;}
                PRINT_CHUNK_SUMMARY_HEADER(fname);
                int k = 0;
                for (k = 0; strcmp(c_summary[k].type, "IEND") !=0 ; k++) PRINT_CHUNK_INFO(k, c_summary[k]);
                PRINT_CHUNK_INFO(k, c_summary[k]);
                free(c_summary); c_summary = NULL;
                s--;
            }
            else if (arg[j] == 'p' && p) {
                png_color_t* p_summary = NULL;
                size_t p_count = 0;
                FILE* fp = png_open(fname);
                if (fp == NULL) {PRINT_ERROR_OPEN_FILE(fname); return EXIT_FAILURE;}
                if (png_extract_plte(fp, &p_summary, &p_count) < 0)
                {fclose(fp); PRINT_ERROR_PLTE_NOT_FOUND(); return EXIT_FAILURE;}
                PRINT_PALETTE_HEADER(fname);
                PRINT_PALETTE_COUNT(p_count);
                for (size_t k=0;k<p_count;k++) PRINT_PALETTE_COLOR(k, p_summary[k].r, p_summary[k].g, p_summary[k].b);
                fclose(fp); fp = NULL;
                free(p_summary); p_summary = NULL;
                p--;
            }
            else if (arg[j] == 'i' && ii) {
                png_ihdr_t i_summary;
                FILE* fp = png_open(fname);
                if (fp == NULL) {PRINT_ERROR_OPEN_FILE(fname); return EXIT_FAILURE;}
                if (png_extract_ihdr(fp, &i_summary) < 0)
                {fclose(fp); PRINT_ERROR_READ_IHDR(); return EXIT_FAILURE;}
                PRINT_IHDR(fname, i_summary);
                fclose(fp); fp = NULL;
                ii--;
            }
            else if (arg[j] == 'd' && d) {
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
                free(msg); msg = NULL;
                d--;
            }
            else if (arg[j]!='f' && arg[j]!='h'){
                char matta[3];
                matta[0] = '-'; matta[1] = arg[j]; matta[2] = '\0';
                PRINT_ERROR_UNKNOWN_OPTION(matta);
                return EXIT_FAILURE;
            }
        }
    }

    if (e==2 && eo==2) {
        if (png_encode_lsb(fname, eoname, emsg) < 0)
        {PRINT_ERROR_ENCODE_FAILED(); return EXIT_FAILURE;}
        else PRINT_ENCODE_SUCCESS(eoname);
    }
    else if (e) PRINT_ERROR_ENCODE_REQUIRES();
    free(emsg); emsg = NULL; free(eoname); eoname = NULL;

    if (m==2 && mo==2) {
        if (mw == 1) PRINT_ERROR_WIDTH_REQUIRES();
        if (mg == 1) PRINT_ERROR_HEIGHT_REQUIRES();
        FILE* fp = png_open(fname);
        if (fp == NULL) {PRINT_ERROR_OPEN_FILE(fname); return EXIT_FAILURE;}
        FILE* mp = png_open(mname);
        if (mp == NULL) {fclose(fp); PRINT_ERROR_OPEN_FILE(mname); return EXIT_FAILURE;}
        png_ihdr_t fihdr; png_ihdr_t mihdr;
        if (png_extract_ihdr(fp, &fihdr) < 0)
        {fclose(fp); fclose(mp); PRINT_ERROR_EXTRACT_FAILED(); return EXIT_FAILURE;}
        if (png_extract_ihdr(mp, &mihdr) < 0)
        {fclose(mp); fclose(fp); PRINT_ERROR_EXTRACT_FAILED(); return EXIT_FAILURE;}
        fclose(fp); fp = NULL; fclose(mp); mp = NULL;
        size_t flen = fihdr.width * fihdr.height;
        size_t mlen = mihdr.width * mihdr.height;
        if (flen > mlen) {
            if (png_overlay_paste(fname, mname, moname, mwval, mgval) < 0)
            {PRINT_ERROR_OVERLAY_FAILED(); return EXIT_FAILURE;}
        } else {
            if (png_overlay_paste(mname, fname, moname, mwval, mgval) < 0)
            {PRINT_ERROR_OVERLAY_FAILED(); return EXIT_FAILURE;}
        }
        PRINT_OVERLAY_SUCCESS(moname);
    }
    else if (m) PRINT_ERROR_OVERLAY_REQUIRES();
    free(mname); mname = NULL; free(moname); moname = NULL;

    free(fname);
    return EXIT_SUCCESS;
}
