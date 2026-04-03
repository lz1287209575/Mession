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
MGO_PORT = 8006

SERVER_ORDER = [
    ("RouterServer", ROUTER_PORT),
    ("LoginServer", LOGIN_PORT),
    ("WorldServer", WORLD_PORT),
    ("SceneServer", SCENE_PORT),
    ("GatewayServer", GATEWAY_PORT),
    ("MgoServer", MGO_PORT),
]

PID_FILE_NAME = ".mession_servers.pid"
SERVER_LOG_DIR = Path("Logs") / "servers"
SERVER_LAUNCHER_DIR = Path("Build") / ".server_launchers"
IS_WINDOWS = sys.platform == "win32"


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


def normalize_build_dir(build_dir: Path) -> Path:
    if build_dir.is_absolute():
        return build_dir.resolve()
    return (get_project_root() / build_dir).resolve()


def get_server_port(name: str) -> int:
    for server_name, port in SERVER_ORDER:
        if server_name == name:
            return port
    raise ValueError(f"Unknown server: {name}")


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

    if unnamed_pids:
        for index, pid in enumerate(unnamed_pids):
            if index >= len(SERVER_ORDER):
                break
            registry.setdefault(SERVER_ORDER[index][0], pid)

    return registry


def write_pid_registry(build_dir: Path, registry: dict[str, int]) -> None:
    pid_file = get_pid_file_path(build_dir)
    lines = []
    for name, _port in SERVER_ORDER:
        pid = registry.get(name)
        if pid:
            lines.append(f"{name} {pid}")

    if not lines:
        try:
            pid_file.unlink()
        except OSError:
            pass
        return

    pid_file.write_text("\n".join(lines) + "\n", encoding="utf-8")


def update_server_pid(build_dir: Path, name: str, pid: int) -> None:
    registry = read_pid_registry(build_dir)
    registry[name] = pid
    write_pid_registry(build_dir, registry)


def remove_server_pid(build_dir: Path, name: str) -> None:
    registry = read_pid_registry(build_dir)
    registry.pop(name, None)
    write_pid_registry(build_dir, registry)


def get_server_log_dir() -> Path:
    return get_project_root() / SERVER_LOG_DIR


def get_server_launcher_dir() -> Path:
    return get_project_root() / SERVER_LAUNCHER_DIR


def get_watch_command_hint() -> str:
    if IS_WINDOWS:
        return r'Get-Content Logs\servers\GatewayServer.log -Wait'
    return "tail -f Logs/servers/GatewayServer.log"


def get_stop_command_hint() -> str:
    if IS_WINDOWS:
        return r"py -3 Scripts\servers.py stop [--build-dir <dir>]"
    return "python3 Scripts/servers.py stop [--build-dir <dir>]"


def get_popen_kwargs() -> dict:
    if IS_WINDOWS:
        return {"creationflags": subprocess.CREATE_NEW_PROCESS_GROUP}
    return {"start_new_session": True}


def get_foreground_popen_kwargs() -> dict:
    return {}


def get_split_window_popen_kwargs() -> dict:
    if IS_WINDOWS:
        return {
            "creationflags": subprocess.CREATE_NEW_CONSOLE,
        }
    return {}


def get_split_window_color(name: str) -> str:
    color_map = {
        "RouterServer": "0A",
        "LoginServer": "0E",
        "WorldServer": "09",
        "SceneServer": "0B",
        "GatewayServer": "0D",
    }
    return color_map.get(name, "07")


def get_server_window_title(name: str) -> str:
    return f"Mession - {name}"


def create_split_window_launcher(exe: Path, build_dir: Path, name: str) -> Path:
    launcher_dir = get_server_launcher_dir()
    launcher_dir.mkdir(parents=True, exist_ok=True)

    launcher_path = launcher_dir / f"{name}.cmd"
    launcher_contents = "\r\n".join([
        "@echo off",
        f"title {get_server_window_title(name)}",
        "chcp 65001>nul",
        f"color {get_split_window_color(name)}",
        f'cd /d "{build_dir}"',
        f'call "{exe}"',
        "",
    ])
    launcher_path.write_text(launcher_contents, encoding="utf-8", newline="")
    return launcher_path


def build_split_window_command(exe: Path, build_dir: Path, name: str) -> list[str]:
    if IS_WINDOWS:
        launcher = create_split_window_launcher(exe, build_dir, name)
        return [
            "cmd.exe",
            "/d",
            "/k",
            str(launcher),
        ]
    return [str(exe)]


def is_process_alive(pid: int) -> bool:
    if pid <= 0:
        return False

    if IS_WINDOWS:
        try:
            result = subprocess.run(
                ["tasklist", "/FI", f"PID eq {pid}", "/FO", "CSV", "/NH"],
                capture_output=True,
                text=True,
                timeout=5,
            )
        except (FileNotFoundError, subprocess.TimeoutExpired):
            return False
        output = result.stdout.strip()
        return output.startswith('"')

    try:
        os.kill(pid, 0)
        return True
    except PermissionError:
        return True
    except OSError:
        return False


def request_window_close(name: str) -> bool:
    if not IS_WINDOWS:
        return False

    title = get_server_window_title(name).replace("'", "''")
    script = (
        "Add-Type @\"\n"
        "using System;\n"
        "using System.Runtime.InteropServices;\n"
        "public static class CodexWin32 {\n"
        "  [DllImport(\"user32.dll\", CharSet = CharSet.Unicode, SetLastError = true)]\n"
        "  public static extern IntPtr FindWindow(string lpClassName, string lpWindowName);\n"
        "  [DllImport(\"user32.dll\", SetLastError = true)]\n"
        "  public static extern bool PostMessage(IntPtr hWnd, uint Msg, IntPtr wParam, IntPtr lParam);\n"
        "}\n"
        "\"@;"
        f"$hwnd = [CodexWin32]::FindWindow($null, '{title}');"
        "if ($hwnd -eq [IntPtr]::Zero) { exit 2 }"
        "if ([CodexWin32]::PostMessage($hwnd, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero)) { exit 0 }"
        "exit 1"
    )

    try:
        result = subprocess.run(
            ["powershell", "-NoProfile", "-Command", script],
            capture_output=True,
            timeout=10,
        )
    except (FileNotFoundError, subprocess.TimeoutExpired):
        return False
    return result.returncode == 0


def terminate_pid(pid: int, force: bool = False) -> bool:
    if pid <= 0:
        return False

    if IS_WINDOWS:
        cmd = ["taskkill", "/PID", str(pid), "/T"]
        if force:
            cmd.append("/F")
        try:
            result = subprocess.run(cmd, capture_output=True, timeout=10)
        except (FileNotFoundError, subprocess.TimeoutExpired):
            return False
        return result.returncode == 0

    try:
        os.kill(pid, signal.SIGKILL if force else signal.SIGTERM)
        return True
    except OSError:
        return False


def find_pids_by_port(port: int) -> list[int]:
    if port <= 0:
        return []

    if IS_WINDOWS:
        try:
            result = subprocess.run(
                ["netstat", "-ano", "-p", "tcp"],
                capture_output=True,
                text=True,
                timeout=10,
            )
        except (FileNotFoundError, subprocess.TimeoutExpired):
            return []

        pids: set[int] = set()
        for line in result.stdout.splitlines():
            parts = line.split()
            if len(parts) < 4:
                continue
            local_addr = parts[1]
            pid_text = parts[-1]
            if not pid_text.isdigit():
                continue
            if local_addr.endswith(f":{port}"):
                pids.add(int(pid_text))
        return sorted(pids)

    return []


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


def stop_processes(
    pids: list[int],
    kill_by_port: bool = True,
    target_names: Optional[list[str]] = None,
    target_ports: Optional[list[int]] = None,
) -> int:
    if target_names is None:
        target_names = [name for name, _port in SERVER_ORDER]
    if target_ports is None:
        target_ports = [port for _name, port in SERVER_ORDER]

    if IS_WINDOWS:
        stopped_any = False
        graceful_requested = False

        for name in target_names:
            if request_window_close(name):
                log(f"Requested graceful shutdown for {name}")
                graceful_requested = True

        if graceful_requested:
            deadline = time.time() + 5.0
            while time.time() < deadline:
                if all(not is_process_alive(pid) for pid in pids):
                    break
                time.sleep(0.2)

        for pid in pids:
            if graceful_requested and not is_process_alive(pid):
                log(f"Stopped PID {pid}")
                stopped_any = True
                continue
            try:
                if terminate_pid(pid, force=True):
                    log(f"Stopped PID {pid}")
                    stopped_any = True
                elif is_process_alive(pid):
                    log(f"Could not stop PID {pid}")
            except OSError as e:
                log(f"Could not kill {pid}: {e}")

        if kill_by_port:
            for port in target_ports:
                for pid in find_pids_by_port(port):
                    if terminate_pid(pid, force=True):
                        log(f"Stopped PID {pid} on TCP port {port}")
                        stopped_any = True

        if not stopped_any and pids:
            log("No server processes were stopped.")
        log("Stop done.")
        return 0

    stopped_any = False
    for pid in pids:
        try:
            if terminate_pid(pid, force=False):
                log(f"Sent stop signal to PID {pid}")
                stopped_any = True
            elif is_process_alive(pid):
                log(f"Could not stop PID {pid} gracefully")
        except OSError as e:
            log(f"Could not kill {pid}: {e}")

    if stopped_any:
        time.sleep(1.0)

    for pid in pids:
        if not is_process_alive(pid):
            continue
        try:
            if terminate_pid(pid, force=True):
                log(f"Force-stopped PID {pid}")
        except OSError:
            pass

    if kill_by_port and not IS_WINDOWS:
        for port in target_ports:
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


def start_server(
    build_dir: Path,
    name: str,
    wait_ready: bool = True,
    foreground: bool = False,
    split_windows: bool = False,
) -> int:
    build_dir = normalize_build_dir(build_dir)
    if not build_dir.exists():
        log(f"Build dir does not exist: {build_dir}")
        return 1

    if split_windows and not IS_WINDOWS:
        log("--split-windows is only supported on Windows")
        return 1

    if foreground and split_windows:
        log("--foreground and --split-windows cannot be used together")
        return 1

    port = get_server_port(name)
    registry = read_pid_registry(build_dir)
    tracked_pid = registry.get(name)

    if tracked_pid and is_process_alive(tracked_pid):
        log(f"{name} already tracked as running with PID {tracked_pid}")
        return 0

    if wait_ready and port and wait_for_port("127.0.0.1", port, 0.2):
        log(f"{name} already appears to be listening on port {port}")
        return 0

    log_dir = get_server_log_dir()
    try:
        log_dir.mkdir(parents=True, exist_ok=True)
    except OSError as e:
        log(f"Could not create log dir {log_dir}: {e}")
        return 1

    exe = get_executable_path(build_dir, name)
    if not exe:
        log(f"Executable not found: {name}")
        return 1

    log_handle = None
    try:
        log(f"Starting {name} (port {port})...")
        if foreground:
            stdout_target = None
            stderr_target = None
            extra_kwargs = get_foreground_popen_kwargs()
            command = [str(exe)]
        elif split_windows:
            stdout_target = None
            stderr_target = None
            extra_kwargs = get_split_window_popen_kwargs()
            command = build_split_window_command(exe, build_dir, name)
        else:
            log_path = log_dir / f"{name}.log"
            log_handle = open(log_path, "w", encoding="utf-8")
            stdout_target = log_handle
            stderr_target = log_handle
            extra_kwargs = get_popen_kwargs()
            command = [str(exe)]

        proc = subprocess.Popen(
            command,
            cwd=str(build_dir),
            stdout=stdout_target,
            stderr=stderr_target,
            env=os.environ.copy(),
            **extra_kwargs,
        )

        update_server_pid(build_dir, name, proc.pid)

        if wait_ready and port:
            if not wait_for_port("127.0.0.1", port, 15.0):
                log(f"{name} did not become ready on port {port}")
                try:
                    proc.terminate()
                    proc.wait(timeout=2)
                except Exception:
                    proc.kill()
                remove_server_pid(build_dir, name)
                return 1
            time.sleep(0.3)

        log(f"{name} started with PID {proc.pid}")
        return 0
    finally:
        if log_handle is not None:
            log_handle.close()


def stop_server(build_dir: Path, name: str, kill_by_port: bool = True) -> int:
    build_dir = normalize_build_dir(build_dir)
    registry = read_pid_registry(build_dir)
    tracked_pid = registry.get(name)
    port = get_server_port(name)

    pids: list[int] = []
    if tracked_pid:
        pids.append(tracked_pid)

    stop_processes(
        pids,
        kill_by_port=kill_by_port,
        target_names=[name],
        target_ports=[port],
    )
    remove_server_pid(build_dir, name)
    log(f"{name} stop requested")
    return 0


def restart_server(build_dir: Path, name: str, wait_ready: bool = True) -> int:
    stop_server(build_dir, name, kill_by_port=True)
    return start_server(build_dir, name, wait_ready=wait_ready)


def start_servers(
    build_dir: Path,
    wait_ready: bool = True,
    foreground: bool = False,
    split_windows: bool = False,
) -> int:
    project_root = get_project_root()
    build_dir = normalize_build_dir(build_dir)
    if not build_dir.exists():
        log(f"Build dir does not exist: {build_dir}")
        return 1

    if split_windows and not IS_WINDOWS:
        log("--split-windows is only supported on Windows")
        return 1

    if foreground and split_windows:
        log("--foreground and --split-windows cannot be used together")
        return 1

    log_dir = get_server_log_dir()
    try:
        log_dir.mkdir(parents=True, exist_ok=True)
    except OSError as e:
        log(f"Could not create log dir {log_dir}: {e}")
        return 1

    registry = read_pid_registry(build_dir)
    pids: list[int] = []
    procs: list[subprocess.Popen] = []
    log_files = []

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
            for f in log_files:
                f.close()
            return 1
        log(f"Starting {name} (port {port})...")
        if foreground:
            stdout_target = None
            stderr_target = None
            extra_kwargs = get_foreground_popen_kwargs()
            command = [str(exe)]
        elif split_windows:
            stdout_target = None
            stderr_target = None
            extra_kwargs = get_split_window_popen_kwargs()
            command = build_split_window_command(exe, build_dir, name)
        else:
            log_path = log_dir / f"{name}.log"
            log_file = open(log_path, "w", encoding="utf-8")
            log_files.append(log_file)
            stdout_target = log_file
            stderr_target = log_file
            extra_kwargs = get_popen_kwargs()
            command = [str(exe)]
        proc = subprocess.Popen(
            command,
            cwd=str(build_dir),
            stdout=stdout_target,
            stderr=stderr_target,
            env=os.environ.copy(),
            **extra_kwargs,
        )
        procs.append(proc)
        pids.append(proc.pid)
        registry[name] = proc.pid

        if wait_ready and port:
            if not wait_for_port("127.0.0.1", port, 15.0):
                log(f"{name} did not become ready on port {port}")
                for p in procs:
                    try:
                        p.terminate()
                        p.wait(timeout=2)
                    except Exception:
                        p.kill()
                for f in log_files:
                    f.close()
                return 1
            time.sleep(0.3)

    for f in log_files:
        f.close()

    if foreground:
        log("All servers started in foreground mode.")
        log(f"  PIDs: {pids}")
        log("  Press Ctrl+C to stop all servers.")
        try:
            while True:
                for proc, name in zip(procs, [item[0] for item in SERVER_ORDER]):
                    code = proc.poll()
                    if code is not None:
                        log(f"{name} exited with code {code}")
                        return_code = 0 if code == 0 else code
                        stop_processes([p.pid for p in procs if p.poll() is None], kill_by_port=True)
                        return return_code
                time.sleep(0.5)
        except KeyboardInterrupt:
            log("Received Ctrl+C, stopping all servers...")
            stop_processes([p.pid for p in procs if p.poll() is None], kill_by_port=True)
            return 0

    pid_file = get_pid_file_path(build_dir)
    try:
        write_pid_registry(build_dir, registry)
    except OSError as e:
        log(f"Warning: could not write PID file: {e}")

    if split_windows:
        log("All servers started in split-window mode.")
    else:
        log("All servers started.")
    log(f"  PIDs: {pids}")
    if not split_windows:
        log(f"  Logs: {log_dir}")
        log(f"  Watch with: {get_watch_command_hint()}")
    log(f"  Stop with: {get_stop_command_hint()}")
    return 0


def stop_servers(build_dir: Path, kill_by_port: bool = True) -> int:
    build_dir = normalize_build_dir(build_dir)
    registry = read_pid_registry(build_dir)
    pids = [registry[name] for name, _port in SERVER_ORDER if name in registry]
    result = stop_processes(pids, kill_by_port=kill_by_port)
    write_pid_registry(build_dir, {})
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description="One-click start/stop Mession servers")
    parser.add_argument(
        "command",
        choices=["start", "stop", "start-server", "stop-server", "restart-server"],
        help="server control command",
    )
    parser.add_argument("server_name", nargs="?", help="server name for single-server commands")
    parser.add_argument("--build-dir", type=Path, default=Path("Build"), help="Build output directory (default: Build)")
    parser.add_argument("--no-wait", action="store_true", help="(start only) Do not wait for each server port to be ready")
    parser.add_argument("--foreground", action="store_true", help="(start only) Run servers in the current terminal instead of the background")
    parser.add_argument("--split-windows", action="store_true", help="(start only, Windows) Run each server in its own console window")
    args = parser.parse_args()

    if args.command == "start":
        return start_servers(
            args.build_dir,
            wait_ready=not args.no_wait,
            foreground=args.foreground,
            split_windows=args.split_windows,
        )
    if args.command == "stop":
        return stop_servers(args.build_dir)

    if not args.server_name:
        parser.error("server_name is required for single-server commands")

    server_names = {name for name, _port in SERVER_ORDER}
    if args.server_name not in server_names:
        parser.error(f"unknown server_name: {args.server_name}")

    if args.command == "start-server":
        return start_server(
            args.build_dir,
            args.server_name,
            wait_ready=not args.no_wait,
            foreground=args.foreground,
            split_windows=args.split_windows,
        )
    if args.command == "stop-server":
        return stop_server(args.build_dir, args.server_name)
    return restart_server(args.build_dir, args.server_name, wait_ready=not args.no_wait)


if __name__ == "__main__":
    sys.exit(main())
