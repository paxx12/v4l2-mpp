# control-v4l2

Standalone JSON-RPC service for managing V4L2 camera controls.

## Features

- JSON-RPC 2.0 control API (`list`, `get`, `set`, `info`)
- Single-device focus with explicit `--device`
- Control settings persistence and automatic restore on startup
- Menu-aware control metadata

## Requirements

- Python 3
- `v4l2-ctl` available in `PATH`

## Usage

```sh
python3 control-v4l2.py --device /dev/video11 --sock /tmp/control-v4l2.sock
```

**Optional state file:**
```sh
python3 control-v4l2.py \
  --device /dev/video11 \
  --sock /tmp/control-v4l2.sock \
  --state-file /var/lib/control-v4l2/video11.json
```

## Command-line Options

- `--device <path>` - V4L2 device path (required)
- `--sock <path>` - Unix socket path to expose JSON-RPC (required)
- `--state-file <path>` - Optional path to persist control state

## JSON-RPC API

All requests are line-delimited JSON objects over the Unix socket and follow JSON-RPC 2.0.

### list
Returns all controls and metadata, including current values.
```json
{"jsonrpc":"2.0","id":1,"method":"list"}
```

### get
Returns current values for a subset of controls.
```json
{"jsonrpc":"2.0","id":2,"method":"get","params":{"controls":["brightness","contrast"]}}
```

### set
Applies control updates and persists them.
```json
{"jsonrpc":"2.0","id":3,"method":"set","params":{"controls":{"brightness":128,"contrast":32}}}
```

### info
Returns device information from `v4l2-ctl -D`.
```json
{"jsonrpc":"2.0","id":4,"method":"info"}
```

## Integration

Pair `control-v4l2` with `stream-http` by passing `--control-sock` to the HTTP server. The UI is served by `stream-http` under `/control/`.

## Notes

- Persisted controls are restored on startup after validating against the current device controls.
- The default persistence location is `$XDG_STATE_HOME/control-v4l2/<device>.json` (or `~/.local/state/control-v4l2/` if unset).
- Auto/manual mode controls (exposure/focus/white-balance) are applied before dependent controls to avoid precedence issues.
