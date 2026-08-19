#!/bin/bash

DIR=$(dirname "$0")
BINDIR="$DIR/usr/local/bin"
HTMLDIR="$DIR/usr/share/stream-http/html"

CAMERA_INDEX=${CAMERA_INDEX:-0}
WIDTH=${WIDTH:-1920}
HEIGHT=${HEIGHT:-1080}
FPS=${FPS:-30}
H264_BITRATE=${H264_BITRATE:-2000}
JPEG_QUALITY=${JPEG_QUALITY:-90}
IDLE_MS=${IDLE_MS:-1000}
HTTP_PORT=${HTTP_PORT:-8080}
HTTP_BIND=${HTTP_BIND:-0.0.0.0}

JPEG_SOCK=${JPEG_SOCK:-/tmp/capture-rpi-jpeg.sock}
MJPEG_SOCK=${MJPEG_SOCK:-/tmp/capture-rpi-mjpeg.sock}
H264_SOCK=${H264_SOCK:-/tmp/capture-rpi-h264.sock}
CONTROL_SOCK=${CONTROL_SOCK:-/tmp/control-rpi.sock}
STATE_FILE=${STATE_FILE:-/tmp/control-rpi.json}

cleanup() {
    kill $PIDS 2>/dev/null
    exit 1
}

trap cleanup INT TERM EXIT
umask 0022

RUNCMD="bash"
if id -u lava &> /dev/null; then
    RUNCMD="su lava"
fi

"$BINDIR/capture-pycamera2.py" \
    --camera "$CAMERA_INDEX" \
    --width "$WIDTH" \
    --height "$HEIGHT" \
    --fps "$FPS" \
    --h264-bitrate "$H264_BITRATE" \
    --jpeg-quality "$JPEG_QUALITY" \
    --idle "$IDLE_MS" \
    --jpeg-sock "$JPEG_SOCK" \
    --mjpeg-sock "$MJPEG_SOCK" \
    --h264-sock "$H264_SOCK" \
    --control-sock "$CONTROL_SOCK" \
    --state-file "$STATE_FILE" &
PIDS="$!"

$RUNCMD -c "$BINDIR/stream-http.py \
    --html-dir $HTMLDIR \
    --port $HTTP_PORT \
    --bind $HTTP_BIND \
    --jpeg-sock $JPEG_SOCK \
    --mjpeg-sock $MJPEG_SOCK \
    --h264-sock $H264_SOCK \
    --control-sock $CONTROL_SOCK" &
PIDS="$PIDS $!"

wait -n
echo "One of the processes has exited, cleaning up..."
