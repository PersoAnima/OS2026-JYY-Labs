#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
TMPDIR=$(mktemp -d)
trap 'rm -rf "$TMPDIR"' EXIT INT TERM

LABYRINTH="$ROOT/labyrinth"
MAP="$TMPDIR/demo.map"

cat >"$MAP" <<'MAP'
#######
#1....#
#.###.#
#.....#
#######
MAP

render_map() {
    awk '
    BEGIN {
        wall = "\033[90m"
        player = "\033[32m"
        reset = "\033[0m"
    }
    {
        for (i = 1; i <= length($0); i++) {
            ch = substr($0, i, 1)
            if (ch == "#") {
                printf "%s#%s", wall, reset
            } else if (ch ~ /^[0-9]$/) {
                printf "%s%s%s", player, ch, reset
            } else {
                printf "."
            }
        }
        printf "\n"
    }' "$1"
}

show_state() {
    title=$1
    printf '\n== %s ==\n' "$title"
    render_map "$MAP"
}

run_move() {
    direction=$1
    printf '\n$ ./labyrinth --map demo.map --player 1 --move %s\n' "$direction"
    if "$LABYRINTH" --map "$MAP" --player 1 --move "$direction"; then
        printf 'result: success\n'
    else
        printf 'result: failed\n'
    fi
}

printf 'M1 labyrinth visual demo\n'
printf 'legend: # wall, . empty, green digit player\n'

show_state "initial map"
run_move right
show_state "after moving right"
run_move right
show_state "after moving right again"
run_move down
show_state "after blocked move down"

printf '\n== automatic checks ==\n'
"$ROOT/tests/run.sh"

