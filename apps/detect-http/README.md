# detect-http

HTTP server for AI object detection visualization with real-time bounding boxes and statistics.

## Features

- Real-time object detection visualization in browser
- Bounding boxes with class labels and confidence scores
- Support for multiple detection backends (via Unix sockets)
- Live FPS counter and processing time metrics
- Start/stop detection control
- Color-coded detection classes
- Responsive web interface

## Usage

```sh
detect-http.py --jpeg-sock SOCKET [OPTIONS]
```

## Options

| Option | Description | Default |
|--------|-------------|---------|
| `--jpeg-sock SOCKET` | JPEG snapshot socket (required) | - |
| `--detect-sock SOCKET` | Detection socket path (can be used multiple times for multiple detectors) | - |
| `-p, --port PORT` | HTTP server port | 8091 |
| `--bind ADDRESS` | Bind address | 0.0.0.0 |

## Example

```sh
detect-http.py \
  --jpeg-sock /tmp/camera-jpeg.sock \
  --detect-sock /tmp/detector-yolo.sock \
  --port 8091
```

Then open http://localhost:8091 in your browser.

## Multiple Detectors

You can use multiple detection backends simultaneously:

```sh
detect-http.py \
  --jpeg-sock /tmp/camera-jpeg.sock \
  --detect-sock /tmp/detector-yolo.sock \
  --detect-sock /tmp/detector-custom.sock \
  --port 8091
```

## Socket Requirements

- **JPEG Socket**: Must provide JPEG image data when connected
- **Detection Socket**: Must accept JSON request with `{"image": "/path/to/image.jpg"}` and return JSON with detection results in format:
  ```json
  {
    "detections": {
      "class_name": [
        {"x": 10, "y": 20, "w": 100, "h": 150, "confidence": 0.95}
      ]
    }
  }
  ```
