#!/bin/sh
set -eu

if ! command -v curl >/dev/null 2>&1; then
    printf 'curl is required for demo\n' >&2
    exit 1
fi

chmod +x cgi-bin/echo cgi-bin/status cgi-bin/fail

port=18088
log=$(mktemp)
trap 'kill ${server_pid:-0} >/dev/null 2>&1 || true; rm -f "$log"' EXIT INT TERM

./httpd "$port" >"$log" 2>&1 &
server_pid=$!
sleep 0.4

printf 'M7 demo: CGI response\n\n'
curl --noproxy '*' -fsS "http://127.0.0.1:$port/cgi-bin/echo?name=os"

printf '\nM7 demo: ordered logs\n\n'
sleep 0.2
cat "$log"
