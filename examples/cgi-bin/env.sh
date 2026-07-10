#!/bin/bash
echo "Content-Type: text/plain; charset=utf-8"
echo "X-Cgi-Handler: env.sh"
echo ""
echo "hello from bash cgi"
echo "REQUEST_METHOD=$REQUEST_METHOD"
echo "QUERY_STRING=$QUERY_STRING"
echo "SCRIPT_NAME=$SCRIPT_NAME"
echo "HTTP_HOST=$HTTP_HOST"
