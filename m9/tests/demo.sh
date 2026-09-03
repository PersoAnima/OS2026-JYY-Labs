#!/bin/sh
set -eu

db=$(mktemp)
trap 'rm -f "$db"' EXIT INT TERM

printf 'M9 demo: append-only kvdb with reopen and concurrent process checks\n\n'
./libkvdb_test "$db"
