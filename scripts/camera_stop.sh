#!/usr/bin/env bash
# Stops the camera publisher and the web viewer on the node.
set -uo pipefail

for name in prts-camera prts-web; do
    if pkill -x "$name" 2>/dev/null; then
        echo "$name: stopped"
    else
        echo "$name: not running"
    fi
done
