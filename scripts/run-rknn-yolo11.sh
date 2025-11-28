#!/bin/bash

DIR=$(dirname "$0")

if [[ ! -d /tmp/v4l2-venv ]]; then
    python3 -m venv /tmp/v4l2-venv
fi

source /tmp/v4l2-venv/bin/activate

if [[ ! -e /tmp/v4l2-venv/initialized ]]; then
    pip3 install /tmp/v4l2/rknn_toolkit_lite2-2.3.2-cp311-cp311-manylinux_2_17_aarch64.manylinux2014_aarch64.whl
    pip3 install opencv-python-headless numpy
    touch /tmp/v4l2-venv/initialized
fi

cd "$DIR"

set -x

/tmp/v4l2-venv/bin/python3 /tmp/v4l2/detect-rknn-yolo11.py \
    --image /tmp/.monitor.jpg \
    --output-image /home/lava/dirty_output.jpg \
    --model-path /home/lava/printer_data/model/printer_detector.0930.fp.rknn \
    --labels-path /home/lava/printer_data/model/printer_detect_labels_list.txt \
    "$@"
