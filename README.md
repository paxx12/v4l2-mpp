# v4l2-mpp

V4L2 camera capture and streaming tools with Rockchip MPP hardware encoding for ARM64 Linux.

## Applications

| App | Description |
|-----|-------------|
| capture-v4l2-raw-mpp | V4L2 capture for raw pixel formats (YUV/RGB) with MPP hardware encoding (JPEG/H264) |
| capture-v4l2-jpeg-mpp | V4L2 capture for JPEG/MJPEG input with MPP transcoding to H264 |
| stream-http | HTTP server for camera streaming (snapshots, MJPEG, H264, browser player) |
| stream-snap-mqtt | Snapmaker U1 camera interface (timelapse, monitoring, MQTT control) |
| stream-webrtc | WebRTC server for low-latency H264 video streaming |

## Building

```sh
./deps/compile_mpp.sh
cd apps/capture-v4l2-raw-mpp && make
cd apps/capture-v4l2-jpeg-mpp && make
```

Python apps (stream-http, stream-snap-mqtt) require no compilation.

## Dependencies

- Rockchip MPP library
- Python 3 with paho-mqtt (for stream-snap-mqtt)
- ffmpeg (for timelapse video generation)

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for information about contributing to this project.

## License

GPL-3.0-or-later (currently). See [LICENSE](LICENSE) for details.
