#!/usr/bin/env bash
for len in 10 50 100 150 200 250 300 400 500 700 900 1000; do
    long=$(printf 'x%.0s' $(seq 1 $len))
    s=$(printf 'GET /%s HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n' "$long" | nc -q 1 127.0.0.1 18100 | head -1 | tr -d '\r' | awk '{print $2}')
    echo "len=$len -> $s"
done
