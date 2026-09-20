#!/usr/bin/env bash
# Find the URI length above which webserv starts responding 500
for len in 1000 2000 3000 4000 5000 6000 7000 8000 8100 8192 8300 8500 9000 10000 12000; do
    long=$(printf 'x%.0s' $(seq 1 $len))
    s=$(printf 'GET /%s HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n' "$long" | nc -q 1 127.0.0.1 18100 | head -1 | tr -d '\r' | awk '{print $2}')
    echo "len=$len -> $s"
done
