#!/usr/bin/env bash
# Connection semantics: keep-alive, chunked, timeouts, pipelining, slow-loris
set -u
cd "$(dirname "$0")/../.."
source tests/scripts/hdr.sh

# ---- HTTP/1.1 default keep-alive: two requests on same connection
resp=$(printf 'GET / HTTP/1.1\r\nHost: localhost\r\n\r\nGET /about.html HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n' | nc -q 1 127.0.0.1 18100)
first=$(printf '%s' "$resp" | grep -c 'HTTP/1.1 200')
check_eq "pipelined two requests -> two 200s" "2" "$first"

# ---- Connection: close respected on 1.1
resp=$(printf 'GET / HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n' | nc -q 1 127.0.0.1 18100)
if printf '%s' "$resp" | grep -qiE '^Connection: close'; then
  pass "server echoes Connection: close"
else
  # some servers omit the header when default is close
  log "  server did not echo Connection: close (may still close TCP)"
fi

# ---- HTTP/1.0 default is close
resp=$(printf 'GET / HTTP/1.0\r\n\r\n' | nc -q 1 127.0.0.1 18100)
check_contains "HTTP/1.0 -> 200" " 200 " "$resp"

# ---- HTTP/1.0 with Connection: keep-alive: should honor keep-alive for that response
resp=$(printf 'GET / HTTP/1.0\r\nHost: localhost\r\nConnection: keep-alive\r\n\r\n' | nc -q 1 127.0.0.1 18100)
h=$(printf '%s' "$resp" | head -1)
log "  HTTP/1.0 keep-alive: $h"
if printf '%s' "$resp" | grep -qi 'Connection: keep-alive'; then
  pass "HTTP/1.0 keep-alive honored"
else
  log "  HTTP/1.0 keep-alive: server did not opt in (implementation choice)"
fi

# ---- Header case-insensitivity
resp=$(printf 'GET / HTTP/1.1\r\nhOsT: localhost\r\nCoNnEcTiOn: close\r\n\r\n' | nc -q 1 127.0.0.1 18100)
check_contains "case-insensitive header names -> 200" " 200 " "$resp"

# ---- Trailing whitespace in header value trimmed
resp=$(printf 'GET / HTTP/1.1\r\nHost: localhost   \r\nConnection: close   \r\n\r\n' | nc -q 1 127.0.0.1 18100)
check_contains "trailing spaces in headers -> 200" " 200 " "$resp"

# ---- Missing final CRLF terminator (unterminated header block) — should timeout
resp=$(timeout 3 bash -c 'printf "GET / HTTP/1.1\r\nHost: localhost\r\n" | nc -q 3 127.0.0.1 18100')
h=$(printf '%s' "$resp" | head -1)
log "  incomplete request (single CRLF only): $h"

# ---- Slow-loris: send byte-per-100ms
resp=$( ( for c in G E T ' ' / ' ' H T T P / 1 . 1 $'\r' $'\n' H o s t : ' ' l o c a l h o s t $'\r' $'\n' $'\r' $'\n'; do
  printf '%s' "$c"; sleep 0.05
done ) | nc -q 1 127.0.0.1 18100 )
h=$(printf '%s' "$resp" | head -1)
log "  slow-loris headers (byte trickle): $h"
if printf '%s' "$h" | grep -qE ' 200 '; then
  pass "slow-loris headers still parsed"
else
  # Also acceptable: server closed with 408 or similar
  log "  slow-loris result: $h"
fi

# ---- Slow read: server should timeout if peer never reads
# Skipping active POLL-out timeout test (would require driving many bytes)

# ---- Pipelining ordering: three requests, three responses in order
resp=$(printf 'GET /about.html HTTP/1.1\r\nHost: localhost\r\n\r\nGET /style.css HTTP/1.1\r\nHost: localhost\r\n\r\nGET /data.json HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n' | nc -q 2 127.0.0.1 18100)
count=$(printf '%s' "$resp" | grep -c 'HTTP/1.1 200')
check_eq "pipelined 3 -> 3 responses" "3" "$count"
# Check ordering: about.html body first, then style.css, then data.json
if printf '%s' "$resp" | grep -q '<!doctype html>' && printf '%s' "$resp" | grep -q 'body {' && printf '%s' "$resp" | grep -q '"webserv":'; then
  pass "pipelined bodies all present"
else
  fail "pipelined body content check"
fi

# ---- Whitespace before colon in header (RFC forbids)
resp=$(printf 'GET / HTTP/1.1\r\nHost : localhost\r\nConnection: close\r\n\r\n' | nc -q 1 127.0.0.1 18100)
h=$(printf '%s' "$resp" | head -1)
log "  'Host : ' (space before colon): $h"
if printf '%s' "$h" | grep -q ' 400 '; then
  pass "space before colon -> 400"
else
  log "  space before colon: server tolerated"
fi

# ---- Very many headers
manyH=$(for i in $(seq 1 50); do printf 'X-Test-%d: value%d\r\n' "$i" "$i"; done)
resp=$(printf 'GET / HTTP/1.1\r\nHost: localhost\r\n%sConnection: close\r\n\r\n' "$manyH" | nc -q 1 127.0.0.1 18100)
check_contains "50 headers -> 200" " 200 " "$resp"

# ---- Way more headers -> 431
manyH=$(for i in $(seq 1 1000); do printf 'X-Test-%d: value%d\r\n' "$i" "$i"; done)
resp=$(printf 'GET / HTTP/1.1\r\nHost: localhost\r\n%sConnection: close\r\n\r\n' "$manyH" | nc -q 1 127.0.0.1 18100)
h=$(printf '%s' "$resp" | head -1)
if printf '%s' "$h" | grep -qE ' (431|400) '; then
  pass "1000 headers -> $h"
else
  fail "1000 headers: $h"
fi

# ---- Single very large header value
big=$(printf 'x%.0s' {1..16384})
resp=$(printf 'GET / HTTP/1.1\r\nHost: localhost\r\nX-Huge: %s\r\nConnection: close\r\n\r\n' "$big" | nc -q 1 127.0.0.1 18100)
h=$(printf '%s' "$resp" | head -1)
if printf '%s' "$h" | grep -qE ' (200|431|400) '; then
  pass "16KB header -> $h"
else
  fail "16KB header: $h"
fi

# ---- Empty header value
resp=$(printf 'GET / HTTP/1.1\r\nHost: localhost\r\nX-Empty:\r\nConnection: close\r\n\r\n' | nc -q 1 127.0.0.1 18100)
check_contains "empty header value -> 200" " 200 " "$resp"

# ---- Whitespace-only header value
resp=$(printf 'GET / HTTP/1.1\r\nHost: localhost\r\nX-Empty:    \r\nConnection: close\r\n\r\n' | nc -q 1 127.0.0.1 18100)
check_contains "whitespace-only header value -> 200" " 200 " "$resp"

# ---- Bare CR (no LF) in headers
resp=$(printf 'GET / HTTP/1.1\rHost: localhost\r\nConnection: close\r\n\r\n' | nc -q 1 127.0.0.1 18100)
h=$(printf '%s' "$resp" | head -1)
log "  bare CR in request-line: $h"

# ---- Bare LF (no CR)
resp=$(printf 'GET / HTTP/1.1\nHost: localhost\nConnection: close\n\n' | nc -q 1 127.0.0.1 18100)
h=$(printf '%s' "$resp" | head -1)
log "  LF-only line endings: $h"

# ---- Absolute-form URI
resp=$(printf 'GET http://localhost:18100/about.html HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n' | nc -q 1 127.0.0.1 18100)
check_contains "absolute-form URI -> 200" " 200 " "$resp"

# ---- Concurrent connections (10 at once)
tempdir=$(mktemp -d)
for i in $(seq 1 20); do
  (curl -s -o "$tempdir/r$i" -w '%{http_code}\n' $WEBSERV/about.html > "$tempdir/s$i") &
done
wait
codes=$(cat $tempdir/s* | sort -u)
if [ "$codes" = "200" ]; then
  pass "20 concurrent GETs all 200"
else
  fail "concurrent GETs: statuses = $codes"
fi
rm -rf "$tempdir"

summary
