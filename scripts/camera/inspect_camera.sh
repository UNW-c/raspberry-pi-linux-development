#!/usr/bin/env bash
# Reports what the kernel currently sees of cameras and video capture devices.
# Run on the Raspberry Pi: bash scripts/camera/inspect_camera.sh
set -uo pipefail

echo "=== USB devices ==="
lsusb

echo
echo "=== video4linux devices ==="
if command -v v4l2-ctl >/dev/null 2>&1; then
    v4l2-ctl --list-devices
else
    echo "v4l2-ctl not installed (sudo apt-get install v4l-utils)"
    ls -l /dev/video* 2>/dev/null || echo "no /dev/video* nodes"
fi

echo
echo "=== capture-capable nodes ==="
for node in /dev/video*; do
    [ -e "$node" ] || continue
    if v4l2-ctl -d "$node" --all 2>/dev/null | grep -q "Video Capture"; then
        echo "--- $node ---"
        v4l2-ctl -d "$node" --list-formats-ext 2>/dev/null | head -20
    fi
done

echo
echo "=== kernel messages (camera / uvc / unicam) ==="
dmesg 2>/dev/null | grep -iE "usb|camera|uvc|unicam|imx|ov[0-9]{4}" | tail -20 ||
    echo "dmesg needs root: run with sudo to see attach events"

echo
echo "=== configured device tree overlays ==="
grep -vE '^\s*(#|$)' /boot/firmware/config.txt 2>/dev/null | grep -iE "dtoverlay|camera" ||
    echo "no camera overlay configured"
