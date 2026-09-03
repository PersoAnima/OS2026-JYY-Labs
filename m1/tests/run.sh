#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
TMPDIR=$(mktemp -d)
trap 'rm -rf "$TMPDIR"' EXIT INT TERM

LABYRINTH="$ROOT/labyrinth"

pass() {
    printf 'ok - %s\n' "$1"
}

fail() {
    printf 'not ok - %s\n' "$1" >&2
    exit 1
}

expect_status() {
    expected_status=$1
    test_name=$2
    shift 2

    set +e
    "$@" >"$TMPDIR/stdout" 2>"$TMPDIR/stderr"
    actual_status=$?
    set -e

    [ "$actual_status" -eq "$expected_status" ] || {
        cat "$TMPDIR/stdout" >&2
        cat "$TMPDIR/stderr" >&2
        fail "$test_name: expected exit $expected_status, got $actual_status"
    }
    pass "$test_name"
}

expect_stdout() {
    expected_stdout=$1
    test_name=$2
    shift 2

    expect_status 0 "$test_name" "$@"
    actual_stdout=$(cat "$TMPDIR/stdout")
    [ "$actual_stdout" = "$expected_stdout" ] || {
        printf 'expected:\n%s\nactual:\n%s\n' "$expected_stdout" "$actual_stdout" >&2
        fail "$test_name: unexpected stdout"
    }
}

cat >"$TMPDIR/print.map" <<'MAP'
###
#1.
###
MAP
expect_stdout "###
#1.
###" "print map verbatim" "$LABYRINTH" --map "$TMPDIR/print.map" --player 1

expect_stdout "Labyrinth Game" "version" "$LABYRINTH" --version
expect_status 1 "version rejects extra args" "$LABYRINTH" --version extra
expect_status 1 "unknown option" "$LABYRINTH" --no-such-option
expect_status 1 "missing map" "$LABYRINTH" --player 1
expect_status 1 "missing player" "$LABYRINTH" --map "$TMPDIR/print.map"
expect_status 1 "invalid player" "$LABYRINTH" --map "$TMPDIR/print.map" --player X

cat >"$TMPDIR/move.map" <<'MAP'
1.
..
MAP
expect_status 0 "move existing player" "$LABYRINTH" --map "$TMPDIR/move.map" --player 1 --move right
[ "$(cat "$TMPDIR/move.map")" = ".1
.." ] || fail "move existing player: map not persisted correctly"

cat >"$TMPDIR/spawn.map" <<'MAP'
..
##
MAP
expect_status 0 "spawn missing player then move" "$LABYRINTH" --map "$TMPDIR/spawn.map" --player 2 --move right
[ "$(cat "$TMPDIR/spawn.map")" = ".2
##" ] || fail "spawn missing player: map not persisted correctly"

cat >"$TMPDIR/wall.map" <<'MAP'
1#
..
MAP
before=$(cat "$TMPDIR/wall.map")
expect_status 1 "move into wall fails" "$LABYRINTH" --map "$TMPDIR/wall.map" --player 1 --move right
[ "$(cat "$TMPDIR/wall.map")" = "$before" ] || fail "failed move changed map"

cat >"$TMPDIR/occupied.map" <<'MAP'
12
..
MAP
expect_status 1 "move into player fails" "$LABYRINTH" --map "$TMPDIR/occupied.map" --player 1 --move right

cat >"$TMPDIR/ragged.map" <<'MAP'
...
..
MAP
expect_status 1 "ragged map rejected" "$LABYRINTH" --map "$TMPDIR/ragged.map" --player 1

cat >"$TMPDIR/disconnected.map" <<'MAP'
.#.
###
.#.
MAP
expect_status 1 "disconnected map rejected" "$LABYRINTH" --map "$TMPDIR/disconnected.map" --player 1

printf 'all m1 tests passed\n'
