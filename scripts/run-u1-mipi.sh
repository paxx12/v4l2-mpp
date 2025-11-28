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

"$DIR/capture-v4l2-raw-mpp" \
    --device /dev/video11 --format nv12 \
    --jpeg-quality 7 \
    --jpeg-sock /tmp/capture-mipi-jpeg.sock \
    --mjpeg-sock /tmp/capture-mipi-mjpeg.sock \
    --h264-sock /tmp/capture-mipi-h264.sock &
PIDS="$!"

$RUNCMD -c "$DIR/stream-webrtc \
    --h264-sock /tmp/capture-mipi-h264.sock \
    --webrtc-sock /tmp/capture-mipi-webrtc.sock" &
PIDS="$PIDS $!"

$RUNCMD -c "$DIR/stream-http.py \
    --jpeg-sock /tmp/capture-mipi-jpeg.sock \
    --mjpeg-sock /tmp/capture-mipi-mjpeg.sock \
    --h264-sock /tmp/capture-mipi-h264.sock \
    --webrtc-sock /tmp/capture-mipi-webrtc.sock" &
PIDS="$PIDS $!"

bash -c "$DIR/stream-snap-mqtt.py \
    --jpeg-sock /tmp/capture-mipi-jpeg.sock \
    --publish-dir /home/lava/printer_data/camera" &
PIDS="$PIDS $!"

$RUNCMD -c "$DIR/stream-rtsp \
    --h264-sock /tmp/capture-mipi-h264.sock \
    --rtsp-port 8554" &
PIDS="$PIDS $!"

wait -n
echo "One of the processes has exited, cleaning up..."
