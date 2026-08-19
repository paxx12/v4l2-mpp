#!/usr/bin/env python3
"""
capture-pycamera2

Single-file PyCamera2 capture app with Unix socket outputs and integrated JSON-RPC controls.

Features:
- JPEG snapshot socket (write one frame and close)
- MJPEG socket (continuous concatenated JPEG frames)
- H264 socket (continuous raw H264 byte stream)
- Optional JPEG file output (atomic rename)
- Integrated control API compatible with control-v4l2 JSON-RPC methods: list/get/set/info/reset
"""

import argparse
import io
import json
import os
import re
import select
import signal
import socket
import struct
import tempfile
import threading
import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Dict, List, Optional, Tuple

try:
    from picamera2 import Picamera2
    from picamera2.encoders import H264Encoder
    from picamera2.outputs import FileOutput
except Exception:
    Picamera2 = None
    H264Encoder = None
    FileOutput = None

SIOCOUTQ = 0x5411
SOCK_MAX_CLIENTS = 8
SOCK_IDLE_TIMEOUT_MS = 3000
SOCK_WRITE_TIMEOUT_MS = 100

DEBUG = False
RUNNING = True

CONTROL_ORDER = [
    "af_mode",
    "af_trigger",
    "lens_position",
    "ae_enable",
    "ae_exposure_mode",
    "exposure_time",
    "analogue_gain",
    "awb_enable",
    "colour_gains",
    "brightness",
    "contrast",
    "saturation",
    "sharpness",
]

AUTO_FIRST_CONTROLS = {
    "ae_enable",
    "ae_exposure_mode",
    "awb_enable",
    "af_mode",
    "af_trigger",
}


def log(msg: str) -> None:
    ts = time.strftime("%H:%M:%S")
    print(f"[{ts}] {msg}", flush=True)


def debug_log(msg: str) -> None:
    if DEBUG:
        log(msg)


def signal_handler(sig: int, _frame: Any) -> None:
    global RUNNING
    log(f"Signal {sig} received, stopping")
    RUNNING = False


def ensure_parent_dir(path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)


def write_output_rename(path: str, data: bytes) -> None:
    tmp_path = f"{path}.tmp"
    with open(tmp_path, "wb") as fp:
        fp.write(data)
    os.replace(tmp_path, path)


def parse_camera_index(camera_arg: Optional[int], device_arg: str) -> int:
    if camera_arg is not None:
        return camera_arg
    if device_arg:
        if device_arg.isdigit():
            return int(device_arg)
        match = re.match(r"^/dev/video(\d+)$", device_arg)
        if match:
            return int(match.group(1))
    return 0


def to_snake_case(name: str) -> str:
    out = re.sub(r"(.)([A-Z][a-z]+)", r"\1_\2", name)
    out = re.sub(r"([a-z0-9])([A-Z])", r"\1_\2", out)
    out = out.replace(" ", "_").replace("-", "_").lower()
    out = re.sub(r"[^a-z0-9_]+", "", out)
    out = re.sub(r"_+", "_", out).strip("_")
    return out or "ctrl"


def safe_json_load(path: Path) -> Dict[str, Any]:
    if not path.exists():
        return {}
    try:
        raw = json.loads(path.read_text())
        if isinstance(raw, dict):
            return raw
    except Exception as exc:
        log(f"Failed to read state file {path}: {exc}")
    return {}


def safe_json_save(path: Path, data: Dict[str, Any]) -> None:
    ensure_parent_dir(path)
    tmp_path = path.with_suffix(path.suffix + ".tmp")
    tmp_path.write_text(json.dumps(data, indent=2, sort_keys=True))
    os.replace(tmp_path, path)


@dataclass
class SocketClient:
    fd: socket.socket
    last_size: int = 0
    last_time: float = field(default_factory=time.monotonic)
    num_frames: int = 0
    num_dropped: int = 0


class SocketBroadcaster:
    def __init__(self, path: Optional[str], one_frame: bool = False, allow_drops: bool = False):
        self.path = path
        self.one_frame = one_frame
        self.allow_drops = allow_drops
        self.listen_sock: Optional[socket.socket] = None
        self.clients: List[SocketClient] = []
        self.lock = threading.Lock()
        self.need_keyframe = False

    def open(self) -> None:
        if not self.path:
            return
        if os.path.exists(self.path):
            os.unlink(self.path)
        sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        sock.bind(self.path)
        sock.listen(SOCK_MAX_CLIENTS)
        sock.setblocking(False)
        os.chmod(self.path, 0o777)
        self.listen_sock = sock
        log(f"Socket listening: {self.path}")

    def close(self) -> None:
        with self.lock:
            for client in self.clients:
                try:
                    client.fd.close()
                except Exception:
                    pass
            self.clients = []
        if self.listen_sock:
            try:
                self.listen_sock.close()
            except Exception:
                pass
            self.listen_sock = None
        if self.path and os.path.exists(self.path):
            try:
                os.unlink(self.path)
            except Exception:
                pass

    def accept_clients(self) -> bool:
        if not self.listen_sock:
            return False
        accepted = False
        while True:
            try:
                conn, _ = self.listen_sock.accept()
            except BlockingIOError:
                break
            except OSError as exc:
                debug_log(f"Accept error on {self.path}: {exc}")
                break

            conn.setblocking(False)
            with self.lock:
                if len(self.clients) >= SOCK_MAX_CLIENTS:
                    conn.close()
                    log(f"Socket {self.path}: rejected client, max reached")
                    continue
                self.clients.append(SocketClient(fd=conn))
                self.need_keyframe = True
                accepted = True
                log(f"Socket {self.path}: client connected (total {len(self.clients)})")
        return accepted

    def has_clients(self) -> bool:
        with self.lock:
            return bool(self.clients)

    def consume_need_keyframe(self) -> bool:
        with self.lock:
            needed = self.need_keyframe
            self.need_keyframe = False
            return needed

    def _close_client(self, idx: int, reason: str) -> None:
        client = self.clients[idx]
        try:
            client.fd.close()
        except Exception:
            pass
        log(
            f"Socket {self.path}: client {idx} {reason}, closing "
            f"(frames={client.num_frames}, dropped={client.num_dropped})"
        )
        del self.clients[idx]

    @staticmethod
    def _unsent_bytes(fd: socket.socket) -> int:
        try:
            outq = fcntl_ioctl_outq(fd)
            return outq
        except Exception:
            return 0

    @staticmethod
    def _send_with_timeout(fd: socket.socket, data: bytes, timeout_ms: int) -> bool:
        total = 0
        start = time.monotonic()
        while total < len(data):
            try:
                sent = fd.send(data[total:])
                if sent <= 0:
                    return False
                total += sent
                continue
            except BlockingIOError:
                elapsed_ms = (time.monotonic() - start) * 1000.0
                if elapsed_ms >= timeout_ms:
                    return False
                _, writable, _ = select.select([], [fd], [], 0.001)
                if not writable:
                    continue
            except OSError:
                return False
        return True

    def broadcast(self, data: bytes) -> None:
        if not data:
            return
        self.accept_clients()

        now = time.monotonic()
        with self.lock:
            idx = 0
            while idx < len(self.clients):
                client = self.clients[idx]
                idle_ms = (now - client.last_time) * 1000.0
                if idle_ms >= SOCK_IDLE_TIMEOUT_MS:
                    self._close_client(idx, "idle timeout")
                    continue

                if self.allow_drops and client.last_size:
                    unsent = self._unsent_bytes(client.fd)
                    if unsent >= client.last_size:
                        client.num_dropped += 1
                        idx += 1
                        continue

                ok = self._send_with_timeout(client.fd, data, SOCK_WRITE_TIMEOUT_MS)
                if not ok:
                    self._close_client(idx, "write error")
                    continue

                client.last_size = len(data)
                client.last_time = now
                client.num_frames += 1

                if self.one_frame:
                    self._close_client(idx, "one frame sent")
                    continue

                idx += 1


class H264SocketWriter:
    def __init__(self, broadcaster: SocketBroadcaster):
        self.broadcaster = broadcaster
        self.total_bytes = 0
        self.total_chunks = 0

    def write(self, data: bytes) -> int:
        if not data:
            return 0
        self.broadcaster.broadcast(data)
        self.total_bytes += len(data)
        self.total_chunks += 1
        return len(data)

    def flush(self) -> None:
        return None

    def close(self) -> None:
        return None


def fcntl_ioctl_outq(fd: socket.socket) -> int:
    import fcntl

    raw = fcntl.ioctl(fd.fileno(), SIOCOUTQ, struct.pack("I", 0))
    return struct.unpack("I", raw)[0]


class JsonRpcError(Exception):
    def __init__(self, code: int, message: str):
        super().__init__(message)
        self.code = code
        self.message = message


@dataclass
class ControlSpec:
    alias: str
    original: str
    ctrl_type: str
    min_value: Optional[float]
    max_value: Optional[float]
    step: Optional[float]
    default: Any
    readonly: bool
    inactive: bool
    menu: List[Dict[str, Any]]


class PyCameraControlService:
    def __init__(self, picam2: Any, state_path: Optional[Path]):
        self.picam2 = picam2
        self.state_path = state_path
        self.lock = threading.Lock()
        self.controls = self._discover_controls()
        self.control_map = {ctrl.alias: ctrl for ctrl in self.controls}

    def _discover_controls(self) -> List[ControlSpec]:
        controls: List[ControlSpec] = []

        raw_map = getattr(self.picam2, "camera_controls", {})
        if not isinstance(raw_map, dict):
            raw_map = {}

        alias_seen: Dict[str, int] = {}

        for original_name, raw_desc in raw_map.items():
            base_alias = to_snake_case(str(original_name))
            alias = base_alias
            if alias in alias_seen:
                alias_seen[alias] += 1
                alias = f"{alias}_{alias_seen[base_alias]}"
            else:
                alias_seen[alias] = 1

            ctrl_type, min_val, max_val, default_val, menu, step = parse_control_descriptor(raw_desc)
            readonly = False
            inactive = False

            controls.append(
                ControlSpec(
                    alias=alias,
                    original=str(original_name),
                    ctrl_type=ctrl_type,
                    min_value=min_val,
                    max_value=max_val,
                    step=step,
                    default=default_val,
                    readonly=readonly,
                    inactive=inactive,
                    menu=menu,
                )
            )

        return sort_controls(controls)

    def _read_metadata(self) -> Dict[str, Any]:
        try:
            with self.lock:
                metadata = self.picam2.capture_metadata()
            if isinstance(metadata, dict):
                return metadata
        except Exception as exc:
            debug_log(f"capture_metadata failed: {exc}")
        return {}

    def list_controls(self) -> Dict[str, Any]:
        metadata = self._read_metadata()
        out: List[Dict[str, Any]] = []
        for ctrl in self.controls:
            value = metadata.get(ctrl.original, ctrl.default)
            normalized = normalize_control_value(ctrl.ctrl_type, value)
            default_norm = normalize_control_value(ctrl.ctrl_type, ctrl.default)
            min_norm = normalize_control_scalar(ctrl.ctrl_type, ctrl.min_value)
            max_norm = normalize_control_scalar(ctrl.ctrl_type, ctrl.max_value)
            step_norm = normalize_control_scalar(ctrl.ctrl_type, ctrl.step)

            out.append(
                {
                    "name": ctrl.alias,
                    "type": ctrl.ctrl_type,
                    "min": min_norm,
                    "max": max_norm,
                    "step": step_norm,
                    "default": default_norm,
                    "value": normalized,
                    "readonly": ctrl.readonly,
                    "inactive": ctrl.inactive,
                    "menu": ctrl.menu,
                }
            )
        return {"controls": out}

    def get_values(self, names: List[str]) -> Dict[str, Any]:
        metadata = self._read_metadata()
        values: Dict[str, Any] = {}
        missing: List[str] = []

        for name in names:
            ctrl = self.control_map.get(name)
            if not ctrl:
                missing.append(name)
                continue
            value = metadata.get(ctrl.original, ctrl.default)
            values[name] = normalize_control_value(ctrl.ctrl_type, value)

        if missing:
            raise JsonRpcError(-32602, f"Unknown controls: {', '.join(missing)}")

        return {"values": values}

    def set_values(self, values: Dict[str, Any]) -> Dict[str, Any]:
        if not isinstance(values, dict):
            raise JsonRpcError(-32602, "params.controls must be an object")

        validated: Dict[str, Any] = {}
        auto_first: Dict[str, Any] = {}
        remaining: Dict[str, Any] = {}

        for alias, value in values.items():
            ctrl = self.control_map.get(alias)
            if not ctrl:
                raise JsonRpcError(-32602, f"Unknown control: {alias}")
            if ctrl.readonly:
                continue
            coerced = coerce_control_value(ctrl, value)
            validated[alias] = coerced
            if alias in AUTO_FIRST_CONTROLS:
                auto_first[ctrl.original] = coerced
            else:
                remaining[ctrl.original] = coerced

        with self.lock:
            if auto_first:
                self.picam2.set_controls(auto_first)
            if remaining:
                self.picam2.set_controls(remaining)

        if self.state_path is not None:
            persisted = safe_json_load(self.state_path)
            persisted.update(validated)
            safe_json_save(self.state_path, persisted)

        return {"applied": validated}

    def reset_defaults(self) -> Dict[str, Any]:
        defaults: Dict[str, Any] = {}
        for ctrl in self.controls:
            if ctrl.readonly:
                continue
            if ctrl.default is None:
                continue
            defaults[ctrl.alias] = normalize_control_value(ctrl.ctrl_type, ctrl.default)

        succeeded: List[str] = []
        failed: List[Dict[str, str]] = []

        for alias, value in defaults.items():
            ctrl = self.control_map.get(alias)
            if not ctrl:
                continue
            try:
                with self.lock:
                    self.picam2.set_controls({ctrl.original: coerce_control_value(ctrl, value)})
                succeeded.append(alias)
            except Exception as exc:
                failed.append({"name": alias, "error": str(exc)})

        state_removed = False
        if self.state_path and self.state_path.exists():
            self.state_path.unlink()
            state_removed = True

        return {"succeeded": succeeded, "failed": failed, "state_removed": state_removed}

    def info(self) -> Dict[str, Any]:
        properties = getattr(self.picam2, "camera_properties", {})
        controls = getattr(self.picam2, "camera_controls", {})
        info_text = "\n".join(
            [
                "PyCamera2 camera info",
                f"properties: {properties}",
                f"controls: {list(controls.keys())}",
            ]
        )
        return {"info": info_text}

    def restore_state(self) -> None:
        if self.state_path is None:
            return
        saved = safe_json_load(self.state_path)
        if not saved:
            log("No persisted controls to restore")
            return

        valid: Dict[str, Any] = {}
        for alias, value in saved.items():
            ctrl = self.control_map.get(alias)
            if not ctrl or ctrl.readonly:
                continue
            try:
                valid[ctrl.original] = coerce_control_value(ctrl, value)
            except Exception as exc:
                log(f"Skipping persisted control {alias}: {exc}")

        if not valid:
            log("No valid persisted controls to apply")
            return

        with self.lock:
            self.picam2.set_controls(valid)
        log(f"Restored {len(valid)} controls from {self.state_path}")


class ControlRpcServer(threading.Thread):
    def __init__(self, service: PyCameraControlService, socket_path: str):
        super().__init__(daemon=True)
        self.service = service
        self.socket_path = socket_path
        self._stop_event = threading.Event()
        self.server_sock: Optional[socket.socket] = None

    def stop(self) -> None:
        self._stop_event.set()
        if self.server_sock:
            try:
                self.server_sock.close()
            except Exception:
                pass
        if os.path.exists(self.socket_path):
            try:
                os.unlink(self.socket_path)
            except Exception:
                pass

    def run(self) -> None:
        if os.path.exists(self.socket_path):
            os.unlink(self.socket_path)

        sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        sock.bind(self.socket_path)
        sock.listen(5)
        sock.settimeout(1.0)
        os.chmod(self.socket_path, 0o666)
        self.server_sock = sock
        log(f"Control JSON-RPC listening on {self.socket_path}")

        try:
            while not self._stop_event.is_set():
                try:
                    conn, _ = sock.accept()
                except socket.timeout:
                    continue
                except OSError:
                    break
                try:
                    self.handle_client(conn)
                finally:
                    try:
                        conn.close()
                    except Exception:
                        pass
        finally:
            try:
                sock.close()
            except Exception:
                pass
            if os.path.exists(self.socket_path):
                try:
                    os.unlink(self.socket_path)
                except Exception:
                    pass

    def handle_client(self, conn: socket.socket) -> None:
        data = b""
        while True:
            chunk = conn.recv(4096)
            if not chunk:
                break
            data += chunk
            if b"\n" in data:
                break

        if not data:
            return

        try:
            request = json.loads(data.decode().strip())
        except Exception as exc:
            response = {
                "jsonrpc": "2.0",
                "id": None,
                "error": {"code": -32700, "message": f"Parse error: {exc}"},
            }
            conn.sendall((json.dumps(response) + "\n").encode())
            return

        response = self.handle_rpc(request)
        if response is not None:
            conn.sendall((json.dumps(response) + "\n").encode())

    def handle_rpc(self, request: Dict[str, Any]) -> Optional[Dict[str, Any]]:
        method = request.get("method")
        req_id = request.get("id")
        params = request.get("params") or {}

        try:
            if request.get("jsonrpc") != "2.0":
                raise JsonRpcError(-32600, "Invalid JSON-RPC version")
            if method == "list":
                result = self.service.list_controls()
            elif method == "get":
                names = params.get("controls")
                if names is None:
                    names = [ctrl.alias for ctrl in self.service.controls]
                if not isinstance(names, list):
                    raise JsonRpcError(-32602, "controls must be a list")
                result = self.service.get_values(names)
            elif method == "set":
                controls = params.get("controls")
                if controls is None:
                    raise JsonRpcError(-32602, "params.controls is required")
                result = self.service.set_values(controls)
            elif method == "reset":
                result = self.service.reset_defaults()
            elif method == "info":
                result = self.service.info()
            else:
                raise JsonRpcError(-32601, f"Unknown method: {method}")

            if req_id is None:
                return None
            return {"jsonrpc": "2.0", "id": req_id, "result": result}

        except JsonRpcError as exc:
            return {
                "jsonrpc": "2.0",
                "id": req_id,
                "error": {"code": exc.code, "message": exc.message},
            }
        except Exception as exc:
            return {
                "jsonrpc": "2.0",
                "id": req_id,
                "error": {"code": -32000, "message": str(exc)},
            }


def parse_control_descriptor(raw_desc: Any) -> Tuple[str, Optional[float], Optional[float], Any, List[Dict[str, Any]], Optional[float]]:
    ctrl_type = "unknown"
    min_val: Optional[float] = None
    max_val: Optional[float] = None
    default_val: Any = None
    menu: List[Dict[str, Any]] = []
    step: Optional[float] = None

    if hasattr(raw_desc, "min") and hasattr(raw_desc, "max"):
        min_val = getattr(raw_desc, "min", None)
        max_val = getattr(raw_desc, "max", None)
        default_val = getattr(raw_desc, "default", None)
        step = getattr(raw_desc, "step", None)
    elif isinstance(raw_desc, (tuple, list)):
        if len(raw_desc) >= 3 and not isinstance(raw_desc[0], (list, tuple, dict, set)):
            min_val = raw_desc[0]
            max_val = raw_desc[1]
            default_val = raw_desc[2]
            if len(raw_desc) >= 4:
                step = raw_desc[3]
        elif len(raw_desc) >= 2 and isinstance(raw_desc[0], (list, tuple)):
            choices = list(raw_desc[0])
            default_val = raw_desc[1]
            menu = [{"value": idx, "label": str(label)} for idx, label in enumerate(choices)]
            min_val = 0
            max_val = max(0, len(choices) - 1)
    else:
        default_val = raw_desc

    if menu:
        ctrl_type = "menu"
        step = 1 if step is None else step
    elif isinstance(default_val, bool):
        ctrl_type = "bool"
        min_val = 0 if min_val is None else min_val
        max_val = 1 if max_val is None else max_val
        step = 1 if step is None else step
    elif isinstance(default_val, int) and not isinstance(default_val, bool):
        ctrl_type = "int"
        step = 1 if step is None else step
    elif isinstance(default_val, float):
        # UI currently supports int/bool/menu; expose float controls as int-ish when range is integral.
        if is_integral_number(min_val) and is_integral_number(max_val) and is_integral_number(default_val):
            ctrl_type = "int"
            step = 1 if step is None else step
        else:
            ctrl_type = "float"
    elif min_val is not None and max_val is not None:
        if is_integral_number(min_val) and is_integral_number(max_val):
            ctrl_type = "int"
            step = 1 if step is None else step
        else:
            ctrl_type = "float"

    return ctrl_type, to_float_or_none(min_val), to_float_or_none(max_val), default_val, menu, to_float_or_none(step)


def is_integral_number(value: Any) -> bool:
    if value is None:
        return False
    if isinstance(value, bool):
        return False
    if isinstance(value, int):
        return True
    if isinstance(value, float):
        return float(value).is_integer()
    return False


def to_float_or_none(value: Any) -> Optional[float]:
    if value is None:
        return None
    try:
        return float(value)
    except Exception:
        return None


def normalize_control_scalar(ctrl_type: str, value: Optional[float]) -> Optional[Any]:
    if value is None:
        return None
    if ctrl_type in ("int", "bool", "menu"):
        return int(round(value))
    return value


def normalize_control_value(ctrl_type: str, value: Any) -> Any:
    if ctrl_type == "bool":
        return int(bool(value))
    if ctrl_type in ("int", "menu"):
        try:
            return int(round(float(value)))
        except Exception:
            return value
    if ctrl_type == "float":
        try:
            return float(value)
        except Exception:
            return value
    return value


def sort_controls(controls: List[ControlSpec]) -> List[ControlSpec]:
    order_map = {name: idx for idx, name in enumerate(CONTROL_ORDER)}
    indexed = list(enumerate(controls))

    def sort_key(item: Tuple[int, ControlSpec]) -> Tuple[int, int]:
        idx, ctrl = item
        prio = order_map.get(ctrl.alias, len(CONTROL_ORDER))
        return (prio, idx)

    return [ctrl for _, ctrl in sorted(indexed, key=sort_key)]


def coerce_control_value(ctrl: ControlSpec, value: Any) -> Any:
    if ctrl.ctrl_type == "bool":
        if isinstance(value, bool):
            return bool(value)
        if isinstance(value, (int, float)):
            return bool(int(value))
        raise JsonRpcError(-32602, f"Value for {ctrl.alias} must be bool/int")

    if ctrl.ctrl_type in ("int", "menu"):
        if isinstance(value, bool):
            value = int(value)
        if not isinstance(value, (int, float)):
            raise JsonRpcError(-32602, f"Value for {ctrl.alias} must be numeric")
        parsed = int(round(float(value)))
        if ctrl.min_value is not None and parsed < int(round(ctrl.min_value)):
            raise JsonRpcError(-32602, f"{ctrl.alias}={parsed} below min {int(round(ctrl.min_value))}")
        if ctrl.max_value is not None and parsed > int(round(ctrl.max_value)):
            raise JsonRpcError(-32602, f"{ctrl.alias}={parsed} above max {int(round(ctrl.max_value))}")
        return parsed

    if ctrl.ctrl_type == "float":
        if isinstance(value, bool):
            value = float(int(value))
        if not isinstance(value, (int, float)):
            raise JsonRpcError(-32602, f"Value for {ctrl.alias} must be numeric")
        parsedf = float(value)
        if ctrl.min_value is not None and parsedf < ctrl.min_value:
            raise JsonRpcError(-32602, f"{ctrl.alias}={parsedf} below min {ctrl.min_value}")
        if ctrl.max_value is not None and parsedf > ctrl.max_value:
            raise JsonRpcError(-32602, f"{ctrl.alias}={parsedf} above max {ctrl.max_value}")
        return parsedf

    return value


class JpegGrabber:
    def __init__(self, picam2: Any, quality: int):
        self.picam2 = picam2
        self.quality = quality
        self.mode: Optional[str] = None

    def capture(self) -> bytes:
        modes = ["capture_file_bytesio", "capture_request", "capture_file_tmp"]
        if self.mode and self.mode in modes:
            modes.remove(self.mode)
            modes.insert(0, self.mode)

        for mode in modes:
            try:
                if mode == "capture_file_bytesio":
                    data = self._capture_file_bytesio()
                elif mode == "capture_request":
                    data = self._capture_request()
                else:
                    data = self._capture_file_tmp()
                if data:
                    self.mode = mode
                    return data
            except Exception as exc:
                debug_log(f"JPEG mode {mode} failed: {exc}")
                continue

        raise RuntimeError("Failed to capture JPEG frame")

    def _capture_file_bytesio(self) -> bytes:
        buf = io.BytesIO()
        try:
            self.picam2.capture_file(buf, format="jpeg", quality=self.quality)
        except TypeError:
            self.picam2.capture_file(buf, format="jpeg")
        return buf.getvalue()

    def _capture_request(self) -> bytes:
        req = self.picam2.capture_request()
        try:
            buf = io.BytesIO()
            try:
                req.save("main", buf, format="jpeg", quality=self.quality)
            except TypeError:
                req.save("main", buf, format="jpeg")
            return buf.getvalue()
        finally:
            req.release()

    def _capture_file_tmp(self) -> bytes:
        fd, tmp = tempfile.mkstemp(suffix=".jpg", prefix="capture-pycamera2-")
        os.close(fd)
        try:
            try:
                self.picam2.capture_file(tmp, format="jpeg", quality=self.quality)
            except TypeError:
                self.picam2.capture_file(tmp, format="jpeg")
            with open(tmp, "rb") as fp:
                return fp.read()
        finally:
            try:
                os.unlink(tmp)
            except Exception:
                pass


def request_keyframe_best_effort(picam2: Any, h264_encoder: Any) -> bool:
    for obj in (h264_encoder, picam2):
        if obj is None:
            continue
        for attr in ("request_key_frame", "force_key_frame", "request_keyframe"):
            fn = getattr(obj, attr, None)
            if callable(fn):
                try:
                    fn()
                    return True
                except Exception as exc:
                    debug_log(f"Keyframe request via {attr} failed: {exc}")
    return False


def create_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="PyCamera2 capture with JPEG/H264 sockets and integrated controls")
    parser.add_argument("--camera", type=int, help="PyCamera2 camera index")
    parser.add_argument("--device", default="/dev/video0", help="Compatibility option: /dev/videoN maps to camera index N")
    parser.add_argument("--width", type=int, default=1920, help="Video width")
    parser.add_argument("--height", type=int, default=1080, help="Video height")
    parser.add_argument("--fps", type=int, default=30, help="Frames per second")
    parser.add_argument("--output", help="JPEG output path (optional)")
    parser.add_argument("--jpeg-quality", type=int, default=90, help="JPEG quality (1-100)")
    parser.add_argument("--jpeg-sock", help="JPEG snapshot socket path (optional)")
    parser.add_argument("--mjpeg-sock", help="MJPEG stream socket path (optional)")
    parser.add_argument("--h264-sock", help="H264 stream socket path (optional)")
    parser.add_argument("--h264-bitrate", type=int, default=2000, help="H264 bitrate in kbps")
    parser.add_argument("--control-sock", help="Control JSON-RPC socket path (optional)")
    parser.add_argument("--state-file", help="Optional control state JSON file")
    parser.add_argument("--idle", type=int, default=1000, help="Idle sleep in ms when no JPEG readers")
    parser.add_argument("--debug", action="store_true", help="Enable debug output")
    return parser


def configure_camera(picam2: Any, width: int, height: int, fps: int) -> None:
    try:
        config = picam2.create_video_configuration(
            main={"size": (width, height)},
            controls={"FrameRate": fps},
        )
    except Exception:
        config = picam2.create_video_configuration(main={"size": (width, height)})
    picam2.configure(config)


def start_h264_if_requested(picam2: Any, args: argparse.Namespace, h264_sock: SocketBroadcaster) -> Tuple[Any, Any, bool]:
    if not args.h264_sock:
        picam2.start()
        return None, None, False

    bitrate_bps = max(1, int(args.h264_bitrate)) * 1000
    encoder = None

    if H264Encoder is None:
        raise RuntimeError("picamera2 H264Encoder is unavailable")

    try:
        encoder = H264Encoder(bitrate=bitrate_bps)
    except TypeError:
        encoder = H264Encoder(bitrate_bps)

    for attr, value in (("repeat", True), ("iperiod", max(1, int(args.fps))), ("framerate", int(args.fps))):
        if hasattr(encoder, attr):
            try:
                setattr(encoder, attr, value)
            except Exception:
                pass

    writer = H264SocketWriter(h264_sock)
    output_target: Any
    if FileOutput is not None:
        output_target = FileOutput(writer)
    else:
        output_target = writer

    recording_started = False

    try:
        picam2.start_recording(encoder, output_target)
        recording_started = True
        log(f"H264 encoder started ({args.h264_bitrate} kbps)")
        return encoder, writer, recording_started
    except Exception as exc:
        debug_log(f"start_recording failed: {exc}")

    picam2.start()
    try:
        picam2.start_encoder(encoder, output_target)
        recording_started = True
        log(f"H264 encoder started via start_encoder ({args.h264_bitrate} kbps)")
    except Exception as exc:
        debug_log(f"start_encoder failed: {exc}")
        raise RuntimeError(f"Failed to start H264 encoder: {exc}")

    return encoder, writer, recording_started


def stop_camera_and_encoder(picam2: Any, encoder: Any, recording_started: bool) -> None:
    if encoder is not None:
        if recording_started:
            try:
                picam2.stop_recording()
                return
            except Exception:
                pass
        try:
            picam2.stop_encoder(encoder)
        except Exception:
            pass
    try:
        picam2.stop()
    except Exception:
        pass


def main() -> int:
    global DEBUG

    parser = create_parser()
    args = parser.parse_args()
    DEBUG = args.debug

    signal.signal(signal.SIGINT, signal_handler)
    signal.signal(signal.SIGTERM, signal_handler)

    if Picamera2 is None:
        log("Error: picamera2 is not installed. Install python3-picamera2 on Raspberry Pi OS.")
        return 2

    camera_index = parse_camera_index(args.camera, args.device)
    log("capture-pycamera2 starting")
    log(f"Camera index: {camera_index}")
    log(f"Resolution: {args.width}x{args.height}")
    log(f"FPS: {args.fps}")
    if args.output:
        log(f"JPEG output: {args.output}")
    if args.jpeg_sock:
        log(f"JPEG snapshot socket: {args.jpeg_sock}")
    if args.mjpeg_sock:
        log(f"MJPEG socket: {args.mjpeg_sock}")
    if args.h264_sock:
        log(f"H264 socket: {args.h264_sock}")
    if args.control_sock:
        log(f"Control socket: {args.control_sock}")

    jpeg_snapshot_sock = SocketBroadcaster(args.jpeg_sock, one_frame=True, allow_drops=False)
    mjpeg_sock = SocketBroadcaster(args.mjpeg_sock, one_frame=False, allow_drops=True)
    h264_sock = SocketBroadcaster(args.h264_sock, one_frame=False, allow_drops=False)

    for ctx in (jpeg_snapshot_sock, mjpeg_sock, h264_sock):
        ctx.open()

    picam2 = Picamera2(camera_index)
    configure_camera(picam2, args.width, args.height, args.fps)

    state_path = Path(args.state_file) if args.state_file else None
    control_service = PyCameraControlService(picam2, state_path)
    control_server: Optional[ControlRpcServer] = None
    if args.control_sock:
        control_server = ControlRpcServer(control_service, args.control_sock)
        control_server.start()

    try:
        if state_path:
            control_service.restore_state()

        h264_encoder, h264_writer, recording_started = start_h264_if_requested(picam2, args, h264_sock)

        jpeg_grabber = JpegGrabber(picam2, max(1, min(int(args.jpeg_quality), 100)))

        frame_interval = 1.0 / max(1, int(args.fps))
        last_frame_time = 0.0
        stats_last = time.monotonic()
        jpeg_frames = 0

        while RUNNING:
            jpeg_snapshot_sock.accept_clients()
            mjpeg_sock.accept_clients()
            h264_sock.accept_clients()

            if h264_sock.consume_need_keyframe():
                if request_keyframe_best_effort(picam2, h264_encoder):
                    debug_log("Requested H264 keyframe for new client")

            has_jpeg_demand = bool(args.output) or jpeg_snapshot_sock.has_clients() or mjpeg_sock.has_clients()
            if not has_jpeg_demand:
                time.sleep(max(0.001, args.idle / 1000.0))
                continue

            now = time.monotonic()
            elapsed = now - last_frame_time
            if elapsed < frame_interval:
                time.sleep(frame_interval - elapsed)

            try:
                jpeg_data = jpeg_grabber.capture()
            except Exception as exc:
                log(f"JPEG capture failed: {exc}")
                time.sleep(0.1)
                continue

            if args.output:
                try:
                    write_output_rename(args.output, jpeg_data)
                except Exception as exc:
                    log(f"JPEG output write failed: {exc}")

            jpeg_snapshot_sock.broadcast(jpeg_data)
            mjpeg_sock.broadcast(jpeg_data)

            jpeg_frames += 1
            last_frame_time = time.monotonic()

            if last_frame_time - stats_last >= 1.0:
                h264_kb = 0
                if h264_writer is not None:
                    h264_kb = h264_writer.total_bytes // 1024
                log(
                    "FPS: "
                    f"{jpeg_frames} (JPEG) "
                    f"clients JPEG={len(jpeg_snapshot_sock.clients)} "
                    f"MJPEG={len(mjpeg_sock.clients)} "
                    f"H264={len(h264_sock.clients)} "
                    f"H264 sent={h264_kb}KB"
                )
                jpeg_frames = 0
                stats_last = last_frame_time

    except Exception as exc:
        log(f"Fatal error: {exc}")
        return 1
    finally:
        if control_server is not None:
            control_server.stop()
            control_server.join(timeout=2.0)

        try:
            stop_camera_and_encoder(picam2, locals().get("h264_encoder"), locals().get("recording_started", False))
        except Exception as exc:
            debug_log(f"Camera stop error: {exc}")

        try:
            picam2.close()
        except Exception:
            pass

        for ctx in (h264_sock, mjpeg_sock, jpeg_snapshot_sock):
            ctx.close()

        log("capture-pycamera2 stopped")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
