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

"$BINDIR/capture-v4l2-jpeg-mpp" \
    --device /dev/video18 \
    --jpeg-sock /tmp/capture-usb-jpeg.sock \
    --mjpeg-sock /tmp/capture-usb-mjpeg.sock \
    --h264-sock /tmp/capture-usb-h264.sock &
PIDS="$!"

$BINDIR/control-v4l2.py \
    --device /dev/video18 \
    --sock /tmp/control-usb.sock \
    --state-file /tmp/control-usb.json &
PIDS="$PIDS $!"

$RUNCMD -c "$BINDIR/stream-webrtc \
    --h264-sock /tmp/capture-usb-h264.sock \
    --webrtc-sock /tmp/capture-usb-webrtc.sock" &
PIDS="$PIDS $!"

$RUNCMD -c "$BINDIR/stream-http.py \
    --html-dir $HTMLDIR \
    --port 8081 \
    --jpeg-sock /tmp/capture-usb-jpeg.sock \
    --mjpeg-sock /tmp/capture-usb-mjpeg.sock \
    --h264-sock /tmp/capture-usb-h264.sock \
    --webrtc-sock /tmp/capture-usb-webrtc.sock \
    --detect-sock /tmp/control-usb.sock" &
PIDS="$PIDS $!"

$RUNCMD -c "$BINDIR/stream-rtsp \
    --h264-sock /tmp/capture-usb-h264.sock \
    --rtsp-port 8555" &
PIDS="$PIDS $!"

wait -n
echo "One of the processes has exited, cleaning up..."
