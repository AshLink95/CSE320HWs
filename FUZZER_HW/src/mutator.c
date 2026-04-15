#include "../include/mutator.h"
#include "../include/global.h"
#include <string.h>
#include <stdio.h>

#define MAX_INPUT_LEN 1024
#define NUM_MUTATIONS 37

static uint64_t H(uint64_t n, uint64_t m, uint64_t x) {
    uint64_t val = x;
    for (uint64_t i = 0; i < m; i++)
        val = hash(val % n);
    return val;
}

static INPUT strategy_fill(const char *str, size_t N, uint64_t K) {
    if (N == 0) return make_input("");
    size_t target;
    if (K > 10)
        target = MAX_INPUT_LEN;
    else {
        target = (size_t)(1ULL << K);
        if (target > MAX_INPUT_LEN) target = MAX_INPUT_LEN;
    }
    char buf[MAX_INPUT_LEN + 1];
    size_t copy = target < N ? target : N;
    memcpy(buf, str, copy);
    for (size_t i = copy; i < target; i++)
        buf[i] = 'a';
    buf[target] = '\0';
    return make_input(buf);
}

static INPUT strategy_duplicate(const char *str, size_t N, uint64_t K) {
    if (N == 0) return make_input("");
    size_t total;
    if (K + 1 > MAX_INPUT_LEN / N)
        total = MAX_INPUT_LEN;
    else {
        total = (size_t)(N * (K + 1));
        if (total > MAX_INPUT_LEN) total = MAX_INPUT_LEN;
    }
    char buf[MAX_INPUT_LEN + 1];
    for (size_t i = 0; i < total; i++)
        buf[i] = str[i % N];
    buf[total] = '\0';
    return make_input(buf);
}

static INPUT strategy_lengthen(const char *str, size_t N, uint64_t S, size_t L) {
    if (N == 0) return make_input("");
    size_t new_len = N + L;
    if (new_len > MAX_INPUT_LEN) new_len = MAX_INPUT_LEN;
    char buf[MAX_INPUT_LEN + 1];
    memcpy(buf, str, N);
    for (size_t i = 0; i < new_len - N; i++) {
        uint64_t idx = H(N, (uint64_t)(i + 1), S) % N;
        buf[N + i] = buf[idx];
    }
    buf[new_len] = '\0';
    return make_input(buf);
}

static INPUT strategy_truncate(const char *str, size_t N, uint64_t S) {
    if (N == 0) return make_input("");
    uint64_t tlen = H(N, 1, S);
    size_t new_len = tlen < N ? (size_t)tlen : N;
    char buf[MAX_INPUT_LEN + 1];
    memcpy(buf, str, new_len);
    buf[new_len] = '\0';
    return make_input(buf);
}

static INPUT strategy_inject_string(const char *str, size_t N, uint64_t S,
                                     uint64_t K, const char *A) {
    if (N == 0) return make_input("");
    char buf[MAX_INPUT_LEN + 1];
    memcpy(buf, str, N);
    buf[N] = '\0';
    size_t A_len = strlen(A);
    uint64_t p = (K + 1) / 2;
    for (uint64_t i = 1; i <= p; i++) {
        size_t pos = (size_t)(H(N, i, S) % N);
        size_t wlen = A_len;
        if (pos + wlen > N) wlen = N - pos;
        memcpy(buf + pos, A, wlen);
    }
    return make_input(buf);
}

static INPUT strategy_inject_random_int(const char *str, size_t N,
                                         uint64_t S, uint64_t K) {
    if (N == 0) return make_input("");
    int32_t z = (int32_t)hash(S);
    char num_buf[16];
    snprintf(num_buf, sizeof(num_buf), "%d", z);
    return strategy_inject_string(str, N, S, K, num_buf);
}

static INPUT strategy_inject_substr(const char *str, size_t N, uint64_t S,
                                     uint64_t K, size_t L) {
    if (N == 0) return make_input("");
    size_t sub_start = (size_t)(H(N, 1, S) % N);
    size_t sub_len = L;
    if (sub_start + sub_len > N) sub_len = N - sub_start;
    char substr[MAX_INPUT_LEN + 1];
    memcpy(substr, str + sub_start, sub_len);

    char buf[MAX_INPUT_LEN + 1];
    memcpy(buf, str, N);
    buf[N] = '\0';

    uint64_t p = (K + 1) / 2;
    for (uint64_t i = 1; i <= p; i++) {
        size_t pos = (size_t)(H(N, (uint64_t)(i + 1), S) % N);
        size_t wlen = sub_len;
        if (pos + wlen > N) wlen = N - pos;
        memcpy(buf + pos, substr, wlen);
    }
    return make_input(buf);
}

static INPUT strategy_inject_char(const char *str, size_t N, uint64_t S,
                                   uint64_t K, char C) {
    if (N == 0) return make_input("");
    char buf[MAX_INPUT_LEN + 1];
    memcpy(buf, str, N);
    buf[N] = '\0';
    uint64_t p = (K + 1) / 2;
    for (uint64_t i = 1; i <= p; i++) {
        size_t pos = (size_t)(H(N, i, S) % N);
        buf[pos] = C;
    }
    return make_input(buf);
}

static INPUT strategy_flip_bits(const char *str, size_t N, uint64_t S,
                                 uint64_t K, size_t L) {
    if (N == 0) return make_input("");
    char buf[MAX_INPUT_LEN + 1];
    memcpy(buf, str, N);
    buf[N] = '\0';
    uint64_t total_bits = 8 * N;
    uint64_t p = (K + 1) / 2;
    for (uint64_t i = 1; i <= p; i++) {
        uint64_t bit_loc = H(total_bits, i, S) % total_bits;
        for (size_t j = 0; j < L && bit_loc + j < total_bits; j++) {
            uint64_t bpos = bit_loc + j;
            buf[bpos / 8] ^= (1 << (bpos % 8));
        }
    }
    return make_input(buf);
}

static INPUT strategy_inc_bytes(const char *str, size_t N, uint64_t S,
                                 uint64_t K) {
    if (N == 0) return make_input("");
    char buf[MAX_INPUT_LEN + 1];
    memcpy(buf, str, N);
    buf[N] = '\0';
    uint64_t p = (K + 1) / 2;
    for (uint64_t i = 1; i <= p; i++) {
        size_t pos = (size_t)(H(N, i, S) % N);
        buf[pos]++;
    }
    return make_input(buf);
}

static INPUT strategy_dec_bytes(const char *str, size_t N, uint64_t S,
                                 uint64_t K) {
    if (N == 0) return make_input("");
    char buf[MAX_INPUT_LEN + 1];
    memcpy(buf, str, N);
    buf[N] = '\0';
    uint64_t p = (K + 1) / 2;
    for (uint64_t i = 1; i <= p; i++) {
        size_t pos = (size_t)(H(N, i, S) % N);
        buf[pos]--;
    }
    return make_input(buf);
}

INPUT mutate(INPUT input) {
    MUTATOR_STATE S = input_mutator_state(input);
    const char *str = input_str(input);
    size_t N = input_len(input);
    uint64_t K = S / NUM_MUTATIONS + 1;
    int idx = S % NUM_MUTATIONS;

    input_state_step(input);

    switch (idx) {
        case 0:  return strategy_inject_substr(str, N, S, K, 1);
        case 1:  return strategy_inject_string(str, N, S, K, "0");
        case 2:  return strategy_lengthen(str, N, S, 4);
        case 3:  return strategy_inject_string(str, N, S, K, "1");
        case 4:  return strategy_inject_char(str, N, S, K, '/');
        case 5:  return strategy_lengthen(str, N, S, 7);
        case 6:  return strategy_flip_bits(str, N, S, K, 1);
        case 7:  return strategy_dec_bytes(str, N, S, K);
        case 8:  return strategy_duplicate(str, N, K);
        case 9:  return strategy_lengthen(str, N, S, 1);
        case 10: return strategy_inject_string(str, N, S, K, "-128");
        case 11: return strategy_lengthen(str, N, S, 8);
        case 12: return strategy_inject_string(str, N, S, K, "2147483647");
        case 13: return strategy_flip_bits(str, N, S, K, 4);
        case 14: return strategy_inject_string(str, N, S, K, "-1");
        case 15: return strategy_lengthen(str, N, S, 2);
        case 16: return strategy_inject_string(str, N, S, K, "32767");
        case 17: return strategy_fill(str, N, K);
        case 18: return strategy_inject_substr(str, N, S, K, 2);
        case 19: return strategy_inject_char(str, N, S, K, '.');
        case 20: return strategy_lengthen(str, N, S, 3);
        case 21: return strategy_inject_string(str, N, S, K, "%p");
        case 22: return strategy_truncate(str, N, S);
        case 23: return strategy_inject_random_int(str, N, S, K);
        case 24: return strategy_inject_string(str, N, S, K, "127");
        case 25: return strategy_inject_char(str, N, S, K, ';');
        case 26: return strategy_lengthen(str, N, S, 5);
        case 27: return strategy_inject_string(str, N, S, K, "-32768");
        case 28: return strategy_inc_bytes(str, N, S, K);
        case 29: return strategy_inject_string(str, N, S, K, "%s");
        case 30: return strategy_inject_substr(str, N, S, K, 8);
        case 31: return strategy_flip_bits(str, N, S, K, 8);
        case 32: return strategy_inject_string(str, N, S, K, "-2147483648");
        case 33: return strategy_inject_char(str, N, S, K, ',');
        case 34: return strategy_lengthen(str, N, S, 6);
        case 35: return strategy_inject_substr(str, N, S, K, 4);
        case 36: return strategy_flip_bits(str, N, S, K, 2);
        default: return make_input("");
    }
}
