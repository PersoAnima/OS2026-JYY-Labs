#!/bin/sh
set -eu

fail() {
    printf 'FAIL: %s\n' "$1" >&2
    exit 1
}

if ! command -v curl >/dev/null 2>&1; then
    fail "curl is required for httpd tests"
fi

chmod +x cgi-bin/echo cgi-bin/status cgi-bin/fail

port=18087
log=$(mktemp)
body=$(mktemp)
trap 'kill ${server_pid:-0} >/dev/null 2>&1 || true; rm -f "$log" "$body"' EXIT INT TERM

./httpd "$port" >"$log" 2>&1 &
server_pid=$!

ready=0
for _ in 1 2 3 4 5 6 7 8 9 10; do
    if curl --noproxy '*' -fsS "http://127.0.0.1:$port/cgi-bin/echo?warmup=1" >/dev/null 2>&1; then
        ready=1
        break
    fi
    sleep 0.2
done
[ "$ready" = 1 ] || fail "server did not start"

curl --noproxy '*' -fsS "http://127.0.0.1:$port/cgi-bin/echo?x=7&y=9" >"$body"
grep -q '^method=GET$' "$body" || fail "REQUEST_METHOD should reach CGI"
grep -q '^query=x=7&y=9$' "$body" || fail "QUERY_STRING should reach CGI"

code=$(curl --noproxy '*' -sS -o "$body" -w '%{http_code}' "http://127.0.0.1:$port/cgi-bin/status")
[ "$code" = 403 ] || fail "CGI Status header should set HTTP status"

code=$(curl --noproxy '*' -sS -o "$body" -w '%{http_code}' "http://127.0.0.1:$port/cgi-bin/missing")
[ "$code" = 404 ] || fail "missing CGI should return 404"

code=$(curl --noproxy '*' -sS -o "$body" -w '%{http_code}' "http://127.0.0.1:$port/cgi-bin/fail")
[ "$code" = 500 ] || fail "failing CGI should return 500"

sleep 0.2
grep -q '\[GET\] \[/cgi-bin/echo\] \[200\]' "$log" || fail "log should contain echo 200"
grep -q '\[GET\] \[/cgi-bin/status\] \[403\]' "$log" || fail "log should contain status 403"
grep -q '\[GET\] \[/cgi-bin/missing\] \[404\]' "$log" || fail "log should contain missing 404"
grep -q '\[GET\] \[/cgi-bin/fail\] \[500\]' "$log" || fail "log should contain fail 500"

printf 'all tests passed\n'
