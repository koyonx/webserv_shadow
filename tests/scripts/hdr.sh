#!/usr/bin/env bash
# Tiny helpers used across test scripts.

PASS=0
FAIL=0
FAILURES=()

WEBSERV="http://127.0.0.1:18100"
NGINX="http://127.0.0.1:18200"

log()  { printf '[TEST ] %s\n' "$*"; }
pass() { printf '  PASS %s\n' "$*"; PASS=$((PASS+1)); }
fail() { printf '  FAIL %s\n' "$*"; FAIL=$((FAIL+1)); FAILURES+=("$*"); }

check_eq() {
  # check_eq "label" "expected" "actual"
  if [ "$2" = "$3" ]; then pass "$1 (=$2)"; else fail "$1: expected='$2' actual='$3'"; fi
}

check_contains() {
  # check_contains "label" "needle" "haystack"
  if printf '%s' "$3" | grep -qF -- "$2"; then pass "$1 (contains '$2')"; else fail "$1: '$2' not in output"; fi
}

check_not_contains() {
  if printf '%s' "$3" | grep -qF -- "$2"; then fail "$1: unexpected '$2' present"; else pass "$1 (no '$2')"; fi
}

summary() {
  printf '\n---\nPASSED: %d\nFAILED: %d\n' "$PASS" "$FAIL"
  if [ ${#FAILURES[@]} -gt 0 ]; then
    printf 'Failures:\n'
    for f in "${FAILURES[@]}"; do printf '  - %s\n' "$f"; done
  fi
  [ "$FAIL" -eq 0 ]
}
