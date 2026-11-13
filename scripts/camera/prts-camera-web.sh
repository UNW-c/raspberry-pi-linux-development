#!/usr/bin/env bash
# PRTS camera stack controller (runs on the node).
#
#   prts-camera-web                       start the publisher and the web page
#   prts-camera-web start [camera opts]   same, with options for prts-camera
#   prts-camera-web stop                  stop both processes
#   prts-camera-web restart               stop, wait for the device, start
#   prts-camera-web status                processes, web endpoint, broker, camera
#   prts-camera-web logs                  tail both logs
#
# Camera options are forwarded as-is, e.g.
#   prts-camera-web start --fps 20
#   prts-camera-web start --width 640 --height 480 --fps 15
set -uo pipefail

BIN_DIR="$HOME/.local/bin"
WEB_LOG="/tmp/prts-web.log"
CAMERA_LOG="/tmp/prts-camera.log"
WEB_PORT=8080
COMMAND="${1:-start}"
[ $# -gt 0 ] && shift
CAMERA_OPTIONS=("$@")

usage() {
    cat <<'USAGE'
prts-camera-web [start|stop|restart|status|logs] [camera options]

  start     start prts-web and prts-camera (default)
  stop      stop both processes
  restart   stop, then start
  status    show processes, web endpoint, broker and camera
  logs      tail /tmp/prts-web.log and /tmp/prts-camera.log

camera options (forwarded to prts-camera):
  --fps N               publish rate (default 15)
  --width N --height N  capture size the device supports (640x480 or 1280x720)
  --meta-every N        metadata cadence (default 30)
USAGE
}

is_running() {
    pgrep -x "$1" >/dev/null 2>&1
}

require_binaries() {
    local missing=0
    for name in prts-web prts-camera; do
        if [ ! -x "$BIN_DIR/$name" ]; then
            echo "  missing: $BIN_DIR/$name"
            missing=1
        fi
    done
    if [ "$missing" -ne 0 ]; then
        echo "  install first: bash scripts/deploy/deploy.sh (or sync_to_pi.ps1 from the desktop)"
        return 1
    fi
    return 0
}

web_status() {
    curl -s --max-time 2 "http://127.0.0.1:$WEB_PORT/status" 2>/dev/null
}

start_web() {
    if is_running prts-web; then
        echo "  prts-web: already running"
        return 0
    fi
    nohup "$BIN_DIR/prts-web" >"$WEB_LOG" 2>&1 &
    echo "  prts-web: started (log $WEB_LOG)"
}

# The V4L2 device needs a moment to be released after the previous process
# exits, otherwise the first open fails with EBUSY. Retry instead of giving up.
start_camera() {
    if is_running prts-camera; then
        echo "  prts-camera: already running"
        return 0
    fi
    local attempt
    for attempt in 1 2 3 4 5 6; do
        nohup "$BIN_DIR/prts-camera" "${CAMERA_OPTIONS[@]}" >"$CAMERA_LOG" 2>&1 &
        sleep 0.8
        if is_running prts-camera; then
            echo "  prts-camera: started (log $CAMERA_LOG)"
            return 0
        fi
        sleep 1
    done
    echo "  prts-camera: failed to start, last log line:"
    tail -n 3 "$CAMERA_LOG" 2>/dev/null | sed 's/^/    /'
    return 1
}

do_start() {
    require_binaries || exit 1
    echo "starting camera stack"
    start_web
    start_camera

    local attempt payload
    for attempt in 1 2 3 4 5 6 7 8 9 10; do
        payload="$(web_status)"
        [ -n "$payload" ] && break
        sleep 0.3
    done

    echo
    if [ -n "$payload" ]; then
        echo "  web: http://127.0.0.1:$WEB_PORT/"
        echo "  $payload"
    else
        echo "  web: not answering yet, check: prts-camera-web logs"
    fi
    echo
    echo "  desktop: run prts-cam on the PC, it forwards local 18080 to this port"
}

do_stop() {
    echo "stopping camera stack"
    local name
    for name in prts-camera prts-web; do
        if pkill -x "$name" 2>/dev/null; then
            echo "  $name: stopped"
        else
            echo "  $name: not running"
        fi
    done
}

do_status() {
    echo "--- processes ---"
    local name
    for name in prts-camera prts-web; do
        if is_running "$name"; then
            echo "  $name: running (pid $(pgrep -x "$name" | tr '\n' ' '))"
        else
            echo "  $name: stopped"
        fi
    done

    echo "--- web endpoint ---"
    local payload
    payload="$(web_status)"
    if [ -n "$payload" ]; then
        echo "  http://127.0.0.1:$WEB_PORT/ -> $payload"
    else
        echo "  http://127.0.0.1:$WEB_PORT/ -> no answer"
    fi

    echo "--- broker ---"
    echo "  mosquitto: $(systemctl is-active mosquitto 2>/dev/null || echo unknown)"

    echo "--- camera ---"
    if lsusb 2>/dev/null | grep -qi -e webcam -e camera; then
        lsusb 2>/dev/null | grep -i -e webcam -e camera | sed 's/^/  /'
    else
        echo "  no usb camera visible"
    fi
    local node
    for node in /dev/video0 /dev/video2; do
        [ -e "$node" ] || continue
        local card
        card=$(v4l2-ctl -d "$node" --info 2>/dev/null | sed -n 's/.*Card type *: //p')
        [ -n "$card" ] && echo "  $node: $card"
    done
}

do_logs() {
    echo "--- $WEB_LOG ---"
    tail -n 15 "$WEB_LOG" 2>/dev/null || echo "  (empty)"
    echo "--- $CAMERA_LOG ---"
    tail -n 15 "$CAMERA_LOG" 2>/dev/null || echo "  (empty)"
}

case "$COMMAND" in
    start) do_start ;;
    stop) do_stop ;;
    restart) do_stop; sleep 1; echo; do_start ;;
    status) do_status ;;
    logs) do_logs ;;
    -h|--help|help) usage ;;
    *)
        echo "prts-camera-web: unknown command '$COMMAND'" >&2
        usage >&2
        exit 2
        ;;
esac
