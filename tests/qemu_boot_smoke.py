#!/usr/bin/env python3
from __future__ import annotations

import argparse
import pathlib
import socket
import subprocess
import sys
import time

MARKER = b"ZENOVOS_BOOT_OK"


def wait_for(path: pathlib.Path, marker: bytes, deadline: float) -> bytes:
    data = b""
    while time.monotonic() < deadline:
        if path.exists():
            data = path.read_bytes()
            if marker in data:
                return data
        time.sleep(0.05)
    raise RuntimeError(f"serial marker {marker!r} not found; serial={data!r}")


def monitor_command(socket_path: pathlib.Path, command: str) -> None:
    deadline = time.monotonic() + 3.0
    while time.monotonic() < deadline and not socket_path.exists():
        time.sleep(0.05)
    if not socket_path.exists():
        raise RuntimeError("QEMU monitor socket was not created")
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as client:
        client.connect(str(socket_path))
        client.sendall(command.encode("ascii") + b"\n")
        time.sleep(0.15)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--qemu", default="qemu-system-i386")
    parser.add_argument("--image", type=pathlib.Path, required=True)
    parser.add_argument("--out", type=pathlib.Path, required=True)
    parser.add_argument("--timeout", type=float, default=8.0)
    args = parser.parse_args()

    args.out.mkdir(parents=True, exist_ok=True)
    serial_log = args.out / "serial.log"
    monitor_socket = args.out / "monitor.sock"
    screenshot = args.out / "screenshot.ppm"
    for path in (serial_log, monitor_socket, screenshot):
        try:
            path.unlink()
        except FileNotFoundError:
            pass

    cmd = [
        args.qemu,
        "-drive", f"file={args.image},format=raw,if=floppy",
        "-boot", "a",
        "-m", "16M",
        "-display", "none",
        "-serial", f"file:{serial_log}",
        "-monitor", f"unix:{monitor_socket},server=on,wait=off",
        "-no-reboot",
        "-no-shutdown",
    ]
    process = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    try:
        serial = wait_for(serial_log, MARKER, time.monotonic() + args.timeout)
        monitor_command(monitor_socket, f"screendump {screenshot}")
        deadline = time.monotonic() + 3.0
        while time.monotonic() < deadline and not screenshot.exists():
            time.sleep(0.05)
        if not screenshot.exists() or screenshot.stat().st_size == 0:
            raise RuntimeError("QEMU screendump was not produced")
        print(serial.decode("ascii", errors="replace"), end="")
        print(f"qemu boot smoke: OK ({screenshot})")
        return 0
    except Exception as exc:
        stderr = b""
        if process.stderr:
            try:
                stderr = process.stderr.read1(8192)
            except Exception:
                pass
        print(f"qemu boot smoke: FAILED: {exc}", file=sys.stderr)
        if stderr:
            print(stderr.decode(errors="replace"), file=sys.stderr)
        return 1
    finally:
        try:
            monitor_command(monitor_socket, "quit")
        except Exception:
            process.terminate()
        try:
            process.wait(timeout=3)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=3)
        try:
            monitor_socket.unlink()
        except FileNotFoundError:
            pass


if __name__ == "__main__":
    raise SystemExit(main())
