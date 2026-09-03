#!/bin/sh
set -eu

fail() {
    printf 'FAIL: %s\n' "$1" >&2
    exit 1
}

assert_eq() {
    expected=$1
    actual=$2
    message=$3
    if [ "$expected" != "$actual" ]; then
        printf 'FAIL: %s\nexpected:\n%s\nactual:\n%s\n' \
            "$message" "$expected" "$actual" >&2
        exit 1
    fi
}

out=$(mktemp)
trap 'rm -f "$out"' EXIT INT TERM

./crepl >"$out" <<'EOF_INPUT'
1 + 2
int sq(int x) { return x * x; }
sq(7) + 1
int twice_sq(int x) { return sq(x) + sq(x); }
twice_sq(3)
int fib(int n) { if (n <= 1) return 1; return fib(n - 1) + fib(n - 2); }
fib(5)
not valid (
2 * 21
EOF_INPUT

expected='= 3.
OK.
= 50.
OK.
= 18.
OK.
= 8.
ERROR.
= 42.'

actual=$(cat "$out")
assert_eq "$expected" "$actual" "crepl should compile functions and evaluate expressions"

if grep -E '\bsystem[[:space:]]*\(|\bpopen[[:space:]]*\(' crepl.c >/dev/null; then
    fail "implementation must not use system() or popen()"
fi

printf 'all tests passed\n'
