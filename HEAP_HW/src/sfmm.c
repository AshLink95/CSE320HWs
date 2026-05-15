/**
 * Do not submit your assignment with a main function in this file.
 * If you submit with a main function in this file, you will get a zero.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "debug.h"
#include "sfmm.h"

static int HEAP_INITIALIZED = 0;
static size_t MAX_AGGREGATE_PAYLOAD = 0;
static size_t CURRENT_AGGREGATE_PAYLOAD = 0;

///////// HELPERS
static size_t get_block_size(size_t size) {
    size_t mod = size % 16;
    size_t pad = mod == 0 ? 0 : 16 - mod;
    size_t tize = size + pad + 16;
    return tize;
}

static void write_block(sf_block *block, size_t block_size, size_t payload_size, int in_qklst, int alloc) {
    uint64_t head = payload_size << 32 | block_size | in_qklst << 1 | alloc;
    block->header = head ^ MAGIC;
    sf_footer *footer = (sf_footer *)((char *)block + block_size - 8);
    *footer = block->header;
}

int get_from_flist(size_t size) {
    size_t m = 32;
    if (size <= m) return 0;
    if (size > m << 10) return 11;
    for (int i = 1; i < NUM_FREE_LISTS; i++) {
        if (size <= m << i) return i;
    }
    return -1;
}

static void remove_from_flist(sf_block *block) {
    block->body.links.prev->body.links.next = block->body.links.next;
    block->body.links.next->body.links.prev = block->body.links.prev;
}

static void add_to_flist(sf_block *block) {
    size_t block_size = (block->header ^ MAGIC) & 0x00000000FFFFFFF0;
    int index = get_from_flist(block_size);
    sf_block *head = &sf_free_list_heads[index];
    block->body.links.next = head->body.links.next;
    block->body.links.prev = head;
    head->body.links.next->body.links.prev = block;
    head->body.links.next = block;
}

static sf_block *coalesce(sf_block *block) {
    size_t block_size = (block->header ^ MAGIC) & 0x00000000FFFFFFF0;

    sf_block *next_block = (sf_block *)((char *)block + block_size);
    uint64_t next_header = next_block->header ^ MAGIC;
    if (!(next_header & THIS_BLOCK_ALLOCATED)) {
        size_t next_size = next_header & 0x00000000FFFFFFF0;
        remove_from_flist(next_block);
        block_size += next_size;
    }

    sf_footer *prev_footer = (sf_footer *)((char *)block - 8);
    uint64_t prev_raw = *prev_footer ^ MAGIC;
    if (!(prev_raw & THIS_BLOCK_ALLOCATED)) {
        size_t prev_size = prev_raw & 0x00000000FFFFFFF0;
        sf_block *prev_block = (sf_block *)((char *)block - prev_size);
        remove_from_flist(prev_block);
        block_size += prev_size;
        block = prev_block;
    }

    write_block(block, block_size, 0, 0, 0);
    add_to_flist(block);
    return block;
}

static void init_heap() {
    for (int i = 0; i < NUM_FREE_LISTS; i++) {
        sf_free_list_heads[i].body.links.next = &sf_free_list_heads[i];
        sf_free_list_heads[i].body.links.prev = &sf_free_list_heads[i];
    }
    for (int i = 0; i < NUM_QUICK_LISTS; i++) {
        sf_quick_lists[i].length = 0;
        sf_quick_lists[i].first = NULL;
    }

    void *page = sf_mem_grow();
    if (page == NULL) return;

    sf_block *prologue = (sf_block *)((char *)sf_mem_start() + 8);
    write_block(prologue, 32, 0, 0, 1);

    sf_block *epilogue = (sf_block *)((char *)sf_mem_end() - 8);
    epilogue->header = (0 | THIS_BLOCK_ALLOCATED) ^ MAGIC;

    sf_block *first_free = (sf_block *)((char *)prologue + 32);
    size_t free_size = (char *)sf_mem_end() - 8 - (char *)first_free;
    write_block(first_free, free_size, 0, 0, 0);
    add_to_flist(first_free);

    HEAP_INITIALIZED = 1;
}

static sf_block *grow_heap() {
    void *new_page = sf_mem_grow();
    if (new_page == NULL) { sf_errno = ENOMEM; return NULL; }

    sf_block *new_block = (sf_block *)((char *)new_page - 8);
    size_t new_size = PAGE_SZ;

    sf_block *epilogue = (sf_block *)((char *)sf_mem_end() - 8);
    epilogue->header = (0 | THIS_BLOCK_ALLOCATED) ^ MAGIC;
    write_block(new_block, new_size, 0, 0, 0);
    return coalesce(new_block);
}

static void flush_quick_list(int qi) {
    sf_block *bp = sf_quick_lists[qi].first;
    while (bp != NULL) {
        sf_block *next = bp->body.links.next;
        size_t block_size = (bp->header ^ MAGIC) & 0x00000000FFFFFFF0;

        write_block(bp, block_size, 0, 0, 0);
        coalesce(bp);
        bp = next;
    }
    sf_quick_lists[qi].first = NULL;
    sf_quick_lists[qi].length = 0;
}

static int validate_pointer(void *pp) {
    if (pp == NULL) return 0;
    if ((uintptr_t)pp % 16 != 0) return 0;

    sf_block *block = (sf_block *)((char *)pp - 8);
    uint64_t header = block->header ^ MAGIC;
    size_t block_size = header & 0x00000000FFFFFFF0;
    int alloc = header & THIS_BLOCK_ALLOCATED;
    int in_qklst = header & IN_QUICK_LIST;

    if (block_size < 32) return 0;
    if (block_size % 16 != 0) return 0;
    if ((char *)block < (char *)sf_mem_start() + 40) return 0;
    if ((char *)block + block_size > (char *)sf_mem_end() - 8) return 0;
    if (!alloc) return 0;
    if (in_qklst) return 0;

    return 1;
}

///////// NEEDEDS
void *sf_malloc(size_t size) {
    if (size == 0) return NULL;

    if (!HEAP_INITIALIZED) init_heap();
    if (!HEAP_INITIALIZED) {
        sf_errno = ENOMEM;
        return NULL;
    }

    size_t block_size = get_block_size(size);

    int qi = (block_size - 32) / 16;
    if (qi >= 0 && qi < NUM_QUICK_LISTS && sf_quick_lists[qi].length > 0) {
        sf_block *found = sf_quick_lists[qi].first;
        sf_quick_lists[qi].first = found->body.links.next;
        sf_quick_lists[qi].length--;
        write_block(found, block_size, size, 0, 1);
        CURRENT_AGGREGATE_PAYLOAD += size;
        if (CURRENT_AGGREGATE_PAYLOAD > MAX_AGGREGATE_PAYLOAD)
            MAX_AGGREGATE_PAYLOAD = CURRENT_AGGREGATE_PAYLOAD;
        return found->body.payload;
    }

    sf_block *found = NULL;
    int start_index = get_from_flist(block_size);
    for (int i = start_index; i < NUM_FREE_LISTS; i++) {
        sf_block *current = sf_free_list_heads[i].body.links.next;
        while (current != &sf_free_list_heads[i]) {
            size_t cur_size = (current->header ^ MAGIC) & 0x00000000FFFFFFF0;
            if (cur_size >= block_size) {
                found = current;
                break;
            }
            current = current->body.links.next;
        }
        if (found) break;
    }

    while (!found) {
        sf_block *new_block = grow_heap();
        if (new_block == NULL) return NULL;
        size_t new_size = (new_block->header ^ MAGIC) & 0x00000000FFFFFFF0;
        if (new_size >= block_size)
            found = new_block;
    }

    remove_from_flist(found);
    size_t found_size = (found->header ^ MAGIC) & 0x00000000FFFFFFF0;
    size_t remainder = found_size - block_size;

    if (remainder >= 32) {
        sf_block *rem_block = (sf_block *)((char *)found + block_size);
        write_block(rem_block, remainder, 0, 0, 0);
        add_to_flist(rem_block);
        write_block(found, block_size, size, 0, 1);
    } else {
        write_block(found, found_size, size, 0, 1);
    }

    CURRENT_AGGREGATE_PAYLOAD += size;
    if (CURRENT_AGGREGATE_PAYLOAD > MAX_AGGREGATE_PAYLOAD)
        MAX_AGGREGATE_PAYLOAD = CURRENT_AGGREGATE_PAYLOAD;

    return found->body.payload;
}

void sf_free(void *pp) {
    if (!validate_pointer(pp)) abort();

    sf_block *block = (sf_block *)((char *)pp - 8);
    uint64_t header = block->header ^ MAGIC;
    size_t block_size = header & 0x00000000FFFFFFF0;
    size_t payload_size = header >> 32;

    CURRENT_AGGREGATE_PAYLOAD -= payload_size;

    int qi = (block_size - 32) / 16;
    if (qi >= 0 && qi < NUM_QUICK_LISTS) {
        if (sf_quick_lists[qi].length >= QUICK_LIST_MAX) {
            flush_quick_list(qi);
        }
        write_block(block, block_size, 0, 1, 1);
        block->body.links.next = sf_quick_lists[qi].first;
        sf_quick_lists[qi].first = block;
        sf_quick_lists[qi].length++;
        return;
    }

    write_block(block, block_size, 0, 0, 0);
    coalesce(block);
}

void *sf_realloc(void *pp, size_t rsize) {
    if (!validate_pointer(pp)) {
        sf_errno = EINVAL;
        return NULL;
    }

    if (rsize == 0) {
        sf_free(pp);
        return NULL;
    }

    sf_block *block = (sf_block *)((char *)pp - 8);
    uint64_t header = block->header ^ MAGIC;
    size_t block_size = header & 0x00000000FFFFFFF0;
    size_t old_payload = header >> 32;

    size_t new_block_size = get_block_size(rsize);

    if (new_block_size > block_size) {
        void *new_ptr = sf_malloc(rsize);
        if (new_ptr == NULL) return NULL;
        memcpy(new_ptr, pp, old_payload);
        sf_free(pp);
        return new_ptr;
    }

    if (new_block_size < block_size) {
        size_t remainder = block_size - new_block_size;
        if (remainder >= 32) {
            write_block(block, new_block_size, rsize, 0, 1);

            sf_block *rem = (sf_block *)((char *)block + new_block_size);
            write_block(rem, remainder, 0, 0, 0);
            coalesce(rem);
        } else {
            write_block(block, block_size, rsize, 0, 1);
        }
    } else {
        write_block(block, block_size, rsize, 0, 1);
    }

    CURRENT_AGGREGATE_PAYLOAD -= old_payload;
    CURRENT_AGGREGATE_PAYLOAD += rsize;
    return pp;
}

double sf_fragmentation() {
    if (!HEAP_INITIALIZED) return 0.0;

    double total_payload = 0.0;
    double total_block_size = 0.0;

    sf_block *bp = (sf_block *)((char *)sf_mem_start() + 40);
    sf_block *epilogue = (sf_block *)((char *)sf_mem_end() - 8);

    while (bp < epilogue) {
        uint64_t hdr = bp->header ^ MAGIC;
        size_t bsize = hdr & 0x00000000FFFFFFF0;
        if (bsize == 0) break;

        int alloc = hdr & THIS_BLOCK_ALLOCATED;
        int in_qklst = hdr & IN_QUICK_LIST;

        if (alloc && !in_qklst) {
            total_payload += (hdr >> 32);
            total_block_size += bsize;
        }

        bp = (sf_block *)((char *)bp + bsize);
    }

    if (total_block_size == 0.0) return 0.0;
    return total_payload / total_block_size;
}

double sf_utilization() {
    if (!HEAP_INITIALIZED) return 0.0;
    size_t heap_size = (char *)sf_mem_end() - (char *)sf_mem_start();
    if (heap_size == 0) return 0.0;
    return (double)MAX_AGGREGATE_PAYLOAD / (double)heap_size;
}
