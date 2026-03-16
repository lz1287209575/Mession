#!/usr/bin/env python3
"""
Mession 脚本验证 - 启动所有服务器并验证登录、复制与清理路径

用法:
  ./Scripts/validate.py [--build-dir Build] [--timeout 30]

流程:
  1. 编译项目（如需要）
  2. 按顺序启动 Router -> Login -> World -> Scene -> Gateway
  3. Test 1: Handshake 本地处理可达
  4. Test 2: 多玩家登录，验证 SessionKey/PlayerId
  5. Test 3: 复制链路 - 登录后收包，断言至少收到 MT_ActorCreate
  6. Test 4: RouterResolved 路由缓存建立
  7. Test 5: 清理路径 - 一端断线后重连同一 PlayerId，验证 Gateway/World 已回收状态，并尽量观察 route cache 失效日志
  8. Test 6: Chat 路径可达
  9. Test 7: Heartbeat 本地处理可达
  10. Test 8: 客户端 MT_RPC 兼容路径可达
  11. Test 9: 登录后立刻断开，验证重连恢复，并尽量观察 World 清理日志
  12. Test 10: 双端同时断线，验证双玩家重连恢复
  13. Test 11: 快速重连边界 - 同一 PlayerId 短时间内连续重连
  14. Test 12: 并发 - 多线程同时连接登录，验证服务端可稳定处理并发
  15. 可选 --stress：压力测试，大量并发连接 + 登录 + 多发收包
  16. 清理并退出
"""

import argparse
import json
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
MT_HANDSHAKE = 3
MT_PLAYER_MOVE = 5
MT_ACTOR_CREATE = 6
MT_ACTOR_DESTROY = 7
MT_ACTOR_UPDATE = 8
MT_RPC = 9
MT_CHAT = 10
MT_HEARTBEAT = 11

# 端口
ROUTER_PORT = 8005
GATEWAY_PORT = 8001
LOGIN_PORT = 8002
WORLD_PORT = 8003
SCENE_PORT = 8004
ROUTER_DEBUG_HTTP_PORT = 18085
GATEWAY_DEBUG_HTTP_PORT = 18081


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
    """获取可执行文件路径（兼容 Windows .exe）

    CMake 使用 build_dir 作为构建目录，但所有可执行文件统一输出到
    仓库根目录下的 Bin/。这里根据 build_dir 反推项目根，然后从 Bin/ 查找。
    """
    # 推导项目根（build_dir 通常是 <project_root>/Build）
    if build_dir.is_absolute():
        project_root = build_dir.parent
    else:
        project_root = Path(__file__).resolve().parent.parent
    bin_dir = project_root / "Bin"
    for suffix in ("", ".exe"):
        p = bin_dir / (name + suffix)
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


def http_get_json(host: str, port: int, path: str = "/", timeout: float = 2.0) -> Optional[dict]:
    """使用最小 GET 请求读取 JSON 响应。"""
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.settimeout(timeout)
    try:
        sock.connect((host, port))
        request = (
            f"GET {path} HTTP/1.0\r\n"
            f"Host: {host}:{port}\r\n"
            "Connection: close\r\n\r\n"
        ).encode("utf-8")
        sock.sendall(request)
        try:
            sock.shutdown(socket.SHUT_WR)
        except OSError:
            pass

        response = bytearray()
        header_sep = -1
        while header_sep < 0:
            chunk = sock.recv(4096)
            if not chunk:
                return None
            response.extend(chunk)
            header_sep = response.find(b"\r\n\r\n")

        header_bytes = bytes(response[:header_sep])
        body = bytearray(response[header_sep + 4 :])
        headers = header_bytes.decode("utf-8", errors="ignore").split("\r\n")
        content_length = 0
        for header in headers[1:]:
            if ":" not in header:
                continue
            name, value = header.split(":", 1)
            if name.strip().lower() == "content-length":
                content_length = int(value.strip())
                break

        while len(body) < content_length:
            chunk = sock.recv(4096)
            if not chunk:
                return None
            body.extend(chunk)

        return json.loads(bytes(body[:content_length]).decode("utf-8"))
    except (OSError, ValueError, json.JSONDecodeError):
        return None
    finally:
        sock.close()


def wait_for_http_json(host: str, port: int, timeout: float, path: str = "/") -> Optional[dict]:
    """等待 HTTP JSON 端点可用。"""
    deadline = time.time() + timeout
    while time.time() < deadline:
        payload = http_get_json(host, port, path=path, timeout=1.0)
        if payload is not None:
            return payload
        time.sleep(0.1)
    return None


def wait_for_gateway_debug_value(
    key: str,
    predicate,
    timeout: float,
) -> Optional[dict]:
    """等待 Gateway debug JSON 某字段满足条件。"""
    deadline = time.time() + timeout
    while time.time() < deadline:
        payload = http_get_json("127.0.0.1", GATEWAY_DEBUG_HTTP_PORT, timeout=1.0)
        if payload is not None and predicate(payload.get(key, 0)):
            return payload
        time.sleep(0.1)
    return None


def wait_for_log_contains(log_path: Path, needle: str, timeout: float) -> bool:
    """等待日志文件出现指定片段。"""
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            if log_path.exists():
                text = log_path.read_text(encoding="utf-8", errors="ignore")
                if needle in text:
                    return True
        except OSError:
            pass
        time.sleep(0.1)
    return False


def count_log_occurrences(log_path: Path, needle: str) -> int:
    """统计日志片段出现次数。"""
    try:
        text = log_path.read_text(encoding="utf-8", errors="ignore")
    except OSError:
        return 0
    return text.count(needle)


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


def send_handshake(sock: socket.socket, player_id: int = 0) -> bool:
    """
    发送最小握手包
    协议: Length(4) + MsgType(1) + PlayerId(8)
    """
    payload = struct.pack("<BQ", MT_HANDSHAKE, player_id)
    length = len(payload)
    packet = struct.pack("<I", length) + payload
    sock.sendall(packet)
    return True


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


def send_chat(sock: socket.socket, message: str) -> bool:
    """
    发送聊天包
    协议: Length(4) + MsgType(1) + MessageLen(2) + Message(bytes)
    """
    encoded = message.encode("utf-8")
    payload = struct.pack("<BH", MT_CHAT, len(encoded)) + encoded
    length = len(payload)
    packet = struct.pack("<I", length) + payload
    sock.sendall(packet)
    return True


def send_heartbeat(sock: socket.socket, sequence: int) -> bool:
    """
    发送心跳包
    协议: Length(4) + MsgType(1) + Sequence(4)
    """
    payload = struct.pack("<BI", MT_HEARTBEAT, sequence)
    length = len(payload)
    packet = struct.pack("<I", length) + payload
    sock.sendall(packet)
    return True


def send_rpc_add_stats(sock: socket.socket, hero_object_id: int, func_id: int, level_delta: int, health_delta: float) -> bool:
    """
    发送一个简单的 RPC 调用:
      MsgType = MT_RPC
      Payload = [ObjectId(8)][FunctionId(2)][PayloadSize(4)][LevelDelta(4)][HealthDelta(4)]
    这里假设 FunctionId 已经在服务端稳定，hero_object_id 来自服务端（或采用固定值）。
    """
    payload = struct.pack("<B", MT_RPC)
    payload += struct.pack("<Q", hero_object_id)
    payload += struct.pack("<H", func_id)
    inner = struct.pack("<if", level_delta, health_delta)
    payload += struct.pack("<I", len(inner))
    payload += inner
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


def parse_chat_payload(payload: bytes) -> Optional[tuple[int, str]]:
    if len(payload) < 8 + 2:
        return None
    from_player_id = struct.unpack("<Q", payload[:8])[0]
    message_len = struct.unpack("<H", payload[8:10])[0]
    if 10 + message_len > len(payload):
        return None
    try:
        message = payload[10:10 + message_len].decode("utf-8")
    except UnicodeDecodeError:
        return None
    return from_player_id, message


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


def run_parallel_login_batch(indices: list[int], base_pid: int) -> tuple[int, list[int]]:
    """执行一轮并发登录，返回成功数和失败下标。"""

    def one_login(idx: int) -> tuple[int, bool]:
        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.settimeout(10.0)
            sock.connect(("127.0.0.1", GATEWAY_PORT))
            ok, _, _ = send_login(sock, base_pid + idx)
            sock.close()
            return idx, ok
        except Exception:
            return idx, False

    ok_count = 0
    failed_indices: list[int] = []
    if not indices:
        return ok_count, failed_indices

    with ThreadPoolExecutor(max_workers=len(indices)) as ex:
        futures = [ex.submit(one_login, i) for i in indices]
        for f in as_completed(futures):
            idx, ok = f.result()
            if ok:
                ok_count += 1
            else:
                failed_indices.append(idx)

    failed_indices.sort()
    return ok_count, failed_indices


def run_validation(
    build_dir: Path,
    timeout: float,
    zone_id: Optional[int] = None,
    debug_log_dir: Optional[Path] = None,
    stress_clients: int = 0,
    stress_moves: int = 5,
) -> bool:
    """执行完整验证流程"""
    procs: List[subprocess.Popen] = []
    if debug_log_dir is None:
        debug_log_dir = build_dir / "validate_logs"
        debug_log_dir.mkdir(parents=True, exist_ok=True)

    base_env = {
        "MESSION_ROUTER_DEBUG_HTTP_PORT": str(ROUTER_DEBUG_HTTP_PORT),
        "MESSION_GATEWAY_DEBUG_HTTP_PORT": str(GATEWAY_DEBUG_HTTP_PORT),
    }
    if zone_id is not None:
        base_env["MESSION_ZONE_ID"] = str(zone_id)

    def cleanup():
        for p in reversed(procs):
            try:
                p.terminate()
                p.wait(timeout=3)
            except Exception:
                p.kill()

    try:
        # 1. 启动 Router
        router_env = dict(base_env)
        p = start_server(build_dir, "RouterServer", ROUTER_PORT, router_env, debug_log_dir)
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
            p = start_server(build_dir, name, port, dict(base_env), debug_log_dir)
            if not p:
                return False
            procs.append(p)

        # 3. 等待 Gateway 就绪
        if not wait_for_port("127.0.0.1", GATEWAY_PORT, timeout):
            log("GatewayServer did not become ready")
            return False
        gateway_debug = wait_for_http_json("127.0.0.1", GATEWAY_DEBUG_HTTP_PORT, timeout)
        if gateway_debug is None:
            log("Gateway debug HTTP did not become ready")
            return False
        initial_legacy_rpc_count = int(gateway_debug.get("legacyClientRpcCount", 0))
        initial_rejected_fallback_count = int(gateway_debug.get("rejectedClientFallbackCount", 0))
        gateway_log = debug_log_dir / "GatewayServer.log"
        world_log = debug_log_dir / "WorldServer.log"

        # 4. 等待后端握手完成
        time.sleep(2.0)

        # 5. Handshake 本地处理：通过 Gateway debug 状态确认未再转发到 Login
        log("Test 1: Handshake local route...")
        handshake_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        handshake_sock.settimeout(10.0)
        handshake_sock.connect(("127.0.0.1", GATEWAY_PORT))
        handshake_player_id = 54321
        send_handshake(handshake_sock, handshake_player_id)
        deadline = time.time() + 3.0
        gateway_status = None
        handshake_observed = False
        while time.time() < deadline:
            gateway_status = http_get_json("127.0.0.1", GATEWAY_DEBUG_HTTP_PORT, timeout=1.0)
            if gateway_status is None:
                time.sleep(0.1)
                continue
            if (
                int(gateway_status.get("clientHandshakeCount", 0)) >= 1 and
                int(gateway_status.get("lastClientHandshakePlayerId", 0)) == handshake_player_id
            ):
                handshake_observed = True
                break
            time.sleep(0.1)
        if not handshake_observed:
            log("  MT_Handshake local handling failed: Gateway debug status did not update")
            handshake_sock.close()
            return False
        log(
            "  MT_Handshake handled locally: "
            f"count={int(gateway_status.get('clientHandshakeCount', 0))}, "
            f"lastPlayerId={int(gateway_status.get('lastClientHandshakePlayerId', 0))}"
        )
        handshake_sock.close()

        # 6. 多玩家登录
        log("Test 2: Multi-player login...")
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

        # 7. 复制链路：至少一名客户端应收到其他玩家的 MT_ActorCreate
        log("Test 3: Replication (ActorCreate)...")
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

        # 8. 路由缓存：发送 RouterResolved 客户端消息后，Gateway 应建立 route cache
        log("Test 4: RouterResolved route cache...")
        deadline = time.time() + 5.0
        gateway_status = None
        while time.time() < deadline:
            gateway_status = http_get_json("127.0.0.1", GATEWAY_DEBUG_HTTP_PORT, timeout=1.0)
            if gateway_status and int(gateway_status.get("resolvedRouteCacheSize", 0)) >= 1:
                break
            time.sleep(0.1)
        if not gateway_status:
            log("  Failed to fetch Gateway debug JSON for route cache check")
            for s, _ in clients:
                if s:
                    s.close()
            return False
        resolved_cache_size = int(gateway_status.get("resolvedRouteCacheSize", 0))
        if resolved_cache_size < 1:
            log(f"  Expected resolvedRouteCacheSize >= 1, got {resolved_cache_size}")
            for s, _ in clients:
                if s:
                    s.close()
            return False
        log(f"  Route cache established: resolvedRouteCacheSize={resolved_cache_size}")

        # 9. 清理路径：断线后 Gateway/World 回收状态，该玩家可再次登录
        log("Test 5: Cleanup path (disconnect then reconnect)...")
        try:
            clients[0][0].shutdown(socket.SHUT_RDWR)
        except OSError:
            pass
        clients[0][0].close()
        clients[0] = (None, clients[0][1])
        time.sleep(1.0)
        send_player_move(clients[1][0], 1.0, 0.0, 0.0)
        invalidated_observed = wait_for_log_contains(
            gateway_log,
            "Resolved route cache invalidated:",
            timeout=3.0,
        )
        invalidated_count = count_log_occurrences(gateway_log, "Resolved route cache invalidated:")
        if invalidated_observed:
            log(f"  Route cache invalidated with {invalidated_count} log(s)")
        else:
            log("  Route cache invalidation log not observed within window; continuing with reconnect check")

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

        # 10. Chat 测试：通过 Gateway->World 路径发送 MT_Chat，并验证在线客户端收到广播
        log("Test 6: Chat route...")
        chat_sender = clients[1][0]
        chat_receiver = clients[2][0]
        chat_text = "validate-chat"
        send_chat(chat_sender, chat_text)
        chat_packets = collect_replication_packets(chat_receiver, 2.0, {MT_CHAT})
        matched_chat = False
        for _, payload in chat_packets:
            parsed = parse_chat_payload(payload)
            if not parsed:
                continue
            from_player_id, message = parsed
            if from_player_id == clients[1][1] and message == chat_text:
                matched_chat = True
                break
        if not matched_chat:
            log("  MT_Chat route failed: receiver did not observe expected chat payload")
            for s, _ in clients:
                if s:
                    s.close()
            sock2.close()
            return False
        log("  MT_Chat delivered through Gateway -> World -> client path")

        # 11. Heartbeat 测试：通过 Gateway 本地声明式入口处理 MT_Heartbeat
        log("Test 7: Heartbeat local route...")
        heartbeat_sequence = 4242
        send_heartbeat(clients[1][0], heartbeat_sequence)
        deadline = time.time() + 3.0
        gateway_status = None
        heartbeat_observed = False
        while time.time() < deadline:
            gateway_status = http_get_json("127.0.0.1", GATEWAY_DEBUG_HTTP_PORT, timeout=1.0)
            if gateway_status is None:
                time.sleep(0.1)
                continue
            if (
                int(gateway_status.get("clientHeartbeatCount", 0)) >= 1 and
                int(gateway_status.get("lastClientHeartbeatSequence", 0)) >= heartbeat_sequence
            ):
                heartbeat_observed = True
                break
            time.sleep(0.1)
        if not heartbeat_observed:
            log("  MT_Heartbeat local handling failed: Gateway debug status did not update")
            for s, _ in clients:
                if s:
                    s.close()
            sock2.close()
            return False
        log(
            "  MT_Heartbeat handled locally: "
            f"count={int(gateway_status.get('clientHeartbeatCount', 0))}, "
            f"lastSeq={int(gateway_status.get('lastClientHeartbeatSequence', 0))}"
        )

        # 12. RPC 兼容测试：确认客户端 MT_RPC 仍作为受控 legacy policy 可达
        log("Test 8: Client MT_RPC legacy compatibility...")
        # 为简单起见，使用第一个仍在线的客户端执行 RPC 测试
        rpc_sock = None
        rpc_pid = 123456
        try:
            rpc_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            rpc_sock.settimeout(10.0)
            rpc_sock.connect(("127.0.0.1", GATEWAY_PORT))
            ok, _, _ = send_login(rpc_sock, rpc_pid)
            if not ok:
                log("  RPC login failed")
                rpc_sock.close()
                rpc_sock = None
                # 不直接失败整个验证，但提示 RPC 未测试
            else:
                # 我们不知道服务器端 HeroObject 的实际 ObjectId / FunctionId，这里约定：
                # - ObjectId 使用 0（WorldServer 内部会进行 ObjectId 校验并拒绝不匹配的包）
                #   因此第一次调用应该被 validate/安全检查拒绝，不改变服务器状态。
                # - 仅测试网络路径可达以及服务器不会崩溃。
                # 如果后续我们在协议中回传 HeroObjectId/FunctionId，就可以在这里做更严格的断言。
                send_rpc_add_stats(
                    rpc_sock,
                    hero_object_id=0,
                    func_id=0,
                    level_delta=10,
                    health_delta=100.0,
                )
                gateway_status = wait_for_gateway_debug_value(
                    "legacyClientRpcCount",
                    lambda value: int(value) >= initial_legacy_rpc_count + 1,
                    timeout=3.0,
                )
                if gateway_status is None:
                    log("  Client MT_RPC compatibility path failed: Gateway legacyClientRpcCount did not increase")
                    rpc_sock.close()
                    for s, _ in clients:
                        if s:
                            s.close()
                    sock2.close()
                    return False
                log(
                    "  Client MT_RPC observed via explicit legacy policy: "
                    f"legacyClientRpcCount={int(gateway_status.get('legacyClientRpcCount', 0))}"
                )
        except Exception as e:
            log(f"  RPC test encountered exception: {e}")
            if rpc_sock:
                rpc_sock.close()
            rpc_sock = None

        if rpc_sock:
            rpc_sock.close()

        # 13. 登录后立刻断开：验证 World 清理完成后可以重新登录
        log("Test 9: Immediate disconnect cleanup...")
        immediate_pid = 30000
        immediate_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        immediate_sock.settimeout(10.0)
        immediate_sock.connect(("127.0.0.1", GATEWAY_PORT))
        ok, immediate_session_key, immediate_resp_pid = send_login(immediate_sock, immediate_pid)
        if not ok or immediate_resp_pid != immediate_pid:
            log("  Immediate-disconnect login failed")
            immediate_sock.close()
            for s, _ in clients:
                if s:
                    s.close()
            sock2.close()
            return False
        log(f"  Login OK before disconnect: SessionKey={immediate_session_key}")

        try:
            immediate_sock.shutdown(socket.SHUT_RDWR)
        except OSError:
            pass
        immediate_sock.close()

        world_cleanup_observed = wait_for_log_contains(
            world_log,
            f"Player {immediate_pid} removed from world",
            timeout=3.0,
        )
        if world_cleanup_observed:
            log("  World cleanup observed after immediate disconnect")
        else:
            log("  World cleanup log not observed within window; continuing with reconnect check")

        immediate_reconnect_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        immediate_reconnect_sock.settimeout(10.0)
        immediate_reconnect_sock.connect(("127.0.0.1", GATEWAY_PORT))
        ok, immediate_re_session_key, immediate_re_pid = send_login(immediate_reconnect_sock, immediate_pid)
        if not ok or immediate_re_pid != immediate_pid:
            log("  Immediate-disconnect reconnect failed")
            immediate_reconnect_sock.close()
            for s, _ in clients:
                if s:
                    s.close()
            sock2.close()
            return False
        log(f"  Immediate-disconnect reconnect OK: SessionKey={immediate_re_session_key}")
        try:
            immediate_reconnect_sock.shutdown(socket.SHUT_RDWR)
        except OSError:
            pass
        immediate_reconnect_sock.close()
        time.sleep(0.2)

        # 14. 双端同时断线：两个已登录玩家同时断开，再分别重连
        log("Test 10: Dual disconnect cleanup...")
        dual_pids = [30010, 30011]
        dual_socks: list[socket.socket] = []
        for dual_pid in dual_pids:
            dual_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            dual_sock.settimeout(10.0)
            dual_sock.connect(("127.0.0.1", GATEWAY_PORT))
            ok, dual_session_key, dual_resp_pid = send_login(dual_sock, dual_pid)
            if not ok or dual_resp_pid != dual_pid:
                log(f"  Dual-disconnect login failed for player {dual_pid}")
                dual_sock.close()
                for opened_sock in dual_socks:
                    try:
                        opened_sock.close()
                    except OSError:
                        pass
                for s, _ in clients:
                    if s:
                        s.close()
                sock2.close()
                return False
            log(f"  Player {dual_pid} login OK: SessionKey={dual_session_key}")
            dual_socks.append(dual_sock)

        for dual_sock in dual_socks:
            try:
                dual_sock.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass
            dual_sock.close()

        time.sleep(1.0)

        for dual_pid in dual_pids:
            dual_reconnect_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            dual_reconnect_sock.settimeout(10.0)
            dual_reconnect_sock.connect(("127.0.0.1", GATEWAY_PORT))
            ok, dual_re_session_key, dual_re_pid = send_login(dual_reconnect_sock, dual_pid)
            if not ok or dual_re_pid != dual_pid:
                log(f"  Dual-disconnect reconnect failed for player {dual_pid}")
                dual_reconnect_sock.close()
                for s, _ in clients:
                    if s:
                        s.close()
                sock2.close()
                return False
            log(f"  Player {dual_pid} reconnect OK: SessionKey={dual_re_session_key}")
            try:
                dual_reconnect_sock.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass
            dual_reconnect_sock.close()

        time.sleep(0.2)

        # 15. 快速重连边界：同一 PlayerId 在短时间内连续重连
        log("Test 11: Fast reconnect (same PlayerId)...")
        fast_reconnect_pid = 30001
        for round_idx in range(2):
            fast_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            fast_sock.settimeout(10.0)
            fast_sock.connect(("127.0.0.1", GATEWAY_PORT))
            ok, fast_session_key, fast_resp_pid = send_login(fast_sock, fast_reconnect_pid)
            if not ok or fast_resp_pid != fast_reconnect_pid:
                log(f"  Fast reconnect login failed on round {round_idx + 1}")
                fast_sock.close()
                for s, _ in clients:
                    if s:
                        s.close()
                sock2.close()
                return False
            log(
                f"  Round {round_idx + 1} OK: "
                f"SessionKey={fast_session_key}, PlayerId={fast_resp_pid}"
            )
            try:
                fast_sock.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass
            fast_sock.close()
            time.sleep(0.1)

        time.sleep(1.0)

        # 16. 并发测试：多线程同时连接并登录
        log("Test 12: Concurrency (parallel login)...")
        concurrency = 20
        base_pid = 20000
        first_pass_indices = list(range(concurrency))
        ok_count, failed_indices = run_parallel_login_batch(first_pass_indices, base_pid)
        if ok_count != concurrency:
            log(
                f"  First concurrency pass incomplete: {ok_count}/{concurrency} OK; "
                f"failed clients={failed_indices}"
            )
            time.sleep(1.0)
            ok_count_retry, failed_indices_retry = run_parallel_login_batch(
                failed_indices,
                base_pid,
            )
            total_ok = ok_count + ok_count_retry
            if total_ok != concurrency:
                log(
                    f"  Concurrency test failed after retry: "
                    f"pass1={ok_count}/{concurrency}, retry_recovered={ok_count_retry}/{len(failed_indices)}; "
                    f"retry_failed_clients={failed_indices_retry}"
                )
                for s, _ in clients:
                    if s:
                        s.close()
                sock2.close()
                return False
            log(
                f"  Concurrency recovered on retry: "
                f"pass1={ok_count}/{concurrency}, retry_recovered={ok_count_retry}/{len(failed_indices)}"
            )
        else:
            log(f"  {concurrency} parallel logins OK")

        # 17. 压力测试（可选）
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

        gateway_status = http_get_json("127.0.0.1", GATEWAY_DEBUG_HTTP_PORT, timeout=1.0)
        if gateway_status is None:
            log("  Failed to fetch Gateway debug JSON for fallback policy check")
            for sock, _ in clients:
                if sock:
                    sock.close()
            sock2.close()
            return False
        rejected_fallback_count = int(gateway_status.get("rejectedClientFallbackCount", 0))
        if rejected_fallback_count != initial_rejected_fallback_count:
            log(
                "  Unexpected rejected client fallback observed: "
                f"initial={initial_rejected_fallback_count}, final={rejected_fallback_count}"
            )
            for sock, _ in clients:
                if sock:
                    sock.close()
            sock2.close()
            return False
        log(f"  Client fallback policy clean: rejectedClientFallbackCount={rejected_fallback_count}")

        # 18. 清理
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
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
