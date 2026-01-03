# v4l2-ctrls

Touch-friendly web UI for managing V4L2 camera controls with embedded video preview.

## Features

- Real-time V4L2 control adjustment via web interface
- Automatic detection and handling of read-only controls
- Multi-camera support with embedded video streams
- Modern, colorful UI optimized for touch devices with light/dark theme support
- Reverse proxy compatible with flexible URL configuration
- LocalStorage persistence for user preferences (theme, camera selection, preview mode)

## Requirements

- Python 3
- Flask (`pip install flask`)
- `v4l2-ctl` available in `PATH`

## Usage

**Single camera (auto-detect):**
```sh
python3 v4l2-ctrls.py
```

**Specify devices:**
```sh
python3 v4l2-ctrls.py --device /dev/video11
```

**Multiple cameras with custom configuration:**
```sh
python3 v4l2-ctrls.py \
  --device /dev/video11 \
  --device /dev/video12 \
  --port 5001 \
  --camera-url http://192.168.1.100/ \
  --app-base-url /camera-controls/ \
  --title "My Camera System"
```

**Custom stream endpoints:**
```sh
python3 v4l2-ctrls.py \
  --device /dev/video12 \
  --stream-prefix /dev/video12=/webcam2/ \
  --stream-path-webrtc "{prefix}webrtc" \
  --stream-path-mjpg "{prefix}stream.mjpg" \
  --stream-path-snapshot "{prefix}snapshot.jpg"
```

**Non-standard stream backend:**
```sh
python3 v4l2-ctrls.py \
  --device /dev/video0 \
  --stream-prefix /dev/video0=/camera/ \
  --stream-path-webrtc "{prefix}live.webrtc" \
  --stream-path-mjpg "{prefix}feed.mjpeg" \
  --stream-path-snapshot "{prefix}snap.png"
```

## Command-line Options

- `--device <path>` - V4L2 device path (can be specified multiple times; auto-detects if omitted)
- `--host <address>` - Host to bind (default: `0.0.0.0`)
- `--port <number>` - Port to bind (default: `5000`)
- `--camera-url <url>` - Base URL for camera streams (default: `http://127.0.0.1/`)
- `--app-base-url <path>` - Base URL path for UI routing when behind a reverse proxy (optional)
- `--title <text>` - Custom page title (optional)
- `--stream-prefix <key=path>` - Override stream prefix per camera (key can be device path, basename, or cam id)
- `--stream-path-webrtc <template>` - Template for WebRTC stream path (default: `{prefix}webrtc`)
- `--stream-path-mjpg <template>` - Template for MJPG stream path (default: `{prefix}stream.mjpg`)
- `--stream-path-snapshot <template>` - Template for snapshot stream path (default: `{prefix}snapshot.jpg`)

## URL Template Variables

The `--camera-url` option supports template variables for flexible stream routing:

- `{path}` - Full path including camera prefix and mode (e.g., `/webcam/stream.mjpg`)
- `{prefix}` - Camera prefix only (e.g., `/webcam/`)
- `{mode}` - Preview mode (e.g., `webrtc`, `mjpg`, `snapshot`)
- `{cam}` - Camera id (e.g., `video12`)
- `{device}` - Device path (e.g., `/dev/video12`)
- `{index}` - Camera index in the list (1-based)
- `{basename}` - Device basename (e.g., `video12`)

**Examples:**
```sh
# Simple append mode (default behavior)
--camera-url http://192.168.1.100/

# Path substitution
--camera-url http://192.168.1.100/streams/{path}

# Custom routing with prefix and mode
--camera-url http://192.168.1.100/{prefix}{mode}
```

## Stream Path Templates

The stream path options (`--stream-path-webrtc`, `--stream-path-mjpg`, `--stream-path-snapshot`) support the same
template variables as above, plus `{basename}` for the device basename (e.g., `video12`).

## API Endpoints

- `GET /` - Main UI page
- `GET /api/cams` - List cameras and streamer configuration
- `GET /api/v4l2/ctrls?cam=<cam_id>` - Get available controls for device
- `POST /api/v4l2/set?cam=<cam_id>` - Apply control value changes (JSON body: `{"control_name": value}`)
- `GET /api/v4l2/info?cam=<cam_id>` - Get device information

## Reverse Proxy Configuration

When running behind a reverse proxy (e.g., nginx), use `--app-base-url` to set the base path:
```sh
python3 v4l2-ctrls.py --app-base-url /v4l2/
```

Then configure your proxy to forward `/v4l2/` to the Flask app.

## Integration

Integrated into the v4l2-mpp firmware build system. Installed to `/usr/local/bin/v4l2-ctrls.py` during firmware compilation.

## Notes

- Control changes are applied immediately but **not persisted** across reboots
- Persistence is handled by the camera streamer/service layer
- Camera auto-detection prefers `/dev/v4l-subdev2` if available
- Up to 8 devices are auto-detected by default
