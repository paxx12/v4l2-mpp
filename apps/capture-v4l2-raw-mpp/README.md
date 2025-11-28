# capture-v4l2-raw-mpp

V4L2 capture application for raw pixel formats with Rockchip MPP hardware encoding.

Captures raw video frames (YUYV, NV12, RGB24, etc.) from V4L2 devices and encodes to JPEG/H264 using MPP. Outputs via Unix sockets for streaming consumers.

## Features

- Multi-planar V4L2 capture support
- Hardware JPEG encoding (MPP)
- Hardware H264 encoding (MPP)
- Unix socket output for JPEG snapshots, MJPEG streams, and H264 streams
- Configurable resolution, FPS, bitrate, and quality
