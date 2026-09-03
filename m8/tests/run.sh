#!/bin/sh
set -eu

cc -std=c17 -Wall -Wextra -Wpedantic -O2 -o tests/make_image tests/make_image.c
img=$(mktemp)
out=$(mktemp)
trap 'rm -f "$img" "$out" tests/make_image' EXIT INT TERM

tests/make_image "$img"
./fsrecov "$img" >"$out"

if ! grep -Eq '^[0-9a-f]{40}[[:space:]]+Art_0001\.bmp$' "$out"; then
    printf 'FAIL: expected recovered sha1 and filename\nactual:\n' >&2
    cat "$out" >&2
    exit 1
fi

printf 'all tests passed\n'
