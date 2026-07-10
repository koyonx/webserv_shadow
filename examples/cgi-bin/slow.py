#!/usr/bin/env python3
import time
import signal
import sys

# Ignore SIGTERM so webserv has to escalate to SIGKILL.
signal.signal(signal.SIGTERM, signal.SIG_IGN)

print("Content-Type: text/plain")
print()
sys.stdout.flush()

# Hang forever.
while True:
    time.sleep(1)
