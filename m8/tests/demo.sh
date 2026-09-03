#!/bin/sh
set -eu

cc -std=c17 -Wall -Wextra -Wpedantic -O2 -o tests/make_image tests/make_image.c
img=$(mktemp)
trap 'rm -f "$img" tests/make_image' EXIT INT TERM

tests/make_image "$img"
printf 'M8 demo: recover a BMP from a tiny FAT32-style image\n\n'
./fsrecov "$img"
