#!/usr/bin/env python3
import os
import sys

body = sys.stdin.read()
print("Content-Type: text/plain; charset=utf-8")
print("X-Cgi-Handler: hello.py")
print()
print("hello from webserv cgi")
print("method:", os.environ.get("REQUEST_METHOD", "?"))
print("query:", os.environ.get("QUERY_STRING", ""))
print("body-length:", len(body))
if body:
    print("body:", body)
print("script-name:", os.environ.get("SCRIPT_NAME", ""))
print("http-host:", os.environ.get("HTTP_HOST", ""))
