#!/usr/bin/env bash
# CGI tests. serve.conf declares:
#   location /cgi-bin/ { cgi_pass .py /usr/bin/python3;
#                        cgi_pass .sh /bin/bash; }
set -u
cd "$(dirname "$0")/../.."
source tests/scripts/hdr.sh

status() { curl -s -o /dev/null -w '%{http_code}' "$@"; }

# ---- hello.py GET
resp=$(curl -si "$WEBSERV/cgi-bin/hello.py")
check_contains "hello.py -> 200 (ws)" " 200 " "$resp"
check_contains "hello.py Content-Type from CGI" "text/plain" "$resp"
check_contains "hello.py custom header X-Cgi-Handler" "X-Cgi-Handler: hello.py" "$resp"
check_contains "hello.py env REQUEST_METHOD" "method: GET" "$resp"

# ---- hello.py GET with query string
resp=$(curl -si "$WEBSERV/cgi-bin/hello.py?a=1&b=%20&c=hello%2Fworld")
check_contains "query passes through" "query: a=1&b=%20&c=hello%2Fworld" "$resp"

# ---- hello.py POST body
resp=$(curl -si -X POST -H "Content-Type: text/plain" --data-binary 'THISISBODY' "$WEBSERV/cgi-bin/hello.py")
check_contains "hello.py POST body length" "body-length: 10" "$resp"
check_contains "hello.py POST body content" "body: THISISBODY" "$resp"

# ---- env.sh
resp=$(curl -si "$WEBSERV/cgi-bin/env.sh?FOO=BAR")
check_contains "env.sh -> 200 (ws)"       " 200 " "$resp"
check_contains "env.sh script name"       "SCRIPT_NAME=/cgi-bin/env.sh" "$resp"
check_contains "env.sh REQUEST_METHOD"    "REQUEST_METHOD=GET" "$resp"
check_contains "env.sh QUERY_STRING"      "QUERY_STRING=FOO=BAR" "$resp"

# ---- cookies.py — two Set-Cookie headers
resp=$(curl -si "$WEBSERV/cgi-bin/cookies.py" -H "Cookie: existing=yes")
nsc=$(printf '%s' "$resp" | tr -d '\r' | grep -c '^Set-Cookie:')
check_eq "cookies.py two Set-Cookie" "2" "$nsc"
check_contains "HTTP_COOKIE env" "existing=yes" "$resp"

# ---- redirect.py — Status: 302
resp=$(curl -si "$WEBSERV/cgi-bin/redirect.py")
h=$(printf '%s' "$resp" | head -1)
check_contains "redirect.py 302 status" "302" "$resp"
check_contains "redirect.py Location: /about.html" "/about.html" "$resp"

# ---- slow.py — expected to be killed after runtime cap (5s in code)
start=$(date +%s%N)
resp=$(curl -si --max-time 15 "$WEBSERV/cgi-bin/slow.py")
end=$(date +%s%N)
elapsed_ms=$(( (end - start) / 1000000 ))
h=$(printf '%s' "$resp" | head -1)
log "  slow.py returned after ${elapsed_ms}ms: $h"
if [ "$elapsed_ms" -lt 12000 ]; then
  pass "slow.py killed under 12s"
else
  fail "slow.py did NOT get killed (${elapsed_ms}ms)"
fi
if printf '%s' "$h" | grep -qE ' (502|504|500) '; then
  pass "slow.py returns error status: $h"
else
  fail "slow.py status: $h"
fi

# ---- CGI missing script (nonexistent .py) -> 404
resp=$(curl -si "$WEBSERV/cgi-bin/no_such.py")
check_contains "missing .py -> 404" "404" "$resp"

# ---- CGI executable dropped mid-request (permission bit off would be the way,
#      but here we just point at a broken interpreter path via wrong extension)
resp=$(curl -si "$WEBSERV/cgi-bin/hello.py.no")
# no cgi_pass for .no -> should serve static / 404
h=$(printf '%s' "$resp" | head -1)
log "  unknown extension /cgi-bin/hello.py.no: $h"

# ---- CGI process leak: fire N quick requests, then check ps for python3 children
for i in 1 2 3 4 5; do
  curl -s -o /dev/null "$WEBSERV/cgi-bin/hello.py?i=$i"
done
sleep 0.5
# Look for orphan children under webserv pid
webpid=$(pgrep -f 'webserv --serve' | head -1)
children=$(ps -o pid= --ppid "$webpid" 2>/dev/null | wc -l)
log "  webserv children after burst: $children"
if [ "$children" -lt 3 ]; then
  pass "CGI processes cleaned up"
else
  fail "possible CGI leak: $children children"
fi

# ---- CGI with body larger than pipe buffer to catch buffering issues
big=$(head -c 262144 /dev/urandom | base64)
resp=$(printf '%s' "$big" | curl -si -X POST --data-binary @- -H 'Content-Type: text/plain' "$WEBSERV/cgi-bin/hello.py")
h=$(printf '%s' "$resp" | head -1)
check_contains "large POST to CGI: 200" " 200 " "$resp"

# ---- CGI script with no Content-Type header should still work
# hello.py sets it, but we can't easily test malformed CGI output without editing scripts here.

# ---- PATH_INFO: /cgi-bin/hello.py/extra/path
resp=$(curl -si "$WEBSERV/cgi-bin/hello.py/extra/path")
h=$(printf '%s' "$resp" | head -1)
log "  PATH_INFO test /cgi-bin/hello.py/extra/path: $h"
# Some servers pass /extra/path as PATH_INFO. Webserv might return 404 if it insists
# on exact script mapping.

# ---- Very long query string
long=$(printf 'a=%.0s' {1..1000})b
resp=$(curl -si "$WEBSERV/cgi-bin/hello.py?$long")
h=$(printf '%s' "$resp" | head -1)
log "  long query CGI: $h"

# ---- Malformed CGI script (mkstemp)
cat >examples/cgi-bin/bad_output.py <<'EOF'
#!/usr/bin/env python3
# missing double CRLF between headers and body
import sys
sys.stdout.write("Just body no headers\n")
EOF
chmod +x examples/cgi-bin/bad_output.py
resp=$(curl -si "$WEBSERV/cgi-bin/bad_output.py")
h=$(printf '%s' "$resp" | head -1)
log "  malformed CGI output (missing CRLF): $h"
# Webserv should return 502 Bad Gateway or 500 Internal Server Error
if printf '%s' "$h" | grep -qE ' (500|502) '; then
  pass "malformed CGI -> $h"
else
  fail "malformed CGI: $h"
fi
rm -f examples/cgi-bin/bad_output.py

# ---- CGI script that exits non-zero
cat >examples/cgi-bin/exit_nonzero.py <<'EOF'
#!/usr/bin/env python3
import sys
print("Content-Type: text/plain\r")
print("\r")
print("about to exit 1")
sys.exit(1)
EOF
chmod +x examples/cgi-bin/exit_nonzero.py
resp=$(curl -si "$WEBSERV/cgi-bin/exit_nonzero.py")
h=$(printf '%s' "$resp" | head -1)
log "  CGI exit(1): $h"
rm -f examples/cgi-bin/exit_nonzero.py

# ---- CGI script that writes a lot of stderr (should be ignored)
cat >examples/cgi-bin/noisy_stderr.py <<'EOF'
#!/usr/bin/env python3
import sys
sys.stderr.write("NOISE " * 4096)
print("Content-Type: text/plain\r")
print("\r")
print("body ok")
EOF
chmod +x examples/cgi-bin/noisy_stderr.py
resp=$(curl -si "$WEBSERV/cgi-bin/noisy_stderr.py")
h=$(printf '%s' "$resp" | head -1)
check_contains "noisy stderr still OK" " 200 " "$resp"
check_contains "noisy stderr body reached" "body ok" "$resp"
rm -f examples/cgi-bin/noisy_stderr.py

# ---- Method-not-allowed for CGI when GET stripped from cgi-bin?
# serve.conf has "allowed_methods GET POST" for /cgi-bin/, so DELETE should 405.
resp=$(curl -si -X DELETE "$WEBSERV/cgi-bin/hello.py")
check_contains "DELETE on CGI -> 405" "405" "$resp"

summary
