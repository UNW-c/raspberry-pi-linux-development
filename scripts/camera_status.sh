#!/usr/bin/env bash
# Compact status report for the camera stack on the node.
set -uo pipefail

echo "--- processes ---"
for name in prts-camera prts-web; do
    if pgrep -x "$name" >/dev/null 2>&1; then
        echo "$name: running"
    else
        echo "$name: stopped"
    fi
done

echo "--- web endpoint ---"
rm -f /tmp/prts-status.json
code=$(curl -s -o /tmp/prts-status.json -w '%{http_code}' --max-time 3 \
    http://127.0.0.1:8080/status 2>/dev/null || true)
echo "GET /status -> ${code:-000}"
if [ "${code:-000}" = "200" ] && [ -s /tmp/prts-status.json ]; then
    cat /tmp/prts-status.json
    echo
fi

echo "--- broker ---"
systemctl is-active mosquitto 2>/dev/null || echo "unknown"

echo "--- camera device ---"
lsusb 2>/dev/null | grep -i -e webcam -e camera || echo "no usb camera visible"
for node in /dev/video0 /dev/video2; do
    [ -e "$node" ] || continue
    name=$(v4l2-ctl -d "$node" --info 2>/dev/null | sed -n 's/.*Card type *: //p')
    [ -n "$name" ] && echo "$node: $name"
done
