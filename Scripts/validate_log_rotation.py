#!/usr/bin/env python3
"""
validate_log_rotation.py — Log 模块端到端验证 #2

写满 N bytes 后,断言 rotate 生效、归档数 ≤ NumArchives。

用法:
  python3 Scripts/validate_log_rotation.py [--build-dir Build]
"""

import argparse
import os
import socket
import subprocess
import sys
import time
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parent.parent
BIN_DIR = PROJECT_ROOT / "Bin"
LOG_DIR = PROJECT_ROOT / "Logs" / "validate"

ECHO_PORT = 7102
LOG_FILE = LOG_DIR / "log-rotation.jsonl"

# Aim for ~250KB to trigger at least one rotate against a tiny 32KB cap.
TARGET_BYTES = 250 * 1024
ROTATE_BYTES = 32 * 1024


def log(msg: str) -> None:
    print(f"[log-rotation] {msg}", flush=True)


def find_exe(name: str) -> Path | None:
    for suffix in ("", ".exe"):
        candidate = BIN_DIR / (name + suffix)
        if candidate.exists():
            return candidate
    return None


def wait_for_port(host: str, port: int, timeout: float) -> bool:
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
                s.settimeout(0.3)
                s.connect((host, port))
                return True
        except OSError:
            time.sleep(0.1)
    return False


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--build-dir", default="Build")
    args = ap.parse_args()

    exe = find_exe("EchoService")
    if exe is None:
        log("EchoService not found")
        return 1

    LOG_DIR.mkdir(parents=True, exist_ok=True)
    if LOG_FILE.exists():
        LOG_FILE.unlink()
    for sibling in LOG_FILE.parent.glob(f"{LOG_FILE.name}.*"):
        sibling.unlink()

    cmd = [
        str(exe),
        f"--listen={ECHO_PORT}",
        "--inst=98",
        "--actors=9101,9102",
        "--service=MEchoService",
        f"--log-file={LOG_FILE}",
        f"--log-rotate-bytes={ROTATE_BYTES}",
        "--log-archives=3",
    ]

    log(f"starting: {' '.join(cmd)}")
    proc = subprocess.Popen(cmd,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL)
    try:
        if not wait_for_port("127.0.0.1", ECHO_PORT, timeout=5.0):
            log(f"FAIL: EchoService did not start on :{ECHO_PORT}")
            return 1

        # Spam the service until the log file exceeds the rotation threshold.
        # We just hammer the listener socket; the log file will grow with the
        # request/response traffic regardless of payload meaning.
        deadline = time.time() + 20.0
        while time.time() < deadline:
            try:
                with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
                    s.settimeout(0.5)
                    s.connect(("127.0.0.1", ECHO_PORT))
                    s.send(b"\x00\x00\x00\x04SPAM" + b"X" * 512)
                    s.recv(64)
            except OSError:
                pass

            size = LOG_FILE.stat().st_size if LOG_FILE.exists() else 0
            if size >= TARGET_BYTES:
                # Continue for one more second so the rotate catches up.
                time.sleep(1.0)
                break
            time.sleep(0.02)

        # Find any archive (.N) files.
        archives = sorted(LOG_FILE.parent.glob(f"{LOG_FILE.name}.*"))
        log(f"  live size={LOG_FILE.stat().st_size if LOG_FILE.exists() else 0}  archives={len(archives)}")
        for a in archives:
            log(f"    {a.name}  size={a.stat().st_size}")

        if not archives:
            log("FAIL: no archive files produced; rotation never triggered")
            return 1

        # We asked for --log-archives=3, so up to 3 archives should remain.
        if len(archives) > 3:
            log(f"FAIL: {len(archives)} archives > 3 (NumArchives cap)")
            return 1

        log("PASS")
        return 0
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=5.0)
        except subprocess.TimeoutExpired:
            proc.kill()


if __name__ == "__main__":
    sys.exit(main())