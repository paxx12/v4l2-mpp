#!/bin/bash

DIR=$(dirname "$0")

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

"$DIR/capture-usb-mpp" \
    --device /dev/video18 \
    --jpeg-sock /tmp/capture-usb-jpeg.sock \
    --mjpeg-sock /tmp/capture-usb-mjpeg.sock \
    --h264-sock /tmp/capture-usb-h264.sock &
PIDS="$!"

$RUNCMD -c "$DIR/stream-webrtc \
    --h264-sock /tmp/capture-usb-h264.sock \
    --webrtc-sock /tmp/capture-usb-webrtc.sock" &
PIDS="$PIDS $!"

$RUNCMD -c "$DIR/stream-http.py \
    --port 8081 \
    --jpeg-sock /tmp/capture-usb-jpeg.sock \
    --mjpeg-sock /tmp/capture-usb-mjpeg.sock \
    --h264-sock /tmp/capture-usb-h264.sock \
    --webrtc-sock /tmp/capture-usb-webrtc.sock" &
PIDS="$PIDS $!"

$RUNCMD -c "$DIR/stream-rtsp \
    --h264-sock /tmp/capture-usb-h264.sock \
    --rtsp-port 8555" &
PIDS="$PIDS $!"

$RUNCMD -c "$DIR/stream-snap-mqtt.py \
    --jpeg-sock /tmp/capture-usb-jpeg.sock \
    --publish-dir /home/lava/printer_data/camera" &
PIDS="$PIDS $!"

wait -n
echo "One of the processes has exited, cleaning up..."
