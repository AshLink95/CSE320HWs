#include "png_steg.h"
#include "png_reader.h"
#include "png_chunks.h"
#include "png_crc.h"
#include "util.h"
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>


// helper to write big-endian uint32
#define WRITE_U32_BE(val, fp) do { \
    uint8_t b[4]; \
    b[0] = ((val) >> 24) & 0xFF; \
    b[1] = ((val) >> 16) & 0xFF; \
    b[2] = ((val) >> 8) & 0xFF; \
    b[3] = (val) & 0xFF; \
    fwrite(b, 1, 4, fp); \
} while(0)

// helper to write chunks
int png_write_chunk(FILE *fp, const char *type, const uint8_t *data, uint32_t length)
{
    if (fp == NULL || type == NULL) return -1;
    if (length > 0 && data == NULL) return -1;

    WRITE_U32_BE(length, fp);

    uint8_t *crc_buf = malloc(4 + length);
    if (crc_buf == NULL) return -1;
    memcpy(crc_buf, type, 4);
    if (length > 0) memcpy(crc_buf + 4, data, length);

    if (fwrite(crc_buf, 1, 4 + length, fp) != 4 + length) {free(crc_buf); return -1;}

    uint32_t crc = png_crc(crc_buf, 4 + length);
    free(crc_buf);

    WRITE_U32_BE(crc, fp);

    return 0;
}

// helper to write (I don't like this)
int png_write(const char *input_path, const char *output_path, png_ihdr_t *ihdr, png_color_t *colors, size_t new_count, uint8_t *new_compbuf, size_t new_complen)
{
    if (input_path == NULL || output_path == NULL || new_compbuf == NULL) return -1;
    FILE *fp = png_open(input_path);
    if (fp == NULL) return -1;
    FILE *out = fopen(output_path, "wb");
    if (out == NULL) {fclose(fp); return -1;}

    uint8_t sig[8] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
    fwrite(sig, 1, 8, out);
    png_chunk_t chunk;
    int wrote_idat = 0;
    while (png_read_chunk(fp, &chunk) == 0) {
        if (strcmp(chunk.type, "PLTE") == 0 && ihdr->color_type == 3) {
            uint32_t plte_len = new_count * 3;
            uint8_t *plte_data = malloc(plte_len);
            if (plte_data == NULL) { png_free_chunk(&chunk); fclose(fp); fclose(out); return -1; }
            for (size_t i = 0; i < new_count; i++) {
                plte_data[i * 3 + 0] = colors[i].r;
                plte_data[i * 3 + 1] = colors[i].g;
                plte_data[i * 3 + 2] = colors[i].b;
            }
            png_write_chunk(out, "PLTE", plte_data, plte_len);
            free(plte_data);
        } else if (strcmp(chunk.type, "IDAT") == 0) {
            if (!wrote_idat) {
                png_write_chunk(out, "IDAT", new_compbuf, (uint32_t)new_complen);
                wrote_idat = 1;
            }
        } else if (strcmp(chunk.type, "IEND") == 0) {
            png_write_chunk(out, "IEND", NULL, 0);
            png_free_chunk(&chunk);
            break;
        } else {
            png_write_chunk(out, chunk.type, chunk.data, chunk.length);
        }
        png_free_chunk(&chunk);
    }

    fclose(fp);
    fclose(out);
    return 0;
}

// helper for readiing IDATs
int png_extract_idat(FILE *fp, uint8_t **out_buf, size_t *out_len)
{
    if (fp == NULL || out_buf == NULL || out_len == NULL) return -1;

    *out_buf = NULL;
    *out_len = 0;
    png_chunk_t chunk;
    int status = png_read_chunk(fp, &chunk);
    if (status < 0) return -1;
    while (strcmp(chunk.type, "IDAT") != 0 && strcmp(chunk.type, "IEND") != 0) {
        png_free_chunk(&chunk);
        status = png_read_chunk(fp, &chunk);
        if (status < 0) return -1;
    }
    if (strcmp(chunk.type, "IDAT") != 0) {png_free_chunk(&chunk); return -1;}

    uint8_t *buffer = NULL;
    size_t total = 0;
    while (strcmp(chunk.type, "IDAT") == 0) {
        uint8_t *tmp = realloc(buffer, total + chunk.length);
        if (tmp == NULL) {free(buffer); png_free_chunk(&chunk); return -1;}
        buffer = tmp;
        memcpy(buffer + total, chunk.data, chunk.length);
        total += chunk.length;
        png_free_chunk(&chunk);
        status = png_read_chunk(fp, &chunk);
        if (status < 0) {free(buffer); return -1;}
    }

    png_free_chunk(&chunk);
    *out_buf = buffer;
    *out_len = total;
    return 0;
}

/* Encode secret string into LSBs of image data */
int png_encode_lsb(const char *input_path, const char *output_path, const char *secret)
{
    if (input_path == NULL || output_path == NULL || secret == NULL) return -1;

    FILE *fp = png_open(input_path);
    if (fp == NULL) return -1;
    png_ihdr_t ihdr;
    if (png_extract_ihdr(fp, &ihdr) < 0) {fclose(fp); return -1;}
    if (ihdr.bit_depth != 8) {fclose(fp); return -1;}

    png_color_t* colors = NULL;
    size_t og_count = 0;
    size_t new_count = 0;
    int pairs[256];
    for (int i = 0; i < 256; i++) pairs[i] = -1;
    if (ihdr.color_type == 3) {
        if (png_extract_plte(fp, &colors, &og_count) < 0) {fclose(fp); return -1;}
        for (size_t i = 0; i < og_count; i++) {
            if (pairs[i] != -1) continue;
            for (size_t j = i + 1; j < og_count; j++) {
                if (pairs[j] != -1) continue;
                if (colors[i].r == colors[j].r &&
                    colors[i].g == colors[j].g &&
                    colors[i].b == colors[j].b) {
                    pairs[i] = j;
                    pairs[j] = i;
                    break;
                }
            }
        }
        new_count = og_count;
        for (size_t i = 0; i < og_count; i++) {
            if (pairs[i] == -1) {
                if (new_count >= 256) {free(colors); fclose(fp); return -1;}
                png_color_t* tmp = realloc(colors, (new_count + 1) * sizeof(png_color_t));
                if (tmp == NULL) {free(colors); fclose(fp); return -1;}
                colors = tmp;
                colors[new_count] = colors[i];
                pairs[i] = new_count;
                pairs[new_count] = i;
                new_count++;
            }
        }
    }
    size_t compbuflen = 0;
    uint8_t* compbuf = NULL;
    if (png_extract_idat(fp, &compbuf, &compbuflen) < 0) {free(colors); fclose(fp); return -1;}
    uint8_t* buf = NULL;
    size_t buflen = 0;
    if (util_inflate_data(compbuf, compbuflen, &buf, &buflen) < 0)
    {free(compbuf); free(colors); fclose(fp); return -1;}
    free(compbuf); compbuf = NULL;
    size_t cap = ihdr.width * ihdr.height;
    size_t len = (strlen(secret) + 1) * 8;
    if (len > cap) {free(buf); free(colors); fclose(fp); return -1;}

    int bpp = (ihdr.color_type == 0) ? 1 : (ihdr.color_type == 2) ? 3 : (ihdr.color_type == 3) ? 1 :
              (ihdr.color_type == 4) ? 2 : (ihdr.color_type == 6) ? 4 : 0;
    if (bpp == 0) {free(buf); free(colors); fclose(fp); return -1;}

    size_t scanline_width = 1 + ihdr.width * bpp;
    size_t secret_len = strlen(secret) + 1;
    for (size_t i = 0; i < secret_len; i++) {
        uint8_t byte = (uint8_t) secret[i];
        for (int bit = 0; bit < 8; bit++) {
            size_t pixel = i * 8 + bit;
            size_t row = pixel / ihdr.width;
            size_t col = pixel % ihdr.width;
            size_t byte_pos = row * scanline_width + 1 + col * bpp;
            int bit_val = (byte >> bit) & 1;
            if (ihdr.color_type == 3) {
                uint8_t idx = buf[byte_pos];
                int pair = pairs[idx];
                if (pair == -1) {free(buf); free(colors); fclose(fp); return -1;}
                int lower = (idx < pair) ? idx : pair;
                int higher = (idx < pair) ? pair : idx;
                buf[byte_pos] = (bit_val == 0) ? lower : higher;
            } else {
                buf[byte_pos] = (buf[byte_pos] & 0xFE) | bit_val;
            }
        }
    }
    compbuflen = 0;
    if (util_deflate_data_png(buf, buflen, &compbuf, &compbuflen) < 0)
    {free(buf); free(colors); fclose(fp); return -1;}
    free(buf); buf = NULL;
    fclose(fp);

    if (png_write(input_path, output_path, &ihdr, colors, new_count, compbuf, compbuflen) < 0)
    {free(compbuf); free(colors); return -1;}
    free(compbuf);
    free(colors);
    return 0;
}

#undef WRITE_U32_BE

/* Extract secret string from LSBs of image data */
int png_extract_lsb(const char *input_path, char *out, size_t max_len)
{
    if (input_path == NULL || out == NULL || max_len == 0) return -1;

    FILE *fp = png_open(input_path);
    if (fp == NULL) return -1;

    png_ihdr_t ihdr;
    if (png_extract_ihdr(fp, &ihdr) < 0) {fclose(fp); return -1;}
    if (ihdr.bit_depth != 8) {fclose(fp); return -1;}

    fclose(fp);
    return 0;
}
