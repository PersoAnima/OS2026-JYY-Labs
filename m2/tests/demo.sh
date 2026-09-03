#!/bin/sh
set -eu

tmpdir=$(mktemp -d)
trap 'rm -rf "$tmpdir"' EXIT INT TERM

make_proc() {
    pid=$1
    name=$2
    ppid=$3
    mkdir -p "$tmpdir/$pid"
    cat >"$tmpdir/$pid/status" <<EOF_STATUS
Name:	$name
PPid:	$ppid
EOF_STATUS
}

make_proc 1 init 0
make_proc 12 login 1
make_proc 20 sshd 1
make_proc 31 bash 20
make_proc 42 vim 31

printf 'Fake /proc root: %s\n\n' "$tmpdir"
printf '$ PSTREE_PROC_ROOT=%s ./pstree\n' "$tmpdir"
PSTREE_PROC_ROOT="$tmpdir" ./pstree

printf '\n$ PSTREE_PROC_ROOT=%s ./pstree -p -n\n' "$tmpdir"
PSTREE_PROC_ROOT="$tmpdir" ./pstree -p -n
