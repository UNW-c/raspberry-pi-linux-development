#!/usr/bin/env bash
# Measures the V4L2 + FFmpeg path on the node before any network is involved.
#
#   bench_capture.sh            # copy (no re-encode) plus H.264 at 2/4/6 Mbps
#
# The numbers decide what the RTSP stage can push: the wifi link to the desktop
# carries about 900 KiB/s, so anything above that is unusable regardless of how
# good it looks.
set -uo pipefail

DEVICE="${PRTS_CAMERA:-/dev/video0}"
SIZE="640x480"
SECONDS_PER_RUN=5
OUT="/tmp/prts-bench"

mkdir -p "$OUT"

if pgrep -x prts-camera >/dev/null 2>&1; then
    echo "stopping prts-camera so the device is free"
    pkill -x prts-camera
    sleep 2
fi

rate_of() {
    local bytes
    bytes=$(stat -c %s "$1")
    echo $(( bytes / SECONDS_PER_RUN / 1024 ))
}

echo "device $DEVICE ($SIZE)"

echo
echo "--- copy: native MJPG, no re-encode ---"
ffmpeg -hide_banner -loglevel quiet -f v4l2 -input_format mjpeg -video_size "$SIZE" \
    -framerate 30 -i "$DEVICE" -t "$SECONDS_PER_RUN" -c copy -y "$OUT/copy.mkv"
frames=$(ffprobe -v error -count_frames -select_streams v:0 \
    -show_entries stream=nb_read_frames -of csv=p=0 "$OUT/copy.mkv" 2>/dev/null)
echo "  $(rate_of "$OUT/copy.mkv") KiB/s   ${frames:-?} frames / ${SECONDS_PER_RUN}s"

echo
echo "--- H.264 hardware encoder (h264_v4l2m2m) ---"
for bitrate in 2000k 4000k 6000k; do
    target="$OUT/h264-$bitrate.mp4"
    rm -f "$target"
    ffmpeg -hide_banner -loglevel quiet -f v4l2 -input_format mjpeg -video_size "$SIZE" \
        -framerate 30 -i "$DEVICE" -t "$SECONDS_PER_RUN" \
        -c:v h264_v4l2m2m -pix_fmt yuv420p -b:v "$bitrate" -y "$target" &
    encoder=$!
    sleep 3
    cpu=$(ps -o pcpu= -p "$encoder" 2>/dev/null | tr -d ' ')
    wait "$encoder"
    echo "  $bitrate -> $(rate_of "$target") KiB/s   ffmpeg cpu ${cpu:-?}%"
done

echo
echo "files in $OUT"
