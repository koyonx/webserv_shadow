#!/usr/bin/env bash
# Static / autoindex / index / redirects / error page tests
set -u
cd "$(dirname "$0")/../.."
source tests/scripts/hdr.sh

status() { curl -s -o /dev/null -w '%{http_code}' "$@"; }
raw()    { curl -si "$@"; }

# ---- index resolution
check_eq "GET / -> 200 index.html (ws)"    "200" "$(status $WEBSERV/)"
b_ws=$(curl -s $WEBSERV/)
b_ng=$(curl -s $NGINX/)
check_eq "GET / body parity"               "$b_ng" "$b_ws"

# ---- explicit file
check_eq "GET /about.html (ws)"            "200" "$(status $WEBSERV/about.html)"
check_eq "GET /about.html (ng)"            "200" "$(status $NGINX/about.html)"

# ---- content type sniffing (MIME)
hd=$(curl -sI $WEBSERV/style.css)
check_contains "MIME text/css (ws)" "text/css" "$hd"
hd=$(curl -sI $NGINX/style.css)
check_contains "MIME text/css (ng)" "text/css" "$hd"

hd=$(curl -sI $WEBSERV/data.json)
check_contains "MIME application/json (ws)" "application/json" "$hd"

# ---- 404 for missing file
check_eq "GET /missing (ws)"               "404" "$(status $WEBSERV/missing.html)"

# ---- Custom 404 page: webserv should serve /404.html body
b=$(curl -s $WEBSERV/nope)
if printf '%s' "$b" | grep -qF "$(cat examples/www/404.html)"; then
  pass "custom 404 page served (ws)"
else
  fail "custom 404 body mismatch"
fi

# ---- Autoindex when index missing
r=$(raw $WEBSERV/listing/)
check_contains "autoindex listing has 'a.txt' (ws)" "a.txt" "$r"
check_contains "autoindex listing has 'b.txt' (ws)" "b.txt" "$r"
check_contains "autoindex Content-Type html (ws)"   "text/html" "$r"

r_ng=$(raw $NGINX/listing/)
check_contains "autoindex listing (ng)"             "a.txt" "$r_ng"

# ---- Trailing-slash 301 redirect for directory URL missing slash
resp=$(curl -sI $WEBSERV/listing)
check_contains "listing without slash -> 301 (ws)"  "301" "$resp"
check_contains "listing without slash has Location (ws)" "Location:" "$resp"
resp_ng=$(curl -sI $NGINX/listing)
check_contains "listing without slash -> 301 (ng)"  "301" "$resp_ng"

# ---- return 302
resp=$(curl -sI $WEBSERV/redirect)
check_contains "return 302 status" "302" "$resp"
check_contains "return 302 Location" "/about.html" "$resp"

# ---- Path traversal .. should not escape root
# webserv should normalize .. and NOT reach /etc/passwd
resp=$(curl -si "$WEBSERV/../etc/passwd")
h_ws=$(printf '%s' "$resp" | head -1)
# curl normalizes .. client-side, so use raw socket
resp_raw=$(printf 'GET /../etc/passwd HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n' | nc -q 1 127.0.0.1 18100)
if printf '%s' "$resp_raw" | grep -qE ' (400|403|404) '; then
  pass "path traversal blocked (ws): $(printf '%s' "$resp_raw" | head -1)"
else
  fail "path traversal handling: $(printf '%s' "$resp_raw" | head -1)"
fi
# Should also NOT leak /etc/passwd contents
if printf '%s' "$resp_raw" | grep -q "root:x:"; then
  fail "path traversal leaked /etc/passwd contents!"
else
  pass "no /etc/passwd leaked (ws)"
fi

# ---- Percent-encoded traversal
resp_raw=$(printf 'GET /..%%2fetc/passwd HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n' | nc -q 1 127.0.0.1 18100)
if printf '%s' "$resp_raw" | grep -qE ' (400|403|404) '; then
  pass "%2f traversal blocked (ws): $(printf '%s' "$resp_raw" | head -1)"
else
  fail "%2f traversal: $(printf '%s' "$resp_raw" | head -1)"
fi

# ---- Percent-encoded space in filename works?
mkdir -p examples/www/space test 2>/dev/null
touch 'examples/www/has space.txt'
resp=$(curl -si "$WEBSERV/has%20space.txt")
check_contains "%20 in filename works (ws)" " 200 " "$resp"

# ---- request URL length (very long)
long=$(printf 'x%.0s' {1..8000})
resp=$(printf 'GET /%s HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n' "$long" | nc -q 1 127.0.0.1 18100)
h=$(printf '%s' "$resp" | head -1)
if printf '%s' "$h" | grep -qE ' (400|414|404) '; then
  pass "long URI handled (ws): $h"
else
  fail "long URI: $h"
fi

# ---- Range request (webserv likely does not implement) — should return full body
resp=$(curl -si -H 'Range: bytes=0-10' $WEBSERV/about.html)
h=$(printf '%s' "$resp" | head -1)
log "  Range request (ws): $h  (nginx would return 206 Partial Content)"

# ---- If-Modified-Since (probably ignored)
resp=$(curl -si -H "If-Modified-Since: Thu, 01 Jan 1970 00:00:00 GMT" $WEBSERV/about.html)
h=$(printf '%s' "$resp" | head -1)
log "  If-Modified-Since (ws): $h  (nginx returns 304 if unchanged)"

# ---- ETag header behavior
if curl -sI $WEBSERV/about.html | grep -q "ETag:"; then
  log "  webserv sends ETag"
else
  log "  webserv does NOT send ETag (nginx does)"
fi

# ---- Directory index fallthrough to autoindex when index absent
mkdir -p examples/www/only_dir
resp=$(curl -si $WEBSERV/only_dir/)
h=$(printf '%s' "$resp" | head -1)
# Since only_dir is under location / (autoindex off), no index -> 403 or 404
log "  directory with no index (ws): $h"

# ---- listing DELETE method
touch examples/www/files/willdelete.txt
mkdir -p examples/www/files
touch examples/www/files/willdelete.txt
resp=$(curl -si -X DELETE $WEBSERV/files/willdelete.txt)
h=$(printf '%s' "$resp" | head -1)
if printf '%s' "$h" | grep -qE ' (200|204) '; then
  pass "DELETE /files/willdelete.txt (ws): $h"
else
  fail "DELETE not accepted: $h"
fi
if [ ! -e examples/www/files/willdelete.txt ]; then
  pass "DELETE removed the file"
else
  fail "DELETE did not remove the file"
fi

# ---- DELETE on a directory — should be rejected
mkdir -p examples/www/files/subdir
resp=$(curl -si -X DELETE $WEBSERV/files/subdir/)
h=$(printf '%s' "$resp" | head -1)
if printf '%s' "$h" | grep -qE ' (403|405|409|500) '; then
  pass "DELETE dir rejected: $h"
else
  fail "DELETE dir behavior: $h"
fi

# ---- DELETE missing file
resp=$(curl -si -X DELETE $WEBSERV/files/does_not_exist.txt)
h=$(printf '%s' "$resp" | head -1)
if printf '%s' "$h" | grep -qE ' 404 '; then
  pass "DELETE missing -> 404: $h"
else
  fail "DELETE missing: $h"
fi

# ---- Cache-Control? (not implemented)
if curl -sI $WEBSERV/style.css | grep -qi "Cache-Control:"; then
  log "  Cache-Control present"
else
  log "  Cache-Control absent (webserv does not set it)"
fi

# ---- HEAD parity across many files
for f in about.html style.css data.json 50x.html; do
  ws=$(curl -sI $WEBSERV/$f | tr -d '\r' | grep -i '^Content-Length' | awk '{print $2}')
  ng=$(curl -sI $NGINX/$f   | tr -d '\r' | grep -i '^Content-Length' | awk '{print $2}')
  check_eq "Content-Length parity $f" "$ng" "$ws"
done

# ---- Content-Type header casing (webserv "; charset=utf-8" vs nginx no charset for image)
hd_ws=$(curl -sI $WEBSERV/index.html)
hd_ng=$(curl -sI $NGINX/index.html)
ct_ws=$(printf '%s' "$hd_ws" | tr -d '\r' | grep -i '^Content-Type' | awk '{print $2, $3}')
ct_ng=$(printf '%s' "$hd_ng" | tr -d '\r' | grep -i '^Content-Type' | awk '{print $2, $3}')
log "  index.html Content-Type ws='$ct_ws'  ng='$ct_ng'"

summary
