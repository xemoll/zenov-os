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
BREAK_EVENT = "pwritev"


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
                timeout: float = 20.0) -> dict[str, Any]:
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

    def hmp(self, command_line: str, timeout: float = 60.0) -> str:
        response = self.command(
            "human-monitor-command",
            {"command-line": command_line},
            timeout=timeout,
        )
        result = response.get("return", "")
        return result if isinstance(result, str) else json.dumps(result, sort_keys=True)


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


def launch(qemu: str, boot: Path, runtime: Path, serial: Path, qmp_path: Path,
           stderr: Path, use_blkdebug: bool) -> tuple[subprocess.Popen[bytes], QmpClient, Any]:
    for path in (serial, qmp_path, stderr):
        path.unlink(missing_ok=True)
    stderr_handle = stderr.open("wb")
    command = [qemu, "-drive", f"file={boot},format=raw,if=floppy"]
    if use_blkdebug:
        command += [
            "-blockdev", f"driver=file,node-name=runtime-file,filename={runtime},cache.direct=on,cache.no-flush=off",
            "-blockdev", "driver=raw,node-name=runtime-raw,file=runtime-file",
            "-blockdev", "driver=blkdebug,node-name=runtime-debug,image=runtime-raw",
            "-device", "ide-hd,drive=runtime-debug,bus=ide.0,unit=0",
        ]
    else:
        command += ["-drive", f"file={runtime},format=raw,if=ide,index=0,media=disk,cache=none"]
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
        qmp.command("stop", timeout=3.0)
    except (ConnectionError, TimeoutError):
        pass
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


def wait_boot(serial: Path) -> None:
    for marker in (
        "ZENOVOS_BOOT_OK",
        "ZENOVFS_MOUNT_OK",
        "ZENOV_GUARD_READY",
        "ZENREPO_READY trust=verified packages=2",
        "ZENPKG_SHA256_OK",
        "ZENPKG_MANAGER_READY",
        PROMPT,
    ):
        wait_for_text(serial, marker)


def qemu_io(qmp: QmpClient, command: str, timeout: float = 60.0) -> str:
    return qmp.hmp(f'qemu-io runtime-debug "{command}"', timeout=timeout)


def fault_boot(qemu: str, boot: Path, runtime: Path, out: Path, name: str,
               ordinal: int, guest_command: str) -> None:
    if ordinal <= 0:
        raise ValueError("breakpoint ordinal must be positive")
    serial = out / f"serial-{name}-fault.log"
    qmp_path = out / f"qmp-{name}-fault.sock"
    stderr = out / f"qemu-{name}-fault.stderr"
    evidence_path = out / f"qmp-{name}-break.json"
    tag = f"zenpkg-{name}"
    process, qmp, stderr_handle = launch(qemu, boot, runtime, serial, qmp_path, stderr, True)
    evidence: dict[str, Any] = {
        "event": BREAK_EVENT,
        "ordinal": ordinal,
        "tag": tag,
        "guest_command": guest_command,
        "hits": [],
    }
    try:
        wait_boot(serial)
        break_output = qemu_io(qmp, f"break {BREAK_EVENT} {tag}")
        if "Could not" in break_output or "not found" in break_output:
            raise RuntimeError(f"cannot arm blkdebug breakpoint: {break_output.strip()}")
        evidence["break_output"] = break_output
        send_command(qmp, guest_command)
        for hit in range(1, ordinal + 1):
            wait_output = qemu_io(qmp, f"wait_break {tag}", timeout=60.0)
            evidence["hits"].append({"ordinal": hit, "wait_output": wait_output})
            if hit < ordinal:
                resume_output = qemu_io(qmp, f"resume {tag}")
                if "Could not" in resume_output:
                    raise RuntimeError(f"cannot resume blkdebug breakpoint: {resume_output.strip()}")
                evidence["hits"][-1]["resume_output"] = resume_output
        evidence["status_at_crash"] = qmp.command("query-status").get("return", {})
        evidence_path.write_text(json.dumps(evidence, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    finally:
        stop(process, qmp, stderr_handle)
    if contains(serial, "ZENPKG_CACHE_FETCH_COMMIT_OK"):
        raise RuntimeError(f"fault scenario completed before breakpoint crash: {name}")
    if stderr.stat().st_size:
        raise RuntimeError(f"fault QEMU stderr is not empty: {stderr}")


def recovery_boot(qemu: str, boot: Path, runtime: Path, out: Path, name: str) -> None:
    serial = out / f"serial-{name}-recovery.log"
    qmp_path = out / f"qmp-{name}-recovery.sock"
    stderr = out / f"qemu-{name}-recovery.stderr"
    process, qmp, stderr_handle = launch(qemu, boot, runtime, serial, qmp_path, stderr, False)
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
    parser = argparse.ArgumentParser(description="Crash ZenPkg at live QEMU blkdebug pwritev breakpoints")
    parser.add_argument("--boot", type=Path, required=True)
    parser.add_argument("--fixtures", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--qemu", default=os.environ.get("QEMU", "qemu-system-i386"))
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    scenarios = [
        ("chunk-first-write", "resume.img", 1),
        ("chunk-second-write", "resume.img", 2),
        ("chunk-metadata-sync", "resume.img", 8),
        ("rename-first-write", "ready.img", 1),
    ]
    summary: list[str] = []
    for name, fixture, ordinal in scenarios:
        runtime = args.out / f"runtime-{name}.img"
        shutil.copyfile(args.fixtures / fixture, runtime)
        fault_boot(
            args.qemu,
            args.boot,
            runtime,
            args.out,
            name,
            ordinal,
            "pkg transport resume hello-native",
        )
        recovery_boot(args.qemu, args.boot, runtime, args.out, name)
        summary.append(
            f"ZENPKG_BLKDEBUG_BREAKPOINT_SCENARIO_OK name={name} event={BREAK_EVENT} ordinal={ordinal}"
        )
    summary_path = args.out / "summary.log"
    summary_path.write_text("\n".join(summary) + "\n", encoding="utf-8")
    print(summary_path.read_text(encoding="utf-8"), end="")
    print(f"ZENPKG_BLKDEBUG_LIVE_CRASHES_OK scenarios={len(scenarios)} qemu-boots={len(scenarios) * 2}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as error:
        print(f"qemu-zenpkg-blkdebug-faults: {error}", file=sys.stderr)
        raise SystemExit(1)
