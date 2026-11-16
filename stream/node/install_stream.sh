#!/usr/bin/env bash
# Prepares the node for the RTSP stream: ffmpeg plus the mediamtx server.
#
#   install_stream.sh                 # check what is there, install what is missing
#   install_stream.sh --mediamtx FILE # install a mediamtx binary unpacked from FILE
#
# ffmpeg comes from apt. mediamtx is not in any apt repository and GitHub is not
# reachable from here, so its archive has to be brought over from the desktop
# first (see stream/README.md); without it only ffmpeg is installed.
set -uo pipefail

BIN_DIR="$HOME/.local/bin"
MEDIAMTX_ARCHIVE=""

while [ $# -gt 0 ]; do
    case "$1" in
        --mediamtx) MEDIAMTX_ARCHIVE="${2:-}"; shift 2 ;;
        -h|--help) sed -n '2,9p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "install_stream: unknown option '$1'" >&2; exit 2 ;;
    esac
done

if command -v ffmpeg >/dev/null 2>&1; then
    echo "  ffmpeg: $(ffmpeg -version 2>/dev/null | head -n 1)"
else
    echo "  ffmpeg: installing via apt ..."
    echo 123456 | sudo -S apt-get update -qq
    echo 123456 | sudo -S DEBIAN_FRONTEND=noninteractive apt-get install -y -qq ffmpeg
    if ! command -v ffmpeg >/dev/null 2>&1; then
        echo "  ffmpeg: install failed" >&2
        exit 1
    fi
    echo "  ffmpeg: $(ffmpeg -version 2>/dev/null | head -n 1)"
fi

install -d "$BIN_DIR"

if [ -n "$MEDIAMTX_ARCHIVE" ]; then
    if [ ! -f "$MEDIAMTX_ARCHIVE" ]; then
        echo "  mediamtx: $MEDIAMTX_ARCHIVE not found" >&2
        exit 2
    fi
    staging=$(mktemp -d)
    tar -xzf "$MEDIAMTX_ARCHIVE" -C "$staging"
    install -m 0755 "$staging/mediamtx" "$BIN_DIR/mediamtx"
    rm -rf "$staging"
fi

if [ -x "$BIN_DIR/mediamtx" ]; then
    echo "  mediamtx: $("$BIN_DIR/mediamtx" --version 2>/dev/null | head -n 1)"
else
    echo "  mediamtx: missing - copy mediamtx_v*_linux_arm64.tar.gz over and run:"
    echo "            bash install_stream.sh --mediamtx /tmp/mediamtx.tar.gz"
fi

install -m 0755 "$(dirname "$0")/prts-stream.sh" "$BIN_DIR/prts-stream"
echo "  prts-stream: $BIN_DIR/prts-stream"
