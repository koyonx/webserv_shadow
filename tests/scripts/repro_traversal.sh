#!/usr/bin/env bash
cd "$(dirname "$0")/../.."
rm -f examples/uploads/*
tf=$(mktemp)
echo 'malicious' > "$tf"
echo "== attempt: filename with ../../../evil.txt =="
curl -si -F "file=@$tf;filename=../../../evil.txt" http://127.0.0.1:18100/upload/ | head -5
echo "== attempt: filename /etc/wtf =="
curl -si -F "file=@$tf;filename=/etc/wtf" http://127.0.0.1:18100/upload/ | head -5
echo "== attempt: filename 'a/b/c/d/e/f.txt' =="
curl -si -F "file=@$tf;filename=a/b/c/d/e/f.txt" http://127.0.0.1:18100/upload/ | head -5
echo "== ls examples/uploads/ =="
ls -la examples/uploads/
echo "== ls / evil.txt? =="
ls /evil.txt /etc/wtf /etc/hostile.txt 2>&1 | head -5
rm -f "$tf"
