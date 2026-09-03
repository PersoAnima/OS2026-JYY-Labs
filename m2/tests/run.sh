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

make_proc 1 init 0
make_proc 20 worker 1
make_proc 10 sshd 1
make_proc 30 shell 10
make_proc 40 orphan 999
mkdir -p "$tmpdir/not-a-pid"
mkdir -p "$tmpdir/50"
printf 'Name:\tbroken\n' >"$tmpdir/50/status"

output=$(PSTREE_PROC_ROOT="$tmpdir" ./pstree)
expected='init
  sshd
    shell
  worker
orphan'
assert_eq "$expected" "$output" "default output should be name-sorted"

output=$(PSTREE_PROC_ROOT="$tmpdir" ./pstree -n -p)
expected='init(1)
  sshd(10)
    shell(30)
  worker(20)
orphan(40)'
assert_eq "$expected" "$output" "-n -p should sort children by pid and show pids"

output=$(PSTREE_PROC_ROOT="$tmpdir" ./pstree --show-pids --numeric-sort)
assert_eq "$expected" "$output" "long options should match short options"

output=$(PSTREE_PROC_ROOT="$tmpdir" ./pstree -np)
assert_eq "$expected" "$output" "combined short options should work"

output=$(./pstree --version)
case "$output" in
    *pstree*) ;;
    *)
        printf 'FAIL: version output should contain pstree\n' >&2
        exit 1
        ;;
esac

if ./pstree --bad-option >/dev/null 2>&1; then
    printf 'FAIL: invalid option should fail\n' >&2
    exit 1
fi

if PSTREE_PROC_ROOT="$tmpdir/missing" ./pstree >/dev/null 2>&1; then
    printf 'FAIL: missing proc root should fail\n' >&2
    exit 1
fi

printf 'all tests passed\n'
