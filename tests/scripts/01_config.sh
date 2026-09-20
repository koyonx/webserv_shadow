#!/usr/bin/env bash
# Config parser / validator tests. We run the webserv binary in "validate-and-dump"
# mode (default argv[1] = config file) and check exit code + presence/absence of
# expected error messages.
set -u
cd "$(dirname "$0")/../.."
source tests/scripts/hdr.sh

BIN=./webserv
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

expect_ok() {
  # expect_ok label file
  out=$("$BIN" "$2" 2>&1); rc=$?
  if [ "$rc" = "0" ]; then pass "$1 [ok]"; else fail "$1: rc=$rc out=$out"; fi
}
expect_fail() {
  # expect_fail label file needle
  out=$("$BIN" "$2" 2>&1); rc=$?
  if [ "$rc" != "0" ] && printf '%s' "$out" | grep -q -- "$3"; then
    pass "$1 [fail as expected: $3]"
  else
    fail "$1: rc=$rc need='$3' out=$out"
  fi
}

# ---------- positive cases ----------

cat >"$TMP/ok_minimal.conf" <<'EOF'
server { listen 8080; location / { allowed_methods GET; } }
EOF
expect_ok "minimal server (bare)" "$TMP/ok_minimal.conf"

cat >"$TMP/ok_http_wrap.conf" <<'EOF'
http { server { listen 8080; location / { allowed_methods GET; } } }
EOF
expect_ok "http { server { ... } }" "$TMP/ok_http_wrap.conf"

expect_ok "provided serve.conf" examples/serve.conf
expect_ok "provided basic.conf" examples/basic.conf
expect_ok "provided tester.conf" examples/tester.conf

cat >"$TMP/ok_multiport.conf" <<'EOF'
server { listen 8080; listen 8081; location / { allowed_methods GET; } }
EOF
expect_ok "multiple listens" "$TMP/ok_multiport.conf"

cat >"$TMP/ok_multi_vhost.conf" <<'EOF'
server { listen 8080; server_name a.local; location / { allowed_methods GET; } }
server { listen 8080; server_name b.local; location / { allowed_methods GET; } }
EOF
expect_ok "same-port vhost split" "$TMP/ok_multi_vhost.conf"

cat >"$TMP/ok_nested_loc.conf" <<'EOF'
server {
    listen 8080;
    location /a {
        allowed_methods GET;
        location /a/b {
            allowed_methods POST;
        }
    }
}
EOF
expect_ok "nested location" "$TMP/ok_nested_loc.conf"

cat >"$TMP/ok_size_units.conf" <<'EOF'
http {
    client_max_body_size 4k;
    server {
        listen 8080;
        client_max_body_size 8m;
        location / {
            allowed_methods GET;
            client_max_body_size 16m;
        }
    }
}
EOF
expect_ok "k/m size suffixes" "$TMP/ok_size_units.conf"

cat >"$TMP/ok_multi_errors.conf" <<'EOF'
server {
    listen 8080;
    error_page 500 502 503 504 /50x.html;
    error_page 404 /404.html;
    location / { allowed_methods GET; }
}
EOF
expect_ok "multi-code error_page" "$TMP/ok_multi_errors.conf"

cat >"$TMP/ok_cgi_multi.conf" <<'EOF'
server {
    listen 8080;
    location /cgi-bin/ {
        allowed_methods GET POST;
        cgi_pass .py /usr/bin/python3;
        cgi_pass .sh /bin/bash;
    }
}
EOF
expect_ok "multi cgi_pass" "$TMP/ok_cgi_multi.conf"

cat >"$TMP/ok_return.conf" <<'EOF'
server {
    listen 8080;
    location /old { return 301 /new; }
    location /gone { return 410; }
}
EOF
expect_ok "return code (+URL and bare)" "$TMP/ok_return.conf"

cat >"$TMP/ok_comment.conf" <<'EOF'
# top-level comment
server {
    # inside server
    listen 8080; # trailing
    location / { allowed_methods GET; } # end
}
EOF
expect_ok "comments accepted" "$TMP/ok_comment.conf"

# ---------- negative cases ----------

cat >"$TMP/bad_missing_brace.conf" <<'EOF'
server { listen 8080; location / { allowed_methods GET;
EOF
expect_fail "unterminated block" "$TMP/bad_missing_brace.conf" "config error"

cat >"$TMP/bad_extra_close.conf" <<'EOF'
server { listen 8080; location / { allowed_methods GET; } } }
EOF
expect_fail "extra closing brace" "$TMP/bad_extra_close.conf" "config error"

cat >"$TMP/bad_missing_semi.conf" <<'EOF'
server { listen 8080 location / { allowed_methods GET; } }
EOF
expect_fail "missing semicolon" "$TMP/bad_missing_semi.conf" "config error"

cat >"$TMP/bad_unknown.conf" <<'EOF'
server { listen 8080; frobnicate on; location / { allowed_methods GET; } }
EOF
expect_fail "unknown directive" "$TMP/bad_unknown.conf" "unknown directive"

cat >"$TMP/bad_no_listen.conf" <<'EOF'
server { location / { allowed_methods GET; } }
EOF
expect_fail "server without listen" "$TMP/bad_no_listen.conf" "no 'listen'"

cat >"$TMP/bad_port_zero.conf" <<'EOF'
server { listen 0; location / { allowed_methods GET; } }
EOF
expect_fail "port zero" "$TMP/bad_port_zero.conf" "invalid listen port"

cat >"$TMP/bad_port_high.conf" <<'EOF'
server { listen 70000; location / { allowed_methods GET; } }
EOF
expect_fail "port > 65535" "$TMP/bad_port_high.conf" "invalid listen port"

cat >"$TMP/bad_dup_listen.conf" <<'EOF'
server { listen 8080; listen 8080; location / { allowed_methods GET; } }
EOF
expect_fail "duplicate listen" "$TMP/bad_dup_listen.conf" "duplicate 'listen"

cat >"$TMP/bad_dup_vhost.conf" <<'EOF'
server { listen 8080; server_name a.local; location / { allowed_methods GET; } }
server { listen 8080; server_name a.local; location / { allowed_methods GET; } }
EOF
expect_fail "duplicate server_name+listen" "$TMP/bad_dup_vhost.conf" "duplicate server_name"

cat >"$TMP/bad_method.conf" <<'EOF'
server { listen 8080; location / { allowed_methods GET PUT; } }
EOF
expect_fail "unsupported method PUT" "$TMP/bad_method.conf" "allowed_methods"

cat >"$TMP/bad_cgi_ext.conf" <<'EOF'
server { listen 8080; location / { cgi_pass py /usr/bin/python3; } }
EOF
expect_fail "cgi_pass ext missing dot" "$TMP/bad_cgi_ext.conf" "cgi_pass"

cat >"$TMP/bad_size.conf" <<'EOF'
server { listen 8080; client_max_body_size 5xyz; location / { allowed_methods GET; } }
EOF
expect_fail "invalid size unit" "$TMP/bad_size.conf" "invalid size"

cat >"$TMP/bad_error_code_low.conf" <<'EOF'
server { listen 8080; error_page 42 /oops.html; location / { allowed_methods GET; } }
EOF
expect_fail "error_page code < 100" "$TMP/bad_error_code_low.conf" "invalid integer"

cat >"$TMP/bad_error_code_high.conf" <<'EOF'
server { listen 8080; error_page 999 /oops.html; location / { allowed_methods GET; } }
EOF
expect_fail "error_page code > 599" "$TMP/bad_error_code_high.conf" "invalid integer"

cat >"$TMP/bad_multi_http.conf" <<'EOF'
http { server { listen 8080; location / { allowed_methods GET; } } }
http { server { listen 8081; location / { allowed_methods GET; } } }
EOF
expect_fail "multiple http blocks" "$TMP/bad_multi_http.conf" "multiple 'http'"

cat >"$TMP/bad_toplevel.conf" <<'EOF'
autoindex on;
server { listen 8080; location / { allowed_methods GET; } }
EOF
expect_fail "top-level directive" "$TMP/bad_toplevel.conf" "top-level directive"

cat >"$TMP/bad_no_server.conf" <<'EOF'
http { client_max_body_size 1m; }
EOF
expect_fail "no server block" "$TMP/bad_no_server.conf" "no 'server'"

cat >"$TMP/bad_timeout_in_loc.conf" <<'EOF'
server { listen 8080; location / { allowed_methods GET; keepalive_timeout 60; } }
EOF
expect_fail "timeout inside location" "$TMP/bad_timeout_in_loc.conf" "timeout directives are not allowed"

cat >"$TMP/bad_return_missing.conf" <<'EOF'
server { listen 8080; location / { return; } }
EOF
expect_fail "return with no code" "$TMP/bad_return_missing.conf" "expects"

# Missing config file
out=$("$BIN" /nonexistent/xyz.conf 2>&1); rc=$?
if [ "$rc" != "0" ] && printf '%s' "$out" | grep -q "No such file"; then
  pass "missing file [handled]"
else
  fail "missing file: rc=$rc out=$out"
fi

# Empty file
: >"$TMP/empty.conf"
expect_fail "empty file" "$TMP/empty.conf" "no 'server'"

# Random binary garbage
head -c 128 /dev/urandom >"$TMP/binary.conf"
out=$("$BIN" "$TMP/binary.conf" 2>&1); rc=$?
if [ "$rc" != "0" ]; then pass "binary garbage [rejected, rc=$rc]"; else fail "binary garbage: rc=$rc out=$out"; fi

summary
