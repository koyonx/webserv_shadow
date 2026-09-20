#!/usr/bin/env python3
import os

print("Content-Type: text/plain; charset=utf-8")
print("Set-Cookie: sid=abc123; Path=/; HttpOnly")
print("Set-Cookie: pref=dark; Path=/; Max-Age=3600")
print()
print("HTTP_COOKIE:", os.environ.get("HTTP_COOKIE", ""))
