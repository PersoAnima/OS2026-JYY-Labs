#!/bin/sh
set -eu

fail() {
    printf 'FAIL: %s\n' "$1" >&2
    exit 1
}

sample_args='31373 612 338 635 281 4998 3715 351 2506'
out4=$(./gpt $sample_args)
out1=$(GPT_WORKERS=1 ./gpt $sample_args)

case "$out4" in
    852) ;;
    *) fail "course sample should generate token 852" ;;
esac

if [ "$out4" != "$out1" ]; then
    printf '4-worker output: %s\n1-worker output: %s\n' "$out4" "$out1" >&2
    fail "parallel and serial outputs should match"
fi

lines=$(printf '%s\n' "$out4" | wc -l | tr -d ' ')
if [ "$lines" != 1 ]; then
    fail "sample with 9 input tokens should generate exactly 1 token"
fi

if ./gpt not-a-token >/dev/null 2>&1; then
    fail "invalid token should fail"
fi

grep -q '#include "thread.h"' gpt.c || fail "gpt.c should use classroom thread.h"
grep -q '#include "thread-sync.h"' gpt.c || fail "gpt.c should include classroom thread-sync.h"
grep -q 'spawn(range_worker)' gpt.c || fail "parallel worker should be created through spawn()"
if grep -q 'pthread_create' gpt.c; then
    fail "gpt.c should use classroom spawn() wrapper instead of direct pthread_create"
fi

printf 'all tests passed\n'
