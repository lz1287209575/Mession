#!/usr/bin/env python3
"""
一键起服 / 停服脚本

用法:
  python3 Scripts/servers.py start [--build-dir Build]
  python3 Scripts/servers.py stop  [--build-dir Build]

起服顺序: Router(8005) -> Login(8002) -> World(8003) -> Scene(8004) -> Gateway(8001)
停服时按启动时记录的 PID 结束进程；若无 PID 文件则尝试按端口结束占用进程（仅 Linux）。
"""

import argparse
import os
import signal
import subprocess
import sys
import time
from pathlib import Path
from typing import Optional

# 与 validate.py 一致
ROUTER_PORT = 8005
GATEWAY_PORT = 8001
LOGIN_PORT = 8002
WORLD_PORT = 8003
SCENE_PORT = 8004

SERVER_ORDER = [
    ("RouterServer", ROUTER_PORT),
    ("LoginServer", LOGIN_PORT),
    ("WorldServer", WORLD_PORT),
    ("SceneServer", SCENE_PORT),
    ("GatewayServer", GATEWAY_PORT),
]

PID_FILE_NAME = ".mession_servers.pid"


def log(msg: str) -> None:
    print(f"[servers] {msg}", flush=True)


def get_project_root() -> Path:
    return Path(__file__).resolve().parent.parent


def get_executable_path(build_dir: Path, name: str) -> Optional[Path]:
    """
    获取可执行文件路径。

    说明：
    - CMake 使用 Build/ 作为构建目录；
    - 实际可执行文件统一输出到仓库根目录下的 Bin/。
    因此这里忽略 build_dir 的具体子目录结构，直接从项目根的 Bin/ 中查找。
    """
    project_root = get_project_root()
    bin_dir = project_root / "Bin"
    for suffix in ("", ".exe"):
        p = bin_dir / (name + suffix)
        if p.exists():
            return p
    return None


def get_pid_file_path(build_dir: Path) -> Path:
    return build_dir / PID_FILE_NAME


def wait_for_port(host: str, port: int, timeout: float) -> bool:
    import socket
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            s.settimeout(0.5)
            s.connect((host, port))
            s.close()
            return True
        except OSError:
            time.sleep(0.1)
    return False


def start_servers(build_dir: Path, wait_ready: bool = True) -> int:
    project_root = get_project_root()
    if not build_dir.is_absolute():
        build_dir = (project_root / build_dir).resolve()
    if not build_dir.exists():
        log(f"Build dir does not exist: {build_dir}")
        return 1

    pids: list[int] = []
    procs: list[subprocess.Popen] = []

    for name, port in SERVER_ORDER:
        exe = get_executable_path(build_dir, name)
        if not exe:
            log(f"Executable not found: {name}")
            for p in procs:
                try:
                    p.terminate()
                    p.wait(timeout=2)
                except Exception:
                    p.kill()
            return 1
        log(f"Starting {name} (port {port})...")
        proc = subprocess.Popen(
            [str(exe)],
            cwd=str(build_dir),
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            start_new_session=True,
            env=os.environ.copy(),
        )
        procs.append(proc)
        pids.append(proc.pid)

        if wait_ready and port:
            if not wait_for_port("127.0.0.1", port, 15.0):
                log(f"{name} did not become ready on port {port}")
                for p in procs:
                    try:
                        p.terminate()
                        p.wait(timeout=2)
                    except Exception:
                        p.kill()
                return 1
            time.sleep(0.3)

    pid_file = get_pid_file_path(build_dir)
    try:
        pid_file.write_text("\n".join(str(pid) for pid in pids), encoding="utf-8")
    except OSError as e:
        log(f"Warning: could not write PID file: {e}")

    log("All servers started.")
    log(f"  PIDs: {pids}")
    log("  Stop with: python3 scripts/servers.py stop [--build-dir <dir>]")
    return 0


def stop_servers(build_dir: Path, kill_by_port: bool = True) -> int:
    project_root = get_project_root()
    if not build_dir.is_absolute():
        build_dir = (project_root / build_dir).resolve()
    pid_file = get_pid_file_path(build_dir)

    stopped_any = False
    if pid_file.exists():
        try:
            content = pid_file.read_text(encoding="utf-8")
            pids = [int(line.strip()) for line in content.splitlines() if line.strip()]
        except (ValueError, OSError):
            pids = []
        for pid in pids:
            try:
                os.kill(pid, signal.SIGTERM)
                log(f"Sent SIGTERM to PID {pid}")
                stopped_any = True
            except ProcessLookupError:
                pass
            except OSError as e:
                log(f"Could not kill {pid}: {e}")
        try:
            pid_file.unlink()
        except OSError:
            pass
        if stopped_any:
            time.sleep(1.0)
        for pid in pids:
            try:
                os.kill(pid, signal.SIGKILL)
            except (ProcessLookupError, OSError):
                pass

    if kill_by_port and sys.platform != "win32":
        for _name, port in SERVER_ORDER:
            try:
                subprocess.run(
                    ["fuser", "-k", f"{port}/tcp"],
                    capture_output=True,
                    timeout=5,
                )
            except (FileNotFoundError, subprocess.TimeoutExpired):
                pass
    log("Stop done.")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="One-click start/stop Mession servers")
    parser.add_argument("command", choices=["start", "stop"], help="start or stop all servers")
    parser.add_argument("--build-dir", type=Path, default=Path("Build"), help="Build output directory (default: Build)")
    parser.add_argument("--no-wait", action="store_true", help="(start only) Do not wait for each server port to be ready")
    args = parser.parse_args()

    if args.command == "start":
        return start_servers(args.build_dir, wait_ready=not args.no_wait)
    return stop_servers(args.build_dir)


if __name__ == "__main__":
    sys.exit(main())
