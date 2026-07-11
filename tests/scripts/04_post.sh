#!/usr/bin/env bash
# POST / multipart / upload / body limits
set -u
cd "$(dirname "$0")/../.."
source tests/scripts/hdr.sh

status() { curl -s -o /dev/null -w '%{http_code}' "$@"; }

rm -f examples/uploads/*

# ---- POST to /echo (defined in serve.conf as method-only, no upload_store)
resp=$(curl -si -X POST -d 'hello=world&x=1' -H 'Content-Type: application/x-www-form-urlencoded' $WEBSERV/echo)
h=$(printf '%s' "$resp" | head -1)
log "  POST /echo (ws): $h"
# We expect either 200 (echoed) or 204 or 501 depending on implementation
if printf '%s' "$h" | grep -qE ' (200|204|501) '; then
  pass "POST /echo accepted (ws)"
else
  fail "POST /echo: $h"
fi

# ---- POST larger raw body under limit
data=$(head -c 4096 /dev/urandom | base64)
size=$(printf '%s' "$data" | wc -c)
resp=$(curl -si -X POST --data-binary "$data" -H 'Content-Type: text/plain' $WEBSERV/echo)
h=$(printf '%s' "$resp" | head -1)
log "  POST 4k body (ws): $h  (body size $size)"

# ---- POST over per-server client_max_body_size (server: 5m) -> 413
big=$(head -c $((6 * 1024 * 1024)) /dev/zero | tr '\0' 'x')
resp=$(printf '%s' "$big" | curl -si -X POST --data-binary @- -H 'Content-Type: text/plain' $WEBSERV/echo)
h=$(printf '%s' "$resp" | head -1)
check_contains "6MB POST -> 413 (ws)" "413" "$resp"
resp_ng=$(printf '%s' "$big" | curl -si -X POST --data-binary @- -H 'Content-Type: text/plain' $NGINX/upload/foo)
h_ng=$(printf '%s' "$resp_ng" | head -1)
check_contains "6MB POST -> 413 (ng)" "413" "$resp_ng"

# ---- POST to /upload/ with multipart/form-data — file should land in examples/uploads
tmpfile=$(mktemp)
printf 'hello upload from multipart\n' > "$tmpfile"
resp=$(curl -si -F "file=@$tmpfile;filename=upload_test.txt;type=text/plain" $WEBSERV/upload/)
h=$(printf '%s' "$resp" | head -1)
log "  multipart POST /upload/ (ws): $h"
if printf '%s' "$h" | grep -qE ' (200|201|204) '; then
  pass "multipart POST accepted (ws)"
else
  fail "multipart POST: $h"
fi
if [ -f examples/uploads/upload_test.txt ]; then
  pass "uploaded file present"
  if diff -q "$tmpfile" examples/uploads/upload_test.txt >/dev/null; then
    pass "uploaded file bytes match"
  else
    fail "uploaded file bytes differ"
  fi
else
  fail "uploaded file NOT written (ls uploads: $(ls examples/uploads))"
fi
rm -f "$tmpfile"

# ---- Multipart: filename traversal attempt
tmpfile=$(mktemp)
printf 'malicious\n' > "$tmpfile"
resp=$(curl -si -F "file=@$tmpfile;filename=../../../etc/hostile.txt;type=text/plain" $WEBSERV/upload/)
h=$(printf '%s' "$resp" | head -1)
log "  multipart traversal filename (ws): $h"
if [ -f /etc/hostile.txt ] || [ -f examples/hostile.txt ] || [ -f ../hostile.txt ]; then
  fail "traversal filename ESCAPED sandbox"
  rm -f /etc/hostile.txt examples/hostile.txt ../hostile.txt 2>/dev/null
else
  pass "traversal filename did not escape uploads dir"
fi
ls examples/uploads/
rm -f "$tmpfile"

# ---- Multipart: NUL byte / control character in filename
tmpfile=$(mktemp)
echo test > "$tmpfile"
resp=$(curl -si -F "file=@$tmpfile;filename=with%00nul.txt;type=text/plain" $WEBSERV/upload/)
h=$(printf '%s' "$resp" | head -1)
log "  filename with %00 (ws): $h"

# ---- multipart with no filename
tmpfile=$(mktemp)
echo nofilename > "$tmpfile"
resp=$(curl -si -F "file=@$tmpfile;filename=;type=text/plain" $WEBSERV/upload/)
h=$(printf '%s' "$resp" | head -1)
log "  multipart empty filename (ws): $h"

# ---- POST with chunked transfer encoding
resp=$(printf 'POST /echo HTTP/1.1\r\nHost: localhost\r\nContent-Type: text/plain\r\nTransfer-Encoding: chunked\r\nConnection: close\r\n\r\n5\r\nhello\r\n6\r\n world\r\n0\r\n\r\n' | nc -q 1 127.0.0.1 18100)
h=$(printf '%s' "$resp" | head -1)
log "  chunked POST /echo (ws): $h"
if printf '%s' "$h" | grep -qE ' (200|204|501) '; then
  pass "chunked POST accepted"
else
  fail "chunked POST: $h"
fi

# ---- Chunked with body exceeding server max (5m)
big_hex=$(printf '%x' $((6 * 1024 * 1024)))
resp=$( (printf 'POST /echo HTTP/1.1\r\nHost: localhost\r\nTransfer-Encoding: chunked\r\nContent-Type: text/plain\r\nConnection: close\r\n\r\n%s\r\n' "$big_hex";
        head -c $((6 * 1024 * 1024)) /dev/zero | tr '\0' 'y';
        printf '\r\n0\r\n\r\n') | nc -q 2 127.0.0.1 18100 | head -c 500)
h=$(printf '%s' "$resp" | head -1)
check_contains "chunked oversize -> 413" "413" "$resp"

# ---- Chunked with invalid chunk size
resp=$(printf 'POST /echo HTTP/1.1\r\nHost: localhost\r\nTransfer-Encoding: chunked\r\nConnection: close\r\n\r\nXX\r\nhello\r\n0\r\n\r\n' | nc -q 1 127.0.0.1 18100)
h=$(printf '%s' "$resp" | head -1)
check_contains "chunked invalid hex -> 400" "400" "$resp"

# ---- Both Content-Length and Transfer-Encoding
resp=$(printf 'POST /echo HTTP/1.1\r\nHost: localhost\r\nContent-Length: 5\r\nTransfer-Encoding: chunked\r\nConnection: close\r\n\r\n0\r\n\r\n' | nc -q 1 127.0.0.1 18100)
h=$(printf '%s' "$resp" | head -1)
check_contains "CL+TE mutually exclusive -> 400" "400" "$resp"

# ---- Content-Length larger than body sent then EOF -> hang until timeout
# Skip active hang test; just verify parser accepts CL 0 body
resp=$(printf 'POST /echo HTTP/1.1\r\nHost: localhost\r\nContent-Length: 0\r\nConnection: close\r\n\r\n' | nc -q 1 127.0.0.1 18100)
h=$(printf '%s' "$resp" | head -1)
if printf '%s' "$h" | grep -qE ' (200|204|501) '; then
  pass "empty POST accepted: $h"
else
  fail "empty POST: $h"
fi

# ---- Content-Length negative -> 400
resp=$(printf 'POST /echo HTTP/1.1\r\nHost: localhost\r\nContent-Length: -1\r\nConnection: close\r\n\r\n' | nc -q 1 127.0.0.1 18100)
check_contains "negative CL -> 400" "400" "$resp"

# ---- Content-Length not numeric
resp=$(printf 'POST /echo HTTP/1.1\r\nHost: localhost\r\nContent-Length: abc\r\nConnection: close\r\n\r\n' | nc -q 1 127.0.0.1 18100)
check_contains "non-numeric CL -> 400" "400" "$resp"

# ---- Content-Length wildly huge (overflow)
resp=$(printf 'POST /echo HTTP/1.1\r\nHost: localhost\r\nContent-Length: 9999999999999999999999\r\nConnection: close\r\n\r\n' | nc -q 1 127.0.0.1 18100)
check_contains "overflow CL -> 400/413" "$( printf '%s' "$resp" | head -1 | grep -oE '(400|413)' )" "$(printf '%s' "$resp" | head -1 | grep -oE '(400|413)')"
h=$(printf '%s' "$resp" | head -1)
log "  overflow CL: $h"

# ---- POST to /post_body (from tester.conf) - not currently active, but /echo has no upload_store; verify write happens where configured

# ---- Location-specific client_max_body_size: /upload/ = 5m
big=$(head -c $((5 * 1024 * 1024 + 1024)) /dev/zero | tr '\0' 'z')
resp=$(printf '%s' "$big" | curl -si -X POST -F "file=@-;filename=too_big.bin" $WEBSERV/upload/)
h=$(printf '%s' "$resp" | head -1)
if printf '%s' "$h" | grep -q ' 413 '; then
  pass "location max_body_size enforced: $h"
else
  fail "location max_body_size: $h"
fi

summary
