#!/bin/sh
set -eu

./test-mymalloc

if grep -E '\b(malloc|calloc|realloc|free|pthread_create|pthread_join)[[:space:]]*\(' mymalloc.c >/dev/null; then
    printf 'FAIL: allocator core should not call libc allocation or pthread APIs\n' >&2
    exit 1
fi

size_out=$(size mymalloc.o 2>/dev/null || true)
if [ -n "$size_out" ]; then
    bytes=$(printf '%s\n' "$size_out" | awk 'NR == 2 { print $4 }')
    if [ "${bytes:-0}" -gt 1048576 ]; then
        printf 'FAIL: static object size is too large: %s\n' "$bytes" >&2
        exit 1
    fi
fi

printf 'source checks passed\n'
