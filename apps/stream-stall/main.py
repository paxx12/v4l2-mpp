#!/usr/bin/env python3

import socket
import argparse
import time

def log(msg):
    ts = time.strftime('%H:%M:%S')
    print(f"[{ts}] {msg}", flush=True)

def main():
    parser = argparse.ArgumentParser(description='V4L2-MPP Stream Stall - Stalls socket to trigger errors')
    parser.add_argument('--sock', required=True, help='Unix socket path to connect to')
    parser.add_argument('--stall-duration', type=int, default=60, help='Duration to stall in seconds (default: 60)')
    parser.add_argument('--read-size', type=int, default=65536, help='Read buffer size (default: 65536)')
    parser.add_argument('--read-once', action='store_true', help='Read once then stall indefinitely')
    args = parser.parse_args()

    log(f"Connecting to socket: {args.sock}")

    try:
        sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        sock.connect(args.sock)
        log("Connected successfully")

        if args.read_once:
            log(f"Reading once with buffer size {args.read_size}...")
            chunk = sock.recv(args.read_size)
            log(f"Read {len(chunk)} bytes")
            log(f"Stalling indefinitely (socket remains open but not reading)...")
            while True:
                time.sleep(1)
        else:
            log(f"Starting read loop with {args.stall_duration}s stall between reads...")
            iteration = 0
            while True:
                iteration += 1
                log(f"Iteration {iteration}: Reading {args.read_size} bytes...")
                chunk = sock.recv(args.read_size)
                if not chunk:
                    log("Socket closed by peer (received empty chunk)")
                    break
                log(f"Read {len(chunk)} bytes")
                log(f"Stalling for {args.stall_duration} seconds...")
                time.sleep(args.stall_duration)

    except FileNotFoundError:
        log(f"Error: Socket not found: {args.sock}")
    except ConnectionRefusedError:
        log(f"Error: Connection refused to: {args.sock}")
    except BrokenPipeError:
        log("Error: Broken pipe - socket closed by peer")
    except IOError as e:
        log(f"Error: I/O error: {e}")
    except KeyboardInterrupt:
        log("Interrupted by user")
    finally:
        try:
            sock.close()
            log("Socket closed")
        except:
            pass

if __name__ == '__main__':
    main()
