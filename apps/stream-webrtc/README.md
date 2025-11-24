# stream-webrtc

WebRTC server for low-latency H264 video streaming.

Connects to an H264 stream via Unix socket and serves it to WebRTC clients using libdatachannel. Provides JSON-based signaling over Unix socket.

## Features

- WebRTC peer connections with H264 video track
- RTP packetization with RTCP feedback (SR, NACK)
- Configurable STUN/ICE servers
- Multiple concurrent client support
- Keepalive ping/pong over data channel
