# capture-v4l2-jpeg-mpp

V4L2 capture application for JPEG/MJPEG input with Rockchip MPP transcoding.

Captures MJPEG frames from V4L2 devices, decodes with MPP, and re-encodes to H264 for streaming. Passes through MJPEG directly for JPEG/MJPEG outputs.

## Features

- V4L2 JPEG/MJPEG capture
- Hardware JPEG decoding (MPP)
- Hardware H264 encoding (MPP)
- Unix socket output for JPEG snapshots, MJPEG streams, and H264 streams
- Configurable resolution, FPS, and bitrate
