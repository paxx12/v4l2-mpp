# detect-rknn-yolo11

YOLO11 object detection application using Rockchip NPU (RKNPU2) for real-time inference.

## Features

- Two modes: single image processing or Unix socket server
- Uses RKNN Lite2 runtime for NPU acceleration
- YOLO11 model support with DFL (Distribution Focal Loss)
- Configurable confidence and NMS thresholds
- Letterbox resizing with automatic scaling
- NMS (Non-Maximum Suppression) post-processing
- JSON output with per-class detections and timing statistics
- Optional bounding box visualization

## Dependencies

Install required packages:

```bash
pip install opencv-python numpy
pip install rknnlite2
```

For RKNN Lite2, you may need to install from the Rockchip SDK.

## Run on Snapmaker U1

```bash
python3 -m venv /tmp/venv
/tmp/venv/bin/pip3 install opencv-python numpy
/tmp/venv/bin/pip3 install /tmp/v4l2/rknn_toolkit_lite2-2.3.2-cp311-cp311-manylinux_2_17_aarch64.manylinux2014_aarch64.whl
/tmp/venv/bin/python3 /tmp/v4l2/detect-rknn-yolo11/main.py
```

## Usage

### Single Image Mode

Process a single JPEG image and output JSON results:

```bash
./main.py \
  --model-path ./model.rknn \
  --labels-path ./labels.txt \
  --image ./input.jpg \
  --input-size 800x480 \
  --threshold 0.1 \
  --nms-threshold 0.5
```

With bounding box visualization:

```bash
./main.py \
  --model-path ./model.rknn \
  --labels-path ./labels.txt \
  --image ./input.jpg \
  --input-size 800x480 \
  --output-image ./output.jpg
```

### Socket Server Mode

Run as a Unix socket server that accepts JSON messages:

```bash
./main.py \
  --model-path ./model.rknn \
  --labels-path ./labels.txt \
  --sock /tmp/detection.sock \
  --input-size 800x480
```

Send detection requests:

```bash
echo '{"image": "/path/to/image.jpg"}' | socat - UNIX-CONNECT:/tmp/detection.sock
```

## Arguments

- `--model-path`: Path to RKNN model file (required)
- `--labels-path`: Path to labels file, one label per line (required)
- `--image`: Input JPEG image for single image mode (mutually exclusive with --sock)
- `--sock`: Unix socket path for server mode (mutually exclusive with --image)
- `--input-size`: Model input size in WxH format (default: 800x480)
- `--threshold`: Box confidence threshold (default: 0.1)
- `--nms-threshold`: NMS IoU threshold (default: 0.5)
- `--output-image`: Output image path with bounding boxes (only for --image mode)
- `--debug`: Enable debug output to stderr

## Labels File Format

Create a labels file with one class name per line:

```
person
bicycle
car
motorcycle
```

## Output Format

JSON output includes per-class detections and timing statistics:

```json
{
  "detections": {
    "person": [
      {
        "x": 120,
        "y": 80,
        "w": 330,
        "h": 240,
        "confidence": 0.873,
        "area": 0.0825
      }
    ],
    "car": []
  },
  "stats": {
    "decode_image_ms": 12.5,
    "resize_image_ms": 3.2,
    "inference_ms": 45.8,
    "post_process_ms": 8.1,
    "total_ms": 69.6
  }
}
```
