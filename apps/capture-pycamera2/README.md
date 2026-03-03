# capture-pycamera2

PyCamera2 capture application for Raspberry Pi cameras with integrated control service.

## Features

- JPEG snapshot socket output (`--jpeg-sock`)
- MJPEG stream socket output (`--mjpeg-sock`)
- H264 stream socket output (`--h264-sock`)
- Optional JPEG file output (`--output`)
- Integrated JSON-RPC controls over Unix socket (`--control-sock`)
- Optional control persistence (`--state-file`)

## Requirements

- Python 3
- `picamera2` Python package (`python3-picamera2` on Raspberry Pi OS)

Install from requirements file:

```sh
pip3 install -r requirements.txt
```

On Raspberry Pi OS, `sudo apt install python3-picamera2` is usually the most reliable option.

## Usage

```sh
python3 capture-pycamera2.py \
  --camera 0 \
  --width 1920 --height 1080 --fps 30 \
  --jpeg-sock /tmp/capture-pi-jpeg.sock \
  --mjpeg-sock /tmp/capture-pi-mjpeg.sock \
  --h264-sock /tmp/capture-pi-h264.sock \
  --control-sock /tmp/control-pycamera2.sock
```

Then serve streams/UI with:

```sh
python3 ../stream-http/stream-http.py \
  --jpeg-sock /tmp/capture-pi-jpeg.sock \
  --mjpeg-sock /tmp/capture-pi-mjpeg.sock \
  --h264-sock /tmp/capture-pi-h264.sock \
  --control-sock /tmp/control-pycamera2.sock
```

## JSON-RPC Methods

Control socket (`--control-sock`) supports:

- `list`
- `get`
- `set`
- `info`
- `reset`

Protocol is line-delimited JSON-RPC 2.0, compatible with `stream-http` `/control` endpoint usage.

## Launcher Script

Use `scripts/run-rpi-pycamera2.sh` to start `capture-pycamera2` + `stream-http` together.

Example (via deploy helper):

```sh
scripts/deploy-run.sh <host> run-rpi-pycamera2.sh
```

Runtime options can be overridden via env vars (`CAMERA_INDEX`, `WIDTH`, `HEIGHT`, `FPS`, `HTTP_PORT`, etc.).
