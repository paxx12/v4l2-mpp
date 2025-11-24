# detect-rknn

Object detection application using Rockchip NPU (RKNPU2) for real-time inference.

(NOT YET WORKING)

## Features

- Fetches JPEG images via Unix domain socket from other apps
- Uses RKNN Lite2 runtime for NPU acceleration
- Configurable detection interval
- Supports custom RKNN models
- Optional saving of detection results with bounding boxes
- NMS (Non-Maximum Suppression) post-processing

## Dependencies

Install required packages:

```bash
pip install -r requirements.txt
```

For RKNN Lite2, you may need to install from the Rockchip SDK or use:

```bash
pip install rknnlite2
```

## Run on Snapmaker U1

```bash
python3 -m venv /tmp/venv
/tmp/venv/bin/pip3 install opencv-python
/tmp/venv/bin/pip3 install /tmp/v4l2/rknn_toolkit_lite2-2.3.2-cp311-cp311-manylinux_2_17_aarch64.manylinux2014_aarch64.whl
/tmp/venv/bin/python3 /tmp/v4l2/detect-rknn.py
```

## Configuration

Edit `config.json` to adjust detection parameters:

- `nms_threshold`: NMS threshold for removing overlapping boxes (default: 0.5)
- `box_conf_threshold`: Minimum confidence threshold for detections (default: 0.5)
- `model_name`: Name of the RKNN model file
- `max_detect_pic_cnt`: Maximum number of detection images to save (default: 30)
- `save_orignal_pic`: Whether to save original images (1=yes, 0=no)

Add class labels to `printer_detect_labels_list.txt` (one per line).

## Usage

Basic usage with a JPEG socket from another app:

```bash
./main.py \
  --model-path ./printer_detector.0930.fp.rknn \
  --config-path ./config.json \
  --jpeg-sock /tmp/camera.sock \
  --interval 1.0
```

With detection result saving:

```bash
./main.py \
  --model-path ./printer_detector.0930.fp.rknn \
  --config-path ./config.json \
  --jpeg-sock /tmp/camera.sock \
  --interval 2.0 \
  --save-results \
  --output-dir ./detections
```

## Arguments

- `--model-path`: Path to RKNN model file (required)
- `--config-path`: Path to config.json (required)
- `--jpeg-sock`: Unix socket path for JPEG images (required)
- `--interval`: Detection interval in seconds (default: 1.0)
- `--output-dir`: Directory to save detection results (default: ./detections)
- `--save-results`: Enable saving detection results with bounding boxes

## Integration with Other Apps

This app integrates with the existing socket-based architecture:

1. Start a camera capture app that provides a JPEG socket:

   ```bash
   ../capture-usb-mpp/build/capture-usb-mpp --jpeg-sock /tmp/camera.sock
   ```

2. Run the detection app:

   ```bash
   ./main.py --model-path ./model.rknn --config-path ./config.json --jpeg-sock /tmp/camera.sock
   ```

## Output

Detection results are logged to stdout with:
- Number of detected objects
- Class name and confidence score
- Bounding box coordinates

Example output:
```
[12:34:56] Detected 2 objects in 0.045s:
[12:34:56]   - printer: 0.873 at [120, 80, 450, 320]
[12:34:56]   - printer: 0.651 at [500, 100, 780, 400]
```
