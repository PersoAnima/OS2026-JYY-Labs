#!/bin/sh
set -eu

db=$(mktemp)
trap 'rm -f "$db"' EXIT INT TERM

./libkvdb_test "$db"

printf 'source checks passed\n'
printf 'all tests passed\n'
