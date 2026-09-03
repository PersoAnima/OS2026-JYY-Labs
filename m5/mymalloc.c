#include "mymalloc.h"

#define PAGE_SIZE 4096UL
#define CHUNK_SIZE (64UL * 1024UL)
#define MIN_PAYLOAD 8UL

typedef struct block {
    size_t size;
    int free;
    struct block *prev;
    struct block *next;
} block_t;

static spinlock_t heap_lock;
static block_t *heap_head;
long malloc_count;

static size_t align_up(size_t x, size_t align) {
    return (x + align - 1) & ~(align - 1);
}

static size_t request_size(size_t size) {
    if (size < MIN_PAYLOAD) {
        size = MIN_PAYLOAD;
    }
    return align_up(size, 8);
}

static size_t page_round(size_t size) {
    return align_up(size, PAGE_SIZE);
}

static char *block_end(block_t *block) {
    return (char *)block + sizeof(*block) + block->size;
}

static void insert_block(block_t *block) {
    if (heap_head == 0 || block < heap_head) {
        block->prev = 0;
        block->next = heap_head;
        if (heap_head != 0) {
            heap_head->prev = block;
        }
        heap_head = block;
        return;
    }

    block_t *cur = heap_head;
    while (cur->next != 0 && cur->next < block) {
        cur = cur->next;
    }

    block->prev = cur;
    block->next = cur->next;
    if (cur->next != 0) {
        cur->next->prev = block;
    }
    cur->next = block;
}

static void coalesce_with_next(block_t *block) {
    block_t *next = block->next;
    if (next != 0 && block->free && next->free && block_end(block) == (char *)next) {
        block->size += sizeof(*next) + next->size;
        block->next = next->next;
        if (next->next != 0) {
            next->next->prev = block;
        }
    }
}

static void split_block(block_t *block, size_t size) {
    if (block->size < size + sizeof(block_t) + MIN_PAYLOAD) {
        return;
    }

    block_t *rest = (block_t *)((char *)block + sizeof(*block) + size);
    rest->size = block->size - size - sizeof(*rest);
    rest->free = 1;
    rest->prev = block;
    rest->next = block->next;

    if (block->next != 0) {
        block->next->prev = rest;
    }

    block->size = size;
    block->next = rest;
}

static block_t *find_free_block(size_t size) {
    for (block_t *cur = heap_head; cur != 0; cur = cur->next) {
        if (cur->free && cur->size >= size) {
            return cur;
        }
    }
    return 0;
}

static block_t *extend_heap(size_t size) {
    size_t bytes = page_round(sizeof(block_t) + size);
    if (bytes < CHUNK_SIZE) {
        bytes = CHUNK_SIZE;
    }

    block_t *block = vmalloc(0, bytes);
    if (block == 0) {
        return 0;
    }

    block->size = bytes - sizeof(*block);
    block->free = 1;
    block->prev = 0;
    block->next = 0;
    insert_block(block);

    if (block->prev != 0) {
        coalesce_with_next(block->prev);
        block = block->prev;
    }
    coalesce_with_next(block);
    return block;
}

void *mymalloc(size_t size) {
    size = request_size(size);

    spin_lock(&heap_lock);
    malloc_count++;

    block_t *block = find_free_block(size);
    if (block == 0) {
        block = extend_heap(size);
    }

    if (block == 0) {
        spin_unlock(&heap_lock);
        return 0;
    }

    split_block(block, size);
    block->free = 0;

    spin_unlock(&heap_lock);
    return (char *)block + sizeof(*block);
}

void myfree(void *ptr) {
    if (ptr == 0) {
        return;
    }

    block_t *block = (block_t *)((char *)ptr - sizeof(block_t));

    spin_lock(&heap_lock);
    block->free = 1;
    coalesce_with_next(block);
    if (block->prev != 0) {
        coalesce_with_next(block->prev);
    }
    spin_unlock(&heap_lock);
}
