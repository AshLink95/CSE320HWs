#include "png_overlay.h"
#include "png_reader.h"
#include "png_chunks.h"
#include "png_crc.h"
#include "util.h"
#include "debug.h"
#include <stdio.h>
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
int overlay_write_chunk(FILE *fp, const char *type, const uint8_t *data, uint32_t length)
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

// helper to write output file
int overlay_write(const char *large_path, const char *small_path, const char *output_path, png_ihdr_t *ihdr, png_color_t *colors, size_t new_count, uint8_t *new_compbuf, size_t new_complen)
{
    if (large_path == NULL || small_path == NULL || output_path == NULL || new_compbuf == NULL) return -1;
    FILE *fp = png_open(large_path);
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
            if (plte_data == NULL) {png_free_chunk(&chunk); fclose(fp); fclose(out); return -1;}
            for (size_t i = 0; i < new_count; i++) {
                plte_data[i * 3 + 0] = colors[i].r;
                plte_data[i * 3 + 1] = colors[i].g;
                plte_data[i * 3 + 2] = colors[i].b;
            }
            overlay_write_chunk(out, "PLTE", plte_data, plte_len);
            free(plte_data);
        } else if (strcmp(chunk.type, "IDAT") == 0) {
            if (!wrote_idat) {
                // write ancillary chunks from small image before IDAT
                FILE *fp_small = png_open(small_path);
                if (fp_small != NULL) {
                    png_chunk_t schunk;
                    while (png_read_chunk(fp_small, &schunk) == 0) {
                        if (strcmp(schunk.type, "IDAT") == 0 || strcmp(schunk.type, "IEND") == 0) {
                            png_free_chunk(&schunk);
                            break;
                        }
                        if (strcmp(schunk.type, "IHDR") != 0 && strcmp(schunk.type, "PLTE") != 0) {
                            overlay_write_chunk(out, schunk.type, schunk.data, schunk.length);
                        }
                        png_free_chunk(&schunk);
                    }
                    fclose(fp_small);
                }
                overlay_write_chunk(out, "IDAT", new_compbuf, (uint32_t)new_complen);
                wrote_idat = 1;
            }
        } else if (strcmp(chunk.type, "IEND") == 0) {
            overlay_write_chunk(out, "IEND", NULL, 0);
            png_free_chunk(&chunk);
            break;
        } else {
            overlay_write_chunk(out, chunk.type, chunk.data, chunk.length);
        }
        png_free_chunk(&chunk);
    }
    fclose(fp);
    fclose(out);
    return 0;
}

// paeth predictor for filter type 4
int paeth_predictor(int a, int b, int c)
{
    int p = a + b - c;
    int pa = abs(p - a);
    int pb = abs(p - b);
    int pc = abs(p - c);
    if (pa <= pb && pa <= pc) return a;
    if (pb <= pc) return b;
    return c;
}

// unfilter a single scanline in-place
int unfilter_scanline(uint8_t *row, uint8_t *prev_row, size_t width, int bpp, uint8_t filter_type)
{
    switch (filter_type) {
        case 0: break;
        case 1:
            for (size_t i = bpp; i < width; i++) {
                row[i] = (row[i] + row[i - bpp]) & 0xFF;
            }
            break;
        case 2:
            if (prev_row) {
                for (size_t i = 0; i < width; i++) {
                    row[i] = (row[i] + prev_row[i]) & 0xFF;
                }
            }
            break;
        case 3:
            for (size_t i = 0; i < width; i++) {
                int a = (i >= (size_t)bpp) ? row[i - bpp] : 0;
                int b = prev_row ? prev_row[i] : 0;
                row[i] = (row[i] + ((a + b) / 2)) & 0xFF;
            }
            break;
        case 4:
            for (size_t i = 0; i < width; i++) {
                int a = (i >= (size_t)bpp) ? row[i - bpp] : 0;
                int b = prev_row ? prev_row[i] : 0;
                int c = (prev_row && i >= (size_t)bpp) ? prev_row[i - bpp] : 0;
                row[i] = (row[i] + paeth_predictor(a, b, c)) & 0xFF;
            }
            break;
        default:
            return -1;
    }
    return 0;
}

// unfilter all scanlines in buffer
int unfilter_image(uint8_t *buf, uint32_t width, uint32_t height, int bpp)
{
    size_t row_bytes = width * bpp;
    size_t scanline = 1 + row_bytes;
    uint8_t *prev_row = NULL;
    for (uint32_t y = 0; y < height; y++) {
        uint8_t *row_start = buf + y * scanline;
        uint8_t filter_type = row_start[0];
        uint8_t *row_data = row_start + 1;
        if (unfilter_scanline(row_data, prev_row, row_bytes, bpp, filter_type) < 0) return -1;
        row_start[0] = 0;
        prev_row = row_data;
    }
    return 0;
}

// helper for reading IDATs
int overlay_extract_idat(FILE *fp, uint8_t **out_buf, size_t *out_len)
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

int png_overlay_paste(const char *large_path, const char *small_path,
                      const char *output_path, uint32_t x_offset, uint32_t y_offset)
{
    if (large_path == NULL || small_path == NULL || output_path == NULL) return -1;
    FILE *fp_large = png_open(large_path);
    if (fp_large == NULL) return -1;
    FILE *fp_small = png_open(small_path);
    if (fp_small == NULL) {fclose(fp_large); return -1;}
    png_ihdr_t ihdr_large, ihdr_small;
    if (png_extract_ihdr(fp_large, &ihdr_large) < 0) {fclose(fp_large); fclose(fp_small); return -1;}
    if (png_extract_ihdr(fp_small, &ihdr_small) < 0) {fclose(fp_large); fclose(fp_small); return -1;}
    if (ihdr_large.bit_depth != 8 || ihdr_small.bit_depth != 8) {fclose(fp_large); fclose(fp_small); return -1;}
    if (ihdr_large.color_type != ihdr_small.color_type) {fclose(fp_large); fclose(fp_small); return -1;}
    int bpp = (ihdr_large.color_type == 0) ? 1 : (ihdr_large.color_type == 2) ? 3 :
              (ihdr_large.color_type == 3) ? 1 : (ihdr_large.color_type == 4) ? 2 :
              (ihdr_large.color_type == 6) ? 4 : 0;
    if (bpp == 0) {fclose(fp_large); fclose(fp_small); return -1;}

    png_color_t *merged_colors = NULL;
    size_t merged_count = 0;
    uint8_t index_map[256];
    for (int i = 0; i < 256; i++) index_map[i] = 0;
    if (ihdr_large.color_type == 3) {
        png_color_t *large_colors = NULL, *small_colors = NULL;
        size_t large_count = 0, small_count = 0;
        if (png_extract_plte(fp_large, &large_colors, &large_count) < 0) {
            fclose(fp_large); fclose(fp_small); return -1;
        }
        if (png_extract_plte(fp_small, &small_colors, &small_count) < 0) {
            free(large_colors); fclose(fp_large); fclose(fp_small); return -1;
        }
        merged_colors = malloc(256 * sizeof(png_color_t));
        if (merged_colors == NULL) {
            free(large_colors); free(small_colors); fclose(fp_large); fclose(fp_small); return -1;
        }
        memcpy(merged_colors, large_colors, large_count * sizeof(png_color_t));
        merged_count = large_count;
        for (size_t i = 0; i < small_count; i++) {
            int found = -1;
            for (size_t j = 0; j < merged_count; j++) {
                if (small_colors[i].r == merged_colors[j].r &&
                    small_colors[i].g == merged_colors[j].g &&
                    small_colors[i].b == merged_colors[j].b) {
                    found = j;
                    break;
                }
            }
            if (found >= 0) {
                index_map[i] = found;
            } else {
                if (merged_count >= 256) {
                    free(large_colors); free(small_colors); free(merged_colors);
                    fclose(fp_large); fclose(fp_small); return -1;
                }
                merged_colors[merged_count] = small_colors[i];
                index_map[i] = merged_count;
                merged_count++;
            }
        }
        free(large_colors);
        free(small_colors);
    }

    uint8_t *large_comp = NULL, *small_comp = NULL;
    size_t large_comp_len = 0, small_comp_len = 0;
    if (overlay_extract_idat(fp_large, &large_comp, &large_comp_len) < 0) {
        free(merged_colors); fclose(fp_large); fclose(fp_small); return -1;
    }
    if (overlay_extract_idat(fp_small, &small_comp, &small_comp_len) < 0) {
        free(large_comp); free(merged_colors); fclose(fp_large); fclose(fp_small); return -1;
    }
    fclose(fp_large);
    fclose(fp_small);

    uint8_t *large_buf = NULL, *small_buf = NULL;
    size_t large_len = 0, small_len = 0;
    if (util_inflate_data(large_comp, large_comp_len, &large_buf, &large_len) < 0) {
        free(large_comp); free(small_comp); free(merged_colors); return -1;
    }
    if (util_inflate_data(small_comp, small_comp_len, &small_buf, &small_len) < 0) {
        free(large_buf); free(large_comp); free(small_comp); free(merged_colors); return -1;
    }
    free(large_comp);
    free(small_comp);

    if (unfilter_image(large_buf, ihdr_large.width, ihdr_large.height, bpp) < 0) {
        free(large_buf); free(small_buf); free(merged_colors); return -1;
    }
    if (unfilter_image(small_buf, ihdr_small.width, ihdr_small.height, bpp) < 0) {
        free(large_buf); free(small_buf); free(merged_colors); return -1;
    }

    size_t large_scanline = 1 + ihdr_large.width * bpp;
    size_t small_scanline = 1 + ihdr_small.width * bpp;
    for (uint32_t sy = 0; sy < ihdr_small.height; sy++) {
        uint32_t dy = y_offset + sy;
        if (dy >= ihdr_large.height) break;
        for (uint32_t sx = 0; sx < ihdr_small.width; sx++) {
            uint32_t dx = x_offset + sx;
            if (dx >= ihdr_large.width) break;
            size_t src_pos = sy * small_scanline + 1 + sx * bpp;
            size_t dst_pos = dy * large_scanline + 1 + dx * bpp;
            if (ihdr_large.color_type == 3) {
                uint8_t old_idx = small_buf[src_pos];
                large_buf[dst_pos] = index_map[old_idx];
            } else {
                memcpy(&large_buf[dst_pos], &small_buf[src_pos], bpp);
            }
        }
    }
    free(small_buf);

    uint8_t *new_comp = NULL;
    size_t new_comp_len = 0;
    if (util_deflate_data_png(large_buf, large_len, &new_comp, &new_comp_len) < 0) {
        free(large_buf); free(merged_colors); return -1;
    }
    free(large_buf);

    if (overlay_write(large_path, small_path, output_path, &ihdr_large, merged_colors, merged_count, new_comp, new_comp_len) < 0) {
        free(new_comp); free(merged_colors); return -1;
    }
    free(new_comp);
    free(merged_colors);
    return 0;
}

#undef WRITE_U32_BE
