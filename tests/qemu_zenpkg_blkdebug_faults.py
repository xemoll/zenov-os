#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import shutil
import socket
import subprocess
import sys
import time
from typing import Any

PROMPT = "zenov> "


class QmpClient:
    def __init__(self, path: Path, timeout: float = 20.0) -> None:
        deadline = time.monotonic() + timeout
        self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        while True:
            try:
                self.sock.connect(str(path))
                break
            except (FileNotFoundError, ConnectionRefusedError):
                if time.monotonic() >= deadline:
                    raise TimeoutError(f"QMP socket did not become ready: {path}")
                time.sleep(0.05)
        self.sock.settimeout(0.25)
        self.buffer = b""
        self.events: list[dict[str, Any]] = []
        greeting = self._read_message(deadline)
        if "QMP" not in greeting:
            raise RuntimeError(f"invalid QMP greeting: {greeting}")
        self.command("qmp_capabilities")

    def close(self) -> None:
        self.sock.close()

    def _read_message(self, deadline: float) -> dict[str, Any]:
        while True:
            newline = self.buffer.find(b"\n")
            if newline >= 0:
                raw = self.buffer[:newline].strip()
                self.buffer = self.buffer[newline + 1 :]
                if raw:
                    return json.loads(raw.decode("utf-8"))
                continue
            if time.monotonic() >= deadline:
                raise TimeoutError("timed out waiting for QMP message")
            try:
                chunk = self.sock.recv(65536)
            except socket.timeout:
                continue
            if not chunk:
                raise ConnectionError("QMP connection closed")
            self.buffer += chunk

    def command(self, execute: str, arguments: dict[str, Any] | None = None,
                timeout: float = 10.0) -> dict[str, Any]:
        request_id = f"cmd-{time.monotonic_ns()}"
        request: dict[str, Any] = {"execute": execute, "id": request_id}
        if arguments is not None:
            request["arguments"] = arguments
        self.sock.sendall((json.dumps(request, separators=(",", ":")) + "\r\n").encode("utf-8"))
        deadline = time.monotonic() + timeout
        while True:
            message = self._read_message(deadline)
            if "event" in message:
                self.events.append(message)
                continue
            if message.get("id") != request_id:
                continue
            if "error" in message:
                raise RuntimeError(f"QMP command failed: {message['error']}")
            return message

    def hmp(self, command_line: str) -> None:
        self.command("human-monitor-command", {"command-line": command_line})

    def wait_event(self, name: str, timeout: float = 60.0) -> dict[str, Any]:
        for index, event in enumerate(self.events):
            if event.get("event") == name:
                return self.events.pop(index)
        deadline = time.monotonic() + timeout
        while True:
            message = self._read_message(deadline)
            if message.get("event") == name:
                return message
            if "event" in message:
                self.events.append(message)


def wait_for_text(path: Path, text: str, timeout: float = 60.0) -> None:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if path.exists() and text in path.read_text(encoding="utf-8", errors="replace"):
            return
        time.sleep(0.05)
    raise TimeoutError(f"missing serial marker {text!r} in {path}")


def contains(path: Path, text: str) -> bool:
    return path.exists() and text in path.read_text(encoding="utf-8", errors="replace")


def send_text(qmp: QmpClient, text: str) -> None:
    keymap = {" ": "spc", ".": "dot", "-": "minus", "_": "shift-minus", "/": "slash"}
    for char in text:
        if char.islower() or char.isdigit():
            key = char
        elif char.isupper():
            key = f"shift-{char.lower()}"
        else:
            key = keymap.get(char)
            if key is None:
                raise ValueError(f"unsupported QEMU key: {char!r}")
        qmp.hmp(f"sendkey {key} 10")
        time.sleep(0.008)


def send_command(qmp: QmpClient, command: str) -> None:
    send_text(qmp, command)
    qmp.hmp("sendkey ret 10")


def read_env(path: Path) -> dict[str, int]:
    values: dict[str, int] = {}
    for raw in path.read_text(encoding="utf-8").splitlines():
        if raw and not raw.startswith("#"):
            key, value = raw.split("=", 1)
            values[key] = int(value)
    return values


def write_config(path: Path, sector: int) -> None:
    path.write_text(
        "[inject-error]\n"
        'event = "write_aio"\n'
        'errno = "5"\n'
        f'sector = "{sector}"\n'
        'once = "on"\n',
        encoding="utf-8",
    )


def launch(qemu: str, boot: Path, runtime: Path, serial: Path, qmp_path: Path,
           stderr: Path, config: Path | None) -> tuple[subprocess.Popen[bytes], QmpClient, Any]:
    for path in (serial, qmp_path, stderr):
        path.unlink(missing_ok=True)
    stderr_handle = stderr.open("wb")
    command = [qemu, "-drive", f"file={boot},format=raw,if=floppy"]
    if config is None:
        command += ["-drive", f"file={runtime},format=raw,if=ide,index=0,media=disk,cache=none"]
    else:
        command += [
            "-drive", f"if=none,id=faultdisk,format=raw,cache=none,file=blkdebug:{config}:{runtime}",
            "-device", "ide-hd,drive=faultdisk,bus=ide.0,unit=0",
        ]
    command += [
        "-boot", "a", "-m", "32M", "-machine", "pc,vmport=off", "-vga", "std",
        "-display", "none", "-serial", f"file:{serial}",
        "-qmp", f"unix:{qmp_path},server=on,wait=off", "-monitor", "none",
        "-no-reboot", "-no-shutdown",
    ]
    process = subprocess.Popen(command, stdout=subprocess.DEVNULL, stderr=stderr_handle)
    try:
        return process, QmpClient(qmp_path), stderr_handle
    except Exception:
        process.kill()
        process.wait(timeout=10)
        stderr_handle.close()
        raise


def stop(process: subprocess.Popen[bytes], qmp: QmpClient, stderr_handle: Any) -> None:
    try:
        qmp.command("quit", timeout=3.0)
    except (ConnectionError, TimeoutError):
        pass
    finally:
        qmp.close()
    try:
        process.wait(timeout=10)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait(timeout=10)
    stderr_handle.close()
    if process.returncode not in (0, None):
        raise RuntimeError(f"QEMU exited with status {process.returncode}")


def wait_boot(serial: Path, manager: bool = True) -> None:
    markers = [
        "ZENOVOS_BOOT_OK",
        "ZENOVFS_MOUNT_OK",
        "ZENOV_GUARD_READY",
        "ZENREPO_READY trust=verified packages=2",
        "ZENPKG_SHA256_OK",
    ]
    if manager:
        markers.append("ZENPKG_MANAGER_READY")
    markers.append(PROMPT)
    for marker in markers:
        wait_for_text(serial, marker)


def fault_boot(qemu: str, boot: Path, runtime: Path, out: Path, name: str,
               sector: int, guest_command: str | None) -> None:
    config = out / f"blkdebug-{name}.conf"
    serial = out / f"serial-{name}-fault.log"
    qmp_path = out / f"qmp-{name}-fault.sock"
    stderr = out / f"qemu-{name}-fault.stderr"
    events = out / f"qmp-{name}-events.json"
    write_config(config, sector)
    process, qmp, stderr_handle = launch(qemu, boot, runtime, serial, qmp_path, stderr, config)
    try:
        if guest_command is not None:
            wait_boot(serial)
            send_command(qmp, guest_command)
        event = qmp.wait_event("BLOCK_IO_ERROR")
        data = event.get("data", {})
        if data.get("operation") not in ("write", None):
            raise RuntimeError(f"unexpected block error operation: {data}")
        events.write_text(json.dumps(event, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    finally:
        stop(process, qmp, stderr_handle)
    if contains(serial, "ZENPKG_CACHE_FETCH_COMMIT_OK"):
        raise RuntimeError(f"fault scenario completed before crash: {name}")


def recovery_boot(qemu: str, boot: Path, runtime: Path, out: Path, name: str) -> None:
    serial = out / f"serial-{name}-recovery.log"
    qmp_path = out / f"qmp-{name}-recovery.sock"
    stderr = out / f"qemu-{name}-recovery.stderr"
    process, qmp, stderr_handle = launch(qemu, boot, runtime, serial, qmp_path, stderr, None)
    try:
        wait_boot(serial)
        send_command(qmp, "pkg transport resume hello-native")
        deadline = time.monotonic() + 60.0
        while time.monotonic() < deadline:
            if contains(serial, "ZENPKG_CACHE_FETCH_COMMIT_OK name=hello-native version=0.2.0") or \
               contains(serial, "ZENPKG_CACHE_HIT name=hello-native version=0.2.0"):
                break
            time.sleep(0.05)
        else:
            raise TimeoutError(f"recovery did not produce verified cache object: {name}")
        send_command(qmp, "pkg cache verify")
        wait_for_text(serial, "ZENPKG_CACHE_VERIFY_OK objects=1 partials=0")
        send_command(qmp, "fsck")
        wait_for_text(serial, "ZENOVFS_FSCK_OK")
    finally:
        stop(process, qmp, stderr_handle)
    text = serial.read_text(encoding="utf-8", errors="replace")
    for marker in ("PANIC", "ASSERT", "DOUBLE FAULT", "ZENPKG_CACHE_INIT_REJECTED"):
        if marker in text:
            raise RuntimeError(f"recovery {name} contains forbidden marker: {marker}")
    if stderr.stat().st_size:
        raise RuntimeError(f"recovery QEMU stderr is not empty: {stderr}")


def main() -> int:
    parser = argparse.ArgumentParser(description="Inject QEMU blkdebug EIO into live ZenPkg metadata writes")
    parser.add_argument("--boot", type=Path, required=True)
    parser.add_argument("--fixtures", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--qemu", default=os.environ.get("QEMU", "qemu-system-i386"))
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    values = read_env(args.fixtures / "sectors.env")
    scenarios = [
        ("partial-old-metadata", "resume.img", values["PARTIAL_ENTRY_SECTOR"], "pkg transport resume hello-native"),
        ("journal-old-metadata", "resume.img", values["JOURNAL_ENTRY_SECTOR"], "pkg transport resume hello-native"),
        ("rename-metadata", "ready.img", values["PARTIAL_ENTRY_SECTOR"], "pkg transport resume hello-native"),
        ("journal-remove", "committed.img", values["JOURNAL_ENTRY_SECTOR"], None),
    ]
    summary: list[str] = []
    for name, fixture, sector, guest_command in scenarios:
        runtime = args.out / f"runtime-{name}.img"
        shutil.copyfile(args.fixtures / fixture, runtime)
        fault_boot(args.qemu, args.boot, runtime, args.out, name, sector, guest_command)
        recovery_boot(args.qemu, args.boot, runtime, args.out, name)
        summary.append(f"ZENPKG_BLKDEBUG_SCENARIO_OK name={name} sector={sector} event=BLOCK_IO_ERROR")
    summary_path = args.out / "summary.log"
    summary_path.write_text("\n".join(summary) + "\n", encoding="utf-8")
    print(summary_path.read_text(encoding="utf-8"), end="")
    print(f"ZENPKG_BLKDEBUG_LIVE_IO_FAULTS_OK scenarios={len(scenarios)} qemu-boots={len(scenarios) * 2}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as error:
        print(f"qemu-zenpkg-blkdebug-faults: {error}", file=sys.stderr)
        raise SystemExit(1)
