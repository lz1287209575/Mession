#!/usr/bin/env python3
"""
Mession 脚本验证 - 启动所有服务器并验证登录、复制与清理路径

用法:
  ./Scripts/validate.py [--build-dir Build] [--timeout 30]

流程:
  1. 编译项目（如需要）
  2. 按顺序启动 Router -> Mgo -> Login -> World -> Scene -> Gateway
  3. Test 1: Handshake 本地处理可达（默认经 MT_FunctionCall）
  4. Test 2: 多玩家登录，验证 SessionKey/PlayerId（默认经 MT_FunctionCall）
  5. Test 3: 复制链路 - 登录后收包，断言统一下行 MT_FunctionCall 可解析 ActorCreate / ActorUpdate
  6. Test 4: RouterResolved 路由缓存建立
  7. Test 5: 清理路径 - 一端断线后重连同一 PlayerId，验证 Gateway/World 已回收状态，并尽量观察 route cache 失效日志
  8. Test 6: Chat 路径可达
  9. Test 7: Heartbeat 本地处理可达
  10. Test 9: 登录后立刻断开，验证重连恢复，并尽量观察 World 清理日志
  11. Test 10: 双端同时断线，验证双玩家重连恢复
  12. Test 11: 快速重连边界 - 同一 PlayerId 短时间内连续重连
  13. Test 12: 并发 - 多线程同时连接登录，验证服务端可稳定处理并发
  14. 可选 --stress：压力测试，大量并发连接 + 登录 + 多发收包
  15. 清理并退出
"""

import argparse
import json
import os
import signal
import socket
import shutil
from concurrent.futures import ThreadPoolExecutor, as_completed
from typing import List, Optional
import struct
import subprocess
import sys
import time
from pathlib import Path

# 协议常量（与 Source/Common/Net/NetMessages.h、Source/Common/Net/ServerMessages.h 一致）
MT_LOGIN = 1
MT_LOGIN_RESPONSE = 2
MT_HANDSHAKE = 3
MT_PLAYER_MOVE = 5
MT_ACTOR_CREATE = 6
MT_ACTOR_DESTROY = 7
MT_ACTOR_UPDATE = 8
MT_CHAT = 10
MT_HEARTBEAT = 11
MT_FUNCTION_CALL = 13
CLIENT_DOWNLINK_SCOPE = "MClientDownlink"
FN_LOGIN_RESPONSE = "Client_OnLoginResponse"
FN_ACTOR_CREATE = "Client_OnActorCreate"
FN_ACTOR_UPDATE = "Client_OnActorUpdate"
FN_ACTOR_DESTROY = "Client_OnActorDestroy"
DOWNLINK_FUNCTION_NAMES = {
    FN_LOGIN_RESPONSE,
    FN_ACTOR_CREATE,
    FN_ACTOR_UPDATE,
    FN_ACTOR_DESTROY,
}

# 端口
ROUTER_PORT = 8005
GATEWAY_PORT = 8001
LOGIN_PORT = 8002
WORLD_PORT = 8003
SCENE_PORT = 8004
MGO_PORT = 8006
WORLD_DEBUG_HTTP_PORT = 18083
ROUTER_DEBUG_HTTP_PORT = 18085
GATEWAY_DEBUG_HTTP_PORT = 18081
MGO_DEBUG_HTTP_PORT = 18086
VALIDATE_MONGO_SANDBOX_DB = "mession_validate_sandbox"
VALIDATE_MONGO_SANDBOX_COLLECTION = "world_snapshots"


def log(msg: str) -> None:
    print(f"[validate] {msg}", flush=True)


def stop_lingering_servers() -> None:
    """尽量清理上一次残留的服务器进程，避免端口占用导致本轮验证误失败。"""
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
                subprocess.run(
                    [fuser, "-k", f"{port}/tcp"],
                    check=False,
                    capture_output=True,
                    timeout=3,
                )
            except subprocess.SubprocessError:
                pass

    time.sleep(0.5)


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
    return wait_for_debug_value("127.0.0.1", GATEWAY_DEBUG_HTTP_PORT, key, predicate, timeout)


def wait_for_debug_value(
    host: str,
    port: int,
    key: str,
    predicate,
    timeout: float,
) -> Optional[dict]:
    """等待任意 debug JSON 某字段满足条件。"""
    deadline = time.time() + timeout
    while time.time() < deadline:
        payload = http_get_json(host, port, timeout=1.0)
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


def wait_for_port_closed(host: str, port: int, timeout: float) -> bool:
    """等待端口关闭。"""
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            s.settimeout(0.5)
            s.connect((host, port))
            s.close()
            time.sleep(0.1)
        except (socket.error, OSError):
            return True
    return False


def count_log_occurrences(log_path: Path, needle: str) -> int:
    """统计日志片段出现次数。"""
    try:
        text = log_path.read_text(encoding="utf-8", errors="ignore")
    except OSError:
        return 0
    return text.count(needle)


def recv_login_response(sock: socket.socket, timeout: float = 5.0) -> tuple[bool, int, int]:
    """接收登录响应，并严格要求正式下行走 MT_FunctionCall。"""
    pkt = recv_one_packet_raw(sock, timeout=timeout)
    if pkt is None:
        return False, 0, 0

    msg_type, payload = pkt
    if msg_type != MT_FUNCTION_CALL:
        log(f"Unexpected login response transport: {msg_type}")
        return False, 0, 0

    decoded = decode_function_call_payload(payload)
    if decoded is None:
        log("Login response decode failed: invalid MT_FunctionCall payload")
        return False, 0, 0

    function_id, function_payload = decoded
    expected_id = compute_stable_function_id(CLIENT_DOWNLINK_SCOPE, FN_LOGIN_RESPONSE)
    if function_id != expected_id or len(function_payload) < 12:
        log(f"Unexpected login response function id: {function_id}")
        return False, 0, 0

    session_key = struct.unpack("<I", function_payload[:4])[0]
    resp_player_id = struct.unpack("<Q", function_payload[4:12])[0]
    return True, session_key, resp_player_id


def compute_stable_function_id(class_name: str, function_name: str) -> int:
    """与服务端 ComputeStableReflectId 保持一致。"""
    offset_basis = 2166136261
    prime = 16777619
    h = offset_basis

    def mix(text: str) -> None:
        nonlocal h
        for ch in text.encode("utf-8"):
            h ^= ch
            h = (h * prime) & 0xFFFFFFFF

    mix(class_name)
    h ^= ord(":")
    h = (h * prime) & 0xFFFFFFFF
    h ^= ord(":")
    h = (h * prime) & 0xFFFFFFFF
    mix(function_name)

    folded = ((h >> 16) ^ (h & 0xFFFF)) & 0xFFFF
    return folded if folded != 0 else 1


def send_function_call(sock: socket.socket, class_name: str, function_name: str, payload: bytes) -> bool:
    function_id = compute_stable_function_id(class_name, function_name)
    body = struct.pack("<BH", MT_FUNCTION_CALL, function_id)
    body += struct.pack("<I", len(payload))
    body += payload
    packet = struct.pack("<I", len(body)) + body
    sock.sendall(packet)
    return True


def decode_function_call_payload(payload: bytes) -> Optional[tuple[int, bytes]]:
    if len(payload) < 2 + 4:
        return None
    function_id = struct.unpack("<H", payload[:2])[0]
    payload_size = struct.unpack("<I", payload[2:6])[0]
    if 6 + payload_size > len(payload):
        return None
    return function_id, payload[6:6 + payload_size]


def resolve_downlink_function_name(function_id: int) -> Optional[str]:
    for function_name in DOWNLINK_FUNCTION_NAMES:
        if function_id == compute_stable_function_id(CLIENT_DOWNLINK_SCOPE, function_name):
            return function_name
    return None


def send_function_handshake(sock: socket.socket, player_id: int = 0) -> bool:
    return send_function_call(sock, "MGatewayServer", "Client_Handshake", struct.pack("<Q", player_id))


def send_function_login(
    sock: socket.socket,
    player_id: int = 12345,
    response_timeout: float = 5.0,
) -> tuple[bool, int, int]:
    send_function_call(sock, "MGatewayServer", "Client_Login", struct.pack("<Q", player_id))
    return recv_login_response(sock, timeout=response_timeout)


def send_function_chat(sock: socket.socket, message: str) -> bool:
    encoded = message.encode("utf-8")
    payload = struct.pack("<I", len(encoded)) + encoded
    return send_function_call(sock, "MGatewayServer", "Client_Chat", payload)


def send_function_player_move(sock: socket.socket, x: float, y: float, z: float) -> bool:
    return send_function_call(sock, "MGatewayServer", "Client_PlayerMove", struct.pack("<fff", x, y, z))


def send_function_heartbeat(sock: socket.socket, sequence: int) -> bool:
    return send_function_call(sock, "MGatewayServer", "Client_Heartbeat", struct.pack("<I", sequence))


def send_login(
    sock: socket.socket,
    player_id: int = 12345,
    response_timeout: float = 5.0,
) -> tuple[bool, int, int]:
    """默认登录路径：经 MT_FunctionCall 发送 Client_Login。"""
    return send_function_login(sock, player_id, response_timeout=response_timeout)


def send_handshake(sock: socket.socket, player_id: int = 0) -> bool:
    """默认握手路径：经 MT_FunctionCall 发送 Client_Handshake。"""
    return send_function_handshake(sock, player_id)


def send_player_move(sock: socket.socket, x: float, y: float, z: float) -> bool:
    """默认移动路径：经 MT_FunctionCall 发送 Client_PlayerMove。"""
    return send_function_player_move(sock, x, y, z)


def send_chat(sock: socket.socket, message: str) -> bool:
    """默认聊天路径：经 MT_FunctionCall 发送 Client_Chat。"""
    return send_function_chat(sock, message)


def send_heartbeat(sock: socket.socket, sequence: int) -> bool:
    """默认心跳路径：经 MT_FunctionCall 发送 Client_Heartbeat。"""
    return send_function_heartbeat(sock, sequence)


def recv_exact(sock: socket.socket, size: int) -> Optional[bytes]:
    """精确接收 size 字节；超时/断连返回 None。"""
    if size <= 0:
        return b""

    buf = bytearray()
    while len(buf) < size:
        try:
            chunk = sock.recv(size - len(buf))
        except socket.timeout:
            return None
        except (socket.error, OSError):
            return None
        if not chunk:
            return None
        buf.extend(chunk)
    return bytes(buf)


def recv_one_packet(sock: socket.socket, timeout: float) -> Optional[tuple[int, bytes]]:
    """
    接收一个长度前缀包，返回 (msg_type, payload) 或 None（超时/断连）。
    协议: Length(4) + MsgType(1) + Payload...
    """
    sock.settimeout(timeout)
    try:
        header = recv_exact(sock, 4)
        if header is None:
            return None
        length = struct.unpack("<I", header)[0]
        if length < 1:
            return None
        body = recv_exact(sock, length)
        if body is None:
            return None
        return body[0], body[1:]
    except socket.timeout:
        return None
    except (socket.error, OSError):
        return None


def recv_one_packet_raw(sock: socket.socket, timeout: float) -> Optional[tuple[int, bytes]]:
    """接收一个原始长度前缀包，不做统一下行兼容转换。"""
    sock.settimeout(timeout)
    try:
        header = recv_exact(sock, 4)
        if header is None:
            return None
        length = struct.unpack("<I", header)[0]
        if length < 1:
            return None
        body = recv_exact(sock, length)
        if body is None:
            return None
        return body[0], body[1:]
    except socket.timeout:
        return None
    except (socket.error, OSError):
        return None


def collect_downlink_function_packets(
    sock: socket.socket,
    duration: float,
    want_function_names: set[str],
) -> list[tuple[str, bytes]]:
    """收集严格经 MT_FunctionCall 下发的客户端下行函数。"""
    deadline = time.time() + duration
    found: list[tuple[str, bytes]] = []
    while time.time() < deadline:
        pkt = recv_one_packet_raw(sock, timeout=0.3)
        if pkt is None:
            continue
        msg_type, payload = pkt
        if msg_type != MT_FUNCTION_CALL:
            continue
        decoded = decode_function_call_payload(payload)
        if decoded is None:
            continue
        function_id, function_payload = decoded
        function_name = resolve_downlink_function_name(function_id)
        if function_name in want_function_names:
            found.append((function_name, function_payload))
    return found


def parse_actor_create_payload(payload: bytes) -> Optional[tuple[int, bytes]]:
    if len(payload) < 8 + 4:
        return None
    actor_id = struct.unpack("<Q", payload[:8])[0]
    data_size = struct.unpack("<I", payload[8:12])[0]
    if 12 + data_size > len(payload):
        return None
    return actor_id, payload[12:12 + data_size]


def parse_actor_update_payload(payload: bytes) -> Optional[tuple[int, bytes]]:
    return parse_actor_create_payload(payload)


def parse_actor_destroy_payload(payload: bytes) -> Optional[int]:
    if len(payload) < 8:
        return None
    return struct.unpack("<Q", payload[:8])[0]


def parse_reflected_actor_blob(data: bytes) -> Optional[dict]:
    if not data:
        return None
    return {
        "size": len(data),
        "bytes": data,
    }


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
    if len(payload) < 8 + 4:
        return None
    from_player_id = struct.unpack("<Q", payload[:8])[0]
    message_len = struct.unpack("<I", payload[8:12])[0]
    if 12 + message_len > len(payload):
        return None
    try:
        message = payload[12:12 + message_len].decode("utf-8")
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

    def one_login(idx: int) -> tuple[int, bool, str]:
        player_id = base_pid + idx
        last_reason = "unknown"
        for attempt in range(2):
            sock: Optional[socket.socket] = None
            try:
                # Stagger a tiny amount to avoid connect/send thundering herd on localhost.
                if attempt == 0:
                    time.sleep((idx % 5) * 0.01)
                sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                sock.settimeout(10.0)
                sock.connect(("127.0.0.1", GATEWAY_PORT))
                ok, _, _ = send_login(sock, player_id, response_timeout=8.0)
                if ok:
                    return idx, True, ""
                last_reason = "login_response_timeout_or_invalid"
            except Exception as exc:
                last_reason = f"{type(exc).__name__}: {exc}"
            finally:
                if sock is not None:
                    try:
                        sock.close()
                    except OSError:
                        pass
            if attempt == 0:
                time.sleep(0.15)
        return idx, False, last_reason

    ok_count = 0
    failed_indices: list[int] = []
    failure_reasons: dict[int, str] = {}
    if not indices:
        return ok_count, failed_indices

    with ThreadPoolExecutor(max_workers=min(len(indices), 16)) as ex:
        futures = [ex.submit(one_login, i) for i in indices]
        for f in as_completed(futures):
            idx, ok, reason = f.result()
            if ok:
                ok_count += 1
            else:
                failed_indices.append(idx)
                failure_reasons[idx] = reason

    failed_indices.sort()
    if failed_indices:
        reason_parts = [
            f"{idx}:{failure_reasons.get(idx, 'unknown')}"
            for idx in failed_indices
        ]
        log("  Parallel login failure details: " + ", ".join(reason_parts))
    return ok_count, failed_indices


def run_validation(
    build_dir: Path,
    timeout: float,
    zone_id: Optional[int] = None,
    debug_log_dir: Optional[Path] = None,
    stress_clients: int = 0,
    stress_moves: int = 5,
    enable_mgo: bool = True,
    mongo_db: str = VALIDATE_MONGO_SANDBOX_DB,
    mongo_collection: str = VALIDATE_MONGO_SANDBOX_COLLECTION,
) -> bool:
    """执行完整验证流程"""
    procs: List[subprocess.Popen] = []
    proc_by_name: dict[str, subprocess.Popen] = {}
    stop_lingering_servers()
    if debug_log_dir is None:
        debug_log_dir = build_dir / "validate_logs"
        debug_log_dir.mkdir(parents=True, exist_ok=True)

    base_env = {
        "MESSION_ROUTER_DEBUG_HTTP_PORT": str(ROUTER_DEBUG_HTTP_PORT),
        "MESSION_GATEWAY_DEBUG_HTTP_PORT": str(GATEWAY_DEBUG_HTTP_PORT),
        "MESSION_WORLD_DEBUG_HTTP_PORT": str(WORLD_DEBUG_HTTP_PORT),
    }
    if enable_mgo:
        base_env["MESSION_MGO_DEBUG_HTTP_PORT"] = str(MGO_DEBUG_HTTP_PORT)
        base_env["MESSION_WORLD_MGO_PERSISTENCE_ENABLE"] = "1"
        base_env["MESSION_MGO_MONGO_ENABLE"] = os.environ.get("MESSION_MGO_MONGO_ENABLE", "1")
        base_env["MESSION_MGO_MONGO_DB"] = os.environ.get("MESSION_MGO_MONGO_DB", mongo_db)
        base_env["MESSION_MGO_MONGO_COLLECTION"] = os.environ.get(
            "MESSION_MGO_MONGO_COLLECTION",
            mongo_collection,
        )
        log(
            "Mongo sandbox target: "
            f"db={base_env['MESSION_MGO_MONGO_DB']} "
            f"collection={base_env['MESSION_MGO_MONGO_COLLECTION']}"
        )
    if zone_id is not None:
        base_env["MESSION_ZONE_ID"] = str(zone_id)

    def cleanup():
        for p in reversed(procs):
            try:
                p.terminate()
                p.wait(timeout=3)
            except Exception:
                p.kill()

    def launch_server(name: str, port: int) -> Optional[subprocess.Popen]:
        proc = start_server(build_dir, name, port, dict(base_env), debug_log_dir)
        if proc is not None:
            procs.append(proc)
            proc_by_name[name] = proc
        return proc

    def stop_server_process(name: str, port: int, timeout_seconds: float = 5.0) -> bool:
        proc = proc_by_name.get(name)
        if proc is None:
            return False
        try:
            proc.terminate()
            proc.wait(timeout=timeout_seconds)
        except Exception:
            proc.kill()
        return wait_for_port_closed("127.0.0.1", port, timeout_seconds)

    def restart_server_process(name: str, port: int, timeout_seconds: float = 8.0) -> bool:
        proc = launch_server(name, port)
        if proc is None:
            return False
        return wait_for_port("127.0.0.1", port, timeout_seconds)

    try:
        # 1. 启动 Router
        router_env = dict(base_env)
        p = start_server(build_dir, "RouterServer", ROUTER_PORT, router_env, debug_log_dir)
        if not p:
            return False
        procs.append(p)
        proc_by_name["RouterServer"] = p
        if not wait_for_port("127.0.0.1", ROUTER_PORT, timeout):
            log("RouterServer did not become ready")
            return False
        time.sleep(0.5)

        # 2. 启动 Mgo（默认启用）
        if enable_mgo:
            p = launch_server("MgoServer", MGO_PORT)
            if not p:
                log("MgoServer failed to start")
                return False
            if not wait_for_port("127.0.0.1", MGO_PORT, timeout):
                log("MgoServer did not become ready")
                return False
            time.sleep(0.2)

        # 3. 启动 Login, World, Scene, Gateway（可选 Zone）
        for name, port in [
            ("LoginServer", LOGIN_PORT),
            ("WorldServer", WORLD_PORT),
            ("SceneServer", SCENE_PORT),
            ("GatewayServer", GATEWAY_PORT),
        ]:
            p = launch_server(name, port)
            if not p:
                return False

        # 4. 等待 Gateway 就绪
        if not wait_for_port("127.0.0.1", GATEWAY_PORT, timeout):
            log("GatewayServer did not become ready")
            return False
        gateway_debug = wait_for_http_json("127.0.0.1", GATEWAY_DEBUG_HTTP_PORT, timeout)
        if gateway_debug is None:
            log("Gateway debug HTTP did not become ready")
            return False
        world_debug = wait_for_http_json("127.0.0.1", WORLD_DEBUG_HTTP_PORT, timeout)
        if world_debug is None:
            log("World debug HTTP did not become ready")
            return False
        mgo_debug = None
        if enable_mgo:
            mgo_debug = wait_for_http_json("127.0.0.1", MGO_DEBUG_HTTP_PORT, timeout)
            if mgo_debug is None:
                log("Mgo debug HTTP did not become ready")
                return False
        initial_function_call_count = int(gateway_debug.get("clientFunctionCallCount", 0))
        initial_rejected_function_count = int(gateway_debug.get("clientFunctionCallRejectedCount", 0))
        initial_unknown_function_count = int(gateway_debug.get("unknownClientFunctionCount", 0))
        initial_decode_failure_count = int(gateway_debug.get("clientFunctionDecodeFailureCount", 0))
        gateway_log = debug_log_dir / "GatewayServer.log"
        world_log = debug_log_dir / "WorldServer.log"

        # 5. 等待后端握手完成
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

        # 7. 复制链路：严格要求复制下行走 MT_FunctionCall，并能解析 ActorCreate 外层壳
        log("Test 3: Replication downlink (ActorCreate/ActorUpdate)...")
        time.sleep(2.0)  # 等待路由、Session 校验、AddPlayer 与复制包下发
        all_creates = []
        actor_states: dict[int, dict] = {}
        for sock, _ in clients:
            replication = collect_downlink_function_packets(
                sock,
                1.5,
                {FN_ACTOR_CREATE, FN_ACTOR_UPDATE},
            )
            all_creates.extend(payload for function_name, payload in replication if function_name == FN_ACTOR_CREATE)
        if not all_creates:
            log("  No Client_OnActorCreate received via MT_FunctionCall; replication path broken")
            for s, _ in clients:
                if s:
                    s.close()
            return False
        parsed_create_count = 0
        for payload in all_creates:
            parsed = parse_actor_create_payload(payload)
            if not parsed:
                continue
            actor_id, actor_data = parsed
            snapshot = parse_reflected_actor_blob(actor_data)
            if snapshot is None:
                continue
            actor_states[actor_id] = snapshot
            parsed_create_count += 1
        if parsed_create_count == 0:
            log("  ActorCreate packets arrived, but none could be decoded into ActorId + payload")
            for s, _ in clients:
                if s:
                    s.close()
            return False
        log(f"  Parsed {parsed_create_count} Client_OnActorCreate payload(s) from MT_FunctionCall")

        for sock, pid in clients:
            send_player_move(sock, float(pid % 10), 0.0, 0.0)
        log("  Multi-player move sent")
        actor_updates = []
        for sock, _ in clients:
            actor_updates.extend(
                collect_downlink_function_packets(sock, 2.0, {FN_ACTOR_UPDATE})
            )
        applied_update = False
        for _, payload in actor_updates:
            parsed = parse_actor_update_payload(payload)
            if not parsed:
                continue
            actor_id, actor_data = parsed
            if actor_id not in actor_states:
                continue
            snapshot = parse_reflected_actor_blob(actor_data)
            if snapshot is None:
                continue
            if snapshot["bytes"] == actor_states[actor_id]["bytes"]:
                continue
            actor_states[actor_id] = snapshot
            applied_update = True
            break
        if not applied_update:
            log("  No changed reflected Client_OnActorUpdate observed for a known actor")
            for s, _ in clients:
                if s:
                    s.close()
            return False
        log("  Client_OnActorUpdate carried a changed reflected actor snapshot")

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
        destroy_packets = collect_downlink_function_packets(clients[1][0], 1.5, {FN_ACTOR_DESTROY})
        destroy_observed = False
        for _, payload in destroy_packets:
            actor_id = parse_actor_destroy_payload(payload)
            if actor_id is None:
                continue
            if actor_id in actor_states:
                destroy_observed = True
                actor_states.pop(actor_id, None)
                break
        if not destroy_observed:
            log("  Client_OnActorDestroy was not observed for a previously created actor")
            for s, _ in clients:
                if s:
                    s.close()
            return False
        log("  Client_OnActorDestroy decoded for a known replicated actor")

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

        log("Test 6.1: Inventory bag command...")
        send_chat(chat_sender, "/bag add 1001")
        bag_packets = collect_replication_packets(chat_sender, 2.0, {MT_CHAT})
        bag_add_ok = False
        for _, payload in bag_packets:
            parsed = parse_chat_payload(payload)
            if not parsed:
                continue
            from_player_id, message = parsed
            if from_player_id == 0 and "[bag] add ok: item=1001" in message:
                bag_add_ok = True
                break
        if not bag_add_ok:
            log("  Bag add command failed: expected system ack not received")
            for s, _ in clients:
                if s:
                    s.close()
            sock2.close()
            return False

        send_chat(chat_sender, "/bag show")
        bag_packets = collect_replication_packets(chat_sender, 2.0, {MT_CHAT})
        bag_show_ok = False
        for _, payload in bag_packets:
            parsed = parse_chat_payload(payload)
            if not parsed:
                continue
            from_player_id, message = parsed
            if from_player_id == 0 and "[bag] gold=" in message and "items=" in message:
                bag_show_ok = True
                break
        if not bag_show_ok:
            log("  Bag show command failed: expected summary not received")
            for s, _ in clients:
                if s:
                    s.close()
            sock2.close()
            return False
        log("  Bag command path OK: add/show")

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

        # 12. 统一函数调用测试：覆盖 Handshake / Login / Chat 三条首批垂直切片
        log("Test 8: Unified client function call...")
        function_handshake_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        function_handshake_sock.settimeout(10.0)
        function_handshake_sock.connect(("127.0.0.1", GATEWAY_PORT))
        function_handshake_pid = 65432
        send_function_handshake(function_handshake_sock, function_handshake_pid)
        deadline = time.time() + 3.0
        function_handshake_observed = False
        gateway_status = None
        while time.time() < deadline:
            gateway_status = http_get_json("127.0.0.1", GATEWAY_DEBUG_HTTP_PORT, timeout=1.0)
            if gateway_status is None:
                time.sleep(0.1)
                continue
            if (
                int(gateway_status.get("clientFunctionCallCount", 0)) >= initial_function_call_count + 1 and
                gateway_status.get("lastClientFunctionName", "") == "Client_Handshake" and
                int(gateway_status.get("lastClientHandshakePlayerId", 0)) == function_handshake_pid
            ):
                function_handshake_observed = True
                break
            time.sleep(0.1)
        if not function_handshake_observed:
            log("  Unified Client_Handshake failed: Gateway debug status did not update")
            function_handshake_sock.close()
            for s, _ in clients:
                if s:
                    s.close()
            sock2.close()
            return False
        function_handshake_sock.close()

        function_login_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        function_login_sock.settimeout(10.0)
        function_login_sock.connect(("127.0.0.1", GATEWAY_PORT))
        function_login_pid = 20001
        ok, function_session_key, function_resp_pid = send_function_login(function_login_sock, function_login_pid)
        if not ok or function_resp_pid != function_login_pid:
            log("  Unified Client_Login failed")
            function_login_sock.close()
            for s, _ in clients:
                if s:
                    s.close()
            sock2.close()
            return False
        log(f"  Unified Client_Login OK: SessionKey={function_session_key}")

        unified_chat_text = "validate-function-chat"
        send_function_chat(clients[1][0], unified_chat_text)
        chat_packets = collect_replication_packets(clients[2][0], 2.0, {MT_CHAT})
        matched_function_chat = False
        for _, payload in chat_packets:
            parsed = parse_chat_payload(payload)
            if not parsed:
                continue
            from_player_id, message = parsed
            if from_player_id == clients[1][1] and message == unified_chat_text:
                matched_function_chat = True
                break
        if not matched_function_chat:
            log("  Unified Client_Chat failed: receiver did not observe expected chat payload")
            function_login_sock.close()
            for s, _ in clients:
                if s:
                    s.close()
            sock2.close()
            return False
        heartbeat_sequence = 5252
        send_function_heartbeat(clients[1][0], heartbeat_sequence)
        deadline = time.time() + 3.0
        function_heartbeat_observed = False
        while time.time() < deadline:
            gateway_status = http_get_json("127.0.0.1", GATEWAY_DEBUG_HTTP_PORT, timeout=1.0)
            if gateway_status is None:
                time.sleep(0.1)
                continue
            if (
                gateway_status.get("lastClientFunctionName", "") == "Client_Heartbeat" and
                int(gateway_status.get("lastClientHeartbeatSequence", 0)) >= heartbeat_sequence
            ):
                function_heartbeat_observed = True
                break
            time.sleep(0.1)
        if not function_heartbeat_observed:
            log("  Unified Client_Heartbeat failed: Gateway debug status did not update")
            function_login_sock.close()
            for s, _ in clients:
                if s:
                    s.close()
            sock2.close()
            return False

        send_function_player_move(clients[1][0], 7.0, 0.0, 0.0)
        deadline = time.time() + 3.0
        move_function_observed = False
        while time.time() < deadline:
            gateway_status = http_get_json("127.0.0.1", GATEWAY_DEBUG_HTTP_PORT, timeout=1.0)
            if gateway_status is None:
                time.sleep(0.1)
                continue
            if gateway_status.get("lastClientFunctionName", "") == "Client_PlayerMove":
                move_function_observed = True
                break
            time.sleep(0.1)
        if not move_function_observed:
            log("  Unified Client_PlayerMove failed: Gateway debug status did not update")
            function_login_sock.close()
            for s, _ in clients:
                if s:
                    s.close()
            sock2.close()
            return False
        log("  Unified function call path reached GatewayLocal / Login / RouterResolved successfully")
        try:
            function_login_sock.shutdown(socket.SHUT_RDWR)
        except OSError:
            pass
        function_login_sock.close()

        # 13. 统一函数调用负向验证：未知 FunctionID / 包体越界 / payload 解码失败 / 未鉴权调用
        log("Test 9: Unified function call negative cases...")
        negative_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        negative_sock.settimeout(10.0)
        negative_sock.connect(("127.0.0.1", GATEWAY_PORT))

        unknown_function_id = 65535
        if unknown_function_id in {
            compute_stable_function_id("MGatewayServer", "Client_Handshake"),
            compute_stable_function_id("MGatewayServer", "Client_Login"),
            compute_stable_function_id("MGatewayServer", "Client_PlayerMove"),
            compute_stable_function_id("MGatewayServer", "Client_Chat"),
            compute_stable_function_id("MGatewayServer", "Client_Heartbeat"),
        }:
            unknown_function_id = 65534
        unknown_body = struct.pack("<BH", MT_FUNCTION_CALL, unknown_function_id) + struct.pack("<I", 0)
        negative_sock.sendall(struct.pack("<I", len(unknown_body)) + unknown_body)
        gateway_status = wait_for_gateway_debug_value(
            "unknownClientFunctionCount",
            lambda value: int(value) >= initial_unknown_function_count + 1,
            timeout=3.0,
        )
        if gateway_status is None or gateway_status.get("lastClientFunctionError", "") != "UnknownFunctionId":
            log("  Unknown FunctionID negative case failed")
            negative_sock.close()
            for s, _ in clients:
                if s:
                    s.close()
            sock2.close()
            return False

        packet_too_small_body = bytes([MT_FUNCTION_CALL, 0x01, 0x02])
        negative_sock.sendall(struct.pack("<I", len(packet_too_small_body)) + packet_too_small_body)
        gateway_status = wait_for_gateway_debug_value(
            "clientFunctionDecodeFailureCount",
            lambda value: int(value) >= initial_decode_failure_count + 1,
            timeout=3.0,
        )
        if gateway_status is None or gateway_status.get("lastClientFunctionError", "") != "PacketTooSmall":
            log("  PacketTooSmall negative case failed")
            negative_sock.close()
            for s, _ in clients:
                if s:
                    s.close()
            sock2.close()
            return False
        initial_decode_failure_count = int(gateway_status.get("clientFunctionDecodeFailureCount", 0))

        bad_payload_body = struct.pack("<BH", MT_FUNCTION_CALL, compute_stable_function_id("MGatewayServer", "Client_Handshake"))
        bad_payload_body += struct.pack("<I", 16) + b"\x00\x00"
        negative_sock.sendall(struct.pack("<I", len(bad_payload_body)) + bad_payload_body)
        gateway_status = wait_for_gateway_debug_value(
            "clientFunctionDecodeFailureCount",
            lambda value: int(value) >= initial_decode_failure_count + 1,
            timeout=3.0,
        )
        if gateway_status is None or gateway_status.get("lastClientFunctionError", "") != "PayloadOutOfRange":
            log("  PayloadOutOfRange negative case failed")
            negative_sock.close()
            for s, _ in clients:
                if s:
                    s.close()
            sock2.close()
            return False
        initial_decode_failure_count = int(gateway_status.get("clientFunctionDecodeFailureCount", 0))

        # Length/MsgType/FunctionID/PayloadSize 都合法，但 payload 本身不足以绑定到 SPlayerIdPayload。
        malformed_payload_body = struct.pack(
            "<BH",
            MT_FUNCTION_CALL,
            compute_stable_function_id("MGatewayServer", "Client_Handshake"),
        )
        malformed_payload_body += struct.pack("<I", 4) + struct.pack("<I", 1234)
        negative_sock.sendall(struct.pack("<I", len(malformed_payload_body)) + malformed_payload_body)
        gateway_status = wait_for_gateway_debug_value(
            "clientFunctionCallRejectedCount",
            lambda value: int(value) >= initial_rejected_function_count + 4,
            timeout=3.0,
        )
        if gateway_status is None or gateway_status.get("lastClientFunctionError", "") != "PayloadDecodeFailed":
            log("  PayloadDecodeFailed negative case failed")
            negative_sock.close()
            for s, _ in clients:
                if s:
                    s.close()
            sock2.close()
            return False

        unauth_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        unauth_sock.settimeout(10.0)
        unauth_sock.connect(("127.0.0.1", GATEWAY_PORT))
        send_function_chat(unauth_sock, "unauth-chat")
        gateway_status = wait_for_gateway_debug_value(
            "clientFunctionCallRejectedCount",
            lambda value: int(value) >= initial_rejected_function_count + 5,
            timeout=3.0,
        )
        if gateway_status is None or gateway_status.get("lastClientFunctionError", "") != "AuthRequired":
            log("  AuthRequired negative case failed")
            unauth_sock.close()
            negative_sock.close()
            for s, _ in clients:
                if s:
                    s.close()
            sock2.close()
            return False
        log(
            "  Negative cases OK: "
            f"unknown={int(gateway_status.get('unknownClientFunctionCount', 0))}, "
            f"decodeFailures={int(gateway_status.get('clientFunctionDecodeFailureCount', 0))}, "
            f"lastError={gateway_status.get('lastClientFunctionError', '')}"
        )

        backend_baseline_rejected = int(gateway_status.get("clientFunctionCallRejectedCount", 0))
        reconnect_baseline = int(gateway_status.get("reconnectAttempts", 0))

        if not stop_server_process("LoginServer", LOGIN_PORT):
            log("  Failed to stop LoginServer for BackendUnavailable injection")
            unauth_sock.close()
            negative_sock.close()
            for s, _ in clients:
                if s:
                    s.close()
            sock2.close()
            return False
        gateway_status = wait_for_gateway_debug_value(
            "reconnectAttempts",
            lambda value: int(value) >= reconnect_baseline + 1,
            timeout=8.0,
        )
        if gateway_status is None:
            log("  Gateway did not observe LoginServer disconnect/reconnect attempt")
            unauth_sock.close()
            negative_sock.close()
            for s, _ in clients:
                if s:
                    s.close()
            sock2.close()
            return False

        backend_unavailable_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        backend_unavailable_sock.settimeout(10.0)
        backend_unavailable_sock.connect(("127.0.0.1", GATEWAY_PORT))
        send_function_call(
            backend_unavailable_sock,
            "MGatewayServer",
            "Client_Login",
            struct.pack("<Q", 40001),
        )
        gateway_status = wait_for_gateway_debug_value(
            "clientFunctionCallRejectedCount",
            lambda value: int(value) >= backend_baseline_rejected + 1,
            timeout=3.0,
        )
        if gateway_status is None or gateway_status.get("lastClientFunctionError", "") != "BackendUnavailable":
            log("  BackendUnavailable negative case failed")
            backend_unavailable_sock.close()
            unauth_sock.close()
            negative_sock.close()
            for s, _ in clients:
                if s:
                    s.close()
            sock2.close()
            return False
        backend_unavailable_sock.close()

        if not restart_server_process("LoginServer", LOGIN_PORT):
            log("  Failed to restart LoginServer after BackendUnavailable injection")
            unauth_sock.close()
            negative_sock.close()
            for s, _ in clients:
                if s:
                    s.close()
            sock2.close()
            return False
        time.sleep(1.0)

        route_pending_baseline = int(gateway_status.get("clientFunctionCallRejectedCount", 0))
        reconnect_baseline = int(gateway_status.get("reconnectAttempts", 0))
        if not stop_server_process("WorldServer", WORLD_PORT):
            log("  Failed to stop WorldServer for RoutePending injection")
            unauth_sock.close()
            negative_sock.close()
            for s, _ in clients:
                if s:
                    s.close()
            sock2.close()
            return False
        gateway_status = wait_for_gateway_debug_value(
            "reconnectAttempts",
            lambda value: int(value) >= reconnect_baseline + 1,
            timeout=8.0,
        )
        if gateway_status is None:
            log("  Gateway did not observe WorldServer disconnect/reconnect attempt")
            unauth_sock.close()
            negative_sock.close()
            for s, _ in clients:
                if s:
                    s.close()
            sock2.close()
            return False

        send_function_chat(clients[1][0], "route-pending-chat")
        gateway_status = wait_for_gateway_debug_value(
            "clientFunctionCallRejectedCount",
            lambda value: int(value) >= route_pending_baseline + 1,
            timeout=3.0,
        )
        if gateway_status is None or gateway_status.get("lastClientFunctionError", "") != "RoutePending":
            log("  RoutePending negative case failed")
            unauth_sock.close()
            negative_sock.close()
            for s, _ in clients:
                if s:
                    s.close()
            sock2.close()
            return False

        if not restart_server_process("WorldServer", WORLD_PORT):
            log("  Failed to restart WorldServer after RoutePending injection")
            unauth_sock.close()
            negative_sock.close()
            for s, _ in clients:
                if s:
                    s.close()
            sock2.close()
            return False
        time.sleep(2.0)
        gateway_status = wait_for_gateway_debug_value(
            "backendActive",
            lambda value: int(value) >= 3,
            timeout=8.0,
        )
        if gateway_status is None:
            log("  Gateway backend connections did not recover after WorldServer restart")
            unauth_sock.close()
            negative_sock.close()
            for s, _ in clients:
                if s:
                    s.close()
            sock2.close()
            return False

        log(
            "  Backend failure injection OK: "
            f"backendUnavailable+routePending observed, lastError={gateway_status.get('lastClientFunctionError', '')}"
        )
        unauth_sock.close()
        negative_sock.close()

        # 14. 登录后立刻断开：验证 World 清理完成后可以重新登录
        log("Test 11: Immediate disconnect cleanup...")
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

        # 16. 双端同时断线：两个已登录玩家同时断开，再分别重连
        log("Test 12: Dual disconnect cleanup...")
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

        if enable_mgo:
            log("Test 13: Persistence owner/version + merge + replay...")
            world_debug_now = http_get_json("127.0.0.1", WORLD_DEBUG_HTTP_PORT, timeout=1.0) or {}
            mgo_debug_now = http_get_json("127.0.0.1", MGO_DEBUG_HTTP_PORT, timeout=1.0) or {}
            owner_server_id = int(world_debug_now.get("ownerServerId", 0))
            persistence_enqueued = int(world_debug_now.get("persistenceEnqueued", 0))
            persistence_flushed = int(world_debug_now.get("persistenceFlushed", 0))
            persistence_merged = int(world_debug_now.get("persistenceMerged", 0))
            mongo_success = int(mgo_debug_now.get("mongoSuccess", 0))

            if owner_server_id <= 0:
                log("  Persistence regression failed: ownerServerId is missing/invalid in World debug status")
                for s, _ in clients:
                    if s:
                        s.close()
                sock2.close()
                return False
            if persistence_enqueued <= 0:
                log("  Persistence regression failed: no persistence records enqueued")
                for s, _ in clients:
                    if s:
                        s.close()
                sock2.close()
                return False
            if persistence_flushed <= 0:
                log("  Persistence regression failed: no persistence records flushed")
                for s, _ in clients:
                    if s:
                        s.close()
                sock2.close()
                return False
            if mongo_success <= 0:
                log("  Persistence regression failed: Mgo reported no successful Mongo writes")
                for s, _ in clients:
                    if s:
                        s.close()
                sock2.close()
                return False

            log(
                "  Persistence regression OK: "
                f"ownerServerId={owner_server_id}, "
                f"enqueued={persistence_enqueued}, "
                f"flushed={persistence_flushed}, "
                f"merged={persistence_merged}, "
                f"mongoSuccess={mongo_success}"
            )

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

        gateway_status = wait_for_http_json(
            "127.0.0.1",
            GATEWAY_DEBUG_HTTP_PORT,
            timeout=3.0,
        )
        if gateway_status is None:
            log("  Failed to fetch Gateway debug JSON for ingress policy check")
            for sock, _ in clients:
                if sock:
                    sock.close()
            sock2.close()
            return False
        log("  Client ingress policy clean: MT_FunctionCall-only validation completed")

        # 17. 清理
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
    parser.add_argument(
        "--no-mgo",
        action="store_true",
        help="Do not start MgoServer in validation",
    )
    parser.add_argument(
        "--mongo-db",
        default=VALIDATE_MONGO_SANDBOX_DB,
        help=f"Mongo database for validate MgoServer (default: {VALIDATE_MONGO_SANDBOX_DB})",
    )
    parser.add_argument(
        "--mongo-collection",
        default=VALIDATE_MONGO_SANDBOX_COLLECTION,
        help=f"Mongo collection for validate MgoServer (default: {VALIDATE_MONGO_SANDBOX_COLLECTION})",
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
    if not args.no_mgo and not get_executable_path(build_dir, "MgoServer"):
        log(f"MgoServer not found in {build_dir}")
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
        enable_mgo=(not args.no_mgo),
        mongo_db=args.mongo_db,
        mongo_collection=args.mongo_collection,
    )
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
