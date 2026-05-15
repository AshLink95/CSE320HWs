#include "../include/coverage_map.h"
#include "../include/global.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

struct slot {
    uint64_t key;
    char occupied;
};

struct coverage_map {
    char bitmap[COVERAGE_MAP_SIZE / 8];
    struct slot hashset[COVERAGE_MAP_SIZE];
};

COVERAGE_MAP coverage_map_init() {
    return calloc(1, sizeof(struct coverage_map));
}

void coverage_map_fini(COVERAGE_MAP map) {
    free(map);
}

static uint64_t hash_coverage_data(char *cov_data) {
    uint64_t h = 0;
    for (size_t i = 0; i < COVERAGE_MAP_SIZE / 8; i++)
        h = hash(h ^ (uint64_t)(unsigned char)cov_data[i]);
    return h;
}

static int hashset_insert(struct slot *hs, uint64_t key) {
    size_t idx = key % COVERAGE_MAP_SIZE;
    for (size_t i = 0; i < COVERAGE_MAP_SIZE; i++) {
        size_t pos = (idx + i) % COVERAGE_MAP_SIZE;
        if (!hs[pos].occupied) {
            hs[pos].key = key;
            hs[pos].occupied = 1;
            return 1;
        }
        if (hs[pos].key == key)
            return 0;
    }
    return 0;
}

COVERAGE_PRIORITY coverage_map_add(COVERAGE_MAP map, char *cov_data) {
    int new_edge = 0;
    for (size_t i = 0; i < COVERAGE_MAP_SIZE / 8; i++) {
        if (cov_data[i] & ~map->bitmap[i])
            new_edge = 1;
        map->bitmap[i] |= cov_data[i];
    }

    uint64_t path_hash = hash_coverage_data(cov_data);
    int new_path = hashset_insert(map->hashset, path_hash);

    if (new_edge) return COV_HIGH_PRIO;
    if (new_path) return COV_LOW_PRIO;
    return COV_NO_PRIO;
}
