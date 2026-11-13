#!/usr/bin/env bash
# Ensure the camera publisher and the web viewer are running on the node.
# Idempotent: already running processes are left alone.
set -uo pipefail

BIN_DIR="$HOME/.local/bin"
started=""

if ! pgrep -x prts-web >/dev/null 2>&1; then
    nohup "$BIN_DIR/prts-web" >/tmp/prts-web.log 2>&1 &
    started="$started web"
fi

if ! pgrep -x prts-camera >/dev/null 2>&1; then
    nohup "$BIN_DIR/prts-camera" >/tmp/prts-camera.log 2>&1 &
    started="$started camera"
fi

sleep 1.5

echo "stack: started${started:- nothing}"
for name in prts-web prts-camera; do
    if pgrep -x "$name" >/dev/null 2>&1; then
        echo "  $name: running"
    else
        echo "  $name: not running"
    fi
done

if command -v curl >/dev/null 2>&1; then
    curl -s -o /dev/null -w "  web: http=%{http_code}\n" http://127.0.0.1:8080/status || true
fi
