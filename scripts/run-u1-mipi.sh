#!/bin/bash

DIR=$(dirname "$0")
BINDIR="$DIR/usr/local/bin"
HTMLDIR="$DIR/usr/share/stream-http/html"

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

"$BINDIR/capture-v4l2-raw-mpp" \
    --device /dev/video11 --format nv12 \
    --jpeg-quality 7 \
    --jpeg-sock /tmp/capture-mipi-jpeg.sock \
    --mjpeg-sock /tmp/capture-mipi-mjpeg.sock \
    --h264-sock /tmp/capture-mipi-h264.sock &
PIDS="$!"

$BINDIR/control-v4l2.py \
    --device /dev/video11 \
    --sock /tmp/control-mipi.sock \
    --state-file /tmp/control-mipi.json &
PIDS="$PIDS $!"

$RUNCMD -c "$BINDIR/stream-webrtc \
    --h264-sock /tmp/capture-mipi-h264.sock \
    --webrtc-sock /tmp/capture-mipi-webrtc.sock" &
PIDS="$PIDS $!"

$RUNCMD -c "$BINDIR/stream-http.py \
    --html-dir $HTMLDIR \
    --jpeg-sock /tmp/capture-mipi-jpeg.sock \
    --mjpeg-sock /tmp/capture-mipi-mjpeg.sock \
    --h264-sock /tmp/capture-mipi-h264.sock \
    --webrtc-sock /tmp/capture-mipi-webrtc.sock \
    --control-sock /tmp/control-mipi.sock" &
PIDS="$PIDS $!"

$RUNCMD -c "$BINDIR/stream-rtsp \
    --h264-sock /tmp/capture-mipi-h264.sock \
    --rtsp-port 8554" &
PIDS="$PIDS $!"

wait -n
echo "One of the processes has exited, cleaning up..."
