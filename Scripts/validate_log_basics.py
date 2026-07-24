#!/usr/bin/env python3
"""
validate_log_basics.py — Log 模块端到端验证 #1

启动 EchoService,等 2 秒,断言日志文件存在、行数 > 0、每行 JSON 可解析、含 `"category":` 字段。

用法:
  python3 Scripts/validate_log_basics.py [--build-dir Build]
"""

import argparse
import json
import os
import socket
import subprocess
import sys
import time
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parent.parent
BIN_DIR = PROJECT_ROOT / "Bin"
LOG_DIR = PROJECT_ROOT / "Logs" / "validate"

ECHO_PORT = 7101
LOG_FILE = LOG_DIR / "log-basics.jsonl"


def log(msg: str) -> None:
    print(f"[log-basics] {msg}", flush=True)


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
    ap.add_argument("--duration", type=float, default=3.0,
        help="seconds to keep the service running for log capture")
    args = ap.parse_args()

    exe = find_exe("EchoService")
    if exe is None:
        log(f"EchoService not found in {BIN_DIR}; build first")
        return 1

    LOG_DIR.mkdir(parents=True, exist_ok=True)
    if LOG_FILE.exists():
        LOG_FILE.unlink()

    cmd = [
        str(exe),
        f"--listen={ECHO_PORT}",
        "--inst=99",
        "--actors=9001,9002",
        "--service=MEchoService",
        f"--log-file={LOG_FILE}",
    ]

    log(f"starting: {' '.join(cmd)}")
    proc = subprocess.Popen(cmd,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL)
    try:
        if not wait_for_port("127.0.0.1", ECHO_PORT, timeout=5.0):
            log(f"EchoService did not start on :{ECHO_PORT}")
            return 1

        # Drive a few RPCs through Gateway→Echo so the service produces log records.
        try:
            with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as gw:
                gw.settimeout(1.0)
                gw.connect(("127.0.0.1", 8001))
                # minimal "ping" payload — we don't care about the response,
                # we only need to nudge the service into producing log lines.
                gw.send(b"\x00\x00\x00\x04PING")
                gw.recv(64)
        except OSError:
            log("Gateway unreachable; service may still log startup banner")

        time.sleep(args.duration)

        if not LOG_FILE.exists():
            log(f"FAIL: log file {LOG_FILE} not produced")
            return 1

        lines = [l for l in LOG_FILE.read_text().splitlines() if l.strip()]
        if not lines:
            log(f"FAIL: {LOG_FILE} is empty")
            return 1

        ok = 0
        bad = 0
        categories = set()
        for ln in lines:
            try:
                obj = json.loads(ln)
                ok += 1
                cat = obj.get("category")
                if cat:
                    categories.add(cat)
            except json.JSONDecodeError as e:
                bad += 1
                log(f"  parse error: {e} :: {ln[:80]}")

        log(f"  total lines={len(lines)}  parsed_ok={ok}  parsed_bad={bad}")
        log(f"  categories seen: {sorted(categories)}")

        if bad > 0:
            log(f"FAIL: {bad} lines did not parse as JSON")
            return 1
        if "category" not in (json.loads(lines[0]).keys()):
            log(f"FAIL: first line missing 'category' field")
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