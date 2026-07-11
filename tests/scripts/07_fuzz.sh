#!/usr/bin/env bash
# Fuzz-style / malformed / adversarial requests. Server MUST NOT crash and MUST
# NOT expose files.  All failing paths should return 4xx/5xx *without* aborting.
set -u
cd "$(dirname "$0")/../.."
source tests/scripts/hdr.sh

send() { printf '%b' "$1" | nc -q 1 127.0.0.1 18100 2>/dev/null; }

status_of() { printf '%s' "$1" | head -1 | awk '{print $2}'; }

expect_status_class() {
  # expect_status_class label body_of_request expected_status_prefix
  r=$(send "$2")
  s=$(status_of "$r")
  if [ -z "$s" ]; then
    fail "$1: no response (server may have hung or crashed) req='$(printf '%s' "$2" | tr -d '\r' | head -c 80)'"
    return
  fi
  case "$s" in
    ${3}*) pass "$1: $s" ;;
    *)     fail "$1: got $s (expected ${3}xx)" ;;
  esac
}

# ---- Non-ASCII byte 0xff in method
expect_status_class "0xff in method"              "\xffGET / HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n"        "4"

# ---- Method containing space -> two tokens
expect_status_class "method with embedded tab"    "GE\tT / HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n"          "4"

# ---- Two spaces between method and URI
expect_status_class "double space in request-line" "GET  / HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n"          "4"

# ---- Trailing whitespace after HTTP-version
expect_status_class "trailing space after version" "GET / HTTP/1.1 \r\nHost: x\r\nConnection: close\r\n\r\n"          "4"

# ---- CR inside header name
expect_status_class "CR in header name"           "GET / HTTP/1.1\r\nHo\rst: x\r\nConnection: close\r\n\r\n"          "4"

# ---- NUL inside header value
expect_status_class "NUL in header value"         "GET / HTTP/1.1\r\nHost: x\x00y\r\nConnection: close\r\n\r\n"       "4"

# ---- LF inside header value
expect_status_class "LF in header value"          "GET / HTTP/1.1\r\nHost: x\ny\r\nConnection: close\r\n\r\n"         "4"

# ---- Header value starts with CR-LF (obs-fold)
expect_status_class "obs-fold header"             "GET / HTTP/1.1\r\nHost: x\r\n H\r\n\r\n"                            "4"

# ---- Invalid percent-encoding in URI
expect_status_class "bad %XX in URI"              "GET /%GG HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n"          "4"

# ---- Percent-encoded NUL in URI
expect_status_class "%00 in URI"                  "GET /%00 HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n"          "4"

# ---- Just spaces
expect_status_class "spaces only request"         "     \r\n\r\n"                                                       "4"

# ---- Just CRLFCRLF
r=$(send "\r\n\r\n")
if [ -z "$r" ]; then
  pass "empty request: server closed without echo"
else
  s=$(status_of "$r"); log "  empty request: $s"
fi

# ---- Very short request that does not include HTTP version
expect_status_class "GET / (no version)"          "GET /\r\n\r\n"                                                       "4"

# ---- Odd URI: %2E%2E%2Fetc%2Fpasswd (percent-encoded traversal)
r=$(send "GET /%2e%2e/etc/passwd HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n")
s=$(status_of "$r")
log "  %2e%2e traversal: $s"
if printf '%s' "$r" | grep -q "root:x:"; then
  fail "traversal leaked /etc/passwd"
else
  pass "no /etc/passwd leak (%2e%2e)"
fi

# ---- URI with tab
expect_status_class "tab in URI"                  "GET /a\tb HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n"         "4"

# ---- Content-Length: 5 but only 2 bytes body then EOF
r=$(printf 'POST /echo HTTP/1.1\r\nHost: x\r\nContent-Length: 5\r\nConnection: close\r\n\r\nab' | timeout 3 nc -q 3 127.0.0.1 18100)
s=$(status_of "$r")
log "  CL=5 body=2 EOF: $s (expect 400/408/nothing)"

# ---- Random binary garbage as request
r=$( (head -c 512 /dev/urandom; sleep 0.2) | nc -q 1 127.0.0.1 18100)
s=$(status_of "$r")
log "  random 512B garbage: $s"

# ---- Very large binary blob before any newline (should hit request-line cap)
r=$( (head -c 20000 /dev/urandom | tr '\0\r\n' 'x'; printf '\r\n') | nc -q 2 127.0.0.1 18100)
s=$(status_of "$r")
log "  20KB non-newline garbage: $s"
if [ -z "$s" ] || printf '%s' "$s" | grep -qE '^(4|5)'; then
  pass "20KB garbage handled cleanly"
else
  fail "20KB garbage: $s"
fi

# ---- Header injection via CR/LF in value
expect_status_class "CRLF injection in Cookie"    "GET / HTTP/1.1\r\nHost: x\r\nCookie: a\r\nSet-Cookie: bad\r\nConnection: close\r\n\r\n" "2"
# ^ note: the second "Cookie" line will parse as Set-Cookie header instead; we only
# check that the parser does not accept "\r\n" *inside* a single header value.

# ---- Chunked with negative size hex
expect_status_class "chunked with -1 size"        "POST /echo HTTP/1.1\r\nHost: x\r\nTransfer-Encoding: chunked\r\nConnection: close\r\n\r\n-1\r\n\r\n"  "4"

# ---- Chunked with extremely long chunk-size line
long_hex=$(printf '0%.0s' {1..8192})
expect_status_class "chunk size line >8KB"        "POST /echo HTTP/1.1\r\nHost: x\r\nTransfer-Encoding: chunked\r\nConnection: close\r\n\r\n${long_hex}\r\n\r\n"  "4"

# ---- 10x rapid connect+RST (no data)
for i in $(seq 1 30); do
  (exec 3<>/dev/tcp/127.0.0.1/18100; exec 3<&-; exec 3>&-) 2>/dev/null &
done
wait
sleep 0.2
if curl -s -o /dev/null -w '%{http_code}\n' $WEBSERV/ | grep -q 200; then
  pass "server alive after 30 abrupt connect+close"
else
  fail "server unresponsive after burst"
fi

# ---- After all fuzz, one more sanity check
check_eq "sanity GET after fuzz" "200" "$(curl -s -o /dev/null -w '%{http_code}' $WEBSERV/)"

# ---- Verify webserv process is still running
if pgrep -f 'webserv --serve' >/dev/null; then
  pass "webserv process still alive"
else
  fail "webserv PROCESS CRASHED during fuzz"
fi

summary
