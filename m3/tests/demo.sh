#!/bin/sh
set -eu

tmpdir=$(mktemp -d)
trap 'rm -rf "$tmpdir"' EXIT INT TERM

mkdir -p "$tmpdir/bin"

cat >"$tmpdir/bin/demo-command" <<'EOF_DEMO_COMMAND'
#!/bin/sh
exit 0
EOF_DEMO_COMMAND
chmod +x "$tmpdir/bin/demo-command"

cat >"$tmpdir/bin/strace" <<'EOF_DEMO_STRACE'
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

{
    printf 'openat(AT_FDCWD, "/etc/hosts", O_RDONLY) = 3 <0.012000>\n'
    printf 'read(3, "127.0.0.1", 4096) = 9 <0.041000>\n'
    printf 'read(3, "", 4096) = 0 <0.028000>\n'
    printf 'close(3) = 0 <0.006000>\n'
    printf 'write(1, "done", 4) = 4 <0.003000>\n'
} >"$out"
EOF_DEMO_STRACE
chmod +x "$tmpdir/bin/strace"

printf 'Demo trace output after removing the invisible 80-byte separators:\n\n'
PATH="$tmpdir/bin:$PATH" ./sperf demo-command | LC_ALL=C tr -d '\000'
