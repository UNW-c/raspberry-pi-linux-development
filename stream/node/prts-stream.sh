#!/usr/bin/env bash
# PRTS video stream on the node: V4L2 capture -> FFmpeg -> RTSP (mediamtx).
#
#   prts-stream start [options]   start mediamtx and the ffmpeg publisher
#   prts-stream stop              stop both
#   prts-stream restart           stop, then start
#   prts-stream status            processes, ports, and what the RTSP stream carries
#   prts-stream logs              tail both logs
#
# Options for start:
#   --bitrate 4M        H.264 target bitrate (default 4M)
#   --copy              publish the camera's native MJPG without re-encoding
#                       (no quality loss, but about 1.6 MB/s - too much for wifi)
#   --size 640x480      capture size, the camera offers 640x480 or 1280x720
#   --fps 30            capture frame rate
#   --device /dev/video0
set -uo pipefail

BIN_DIR="$HOME/.local/bin"
RUN_DIR="/tmp/prts-stream"
CONFIG="$RUN_DIR/mediamtx.yml"
RTSP_PORT=8554
HLS_PORT=8888
RTSP_URL="rtsp://127.0.0.1:$RTSP_PORT/prts"

DEVICE="/dev/video0"
SIZE="640x480"
FPS=30
BITRATE="4M"
MODE="h264"

COMMAND="${1:-start}"
[ $# -gt 0 ] && shift

while [ $# -gt 0 ]; do
    case "$1" in
        --bitrate) BITRATE="${2:-4M}"; shift 2 ;;
        --copy) MODE="copy"; shift ;;
        --size) SIZE="${2:-640x480}"; shift 2 ;;
        --fps) FPS="${2:-30}"; shift 2 ;;
        --device) DEVICE="${2:-/dev/video0}"; shift 2 ;;
        *) echo "prts-stream: unknown option '$1'" >&2; exit 2 ;;
    esac
done

usage() {
    sed -n '2,20p' "$0" | sed 's/^# \{0,1\}//'
}

is_running() { pgrep -x "$1" >/dev/null 2>&1; }

require() {
    local missing=0
    [ -x "$BIN_DIR/mediamtx" ] || { echo "  missing: $BIN_DIR/mediamtx (run install_stream.sh)"; missing=1; }
    command -v ffmpeg >/dev/null 2>&1 || { echo "  missing: ffmpeg (apt install ffmpeg)"; missing=1; }
    return $missing
}

write_config() {
    mkdir -p "$RUN_DIR"
    cat > "$CONFIG" <<EOF
logLevel: info
rtspAddress: 127.0.0.1:$RTSP_PORT
hlsAddress: 127.0.0.1:$HLS_PORT
hlsVariant: lowLatency
hlsSegmentDuration: 1s
hlsPartDuration: 200ms
webrtcAddress: 127.0.0.1:8889
api: no
paths:
  all_others:
EOF
}

start_publisher() {
    if is_running ffmpeg; then
        echo "  ffmpeg: already publishing"
        return 0
    fi

    local encoder_args
    if [ "$MODE" = "copy" ]; then
        encoder_args=(-c:v copy)
    else
        encoder_args=(-c:v h264_v4l2m2m -pix_fmt yuv420p -b:v "$BITRATE" -g 60)
    fi

    nohup ffmpeg -hide_banner -loglevel warning \
        -f v4l2 -input_format mjpeg -video_size "$SIZE" -framerate "$FPS" -i "$DEVICE" \
        "${encoder_args[@]}" \
        -f rtsp -rtsp_transport tcp "$RTSP_URL" \
        >"$RUN_DIR/publisher.log" 2>&1 &
    echo "  publisher: $MODE $SIZE@$FPS${BITRATE:+ bitrate $BITRATE} -> $RTSP_URL"
}

do_start() {
    require || exit 1
    if pgrep -x prts-camera >/dev/null 2>&1; then
        echo "  stopping prts-camera first (it holds $DEVICE)"
        pkill -x prts-camera
        sleep 2
    fi

    write_config
    mkdir -p "$RUN_DIR"

    if ! is_running mediamtx; then
        # mediamtx wants its working directory to hold the config.
        ( cd "$RUN_DIR" && nohup "$BIN_DIR/mediamtx" "$CONFIG" >"$RUN_DIR/mediamtx.log" 2>&1 & )
        sleep 1.5
    fi
    if ! is_running mediamtx; then
        echo "  mediamtx failed to start, log:"
        tail -n 5 "$RUN_DIR/mediamtx.log" 2>/dev/null | sed 's/^/    /'
        exit 1
    fi
    echo "  mediamtx: rtsp 127.0.0.1:$RTSP_PORT, hls 127.0.0.1:$HLS_PORT"

    start_publisher || exit 1
    sleep 2

    echo
    local probe
    probe=$(ffprobe -v error -select_streams v:0 \
        -show_entries stream=codec_name,width,height,avg_frame_rate,bit_rate \
        -of default=nw=1 "$RTSP_URL" 2>&1)
    if echo "$probe" | grep -q codec_name; then
        echo "$probe" | sed 's/^/  /'
        echo
        echo "  desktop: http://127.0.0.1:$HLS_PORT/prts   (open the tunnel first)"
    else
        echo "  stream not readable yet: $(echo "$probe" | head -2 | tr '\n' ' ')"
        echo "  check: prts-stream logs"
    fi
}

do_stop() {
    local name
    for name in ffmpeg mediamtx; do
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
    for name in mediamtx ffmpeg; do
        if is_running "$name"; then
            echo "  $name: running (pid $(pgrep -x "$name" | tr '\n' ' '))"
        else
            echo "  $name: stopped"
        fi
    done

    echo "--- stream ---"
    ffprobe -v error -select_streams v:0 \
        -show_entries stream=codec_name,width,height,avg_frame_rate,bit_rate \
        -of default=nw=1 "$RTSP_URL" 2>&1 | sed 's/^/  /'

    echo "--- ports ---"
    ss -lntp 2>/dev/null | grep -E ":$RTSP_PORT|:$HLS_PORT" | sed 's/^/  /'

    echo "--- camera ---"
    lsusb 2>/dev/null | grep -i -e camera -e webcam | sed 's/^/  /'
    ls -l "$DEVICE" 2>/dev/null | sed 's/^/  /'
}

do_logs() {
    echo "--- mediamtx ---"
    tail -n 12 "$RUN_DIR/mediamtx.log" 2>/dev/null || echo "  (empty)"
    echo "--- publisher ---"
    tail -n 12 "$RUN_DIR/publisher.log" 2>/dev/null || echo "  (empty)"
}

case "$COMMAND" in
    start) do_start ;;
    stop) do_stop ;;
    restart) do_stop; sleep 1; echo; do_start ;;
    status) do_status ;;
    logs) do_logs ;;
    -h|--help|help) usage ;;
    *) echo "prts-stream: unknown command '$COMMAND'" >&2; usage >&2; exit 2 ;;
esac
