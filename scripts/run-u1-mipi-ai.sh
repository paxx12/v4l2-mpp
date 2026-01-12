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

$RUNCMD -c "$BINDIR/stream-webrtc \
    --h264-sock /tmp/capture-mipi-h264.sock \
    --webrtc-sock /tmp/capture-mipi-webrtc.sock" &
PIDS="$PIDS $!"

$RUNCMD -c "$BINDIR/stream-http.py \
    --html-dir $HTMLDIR \
    --jpeg-sock /tmp/capture-mipi-jpeg.sock \
    --mjpeg-sock /tmp/capture-mipi-mjpeg.sock \
    --h264-sock /tmp/capture-mipi-h264.sock \
    --webrtc-sock /tmp/capture-mipi-webrtc.sock" &
PIDS="$PIDS $!"

if [[ ! -d /tmp/v4l2-venv ]]; then
    python3 -m venv /tmp/v4l2-venv
fi

if [[ ! -e /tmp/v4l2-venv/initialized ]]; then
    /tmp/v4l2-venv/bin/pip3 install https://github.com/airockchip/rknn-toolkit2/raw/refs/heads/master/rknn-toolkit-lite2/packages/rknn_toolkit_lite2-2.3.2-cp311-cp311-manylinux_2_17_aarch64.manylinux2014_aarch64.whl
    /tmp/v4l2-venv/bin/pip3 install opencv-python-headless numpy
    touch /tmp/v4l2-venv/initialized
fi

/tmp/v4l2-venv/bin/python3 "$BINDIR/detect-rknn-yolo11.py" \
    --model /opt/lava/unisrv/camera_service/model/bed_check.fp.rknn \
    --labels item,nozzle,legend_text,light_spot \
    --sock /tmp/detect-bed-check.sock &
PIDS="$PIDS $!"

/tmp/v4l2-venv/bin/python3 "$BINDIR/detect-rknn-yolo11.py" \
    --model /opt/lava/unisrv/camera_service/model/print_check.fp.rknn \
    --labels item,nozzle,legend_text,light_spot \
    --sock /tmp/detect-print-check.sock &
PIDS="$PIDS $!"

$RUNCMD -c "$BINDIR/stream-rtsp \
    --h264-sock /tmp/capture-mipi-h264.sock \
    --rtsp-port 8554" &
PIDS="$PIDS $!"

/tmp/v4l2-venv/bin/python3 "$BINDIR/detect-http.py" \
    --port 8091 \
    --bind 0.0.0.0 \
    --jpeg-sock /tmp/capture-mipi-jpeg.sock \
    --detect-sock /tmp/detect-bed-check.sock \
    --detect-sock /tmp/detect-print-check.sock &
PIDS="$PIDS $!"

wait -n
echo "One of the processes has exited, cleaning up..."
