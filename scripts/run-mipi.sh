#!/bin/bash

DIR=$(dirname "$0")

cleanup() {
    kill $PIDS 2>/dev/null
    exit 1
}

trap cleanup INT TERM EXIT

"$DIR/capture-mipi-mpp" \
    --device /dev/video11 --format nv12 \
    --jpeg-quality 7 \
    --jpeg-sock /tmp/capture-mipi-jpeg.sock \
    --mjpeg-sock /tmp/capture-mipi-mjpeg.sock \
    --h264-sock /tmp/capture-mipi-h264.sock &
PIDS="$!"

su lava -c "$DIR/stream-http.py \
    --jpeg-sock /tmp/capture-mipi-jpeg.sock \
    --mjpeg-sock /tmp/capture-mipi-mjpeg.sock \
    --h264-sock /tmp/capture-mipi-h264.sock" &
PIDS="$PIDS $!"

su lava -c "$DIR/stream-snap-mqtt.py \
    --jpeg-sock /tmp/capture-mipi-jpeg.sock \
    --publish-dir /home/lava/printer_data/camera" &
PIDS="$PIDS $!"

wait -n
echo "One of the processes has exited, cleaning up..."
