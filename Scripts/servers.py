#!/usr/bin/env python3
"""
一键起服 / 停服脚本（PoC：Gateway + 同质多进程 EchoService）

用法:
  python3 Scripts/servers.py start [--build-dir Build]
  python3 Scripts/servers.py stop  [--build-dir Build]

起服顺序: EchoService@7001 → EchoService@7002 → Gateway@8001
停服时按启动时记录的 PID 结束进程；若无 PID 文件则尝试按端口结束占用进程（仅 Linux）。
"""

import argparse
import os
import signal
import subprocess
import sys
import time
from pathlib import Path
from typing import Optional, Sequence, Tuple


# PoC 拓扑端口
ECHO_PORT_A = 7001
ECHO_PORT_B = 7002
GATEWAY_PORT = 8001

# 同质多进程 EchoService 启动参数：(name, port, argv)
# 两个 EchoService 都是同一个 binary（MEchoService），靠 --inst 和 --actors 区分
SERVER_ORDER: Sequence[Tuple[str, int, Sequence[str]]] = [
    (
        "EchoService",
        ECHO_PORT_A,
        [
            f"--listen={ECHO_PORT_A}",
            "--inst=1",
            "--actors=1001,1002",
            "--service=MEchoService",
            f"--peers=Echo@127.0.0.1:{ECHO_PORT_B}",
        ],
    ),
    (
        "EchoService",
        ECHO_PORT_B,
        [
            f"--listen={ECHO_PORT_B}",
            "--inst=2",
            "--actors=2001,2002",
            "--service=MEchoService",
            f"--peers=Echo@127.0.0.1:{ECHO_PORT_A}",
        ],
    ),
    (
        "GatewayServer",
        GATEWAY_PORT,
        [
            f"--listen={GATEWAY_PORT}",
            f"--peers=Echo@127.0.0.1:{ECHO_PORT_A},Echo@127.0.0.1:{ECHO_PORT_B}",
        ],
    ),
]

PID_FILE_NAME = ".mession_servers.pid"
SERVER_LOG_DIR = Path("Logs") / "servers"
IS_WINDOWS = sys.platform == "win32"


def log(msg: str) -> None:
    print(f"[servers] {msg}", flush=True)


def get_project_root() -> Path:
    return Path(__file__).resolve().parent.parent


def get_executable_path(build_dir: Path, name: str) -> Optional[Path]:
    """
    获取可执行文件路径。
    Bin/ 是仓库根的固定输出目录；忽略 build_dir 子结构。
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


def read_pid_registry(build_dir: Path) -> dict[str, int]:
    pid_file = get_pid_file_path(build_dir)
    if not pid_file.exists():
        return {}

    try:
        content = pid_file.read_text(encoding="utf-8")
    except OSError:
        return {}

    registry: dict[str, int] = {}
    unnamed_pids: list[int] = []

    for line in content.splitlines():
        line = line.strip()
        if not line:
            continue

        parts = line.split()
        if len(parts) == 1 and parts[0].isdigit():
            unnamed_pids.append(int(parts[0]))
            continue

        if len(parts) >= 2 and parts[-1].isdigit():
            registry[parts[0]] = int(parts[-1])

    # Legacy fallback：未命名 PID 按顺序对应 SERVER_ORDER
    if unnamed_pids:
        for index, pid in enumerate(unnamed_pids):
            if index >= len(SERVER_ORDER):
                break
            entry = SERVER_ORDER[index]
            key = f"{entry[0]}:{entry[1]}"
            registry.setdefault(key, pid)

    return registry


def write_pid_registry(build_dir: Path, registry: dict[str, int]) -> None:
    pid_file = get_pid_file_path(build_dir)
    lines = []
    for entry in SERVER_ORDER:
        name, port, _argv = entry
        key = f"{name}:{port}"
        pid = registry.get(key)
        if pid:
            lines.append(f"{key} {pid}")

    if not lines:
        try:
            pid_file.unlink()
        except OSError:
            pass
        return

    pid_file.write_text("\n".join(lines) + "\n", encoding="utf-8")


def parse_args(argv: Optional[Sequence[str]] = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Mession PoC server lifecycle (Gateway + EchoService)")
    parser.add_argument("--build-dir", default="Build", help="Build directory (default: Build)")
    parser.add_argument(
        "--no-logs",
        action="store_true",
        help="Disable per-server log files (still writes to stdout)",
    )
    sub = parser.add_subparsers(dest="cmd", required=True)
    sub.add_parser("start", help="Start Gateway + EchoService processes")
    sub.add_parser("stop", help="Stop all tracked processes")
    sub.add_parser("status", help="Print tracked PID registry")
    sub.add_parser("restart", help="Stop then start")
    return parser.parse_args(argv)


def build_dir_arg(args: argparse.Namespace) -> Path:
    build_dir = Path(args.build_dir)
    if not build_dir.is_absolute():
        build_dir = (get_project_root() / build_dir).resolve()
    return build_dir


def normalize_build_dir(build_dir: Path) -> Path:
    if build_dir.is_absolute():
        return build_dir.resolve()
    return (get_project_root() / build_dir).resolve()


def wait_for_port(host: str, port: int, timeout: float = 10.0) -> bool:
    import socket

    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
                sock.settimeout(0.5)
                sock.connect((host, port))
                return True
        except OSError:
            time.sleep(0.1)
    return False


def start_server_process(
    exe: Path,
    port: int,
    argv: Sequence[str],
    log_dir: Optional[Path],
    no_logs: bool,
) -> subprocess.Popen:
    env = os.environ.copy()
    cmd = [str(exe), *argv]
    stdout = subprocess.DEVNULL
    stderr = subprocess.DEVNULL
    if log_dir and not no_logs:
        log_dir.mkdir(parents=True, exist_ok=True)
        log_path = log_dir / f"{exe.name}_{port}.log"
        try:
            handle = open(log_path, "w", encoding="utf-8")
            stdout = handle
            stderr = handle
        except OSError:
            pass

    return subprocess.Popen(
        cmd,
        cwd=str(get_project_root()),
        stdout=stdout,
        stderr=stderr,
        start_new_session=True,
        env=env,
    )


def cmd_start(args: argparse.Namespace) -> int:
    build_dir = normalize_build_dir(build_dir_arg(args))
    log_dir = SERVER_LOG_DIR if not args.no_logs else None

    registry = read_pid_registry(build_dir)
    started_keys: list[str] = []

    try:
        for entry in SERVER_ORDER:
            name, port, srv_argv = entry
            key = f"{name}:{port}"

            if registry.get(key):
                log(f"skip {key}: already tracked (pid={registry[key]})")
                continue

            exe = get_executable_path(build_dir, name)
            if exe is None:
                log(f"missing executable for {name}; run `cmake --build {build_dir}` first")
                return 1

            log(f"start {key}: argv={srv_argv}")
            proc = start_server_process(exe, port, srv_argv, log_dir, args.no_logs)
            if not wait_for_port("127.0.0.1", port, timeout=10.0):
                log(f"{key} did not bind {port} within 10s; check logs at {log_dir}")
                try:
                    proc.terminate()
                    proc.wait(timeout=2)
                except Exception:
                    pass
                return 1

            registry[key] = proc.pid
            started_keys.append(key)

        write_pid_registry(build_dir, registry)
        log(f"started {len(started_keys)} process(es). Logs: {log_dir if log_dir else 'stdout'}")
        return 0
    except Exception as exc:
        log(f"startup aborted: {exc}")
        for key in started_keys:
            pid = registry.get(key)
            if pid:
                try:
                    os.kill(pid, signal.SIGTERM)
                except OSError:
                    pass
        return 1


def cmd_stop(args: argparse.Namespace) -> int:
    build_dir = normalize_build_dir(build_dir_arg(args))
    registry = read_pid_registry(build_dir)
    if not registry:
        log("no tracked processes; nothing to stop")
        return 0

    stopped = 0
    for entry in reversed(SERVER_ORDER):
        name, port, _argv = entry
        key = f"{name}:{port}"
        pid = registry.get(key)
        if not pid:
            continue
        try:
            os.kill(pid, signal.SIGTERM)
            log(f"stop {key} (pid={pid})")
            stopped += 1
        except OSError:
            log(f"stop {key}: pid={pid} not found (already gone)")
        registry.pop(key, None)

    write_pid_registry(build_dir, registry)
    log(f"stopped {stopped} process(es)")
    return 0


def cmd_status(args: argparse.Namespace) -> int:
    build_dir = normalize_build_dir(build_dir_arg(args))
    registry = read_pid_registry(build_dir)
    if not registry:
        log("no tracked processes")
        return 0
    for key, pid in registry.items():
        try:
            os.kill(pid, 0)
            alive = "alive"
        except OSError:
            alive = "dead"
        log(f"  {key}: pid={pid} ({alive})")
    return 0


def main(argv: Optional[Sequence[str]] = None) -> int:
    args = parse_args(argv)
    if args.cmd == "start":
        return cmd_start(args)
    if args.cmd == "stop":
        return cmd_stop(args)
    if args.cmd == "status":
        return cmd_status(args)
    if args.cmd == "restart":
        cmd_stop(args)
        time.sleep(0.5)
        return cmd_start(args)
    log(f"unknown command: {args.cmd}")
    return 2


if __name__ == "__main__":
    sys.exit(main())