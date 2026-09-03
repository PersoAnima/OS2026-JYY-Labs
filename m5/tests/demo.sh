#!/bin/sh
set -eu

printf 'M5 demo: run allocator correctness and concurrency checks\n\n'
./test-mymalloc
