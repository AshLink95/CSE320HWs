#include "png_chunks.h"
#include "util.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* Parse IHDR data from chunk */
/* Chunk must be an IHDR chunk with length 13 */
int png_parse_ihdr(const png_chunk_t *chunk, png_ihdr_t *out)
{
    if (chunk == NULL || out == NULL) return -1;
    const char* type = chunk->type;
    uint8_t* data = chunk->data;
    uint32_t len = chunk->length;
    if (strncmp(type, "IHDR", 4) != 0) return -1;
    if (len != 13) return -1;
    if (data == NULL) return -1;
    uint32_t width  = read_u32_be(&data[0]);
    uint32_t height = read_u32_be(&data[4]);
    uint8_t  bit_depth = data[8];
    uint8_t  color_type = data[9];
    uint8_t  compression = data[10];
    uint8_t  filter = data[11];
    uint8_t  interlace = data[12];
    if (width == 0 || height == 0) return -1;
    if (color_type != 0 && color_type != 2 && color_type != 3 && color_type != 4 && color_type != 6) {
        return -1;
    } else if (color_type == 0 && (bit_depth != 1 && bit_depth != 2 && bit_depth != 4 && bit_depth != 8 && bit_depth != 16)) {
        return -1;
    } else if (color_type == 2 && (bit_depth != 8 && bit_depth != 16)) {
        return -1;
    } else if (color_type == 3 && (bit_depth != 1 && bit_depth != 2 && bit_depth != 4 && bit_depth != 8)) {
        return -1;
    } else if (color_type == 4 && (bit_depth != 8 && bit_depth != 16)) {
        return -1;
    } else if (color_type == 6 && (bit_depth != 8 && bit_depth != 16)) {
        return -1;
    }
    if (compression != 0 || filter != 0) return -1;
    if (interlace != 0 && interlace != 1) return -1;
    png_ihdr_t parsed = {
        .width = width,
        .height = height,
        .bit_depth = bit_depth,
        .color_type = color_type,
        .compression = compression,
        .filter = filter,
        .interlace = interlace,
    };
    *out = parsed;
    return 0;
}

/* Parse PLTE data from chunk into an allocated array of colors */
/* Chunk must be a PLTE chunk with length multiple of 3 */
int png_parse_plte(const png_chunk_t *chunk, png_color_t **out_colors, size_t *out_count)
{
    if (chunk == NULL || out_colors == NULL || out_count == NULL) return -1;
    const char* type = chunk->type;
    uint8_t* data = chunk->data;
    uint32_t len = chunk->length;
    if (strncmp(type, "PLTE", 4) != 0) return -1;
    if (len%3 != 0 || (len < 3 || len > 768)) return -1;
    if (data == NULL) return -1;
    *out_colors = malloc(len);
    if (*out_colors == NULL) return -1;
    *out_count = len / 3;
    for (size_t i = 0; i<*out_count; i++) {
        (*out_colors)[i].r = data[i*3+0];
        (*out_colors)[i].g = data[i*3+1];
        (*out_colors)[i].b = data[i*3+2];
    }
    return 0;
}
