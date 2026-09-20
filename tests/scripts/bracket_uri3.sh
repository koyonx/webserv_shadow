#!/usr/bin/env bash
for len in 240 250 253 254 255 256 257 260 270 280 300; do
    long=$(python3 -c "print('x' * $len, end='')")
    s=$(printf 'GET /%s HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n' "$long" | nc -q 1 127.0.0.1 18100 | head -1 | tr -d '\r' | awk '{print $2}')
    echo "len=$len -> $s"
done
