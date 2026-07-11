#!/usr/bin/env bash
# HTTP method / version / status / header basics on webserv, cross-checked with nginx.
set -u
cd "$(dirname "$0")/../.."
source tests/scripts/hdr.sh

# Grab just the status code
status() { curl -s -o /dev/null -w '%{http_code}' "$@"; }

# GET / -> 200
check_eq "GET /  status (webserv)" "200" "$(status $WEBSERV/)"
check_eq "GET /  status (nginx)"   "200" "$(status $NGINX/)"

# HEAD /
h_ws=$(curl -sI $WEBSERV/ )
h_ng=$(curl -sI $NGINX/ )
check_contains "HEAD /: 200 (ws)"  " 200 " "$h_ws"
check_contains "HEAD /: 200 (ng)"  " 200 " "$h_ng"
check_contains "HEAD /: Content-Length (ws)"  "Content-Length" "$h_ws"
check_contains "HEAD /: Content-Length (ng)"  "Content-Length" "$h_ng"

# HEAD should NOT return a body
body_bytes_ws=$(curl -sI $WEBSERV/ -o /dev/null -w '%{size_download}')
check_eq "HEAD body bytes (ws)" "0" "$body_bytes_ws"

# 404
check_eq "GET /nope  (ws)" "404" "$(status $WEBSERV/no-such-file)"
check_eq "GET /nope  (ng)" "404" "$(status $NGINX/no-such-file)"

# Custom 404 body from error_page /404.html (webserv only — nginx serves /404.html too)
body_ws=$(curl -s $WEBSERV/nope 2>&1)
check_contains "custom 404 body (ws)" "404" "$body_ws"

# 405 with Allow header. /listing/ is GET-only for webserv (uses "allowed_methods GET").
resp=$(curl -si -X POST $WEBSERV/listing/ --data foo)
check_contains "POST /listing/ -> 405 (ws)" "405" "$resp"
check_contains "405 has Allow header (ws)"   "Allow:" "$resp"

# 501 for an unrecognized method that IS syntactically valid — webserv should reject.
# curl won't send arbitrary method names by default; use --request.
resp=$(printf 'FROB / HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n' | nc -q 1 127.0.0.1 18100)
check_contains "unknown method FROB (ws)" "501" "$resp"

# 400 for empty method
resp=$(printf ' / HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n' | nc -q 1 127.0.0.1 18100)
check_contains "empty method -> 400 (ws)" "400" "$resp"

# 400 for missing HTTP-version
resp=$(printf 'GET /\r\nHost: x\r\nConnection: close\r\n\r\n' | nc -q 1 127.0.0.1 18100)
check_contains "no HTTP version -> 400 (ws)" "400" "$resp"

# 505 for HTTP/2.0 (unsupported)
resp=$(printf 'GET / HTTP/2.0\r\nHost: x\r\nConnection: close\r\n\r\n' | nc -q 1 127.0.0.1 18100)
check_contains "HTTP/2.0 -> 505 (ws)" "505" "$resp"

# HTTP/1.0 no Host header should still work (only 1.1 requires Host)
resp=$(printf 'GET / HTTP/1.0\r\nConnection: close\r\n\r\n' | nc -q 1 127.0.0.1 18100)
check_contains "HTTP/1.0 no Host -> 200 (ws)" " 200 " "$resp"

# HTTP/1.1 without Host -> 400
resp=$(printf 'GET / HTTP/1.1\r\nConnection: close\r\n\r\n' | nc -q 1 127.0.0.1 18100)
check_contains "HTTP/1.1 no Host -> 400 (ws)" "400" "$resp"

# Duplicate Host header -> 400 (webserv rejects; nginx returns 400 too)
resp=$(printf 'GET / HTTP/1.1\r\nHost: a\r\nHost: b\r\nConnection: close\r\n\r\n' | nc -q 1 127.0.0.1 18100)
check_contains "duplicate Host -> 400 (ws)" "400" "$resp"

# Invalid HTTP-version characters
resp=$(printf 'GET / HTTP/1.X\r\nHost: x\r\nConnection: close\r\n\r\n' | nc -q 1 127.0.0.1 18100)
check_contains "HTTP/1.X -> 400 (ws)" "400" "$resp"

# Lower-case http/1.1 -> 400 (RFC requires uppercase)
resp=$(printf 'GET / http/1.1\r\nHost: x\r\nConnection: close\r\n\r\n' | nc -q 1 127.0.0.1 18100)
check_contains "lowercase http/1.1 -> 400 (ws)" "400" "$resp"

# Method case sensitivity: get vs GET
resp=$(printf 'get / HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n' | nc -q 1 127.0.0.1 18100)
# HTTP methods are case sensitive; "get" should be treated as unknown (501 or 400)
if printf '%s' "$resp" | grep -qE ' (400|405|501) '; then
  pass "lower-case 'get' -> rejected (ws): $(printf '%s' "$resp" | head -1)"
else
  fail "lower-case 'get' behavior: $(printf '%s' "$resp" | head -1)"
fi
resp_ng=$(printf 'get / HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n' | nc -q 1 127.0.0.1 18200)
log "  nginx says for 'get /': $(printf '%s' "$resp_ng" | head -1)"

# HEAD parity between ws and nginx: body length should match Content-Length?
hd_ws=$(curl -sI $WEBSERV/index.html)
hd_ng=$(curl -sI $NGINX/index.html)
cl_ws=$(printf '%s' "$hd_ws" | awk 'BEGIN{IGNORECASE=1}/^Content-Length/{gsub(/\r/,"");print $2}')
cl_ng=$(printf '%s' "$hd_ng" | awk 'BEGIN{IGNORECASE=1}/^Content-Length/{gsub(/\r/,"");print $2}')
check_eq "HEAD Content-Length parity" "$cl_ng" "$cl_ws"

# Server header present
check_contains "Server header present (ws)" "Server:" "$hd_ws"

# Date header present
check_contains "Date header present (ws)" "Date:" "$hd_ws"

# Reason phrases: 200 OK / 404 Not Found
resp=$(curl -sI $WEBSERV/no-such-file | head -1)
check_contains "404 Not Found reason" "Not Found" "$resp"
resp=$(curl -sI $WEBSERV/ | head -1)
check_contains "200 OK reason" "OK" "$resp"

# TRACE, CONNECT, PATCH, PUT, OPTIONS should be rejected. On webserv these go
# through parser -> Dispatcher; TRACE/CONNECT etc. should end up 405 or 501.
for meth in PUT PATCH TRACE CONNECT OPTIONS; do
  resp=$(printf '%s / HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n' "$meth" | nc -q 1 127.0.0.1 18100)
  if printf '%s' "$resp" | grep -qE ' (405|501) '; then
    pass "$meth -> rejected (ws): $(printf '%s' "$resp" | head -1)"
  else
    fail "$meth acceptance: $(printf '%s' "$resp" | head -1)"
  fi
done

# GET with query string still resolves
check_eq "GET /?x=1 -> 200 (ws)" "200" "$(status "$WEBSERV/?x=1&y=%2Fa%2Fb")"

summary
