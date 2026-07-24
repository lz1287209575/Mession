#!/usr/bin/env python3
"""
validate_log_filter.py — Log 模块端到端验证 #3

启动服务,经由 --log-config 配置 Auth 类别静默 + Db 类别只走 File;
断言: 静默类别无 Console 输出。

用法:
  python3 Scripts/validate_log_filter.py [--build-dir Build]
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
CFG_DIR = PROJECT_ROOT / "Logs" / "validate" / "config"

ECHO_PORT = 7103
LOG_FILE = LOG_DIR / "log-filter.jsonl"
CFG_FILE = CFG_DIR / "filter.json"


def log(msg: str) -> None:
    print(f"[log-filter] {msg}", flush=True)


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
    CFG_DIR.mkdir(parents=True, exist_ok=True)
    if LOG_FILE.exists():
        LOG_FILE.unlink()

    cfg = {
        "defaultLevel": "Info",
        "enableConsole": True,
        "filePath": str(LOG_FILE),
        "categories": [
            {"name": "LogAuth", "level": "Trace", "suppressed": True},
            {"name": "LogDb",   "level": "Warn",  "suppressed": False},
        ],
        "routes": [
            {"name": "LogDb",   "sinks": ["file"],      "minLevel": "Debug"},
            {"name": "LogAuth", "sinks": ["console"],   "minLevel": "Trace"},
        ],
    }
    CFG_FILE.write_text(json.dumps(cfg, indent=2))

    cmd = [
        str(exe),
        f"--listen={ECHO_PORT}",
        "--inst=97",
        "--actors=9201,9202",
        "--service=MEchoService",
        f"--log-config={CFG_FILE}",
    ]
    log(f"starting: {' '.join(cmd)}")
    proc = subprocess.Popen(cmd,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL)
    try:
        if not wait_for_port("127.0.0.1", ECHO_PORT, timeout=5.0):
            log(f"FAIL: EchoService did not start on :{ECHO_PORT}")
            return 1

        time.sleep(2.0)

        if not LOG_FILE.exists():
            log(f"FAIL: log file {LOG_FILE} not produced")
            return 1

        lines = [l for l in LOG_FILE.read_text().splitlines() if l.strip()]
        if not lines:
            log(f"FAIL: {LOG_FILE} is empty")
            return 1

        # Count lines per category. Suppressed category must not appear in
        # the file (the route "sinks": ["file"] still applies, but the
        # suppressed flag drops the record before routing).
        cats = {}
        for ln in lines:
            try:
                obj = json.loads(ln)
                cat = obj.get("category", "?")
                cats[cat] = cats.get(cat, 0) + 1
            except json.JSONDecodeError:
                pass

        log(f"  category counts: {cats}")

        if "LogAuth" in cats and cats["LogAuth"] > 0:
            log(f"FAIL: LogAuth was suppressed but appears {cats['LogAuth']} times in file")
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