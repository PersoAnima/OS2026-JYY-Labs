#include "mymalloc.h"

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define THREADS 4
#define ROUNDS 4000
#define SLOTS 64

static void fail(const char *msg) {
    fprintf(stderr, "FAIL: %s\n", msg);
    _Exit(1);
}

static void basic_test(void) {
    char *a = mymalloc(13);
    char *b = mymalloc(29);
    if (a == 0 || b == 0) {
        fail("mymalloc should return non-NULL for small allocations");
    }
    if (((uintptr_t)a & 7) != 0 || ((uintptr_t)b & 7) != 0) {
        fail("returned pointers should be 8-byte aligned");
    }
    if (a == b) {
        fail("live allocations should not overlap");
    }

    strcpy(a, "hello");
    strcpy(b, "allocator");
    if (strcmp(a, "hello") != 0 || strcmp(b, "allocator") != 0) {
        fail("payload should be writable");
    }

    myfree(a);
    char *c = mymalloc(8);
    if (c != a) {
        fail("freed small block should be reused");
    }
    myfree(c);
    myfree(b);
}

static void coalesce_test(void) {
    char *a = mymalloc(128);
    char *b = mymalloc(128);
    char *c = mymalloc(128);
    if (a == 0 || b == 0 || c == 0) {
        fail("coalesce setup allocation failed");
    }

    myfree(b);
    myfree(a);
    char *d = mymalloc(240);
    if (d != a) {
        fail("adjacent free blocks should coalesce");
    }

    myfree(d);
    myfree(c);
}

static void *worker(void *arg) {
    uintptr_t base = (uintptr_t)arg;
    void *slots[SLOTS] = {0};

    for (size_t i = 0; i < ROUNDS; i++) {
        size_t idx = (i * 17 + base) % SLOTS;
        if (slots[idx] != 0) {
            myfree(slots[idx]);
            slots[idx] = 0;
        }

        size_t bytes = ((i + base) % 257) + 1;
        unsigned char *p = mymalloc(bytes);
        if (p == 0 || ((uintptr_t)p & 7) != 0) {
            fail("concurrent allocation failed or was misaligned");
        }
        p[0] = (unsigned char)base;
        p[bytes - 1] = (unsigned char)(base + bytes);
        slots[idx] = p;
    }

    for (size_t i = 0; i < SLOTS; i++) {
        if (slots[i] != 0) {
            myfree(slots[i]);
        }
    }
    return 0;
}

static void concurrent_test(void) {
    pthread_t tids[THREADS];
    for (uintptr_t i = 0; i < THREADS; i++) {
        if (pthread_create(&tids[i], 0, worker, (void *)i) != 0) {
            fail("pthread_create failed");
        }
    }
    for (size_t i = 0; i < THREADS; i++) {
        if (pthread_join(tids[i], 0) != 0) {
            fail("pthread_join failed");
        }
    }
}

int main(void) {
    basic_test();
    coalesce_test();
    concurrent_test();
    puts("all tests passed");
    return 0;
}
