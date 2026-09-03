#include "mymalloc.h"

#include <sys/mman.h>

void *vmalloc(void *addr, size_t length) {
    void *result = mmap(addr, length, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (result == MAP_FAILED) {
        return 0;
    }
    return result;
}

void vmfree(void *addr, size_t length) {
    munmap(addr, length);
}
