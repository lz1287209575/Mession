#!/usr/bin/env python3
"""
Mession 最小链路验证

当前脚本对齐新的 server skeleton，只验证最核心的闭环：
1. 编译项目（可选）
2. 启动 Router -> Mgo -> Login -> World -> Scene -> Gateway
3. 客户端连接 Gateway，走统一 MT_FunctionCall
4. 验证 Client_Login / Client_FindPlayer / Client_SwitchScene / Client_Logout
5. 验证转发 ClientCall 的异常链路
5. 清理服务器进程并退出
"""

import os
import shutil
import socket
import subprocess
import sys
import time
from pathlib import Path
from typing import Optional

from build_systems import run_build
from validation.cli import main as validation_cli_main
from validation.legacy_runtime import (
    GATEWAY_PORT,
    LOGIN_PORT,
    MGO_PORT,
    ROUTER_PORT,
    SCENE_PORT,
    WORLD_PORT,
    LegacyValidationRuntime,
    LegacyValidationRuntimeHooks,
)
from validation.runner import ValidationRuntimeHooks


def log(msg: str) -> None:
    print(f"[validate] {msg}", flush=True)


def stop_lingering_servers() -> None:
    server_names = ["GatewayServer", "LoginServer", "WorldServer", "SceneServer", "RouterServer", "MgoServer"]

    pkill = shutil.which("pkill")
    if pkill:
        for name in server_names:
            try:
                subprocess.run([pkill, "-f", f"/{name}"], check=False, capture_output=True, timeout=3)
            except subprocess.SubprocessError:
                pass

    fuser = shutil.which("fuser")
    if fuser and sys.platform != "win32":
        for port in [GATEWAY_PORT, LOGIN_PORT, WORLD_PORT, SCENE_PORT, ROUTER_PORT, MGO_PORT]:
            try:
                subprocess.run([fuser, "-k", f"{port}/tcp"], check=False, capture_output=True, timeout=3)
            except subprocess.SubprocessError:
                pass

    time.sleep(0.5)


def build_project(
    build_dir: Path,
    build_system_name: Optional[str] = None,
    build_system_config: Optional[Path] = None,
) -> bool:
    log("Building project...")
    rc = run_build(
        build_dir=build_dir,
        build_system_name=build_system_name,
        config_path=build_system_config,
    )
    if rc == 0:
        log("Build OK")
        return True
    log(f"Build failed with exit code {rc}")
    return False


def get_executable_path(build_dir: Path, name: str) -> Optional[Path]:
    project_root = build_dir.parent if build_dir.is_absolute() else Path(__file__).resolve().parent.parent
    bin_dir = project_root / "Bin"
    for suffix in ("", ".exe"):
        candidate = bin_dir / (name + suffix)
        if candidate.exists():
            return candidate
    return None


def start_server(
    build_dir: Path,
    name: str,
    env_extra: Optional[dict],
    log_dir: Path,
) -> Optional[subprocess.Popen]:
    exe = get_executable_path(build_dir, name)
    if not exe:
        log(f"Executable not found: {name}")
        return None

    env = os.environ.copy()
    if env_extra:
        env.update(env_extra)

    stdout_dst = subprocess.DEVNULL
    stderr_dst = subprocess.DEVNULL
    log_path = log_dir / f"{name}.log"
    try:
        handle = open(log_path, "w", encoding="utf-8")
        stdout_dst = stderr_dst = handle
    except OSError:
        pass

    log(f"Starting {name}...")
    return subprocess.Popen(
        [str(exe)],
        cwd=build_dir,
        stdout=stdout_dst,
        stderr=stderr_dst,
        start_new_session=True,
        env=env,
    )


def wait_for_port(host: str, port: int, timeout: float) -> bool:
    deadline = time.time() + timeout
    while time.time() < deadline:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(1.0)
        try:
            sock.connect((host, port))
            sock.close()
            return True
        except (socket.error, OSError):
            time.sleep(0.1)
        finally:
            try:
                sock.close()
            except OSError:
                pass
    return False


def main(argv: Optional[list[str]] = None) -> int:
    legacy_runtime = LegacyValidationRuntime(
        LegacyValidationRuntimeHooks(
            log=log,
            stop_lingering_servers=stop_lingering_servers,
            start_server=start_server,
            wait_for_port=wait_for_port,
        )
    )
    hooks = ValidationRuntimeHooks(
        log=log,
        build_project=build_project,
        get_executable_path=get_executable_path,
        run_validation=legacy_runtime.run_validation,
    )
    return validation_cli_main(hooks, argv)


if __name__ == "__main__":
    sys.exit(main())
