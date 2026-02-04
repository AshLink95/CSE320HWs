#include "png_reader.h"
#include "png_crc.h"
#include "util.h"
#include "global.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Opens a PNG file and validates signature */
FILE *png_open(const char *path)
{
    if (path == NULL) return NULL;
    FILE* fptr = fopen(path, "r");
    if (fptr == NULL) return NULL;
    uint8_t ideal_sig[8] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
    uint8_t real_sig[8];
    if (fread(real_sig, 1, 8, fptr) != 8) {
        fclose(fptr);
        return NULL;
    }
    if (memcmp(ideal_sig, real_sig, 8) != 0) {
        fclose(fptr);
        return NULL;
    }
    return fptr;
}

/* Reads the next chunk from the file */
int png_read_chunk(FILE *fp, png_chunk_t *out)
{
    if (fp == NULL || out == NULL) return -1;
    uint8_t* buf = malloc(4);
    if (buf == NULL) return -1;
    read_exact(fp, buf, 4);
    uint32_t length = read_u32_be(buf);

    uint32_t buflen = 4 + length + 4;
    uint8_t* tmp = realloc(buf, buflen);
    if (tmp == NULL) {free(buf); return -1;}
    buf = tmp;
    uint8_t* crc_buf_check = malloc(4+length);
    if (crc_buf_check == NULL) {free(buf); return -1;}
    read_exact(fp, buf, buflen);
    memcpy(crc_buf_check, buf, 4+length);
    out->length = length;
    memcpy(out->type, buf, 4);
    out->type[4] = '\0';
    if (length == 0) {
        out->data=NULL;
    } else {
        out->data = malloc(length);
        if (out->data == NULL) {free(buf); free(crc_buf_check); return -1;}
        memcpy(out->data, &buf[4], length);
    }
    out->crc = read_u32_be(buf + 4 + length);
    uint32_t calcd = png_crc(crc_buf_check, 4+length);
    if (calcd != out->crc) {free(buf); free(crc_buf_check); return -1;}
    free(buf); free(crc_buf_check);
    return 0;
}

/* Frees memory allocated inside png_chunk_t */
void png_free_chunk(png_chunk_t *chunk)
{
    if (chunk != NULL) {
        free(chunk->data);
        chunk->data = NULL;
    }
}

int png_extract_ihdr(FILE *fp, png_ihdr_t *out)
{
    png_chunk_t tmp;
    int status1 = png_read_chunk(fp, &tmp);
    if (status1 < 0) return -1;
    int status2 = png_parse_ihdr(&tmp, out);
    if (status2 < 0) {png_free_chunk(&tmp); return -1;}
    png_free_chunk(&tmp);
    return 0;
}

int png_extract_plte(FILE *fp, png_color_t **out_colors, size_t *out_count)
{
    return 0;
}

int png_summary(const char *filename, png_chunk_t **out_summary)
{
    return 0;
}
