HTML_WEBRTC = """<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>WebRTC Stream</title>
  <link rel="icon" href="data:;base64,iVBORw0KGgo=">
  <style>
    * {
      box-sizing: border-box;
    }
    body {
      margin: 0;
      padding: 0;
      background: #303030;
      overflow: hidden;
      font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif;
    }
    #streamStage {
      position: fixed;
      inset: 0;
      display: flex;
      align-items: center;
      justify-content: center;
    }
    #stream {
      max-width: 100%;
      max-height: 100%;
      background: #000;
    }
    #status {
      position: fixed;
      top: 1rem;
      left: 50%;
      transform: translateX(-50%);
      padding: 0.5rem 1rem;
      background: rgba(0, 0, 0, 0.7);
      color: #fff;
      border-radius: 0.25rem;
      font-size: 0.875rem;
      opacity: 0;
      transition: opacity 0.3s;
      pointer-events: none;
    }
    #status.show {
      opacity: 1;
    }
    #status.error {
      background: rgba(220, 38, 38, 0.9);
    }
    #status.success {
      background: rgba(34, 197, 94, 0.9);
    }
  </style>
</head>
<body>
  <div id="streamStage">
    <video id="stream" controls autoplay muted playsinline></video>
  </div>
  <div id="status"></div>
  <script>
    (function() {
      'use strict';

      let peerConnection = null;
      let reconnectTimeout = null;
      const statusElement = document.getElementById('status');
      const videoElement = document.getElementById('stream');

      function updateStatus(message, type = 'info', duration = 3000) {
        statusElement.textContent = message;
        statusElement.className = `show ${type}`;
        if (duration > 0) {
          setTimeout(() => statusElement.className = '', duration);
        }
      }

      async function sendRequest(body) {
        const response = await fetch(window.location.href, {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify(body)
        });
        if (!response.ok) {
          throw new Error(`HTTP ${response.status}`);
        }
        return response.json();
      }

      async function initializeStream() {
        try {
          updateStatus('Connecting...', 'info', 0);

          const urlParams = new URLSearchParams(window.location.search);
          const params = Object.fromEntries(urlParams.entries());

          const requestBody = {
            type: 'request',
            res: params.res,
            iceServers: [{ urls: ['stun:stun.l.google.com:19302'] }],
            keepAlive: true
          };

          if (params.timeout_s) {
            requestBody.timeout_s = parseInt(params.timeout_s, 10);
          }

          const offer = await sendRequest(requestBody);

          peerConnection = new RTCPeerConnection({
            sdpSemantics: 'unified-plan',
            iceServers: offer.iceServers
          });

          peerConnection.remote_pc_id = offer.id;

          peerConnection.onconnectionstatechange = () => {
            const state = peerConnection.connectionState;
            if (state === 'connected') {
              updateStatus('Connected', 'success', 2000);
              if (reconnectTimeout) {
                clearTimeout(reconnectTimeout);
                reconnectTimeout = null;
              }
            } else if (state === 'failed' || state === 'disconnected') {
              updateStatus('Connection lost, reconnecting in 3s...', 'error', 0);
              if (reconnectTimeout) {
                clearTimeout(reconnectTimeout);
              }
              reconnectTimeout = setTimeout(() => {
                cleanup();
                initializeStream();
              }, 3000);
            } else if (state === 'connecting') {
              updateStatus('Connecting...', 'info', 0);
            }
          };

          peerConnection.ondatachannel = (event) => {
            const channel = event.channel;
            if (channel.label === 'keepalive') {
              channel.onmessage = () => channel.send('pong');
            }
          };

          peerConnection.addTransceiver('video', { direction: 'recvonly' });

          peerConnection.ontrack = (event) => {
            if (event.streams[0]) {
              videoElement.srcObject = event.streams[0];
            }
          };

          peerConnection.onicecandidate = (event) => {
            if (event.candidate) {
              sendRequest({
                type: 'remote_candidate',
                id: peerConnection.remote_pc_id,
                candidates: [event.candidate]
              }).catch(error => {
                console.error('ICE candidate error:', error);
              });
            }
          };

          await peerConnection.setRemoteDescription(offer);

          const answer = await peerConnection.createAnswer();
          await peerConnection.setLocalDescription(answer);

          await sendRequest({
            type: peerConnection.localDescription.type,
            id: peerConnection.remote_pc_id,
            sdp: peerConnection.localDescription.sdp
          });

        } catch (error) {
          console.error('WebRTC error:', error);
          updateStatus(`Error: ${error.message}, retrying in 3s...`, 'error', 0);
          if (reconnectTimeout) {
            clearTimeout(reconnectTimeout);
          }
          reconnectTimeout = setTimeout(() => {
            cleanup();
            initializeStream();
          }, 3000);
        }
      }

      function cleanup() {
        if (reconnectTimeout) {
          clearTimeout(reconnectTimeout);
          reconnectTimeout = null;
        }
        if (peerConnection) {
          peerConnection.close();
          peerConnection = null;
        }
      }

      window.addEventListener('load', initializeStream);
      window.addEventListener('beforeunload', cleanup);

    })();
  </script>
</body>
</html>
"""
