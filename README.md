# v4l2-mpp

V4L2 camera capture and streaming tools with Rockchip MPP hardware encoding for ARM64 Linux.

## Applications

| App | Description |
|-----|-------------|
| capture-v4l2-raw-mpp | V4L2 capture for raw pixel formats (YUV/RGB) with MPP hardware encoding (JPEG/H264) |
| capture-v4l2-jpeg-mpp | V4L2 capture for JPEG/MJPEG input with MPP transcoding to H264 |
| detect-rknn-yolo11 | YOLO11 object detection using Rockchip NPU (RKNPU2) for real-time inference |
| detect-http | HTTP server for AI object detection visualization with real-time bounding boxes |
| stream-http | HTTP server for camera streaming (snapshots, MJPEG, H264, browser player) |
| stream-webrtc | WebRTC server for low-latency H264 video streaming |
| control-v4l2 | JSON-RPC service for managing V4L2 camera controls with optional persistence |

## Building

```sh
./deps/compile_mpp.sh
cd apps/capture-v4l2-raw-mpp && make
cd apps/capture-v4l2-jpeg-mpp && make
```

Python apps (stream-http, detect-http) require no compilation.

## Dependencies

- Rockchip MPP library
- Rockchip RKNN Lite2 runtime (for detect-rknn-yolo11)
- Python 3 with opencv-python, numpy (for detect-rknn-yolo11)
- ffmpeg (for timelapse video generation)

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for information about contributing to this project.

## License

GPL-3.0-or-later (currently). See [LICENSE](LICENSE) for details.
