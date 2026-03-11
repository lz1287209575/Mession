#!/usr/bin/env python3
"""
Mession 脚本验证 - 启动所有服务器并验证登录流程

用法:
  ./scripts/validate.py [--build-dir build] [--timeout 30]
  
流程:
  1. 编译项目（如需要）
  2. 按顺序启动 Router -> Login -> World -> Scene -> Gateway
  3. 连接 Gateway，发送登录包，验证登录响应
  4. 可选：发送玩家移动，验证转发
  5. 清理并退出
"""

import argparse
import os
import signal
import socket
from typing import List, Optional
import struct
import subprocess
import sys
import time
from pathlib import Path

# 协议常量（与 Messages/NetMessages.h、Common/ServerMessages.h 一致）
MT_LOGIN = 1
MT_LOGIN_RESPONSE = 2
MT_PLAYER_MOVE = 5

# 端口
ROUTER_PORT = 8005
GATEWAY_PORT = 8001
LOGIN_PORT = 8002
WORLD_PORT = 8003
SCENE_PORT = 8004


def log(msg: str) -> None:
    print(f"[validate] {msg}", flush=True)


def build_project(build_dir: Path, project_root: Path) -> bool:
    """编译项目"""
    log("Building project...")
    try:
        subprocess.run(
            ["cmake", "..", "-DCMAKE_BUILD_TYPE=Release"],
            cwd=build_dir,
            check=True,
            capture_output=True,
        )
        subprocess.run(
            ["make", "-j4"],
            cwd=build_dir,
            check=True,
            capture_output=True,
        )
        log("Build OK")
        return True
    except subprocess.CalledProcessError as e:
        log(f"Build failed: {e}")
        return False


def get_executable_path(build_dir: Path, name: str) -> Optional[Path]:
    """获取可执行文件路径（兼容 Windows .exe）"""
    for suffix in ("", ".exe"):
        p = build_dir / (name + suffix)
        if p.exists():
            return p
    return None


def start_server(
    build_dir: Path,
    name: str,
    port: int,
    env_extra: Optional[dict] = None,
) -> Optional[subprocess.Popen]:
    """启动单个服务器"""
    exe = get_executable_path(build_dir, name)
    if not exe:
        log(f"Executable not found: {name}")
        return None
    log(f"Starting {name} on port {port}...")
    env = os.environ.copy()
    if env_extra:
        env.update(env_extra)
    proc = subprocess.Popen(
        [str(exe)],
        cwd=build_dir,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        start_new_session=True,
        env=env,
    )
    return proc


def wait_for_port(host: str, port: int, timeout: float) -> bool:
    """等待端口可连接"""
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            s.settimeout(1.0)
            s.connect((host, port))
            s.close()
            return True
        except (socket.error, OSError):
            time.sleep(0.1)
    return False


def send_login(sock: socket.socket, player_id: int = 12345) -> tuple[bool, int, int]:
    """
    发送登录包，返回 (成功, SessionKey, PlayerId)
    协议: Length(4) + MsgType(1) + PlayerId(8)
    """
    payload = struct.pack("<BQ", MT_LOGIN, player_id)  # little endian
    length = len(payload)
    packet = struct.pack("<I", length) + payload

    sock.sendall(packet)

    # 接收响应: Length(4) + MsgType(1) + SessionKey(4) + PlayerId(8)
    sock.settimeout(5.0)
    header = sock.recv(4)
    if len(header) < 4:
        return False, 0, 0

    resp_len = struct.unpack("<I", header)[0]
    if resp_len < 1 + 4 + 8:
        return False, 0, 0

    body = b""
    while len(body) < resp_len:
        chunk = sock.recv(resp_len - len(body))
        if not chunk:
            return False, 0, 0
        body += chunk

    msg_type = body[0]
    if msg_type != MT_LOGIN_RESPONSE:
        log(f"Unexpected response type: {msg_type}")
        return False, 0, 0

    session_key = struct.unpack("<I", body[1:5])[0]
    resp_player_id = struct.unpack("<Q", body[5:13])[0]
    return True, session_key, resp_player_id


def send_player_move(sock: socket.socket, x: float, y: float, z: float) -> bool:
    """
    发送玩家移动包
    协议: Length(4) + MsgType(1) + X(4) + Y(4) + Z(4)
    """
    payload = struct.pack("<Bfff", MT_PLAYER_MOVE, x, y, z)
    length = len(payload)
    packet = struct.pack("<I", length) + payload
    sock.sendall(packet)
    return True


def run_validation(
    build_dir: Path,
    timeout: float,
    zone_id: Optional[int] = None,
) -> bool:
    """执行完整验证流程"""
    project_root = build_dir.parent
    procs: List[subprocess.Popen] = []
    env_zone = {"MESSION_ZONE_ID": str(zone_id)} if zone_id is not None else None

    def cleanup():
        for p in reversed(procs):
            try:
                p.terminate()
                p.wait(timeout=3)
            except Exception:
                p.kill()

    try:
        # 1. 启动 Router
        p = start_server(build_dir, "RouterServer", ROUTER_PORT)
        if not p:
            return False
        procs.append(p)
        if not wait_for_port("127.0.0.1", ROUTER_PORT, timeout):
            log("RouterServer did not become ready")
            return False
        time.sleep(0.5)

        # 2. 启动 Login, World, Scene, Gateway（可选 Zone）
        for name, port in [
            ("LoginServer", LOGIN_PORT),
            ("WorldServer", WORLD_PORT),
            ("SceneServer", SCENE_PORT),
            ("GatewayServer", GATEWAY_PORT),
        ]:
            p = start_server(build_dir, name, port, env_zone)
            if not p:
                return False
            procs.append(p)

        # 3. 等待 Gateway 就绪
        if not wait_for_port("127.0.0.1", GATEWAY_PORT, timeout):
            log("GatewayServer did not become ready")
            return False

        # 4. 等待后端握手完成
        time.sleep(2.0)

        # 5. 多玩家登录
        log("Test 1: Multi-player login...")
        clients = []
        for i, pid in enumerate([10001, 10002, 10003]):
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.settimeout(10.0)
            sock.connect(("127.0.0.1", GATEWAY_PORT))
            ok, session_key, resp_pid = send_login(sock, pid)
            if not ok:
                log(f"Multi-player login failed for player {pid}")
                for s in clients:
                    s.close()
                sock.close()
                return False
            log(f"  Player {pid} OK: SessionKey={session_key}")
            clients.append((sock, pid))

        for sock, pid in clients:
            send_player_move(sock, float(pid % 10), 0.0, 0.0)
        log("  Multi-player move sent")

        # 6. 断线重连
        log("Test 2: Reconnect...")
        clients[0][0].close()
        clients[0] = (None, clients[0][1])
        time.sleep(0.2)

        sock2 = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock2.settimeout(10.0)
        sock2.connect(("127.0.0.1", GATEWAY_PORT))
        ok, session_key2, resp_pid2 = send_login(sock2, 10001)
        if not ok:
            log("Reconnect login failed")
            sock2.close()
            for s, _ in clients:
                if s:
                    s.close()
            return False
        log(f"  Reconnect OK: SessionKey={session_key2}")

        # 7. 清理
        for sock, _ in clients:
            if sock:
                sock.close()
        sock2.close()

        log("Validation PASSED")
        return True

    finally:
        cleanup()


def main() -> int:
    parser = argparse.ArgumentParser(description="Mession script validation")
    parser.add_argument(
        "--build-dir",
        default="build",
        help="Build directory (default: build)",
    )
    parser.add_argument(
        "--no-build",
        action="store_true",
        help="Skip build step",
    )
    parser.add_argument(
        "--timeout",
        type=float,
        default=30.0,
        help="Timeout for server startup (default: 30)",
    )
    parser.add_argument(
        "--zone",
        type=int,
        default=None,
        metavar="ID",
        help="Test with zone_id (sets MESSION_ZONE_ID for all servers)",
    )
    args = parser.parse_args()

    project_root = Path(__file__).resolve().parent.parent
    build_dir = (project_root / args.build_dir).resolve()

    if not build_dir.exists() and not args.no_build:
        build_dir.mkdir(parents=True, exist_ok=True)

    if not args.no_build and not build_project(build_dir, project_root):
        return 1

    if not get_executable_path(build_dir, "GatewayServer"):
        log(f"GatewayServer not found in {build_dir}")
        return 1

    return 0 if run_validation(build_dir, args.timeout, args.zone) else 1


if __name__ == "__main__":
    sys.exit(main())
