#!/usr/bin/env python3
"""
Standalone V4L2 controls JSON-RPC service.

Usage example:
  python3 apps/v4l2-ctrls/v4l2-ctrls.py --device /dev/video11 --socket /tmp/v4l2-ctrls.sock
"""

import argparse
import json
import os
import socket
import subprocess
import time
from pathlib import Path
from typing import Dict, List, Optional, Tuple

CONTROL_ORDER = [
    "focus_auto",
    "focus_automatic_continuous",
    "focus_absolute",
    "exposure_auto",
    "exposure_absolute",
    "exposure_time_absolute",
    "white_balance_temperature_auto",
    "white_balance_temperature",
    "brightness",
    "contrast",
    "saturation",
    "sharpness",
    "gain",
]

AUTO_FIRST_CONTROLS = {
    "exposure_auto",
    "white_balance_temperature_auto",
    "focus_auto",
    "focus_automatic_continuous",
}


def log(msg: str) -> None:
    ts = time.strftime("%H:%M:%S")
    print(f"[{ts}] {msg}", flush=True)


def run_v4l2(args: List[str], timeout: float = 3.0) -> Tuple[int, str, str]:
    try:
        result = subprocess.run(
            args,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            check=False,
            timeout=timeout,
        )
        return result.returncode, result.stdout, result.stderr
    except subprocess.TimeoutExpired as exc:
        return 124, "", f"Timeout running {' '.join(args)}: {exc}"


def normalize_type(ctrl_type: Optional[str]) -> str:
    if not ctrl_type:
        return "unknown"
    if ctrl_type == "bool":
        return "bool"
    if ctrl_type.startswith("int"):
        return "int"
    if ctrl_type == "menu":
        return "menu"
    return ctrl_type


def get_int_from_parts(parts: List[str], field: str) -> Optional[int]:
    token = next((p for p in parts if p.startswith(f"{field}=")), None)
    if not token:
        return None
    try:
        return int(token.split("=", 1)[1])
    except ValueError:
        return None


def parse_ctrls(output: str) -> List[Dict]:
    controls = []
    for line in output.splitlines():
        line = line.strip()
        if not line or line.startswith("Error"):
            continue

        if "0x" not in line:
            continue

        parts = line.split()
        if not parts:
            continue
        name = parts[0]
        type_start = line.find("(")
        type_end = line.find(")")
        ctrl_type = None
        if type_start != -1 and type_end != -1:
            ctrl_type = line[type_start + 1 : type_end].strip()
        controls.append(
            {
                "name": name,
                "type": normalize_type(ctrl_type),
                "min": get_int_from_parts(parts, "min"),
                "max": get_int_from_parts(parts, "max"),
                "step": get_int_from_parts(parts, "step"),
                "value": get_int_from_parts(parts, "value"),
                "menu": [],
            }
        )
    return controls


def parse_ctrl_menus(output: str) -> Dict[str, List[Dict[str, str]]]:
    """Parse v4l2-ctl --list-ctrls-menus output."""
    menus: Dict[str, List[Dict[str, str]]] = {}
    current = None

    for line in output.splitlines():
        if not line.strip():
            continue

        stripped = line.strip()

        if stripped in ["User Controls", "Camera Controls", "Video Controls", "Image Controls"]:
            continue

        if stripped and stripped[0].isdigit() and ":" in stripped:
            if current is None:
                continue
            parts = stripped.split(":", 1)
            try:
                value = int(parts[0].strip())
                label = parts[1].strip()
                menus[current].append({"value": value, "label": label})
            except (ValueError, IndexError):
                continue
        elif "0x" in stripped:
            name = stripped.split()[0]
            current = name
            menus.setdefault(current, [])

    return menus


def sort_controls(controls: List[Dict]) -> List[Dict]:
    order_map = {name: idx for idx, name in enumerate(CONTROL_ORDER)}
    indexed = list(enumerate(controls))

    def sort_key(item: Tuple[int, Dict]) -> Tuple[int, int]:
        original_idx, ctrl = item
        idx = order_map.get(ctrl["name"], len(CONTROL_ORDER))
        return (idx, original_idx)

    return [ctrl for _, ctrl in sorted(indexed, key=sort_key)]


def fetch_controls(device: str) -> List[Dict]:
    code1, out1, err1 = run_v4l2(["v4l2-ctl", "-d", device, "--list-ctrls"])
    if code1 != 0:
        raise RuntimeError(err1 or out1 or "Failed to list controls")
    controls = parse_ctrls(out1)

    code2, out2, err2 = run_v4l2(["v4l2-ctl", "-d", device, "--list-ctrls-menus"])
    if code2 == 0:
        menus = parse_ctrl_menus(out2)
        for ctrl in controls:
            ctrl_name = ctrl["name"]
            if ctrl_name in menus and menus[ctrl_name]:
                ctrl["menu"] = menus[ctrl_name]
                ctrl["type"] = "menu"
    else:
        log(f"Failed to get menus: code={code2}, err={err2}")

    return sort_controls(controls)


def validate_values(values: Dict[str, int], controls: List[Dict]) -> Dict[str, int]:
    allowlist = {ctrl["name"] for ctrl in controls}
    control_map = {ctrl["name"]: ctrl for ctrl in controls}
    validated: Dict[str, int] = {}

    for key, value in values.items():
        if key not in allowlist:
            raise ValueError(f"Unknown control: {key}")
        if isinstance(value, bool):
            value = int(value)
        if not isinstance(value, int):
            raise ValueError(f"Value for {key} must be integer")
        ctrl_def = control_map.get(key)
        if ctrl_def:
            min_val = ctrl_def.get("min")
            max_val = ctrl_def.get("max")
            if min_val is not None and max_val is not None:
                if not (min_val <= value <= max_val):
                    raise ValueError(f"{key}={value} out of range [{min_val}, {max_val}]")
        validated[key] = value

    return validated


def apply_controls(device: str, values: Dict[str, int]) -> Tuple[bool, str, str, int]:
    if not values:
        return True, "", "", 0
    set_parts = [f"{key}={value}" for key, value in values.items()]
    cmd = ["v4l2-ctl", "-d", device, f"--set-ctrl={','.join(set_parts)}"]
    code, out, err = run_v4l2(cmd)
    return code == 0, out, err, code


def split_controls_by_precedence(values: Dict[str, int]) -> Tuple[Dict[str, int], Dict[str, int]]:
    auto_first = {key: value for key, value in values.items() if key in AUTO_FIRST_CONTROLS}
    remaining = {key: value for key, value in values.items() if key not in AUTO_FIRST_CONTROLS}
    return auto_first, remaining


def read_device_info(device: str) -> str:
    code, out, err = run_v4l2(["v4l2-ctl", "-d", device, "-D"])
    if code != 0:
        raise RuntimeError(err or out or "Failed to fetch device info")
    return out


def default_state_file(device: str) -> Path:
    base = os.path.basename(device).replace("/", "_") or "device"
    state_home = os.getenv("XDG_STATE_HOME")
    if not state_home:
        state_home = os.path.join(Path.home(), ".local", "state")
    state_dir = Path(state_home) / "v4l2-ctrls"
    state_dir.mkdir(parents=True, exist_ok=True)
    return state_dir / f"{base}.json"


def load_state(path: Path) -> Dict[str, int]:
    if not path.exists():
        return {}
    try:
        data = json.loads(path.read_text())
        if isinstance(data, dict):
            return {k: int(v) for k, v in data.items() if isinstance(k, str)}
    except Exception as exc:
        log(f"Failed to load state from {path}: {exc}")
    return {}


def save_state(path: Path, values: Dict[str, int]) -> None:
    tmp_path = path.with_suffix(path.suffix + ".tmp")
    tmp_path.write_text(json.dumps(values, indent=2, sort_keys=True))
    os.replace(tmp_path, path)


def restore_state(device: str, path: Path) -> None:
    saved = load_state(path)
    if not saved:
        log("No persisted controls to restore")
        return
    controls = fetch_controls(device)
    try:
        validated = validate_values(saved, controls)
    except ValueError as exc:
        log(f"State validation error: {exc}")
        validated = {}
    if not validated:
        log("No valid persisted controls to apply")
        return
    auto_first, remaining = split_controls_by_precedence(validated)
    ok, out, err, code = apply_controls(device, auto_first)
    if not ok:
        log(f"Failed to restore auto controls: code={code}, err={err or out}")
        return
    ok, out, err, code = apply_controls(device, remaining)
    if ok:
        log(f"Restored {len(validated)} controls from {path}")
    else:
        log(f"Failed to restore controls: code={code}, err={err or out}")


class JsonRpcError(Exception):
    def __init__(self, code: int, message: str):
        super().__init__(message)
        self.code = code
        self.message = message


def handle_rpc(device: str, state_path: Path, request: Dict) -> Optional[Dict]:
    if request.get("jsonrpc") != "2.0":
        raise JsonRpcError(-32600, "Invalid JSON-RPC version")
    method = request.get("method")
    request_id = request.get("id")
    params = request.get("params") or {}
    if not method:
        raise JsonRpcError(-32600, "Missing method")

    if method == "list":
        controls = fetch_controls(device)
        result = {"controls": controls}
    elif method == "get":
        controls = fetch_controls(device)
        names = params.get("controls")
        if names is None:
            names = [ctrl["name"] for ctrl in controls]
        if not isinstance(names, list):
            raise JsonRpcError(-32602, "controls must be a list")
        values = {ctrl["name"]: ctrl.get("value") for ctrl in controls if ctrl["name"] in names}
        missing = [name for name in names if name not in values]
        if missing:
            raise JsonRpcError(-32602, f"Unknown controls: {', '.join(missing)}")
        result = {"values": values}
    elif method == "set":
        if not isinstance(params, dict) or "controls" not in params:
            raise JsonRpcError(-32602, "params.controls is required")
        if not isinstance(params["controls"], dict):
            raise JsonRpcError(-32602, "params.controls must be an object")
        controls = fetch_controls(device)
        validated = validate_values(params["controls"], controls)
        auto_first, remaining = split_controls_by_precedence(validated)
        ok, out, err, code = apply_controls(device, auto_first)
        if not ok:
            raise JsonRpcError(-32000, err or out or f"Failed to set auto controls (code {code})")
        ok, out, err, code = apply_controls(device, remaining)
        if not ok:
            raise JsonRpcError(-32000, err or out or f"Failed to set controls (code {code})")
        persisted = load_state(state_path)
        persisted.update(validated)
        save_state(state_path, persisted)
        result = {"applied": validated, "stdout": out}
    elif method == "info":
        info = read_device_info(device)
        result = {"info": info}
    else:
        raise JsonRpcError(-32601, f"Unknown method: {method}")

    if request_id is None:
        return None
    return {"jsonrpc": "2.0", "id": request_id, "result": result}


def serve_socket(device: str, socket_path: str, state_path: Path) -> None:
    if os.path.exists(socket_path):
        os.unlink(socket_path)

    sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    sock.bind(socket_path)
    sock.listen(5)
    os.chmod(socket_path, 0o666)

    log(f"JSON-RPC socket listening on {socket_path}")

    try:
        while True:
            conn, _ = sock.accept()
            try:
                data = b""
                while True:
                    chunk = conn.recv(4096)
                    if not chunk:
                        break
                    data += chunk
                    if b"\n" in data:
                        break

                if data:
                    request = json.loads(data.decode().strip())
                    try:
                        response = handle_rpc(device, state_path, request)
                    except JsonRpcError as exc:
                        response = {
                            "jsonrpc": "2.0",
                            "id": request.get("id"),
                            "error": {"code": exc.code, "message": exc.message},
                        }
                    except Exception as exc:
                        response = {
                            "jsonrpc": "2.0",
                            "id": request.get("id"),
                            "error": {"code": -32000, "message": str(exc)},
                        }
                    if response is not None:
                        conn.sendall((json.dumps(response) + "\n").encode())
            except Exception as exc:
                log(f"Socket request error: {exc}")
            finally:
                conn.close()
    finally:
        sock.close()
        if os.path.exists(socket_path):
            os.unlink(socket_path)


def main() -> None:
    parser = argparse.ArgumentParser(description="V4L2 control JSON-RPC service")
    parser.add_argument("--device", required=True, help="V4L2 device path")
    parser.add_argument("--socket", required=True, help="Unix socket path")
    parser.add_argument("--state-file", help="Optional path to persist control state")
    args = parser.parse_args()

    state_path = Path(args.state_file) if args.state_file else default_state_file(args.device)

    log(f"Starting v4l2-ctrls for {args.device}")
    log(f"Persisted state: {state_path}")

    try:
        restore_state(args.device, state_path)
    except Exception as exc:
        log(f"Failed to restore persisted state: {exc}")

    serve_socket(args.device, args.socket, state_path)


if __name__ == "__main__":
    main()
