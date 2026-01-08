# stream-http

HTTP server for camera streaming.

Connects to capture app Unix sockets and serves video streams over HTTP. Provides JPEG snapshots, MJPEG streams, raw H264, and a browser-based H264 player.

## Endpoints

- `/snapshot.jpg` - Single JPEG image
- `/stream.mjpg` - MJPEG stream
- `/stream.h264` - Raw H264 stream
- `/player` - Browser H264 player (jmuxer)
- `/control/` - Camera control UI (requires `--control-sock`)
- `POST /control/rpc` - JSON-RPC proxy to the control socket
