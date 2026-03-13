#!/usr/bin/env python3
"""
Mession 脚本验证 - 启动所有服务器并验证登录、复制与清理路径

用法:
  ./Scripts/validate.py [--build-dir Build] [--timeout 30]

流程:
  1. 编译项目（如需要）
  2. 按顺序启动 Router -> Login -> World -> Scene -> Gateway
  3. Test 1: 多玩家登录，验证 SessionKey/PlayerId
  4. Test 2: 复制链路 - 登录后收包，断言至少收到 MT_ActorCreate
  5. 发送玩家移动
  6. Test 3: 清理路径 - 一端断线后重连同一 PlayerId，验证 Gateway/World 已回收状态
  7. Test 4: 并发 - 多线程同时连接登录，验证服务端可稳定处理并发
  8. 可选 --stress：压力测试，大量并发连接 + 登录 + 多发收包
  9. 清理并退出
"""

import argparse
import os
import signal
import socket
from concurrent.futures import ThreadPoolExecutor, as_completed
from typing import List, Optional
import struct
import subprocess
import sys
import time
from pathlib import Path

# 协议常量（与 Source/Messages/NetMessages.h、Source/Common/ServerMessages.h 一致）
MT_LOGIN = 1
MT_LOGIN_RESPONSE = 2
MT_PLAYER_MOVE = 5
MT_ACTOR_CREATE = 6
MT_ACTOR_DESTROY = 7
MT_ACTOR_UPDATE = 8

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
    debug_log_dir: Optional[Path] = None,
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
    stdout_dst = subprocess.DEVNULL
    stderr_dst = subprocess.DEVNULL
    if debug_log_dir:
        log_path = debug_log_dir / f"{name}.log"
        try:
            f = open(log_path, "w", encoding="utf-8")
            stdout_dst = stderr_dst = f
        except OSError:
            pass
    proc = subprocess.Popen(
        [str(exe)],
        cwd=build_dir,
        stdout=stdout_dst,
        stderr=stderr_dst,
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


def recv_one_packet(sock: socket.socket, timeout: float) -> Optional[tuple[int, bytes]]:
    """
    接收一个长度前缀包，返回 (msg_type, payload) 或 None（超时/断连）。
    协议: Length(4) + MsgType(1) + Payload...
    """
    sock.settimeout(timeout)
    try:
        header = sock.recv(4)
        if len(header) < 4:
            return None
        length = struct.unpack("<I", header)[0]
        if length < 1:
            return None
        body = b""
        while len(body) < length:
            chunk = sock.recv(length - len(body))
            if not chunk:
                return None
            body += chunk
        return (body[0], body[1:])
    except socket.timeout:
        return None
    except (socket.error, OSError):
        return None


def collect_replication_packets(
    sock: socket.socket, duration: float, want_types: set[int]
) -> list[tuple[int, bytes]]:
    """
    在 duration 秒内尽量收包，返回 msg_type 在 want_types 中的 (msg_type, payload) 列表。
    """
    deadline = time.time() + duration
    found: list[tuple[int, bytes]] = []
    while time.time() < deadline:
        pkt = recv_one_packet(sock, timeout=0.3)
        if pkt is None:
            continue
        msg_type, payload = pkt
        if msg_type in want_types:
            found.append((msg_type, payload))
    return found


def run_stress(
    num_clients: int,
    moves_per_client: int,
    recv_timeout: float,
) -> tuple[int, int, float]:
    """
    压力测试：num_clients 个客户端并发连接，每人登录后发 moves_per_client 次移动并收包。
    返回 (成功数, 失败数, 耗时秒)。
    """
    base_pid = 30000
    ok_count = 0
    fail_count = 0
    start = time.perf_counter()

    def one_stress_client(idx: int) -> bool:
        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.settimeout(recv_timeout)
            sock.connect(("127.0.0.1", GATEWAY_PORT))
            ok, _, _ = send_login(sock, base_pid + idx)
            if not ok:
                sock.close()
                return False
            for _ in range(moves_per_client):
                send_player_move(sock, 1.0, 0.0, 0.0)
            for _ in range(moves_per_client):
                pkt = recv_one_packet(sock, timeout=0.5)
                if pkt is None:
                    break
            sock.close()
            return True
        except Exception:
            return False

    with ThreadPoolExecutor(max_workers=min(num_clients, 256)) as ex:
        futures = [ex.submit(one_stress_client, i) for i in range(num_clients)]
        for f in as_completed(futures):
            if f.result():
                ok_count += 1
            else:
                fail_count += 1
    elapsed = time.perf_counter() - start
    return ok_count, fail_count, elapsed


def run_validation(
    build_dir: Path,
    timeout: float,
    zone_id: Optional[int] = None,
    debug_log_dir: Optional[Path] = None,
    stress_clients: int = 0,
    stress_moves: int = 5,
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
        p = start_server(build_dir, "RouterServer", ROUTER_PORT, None, debug_log_dir)
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
            p = start_server(build_dir, name, port, env_zone, debug_log_dir)
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

        # 6. 复制链路：至少一名客户端应收到其他玩家的 MT_ActorCreate
        log("Test 2: Replication (ActorCreate)...")
        time.sleep(2.0)  # 等待路由、Session 校验、AddPlayer 与复制包下发
        all_creates = []
        for sock, _ in clients:
            replication = collect_replication_packets(
                sock, 1.5, {MT_ACTOR_CREATE, MT_ACTOR_UPDATE}
            )
            all_creates.extend(p for p in replication if p[0] == MT_ACTOR_CREATE)
        if not all_creates:
            log("  No MT_ActorCreate received; replication path broken")
            for s, _ in clients:
                if s:
                    s.close()
            return False
        log(f"  Received {len(all_creates)} MT_ActorCreate (replication OK)")

        for sock, pid in clients:
            send_player_move(sock, float(pid % 10), 0.0, 0.0)
        log("  Multi-player move sent")

        # 7. 清理路径：断线后 Gateway/World 回收状态，该玩家可再次登录
        log("Test 3: Cleanup path (disconnect then reconnect)...")
        clients[0][0].close()
        clients[0] = (None, clients[0][1])
        time.sleep(0.3)
        send_player_move(clients[1][0], 1.0, 0.0, 0.0)

        sock2 = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock2.settimeout(10.0)
        sock2.connect(("127.0.0.1", GATEWAY_PORT))
        ok, session_key2, resp_pid2 = send_login(sock2, 10001)
        if not ok:
            log("Reconnect login failed (cleanup path broken)")
            sock2.close()
            for s, _ in clients:
                if s:
                    s.close()
            return False
        log(f"  Reconnect OK: SessionKey={session_key2}")

        # 8. 并发测试：多线程同时连接并登录
        log("Test 4: Concurrency (parallel login)...")
        concurrency = 20
        base_pid = 20000

        def one_login(idx: int) -> bool:
            try:
                sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                sock.settimeout(10.0)
                sock.connect(("127.0.0.1", GATEWAY_PORT))
                ok, _, _ = send_login(sock, base_pid + idx)
                sock.close()
                return ok
            except Exception:
                return False

        ok_count = 0
        with ThreadPoolExecutor(max_workers=concurrency) as ex:
            futures = [ex.submit(one_login, i) for i in range(concurrency)]
            for f in as_completed(futures):
                if f.result():
                    ok_count += 1
        if ok_count != concurrency:
            log(f"  Concurrency test failed: {ok_count}/{concurrency} logins OK")
            for s, _ in clients:
                if s:
                    s.close()
            sock2.close()
            return False
        log(f"  {concurrency} parallel logins OK")

        # 9. 压力测试（可选）
        if stress_clients > 0:
            log(f"Stress test: {stress_clients} clients, {stress_moves} moves each...")
            ok_s, fail_s, elapsed = run_stress(stress_clients, stress_moves, recv_timeout=8.0)
            rate = ok_s / elapsed if elapsed > 0 else 0
            log(f"  Stress result: {ok_s} OK, {fail_s} fail, {elapsed:.2f}s ({rate:.0f} conn/s)")
            if ok_s < stress_clients * 0.9:
                log("  Stress test failed: success rate < 90%")
                for s, _ in clients:
                    if s:
                        s.close()
                sock2.close()
                return False

        # 10. 清理
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
        default="Build",
        help="Build directory (default: Build)",
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
    parser.add_argument(
        "--debug",
        action="store_true",
        help="Write server stderr to build_dir/validate_logs/<Server>.log",
    )
    parser.add_argument(
        "--stress",
        type=int,
        default=0,
        metavar="N",
        help="Run stress test with N concurrent clients (default 0 = disabled)",
    )
    parser.add_argument(
        "--stress-moves",
        type=int,
        default=5,
        metavar="M",
        help="Moves per client during stress test (default 5)",
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

    debug_log_dir = None
    if args.debug:
        debug_log_dir = build_dir / "validate_logs"
        debug_log_dir.mkdir(parents=True, exist_ok=True)
        log(f"Debug logs: {debug_log_dir}")

    ok = run_validation(
        build_dir,
        args.timeout,
        args.zone,
        debug_log_dir,
        stress_clients=args.stress,
        stress_moves=args.stress_moves,
    )
    if args.debug and debug_log_dir:
        gateway_log = debug_log_dir / "GatewayServer.log"
        if gateway_log.exists():
            text = gateway_log.read_text(encoding="utf-8", errors="replace")
            if "Forwarding World" in text:
                log("Gateway log contains 'Forwarding World' (replication reached Gateway)")
            else:
                log("Gateway log has no 'Forwarding World' (replication did not reach Gateway)")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
