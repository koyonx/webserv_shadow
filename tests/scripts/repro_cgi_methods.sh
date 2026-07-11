#!/usr/bin/env bash
# Confirm that /cgi-bin/ ignores allowed_methods
cd "$(dirname "$0")/../.."
echo "== effective allowed_methods for /cgi-bin/ per serve.conf: GET POST =="
for m in GET POST HEAD DELETE PUT PATCH TRACE OPTIONS FROB; do
    s=$(curl -s -o /dev/null -w '%{http_code}' -X "$m" http://127.0.0.1:18100/cgi-bin/hello.py)
    echo "  $m -> $s"
done
echo ""
echo "== body echoes REQUEST_METHOD (proving CGI was invoked) =="
echo "-- DELETE --"
curl -s -X DELETE http://127.0.0.1:18100/cgi-bin/hello.py | head -5
echo "-- PUT --"
curl -s -X PUT http://127.0.0.1:18100/cgi-bin/hello.py | head -5
