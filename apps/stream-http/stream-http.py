#!/usr/bin/env python3

import socket
import argparse
import time
import json
import os
from urllib.parse import urlparse
from http.server import ThreadingHTTPServer, SimpleHTTPRequestHandler

ALLOWED_PATHS = {
    '/': 'index.html',
    '/player': 'player.html',
    '/webrtc': 'webrtc.html',
    '/control': 'control.html'
}

def log(msg):
    ts = time.strftime('%H:%M:%S')
    print(f"[{ts}] {msg}", flush=True)

def read_socket(sock_path, chunk_size=65536):
    sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    sock.connect(sock_path)
    try:
        while True:
            chunk = sock.recv(chunk_size)
            if not chunk:
                break
            yield chunk
    finally:
        sock.close()

def read_h264_from_keyframe(sock_path, chunk_size=65536):
    for chunk in read_socket(sock_path, chunk_size):
        yield chunk

def read_jpeg_frames(sock_path, chunk_size=65536):
    buf = b''
    for chunk in read_socket(sock_path, chunk_size):
        buf += chunk
        while True:
            start = buf.find(b'\xff\xd8')
            if start == -1:
                buf = buf[-1:] if buf else b''
                break
            end = buf.find(b'\xff\xd9', start + 2)
            if end == -1:
                buf = buf[start:]
                break
            yield buf[start:end + 2]
            buf = buf[end + 2:]

def socket_req_and_resp(sock_path, request):
    sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    sock.settimeout(5.0)
    try:
        sock.connect(sock_path)
        data = json.dumps(request) + '\n'
        sock.sendall(data.encode())
        response = b''
        while True:
            chunk = sock.recv(65536)
            if not chunk:
                break
            response += chunk
            if b'\n' in response:
                break
        return json.loads(response.decode().strip())
    finally:
        sock.close()

class CameraHandler(SimpleHTTPRequestHandler):
    jpeg_sock = None
    mjpeg_sock = None
    h264_sock = None
    webrtc_sock = None
    control_sock = None
    html_dir = None

    def log_message(self, format, *args):
        log(f"HTTP {self.address_string()} - {format % args}")

    def translate_path(self, path):
        parsed_path = urlparse(path).path
        if parsed_path not in ALLOWED_PATHS:
            return None
        return os.path.join(self.html_dir, ALLOWED_PATHS[path])

    def do_GET(self):
        log(f"Request: {self.path} from {self.address_string()}")
        path = urlparse(self.path).path

        if path == '/snapshot.jpg':
            self.handle_snapshot()
        elif path == '/stream.mjpg':
            self.handle_mjpeg_stream()
        elif path == '/stream.h264':
            self.handle_h264_stream()
        elif path == '/control' and not self.control_sock:
            self.send_error(503, 'Control not available')
        elif path == '/webrtc' and not self.webrtc_sock:
            self.send_response(302)
            self.send_header('Location', 'player')
            self.end_headers()
        elif path not in ALLOWED_PATHS:
            self.send_error(404, "File not found")
        else:
            SimpleHTTPRequestHandler.do_GET(self)

        log(f"Request done: {self.path}")

    def do_POST(self):
        log(f"POST: {self.path} from {self.address_string()}")
        path = urlparse(self.path).path
        if path == '/webrtc' and not self.webrtc_sock:
            self.send_error(503, 'WebRTC not available')
        elif path == '/webrtc':
            self.handle_socket_req_and_resp(self.webrtc_sock)
        elif path == '/control' and not self.control_sock:
            self.send_error(503, 'Control not available')
        elif path == '/control':
            self.handle_socket_req_and_resp(self.control_sock)
        else:
            self.send_error(404, 'Not Found')
        log(f"POST done: {self.path}")

    def handle_snapshot(self):
        if not self.jpeg_sock:
            self.send_error(503, 'Snapshot not available')
            return
        self.send_response(200)
        self.send_header('Content-Type', 'image/jpeg')
        self.send_header('Cache-Control', 'no-cache, no-store, must-revalidate')
        self.end_headers()
        try:
            total = 0
            for chunk in read_socket(self.jpeg_sock):
                self.wfile.write(chunk)
                total += len(chunk)
            log(f"JPEG sent {total} bytes")
        except (BrokenPipeError, ConnectionResetError) as e:
            log(f"JPEG client disconnected: {e}")
        except (FileNotFoundError, IOError, OSError) as e:
            log(f"JPEG error: {e}")

    def handle_mjpeg_stream(self):
        if not self.mjpeg_sock:
            self.send_error(503, 'MJPEG stream not available')
            return
        self.send_response(200)
        self.send_header('Content-Type', 'multipart/x-mixed-replace; boundary=frame')
        self.send_header('Cache-Control', 'no-cache, no-store, must-revalidate')
        self.end_headers()
        log(f"Connecting to MJPEG socket: {self.mjpeg_sock}")
        frame_count = 0
        try:
            for frame in read_jpeg_frames(self.mjpeg_sock):
                self.wfile.write(b'--frame\r\nContent-Type: image/jpeg\r\n\r\n')
                self.wfile.write(frame)
                self.wfile.write(b'\r\n')
                self.wfile.flush()
                frame_count += 1
                if frame_count % 30 == 0:
                    log(f"MJPEG sent {frame_count} frames")
            log("MJPEG socket EOF")
        except (BrokenPipeError, ConnectionResetError) as e:
            log(f"MJPEG client disconnected: {e}")
        except (IOError, OSError) as e:
            log(f"MJPEG stream error: {e}")

    def handle_h264_stream(self):
        if not self.h264_sock:
            self.send_error(503, 'H264 stream not available')
            return
        self.send_response(200)
        self.send_header('Content-Type', 'video/h264')
        self.send_header('Cache-Control', 'no-cache, no-store, must-revalidate')
        self.end_headers()
        log(f"Connecting to H264 socket: {self.h264_sock}")
        try:
            total_bytes = 0
            last_bytes = 0
            for chunk in read_h264_from_keyframe(self.h264_sock):
                self.wfile.write(chunk)
                self.wfile.flush()
                total_bytes += len(chunk)
                last_bytes += len(chunk)
                if last_bytes >= (1024 * 1024):
                    log(f"H264 sent {total_bytes // 1024}KB")
                    last_bytes = 0
            log("H264 socket EOF")
        except (BrokenPipeError, ConnectionResetError) as e:
            log(f"H264 client disconnected: {e}")
        except (IOError, OSError) as e:
            log(f"H264 stream error: {e}")

    def send_json_response(self, code, body):
        data = json.dumps(body).encode()
        self.send_response(code)
        self.send_header('Content-Type', 'application/json')
        self.send_header('Content-Length', len(data))
        self.end_headers()
        self.wfile.write(data)

    def handle_socket_req_and_resp(self, socket_path):
        try:
            content_length = int(self.headers.get('Content-Length', 0))
            request_body = self.rfile.read(content_length).decode()
            request = json.loads(request_body)
            response = socket_req_and_resp(socket_path, request)
            self.send_json_response(200, response)
        except Exception as e:
            log(f"Socket Request and Response: {e}")
            self.send_json_response(500, {'error': str(e)})

def main():
    script_dir = os.path.dirname(os.path.abspath(__file__))
    local_html_dir = os.path.join(script_dir, 'html')
    installed_html_dir = '/usr/share/stream-http/html'

    if os.path.isdir(local_html_dir):
        default_html_dir = local_html_dir
    else:
        default_html_dir = installed_html_dir

    parser = argparse.ArgumentParser(description='V4L2-MPP HTTP Server')
    parser.add_argument('-p', '--port', type=int, default=8080, help='HTTP port')
    parser.add_argument('--bind', default='0.0.0.0', help='Bind address')
    parser.add_argument('--html-dir', default=default_html_dir, help=f'HTML directory for static files (default: {default_html_dir})')
    parser.add_argument('--jpeg-sock', required=True, help='JPEG snapshot socket')
    parser.add_argument('--mjpeg-sock', required=True, help='MJPEG stream socket')
    parser.add_argument('--h264-sock', required=True, help='H264 stream socket')
    parser.add_argument('--webrtc-sock', help='WebRTC signaling socket')
    parser.add_argument('--control-sock', help='V4L2 control interface socket')
    args = parser.parse_args()

    CameraHandler.html_dir = args.html_dir
    CameraHandler.jpeg_sock = args.jpeg_sock
    CameraHandler.mjpeg_sock = args.mjpeg_sock
    CameraHandler.h264_sock = args.h264_sock
    CameraHandler.webrtc_sock = args.webrtc_sock
    CameraHandler.control_sock = args.control_sock

    server = ThreadingHTTPServer((args.bind, args.port), CameraHandler)
    log(f"Server running on http://{args.bind}:{args.port}")
    log(f"  HTML directory: {args.html_dir}")
    log(f"  /              - Camera index")
    log(f"  /snapshot.jpg  - JPEG snapshot")
    log(f"  /stream.mjpg   - MJPEG stream")
    log(f"  /stream.h264   - H264 stream")
    log(f"  /player        - H264 player")
    if args.webrtc_sock:
        log(f"  /webrtc        - WebRTC player")
        log(f"  POST /webrtc   - WebRTC offer")
    if args.control_sock:
        log(f"  /control       - Control interface")
        log(f"  POST /control  - Set controls")

    try:
        server.serve_forever()
    except KeyboardInterrupt:
        log("Shutting down...")

if __name__ == '__main__':
    main()
