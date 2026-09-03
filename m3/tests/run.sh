#!/bin/sh
set -eu

tmpdir=$(mktemp -d)
trap 'rm -rf "$tmpdir"' EXIT INT TERM

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

mkdir -p "$tmpdir/bin"

cat >"$tmpdir/bin/sample-command" <<'EOF_SAMPLE'
#!/bin/sh
exit 0
EOF_SAMPLE
chmod +x "$tmpdir/bin/sample-command"

cat >"$tmpdir/bin/strace" <<'EOF_STRACE'
#!/bin/sh
set -eu

out=
while [ "$#" -gt 0 ]; do
    case "$1" in
        -o)
            out=$2
            shift 2
            ;;
        --)
            shift
            break
            ;;
        *)
            shift
            ;;
    esac
done

case "${out:-}" in
    '')
        echo 'missing -o output path' >&2
        exit 88
        ;;
esac

target=${1:-}
case "$target" in
    */sample-command) ;;
    *)
        echo "unexpected target: $target" >&2
        exit 89
        ;;
esac

{
    printf 'read(3, "abc", 3) = 3 <0.020000>\n'
    printf 'read(3, "def", 3) = 3 <0.030000>\n'
    printf 'close(3) = 0 <0.040000>\n'
    printf 'write(1, "payload <999.000000>", 28) = 28 <0.009000>\n'
    printf 'mmap(NULL, 4096, PROT_READ, MAP_PRIVATE, 3, 0) = 0x1 <0.001000>\n'
    printf 'openat(AT_FDCWD, "/tmp/demo", O_RDONLY) = 3 <0.010000>\n'
    printf 'read(3, "", 1) = 0 <unfinished ...>\n'
    printf '<... read resumed> "", 1) = 0 <0.000000>\n'
    printf '+++ exited with 0 +++\n'
} >"$out"
EOF_STRACE
chmod +x "$tmpdir/bin/strace"

out_file="$tmpdir/out.bin"
PATH="$tmpdir/bin:$PATH" ./sperf sample-command >"$out_file"

clean_output=$(LC_ALL=C tr -d '\000' <"$out_file")
expected='read (45%)
close (36%)
openat (9%)
write (8%)
mmap (0%)'
assert_eq "$expected" "$clean_output" "top five syscall output should be sorted by accumulated time"

nul_count=$(od -An -v -t u1 "$out_file" | awk '{ for (i = 1; i <= NF; i++) if ($i == 0) c++ } END { print c + 0 }')
assert_eq 80 "$nul_count" "each report should end with 80 null bytes"

if ./sperf >/dev/null 2>&1; then
    fail "missing command should fail"
fi

if grep -E 'execv(p|pe)?\(|execl(p|e)?\(' sperf.c >/dev/null; then
    fail "implementation should avoid exec* helpers other than execve"
fi

printf 'all tests passed\n'
