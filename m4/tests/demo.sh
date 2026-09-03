#!/bin/sh
set -eu

printf 'Demo input:\n\n'
cat <<'EOF_INPUT'
int gcd(int a, int b) { return b ? gcd(b, a % b) : a; }
gcd(256, 144) * gcd(56, 84)
int mix(int x) { return gcd(x, 30) + 7; }
mix(42)
bad (
EOF_INPUT

printf '\nDemo output:\n\n'
./crepl <<'EOF_INPUT'
int gcd(int a, int b) { return b ? gcd(b, a % b) : a; }
gcd(256, 144) * gcd(56, 84)
int mix(int x) { return gcd(x, 30) + 7; }
mix(42)
bad (
EOF_INPUT
