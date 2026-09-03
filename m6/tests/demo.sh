#!/bin/sh
set -eu

sample_args='31373 612 338 635 281 4998 3715 351 2506'

printf 'M6 demo: generate the next token from the real GPT-2 checkpoint\n\n'
printf '$ ./gpt %s\n' "$sample_args"
./gpt $sample_args

printf '\nSingle-worker check:\n'
GPT_WORKERS=1 ./gpt $sample_args
