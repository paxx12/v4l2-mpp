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

"$DIR/capture-mipi-mpp" \
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

if [[ ! -d /tmp/v4l2-venv ]]; then
    python3 -m venv /tmp/v4l2-venv
fi

if [[ ! -e /tmp/v4l2-venv/initialized ]]; then
    /tmp/v4l2-venv/bin/pip3 install /tmp/v4l2/rknn_toolkit_lite2-2.3.2-cp311-cp311-manylinux_2_17_aarch64.manylinux2014_aarch64.whl
    /tmp/v4l2-venv/bin/pip3 install opencv-python-headless numpy
    touch /tmp/v4l2-venv/initialized
fi

/tmp/v4l2-venv/bin/python3 "$DIR/detect-rknn-yolo11.py" \
    --sock /tmp/detect-rknn-yolo11.sock \
    --model-path /home/lava/printer_data/model/printer_detector.0930.fp.rknn \
    --labels-path /home/lava/printer_data/model/printer_detect_labels_list.txt &
PIDS="$PIDS $!"

bash -c "$DIR/stream-snap-mqtt.py \
    --jpeg-sock /tmp/capture-mipi-jpeg.sock \
    --detect-sock /tmp/detect-rknn-yolo11.sock \
    --publish-dir /home/lava/printer_data/camera" &
PIDS="$PIDS $!"

$RUNCMD -c "$DIR/stream-rtsp \
    --h264-sock /tmp/capture-mipi-h264.sock \
    --rtsp-port 8554" &
PIDS="$PIDS $!"

wait -n
echo "One of the processes has exited, cleaning up..."
