#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "lz.h"
#include "debug.h"

#define WINDOW_SIZE 32768 // Allowed reference of previous strings (allowed to span previous blocks)
#define MAX_MATCH   258 // Max range of matches 3.2.5 (max(3-258) => max(0-255))
#define MIN_MATCH   3

#define HASH_BITS   15
#define HASH_SIZE  (1 << HASH_BITS)
#define MAX_CHAIN   64

#define HASH_SHIFT  5 //(HASH_BITS + MIN_MATCH - 1) / MIN_MATCH
#define HASH_MASK  (HASH_SIZE - 1)
#define UPDATE_HASH(h,c) (h = (((h) << HASH_SHIFT) ^ (c)) & HASH_MASK)

/**
 * Find longest match of data[pos..] in data[pos - offset_limit .. pos - 1].
 * Return length (0 if none); store best offset in *out_offset.
 * @param data: data stream
 * @param pos: position of original location 
 * @param len: length of an allowed match 
 * @param offset_limit: offset allowed to go to (likely based on sliding window)
 * @param out_offset: offset of the best match per the current location and data in sliding window (LZ77 Distance)
 * @return Length of the best match found, store relative offset in out_offset (LZ77 Length)
 */
static void insert_hash(const unsigned char* data, size_t pos, size_t len, uint32_t* head, uint32_t* prev) {
    if (pos + MIN_MATCH > len) return;
    uint32_t h = data[pos];
    UPDATE_HASH(h, data[pos + 1]);
    UPDATE_HASH(h, data[pos + 2]);
    prev[pos % WINDOW_SIZE] = head[h];
    head[h] = (uint32_t)pos;
}

static int find_match(const unsigned char* data, size_t pos, size_t len, size_t offset_limit, size_t* out_offset, uint32_t* head, uint32_t* prev) {
    if (pos + MIN_MATCH > len) return 0;
    uint32_t h = data[pos];
    UPDATE_HASH(h, data[pos + 1]);
    UPDATE_HASH(h, data[pos + 2]);
    uint32_t candidate = head[h];

    int best_len = 0;
    int steps = 0;
    while (candidate != UINT32_MAX && steps < MAX_CHAIN) {
        if (pos - candidate <= offset_limit) {
            int match_len = 0;
            while (match_len < MAX_MATCH
                   && pos + match_len < len
                   && data[candidate + match_len] == data[pos + match_len]) {
                match_len++;
            }
            if (match_len > best_len) {
                best_len = match_len;
                *out_offset = pos - candidate;
            }
        }
        candidate = prev[candidate % WINDOW_SIZE];
        steps++;
    }

    prev[pos % WINDOW_SIZE] = head[h];
    head[h] = (uint32_t)pos;
	return best_len; // Return length of the best match found
}


/**
 * @param: data: stream of data 
 * @param: len: length of the data
 * @param num_tokens: the number of tokens stored when running on the data
 * @return An array of tokens that contain a concise form of LZ77 data, rather literal or length-distance entries stored in raw data form 
 */
lz_token_t* lz_compress_tokens(const unsigned char* data, size_t len, size_t* num_tokens) {
    if (!data || !num_tokens) return NULL;
    size_t cap = len; // worst case: all literals
    lz_token_t* tokens = malloc(cap * sizeof(lz_token_t));
    if (!tokens) return NULL;
    size_t count = 0;
    size_t pos = 0;
    size_t offset_limit = WINDOW_SIZE;
    if (offset_limit > len) offset_limit = len;

    uint32_t head[HASH_SIZE];
    uint32_t prev[WINDOW_SIZE];
    memset(head, 0xFF, sizeof(head));
    memset(prev, 0xFF, sizeof(prev));

    while (pos < len) {
        size_t best_offset;
        int match_len = find_match(data, pos, len, offset_limit, &best_offset, head, prev);

        if (match_len >= MIN_MATCH) {
            tokens[count].is_literal = 0;
            tokens[count].literal = 0;
            tokens[count].length = (unsigned int)match_len;
            tokens[count].distance = (unsigned int)best_offset;
            count++;
            for (size_t i = pos + 1; i < pos + match_len; i++)
                insert_hash(data, i, len, head, prev);
            pos += match_len;
        } else 
        {
            tokens[count].is_literal = 1;
            tokens[count].literal = data[pos];
            tokens[count].length = 0;
            tokens[count].distance = 0;
            count++;
            pos++;
        }

        if (count >= cap) {
            cap *= 2;
            lz_token_t* tmp = realloc(tokens, cap * sizeof(lz_token_t));
            if (!tmp) { free(tokens); return NULL; }
            tokens = tmp;
        }
    }
    *num_tokens = count;
    return tokens;
}

