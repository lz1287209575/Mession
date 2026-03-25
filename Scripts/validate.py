#!/usr/bin/env python3
"""
Mession 最小链路验证

当前脚本对齐新的 server skeleton，只验证最核心的闭环：
1. 编译项目（可选）
2. 启动 Router -> Mgo -> Login -> World -> Scene -> Gateway
3. 客户端连接 Gateway，走统一 MT_FunctionCall
4. 验证 Client_Login / Client_FindPlayer / Client_SwitchScene / Client_Logout
5. 清理服务器进程并退出
"""

import argparse
import os
import shutil
import socket
import struct
import subprocess
import sys
import time
from pathlib import Path
from typing import Callable, List, Optional

MT_FUNCTION_CALL = 13

ROUTER_PORT = 8005
GATEWAY_PORT = 8001
LOGIN_PORT = 8002
WORLD_PORT = 8003
SCENE_PORT = 8004
MGO_PORT = 8006

VALIDATE_MONGO_SANDBOX_DB = "mession_validate_sandbox"
VALIDATE_MONGO_SANDBOX_COLLECTION = "world_snapshots"

_NEXT_CALL_ID = 1


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


def build_project(build_dir: Path) -> bool:
    log("Building project...")
    try:
        subprocess.run(
            ["cmake", "-S", "..", "-B", ".", "-DCMAKE_BUILD_TYPE=Release"],
            cwd=build_dir,
            check=True,
            capture_output=True,
        )
        subprocess.run(
            ["cmake", "--build", ".", "-j4"],
            cwd=build_dir,
            check=True,
            capture_output=True,
        )
        log("Build OK")
        return True
    except subprocess.CalledProcessError as exc:
        log(f"Build failed: {exc}")
        if exc.stderr:
            try:
                sys.stderr.write(exc.stderr.decode("utf-8", errors="ignore"))
            except Exception:
                pass
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


def recv_exact(sock: socket.socket, size: int) -> Optional[bytes]:
    if size <= 0:
        return b""

    buf = bytearray()
    while len(buf) < size:
        try:
            chunk = sock.recv(size - len(buf))
        except (socket.timeout, socket.error, OSError):
            return None
        if not chunk:
            return None
        buf.extend(chunk)
    return bytes(buf)


def recv_one_packet_raw(sock: socket.socket, timeout: float) -> Optional[tuple[int, bytes]]:
    sock.settimeout(timeout)
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


def compute_stable_function_id(class_name: str, function_name: str) -> int:
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


def next_call_id() -> int:
    global _NEXT_CALL_ID
    call_id = _NEXT_CALL_ID
    _NEXT_CALL_ID += 1
    return call_id


def build_client_call_packet(function_id: int, call_id: int, payload: bytes) -> bytes:
    body = struct.pack("<BHQI", MT_FUNCTION_CALL, function_id, call_id, len(payload))
    body += payload
    return struct.pack("<I", len(body)) + body


def send_client_call(sock: socket.socket, class_name: str, function_name: str, payload: bytes) -> tuple[int, int]:
    function_id = compute_stable_function_id(class_name, function_name)
    call_id = next_call_id()
    sock.sendall(build_client_call_packet(function_id, call_id, payload))
    return function_id, call_id


def decode_client_call_packet(payload: bytes) -> Optional[tuple[int, int, bytes]]:
    if len(payload) < 2 + 8 + 4:
        return None
    function_id = struct.unpack("<H", payload[:2])[0]
    call_id = struct.unpack("<Q", payload[2:10])[0]
    payload_size = struct.unpack("<I", payload[10:14])[0]
    if 14 + payload_size > len(payload):
        return None
    return function_id, call_id, payload[14:14 + payload_size]


def recv_client_call_response(
    sock: socket.socket,
    expected_function_id: int,
    expected_call_id: int,
    timeout: float,
) -> bytes:
    deadline = time.time() + timeout
    while time.time() < deadline:
        remaining = max(0.1, deadline - time.time())
        packet = recv_one_packet_raw(sock, timeout=min(remaining, 1.0))
        if packet is None:
            continue

        msg_type, payload = packet
        if msg_type != MT_FUNCTION_CALL:
            continue

        decoded = decode_client_call_packet(payload)
        if decoded is None:
            continue

        function_id, call_id, response_payload = decoded
        if function_id == expected_function_id and call_id == expected_call_id:
            return response_payload

    raise TimeoutError(
        f"timeout waiting for response: function_id={expected_function_id}, call_id={expected_call_id}"
    )


def call_client_function(
    sock: socket.socket,
    function_name: str,
    request_payload: bytes,
    timeout: float = 5.0,
) -> bytes:
    function_id, call_id = send_client_call(sock, "MGatewayServer", function_name, request_payload)
    return recv_client_call_response(sock, function_id, call_id, timeout)


class ReflectReader:
    def __init__(self, data: bytes):
        self.data = data
        self.offset = 0

    def _read(self, fmt: str):
        size = struct.calcsize(fmt)
        if self.offset + size > len(self.data):
            raise ValueError(f"read overflow: need={size} offset={self.offset} size={len(self.data)}")
        value = struct.unpack_from(fmt, self.data, self.offset)[0]
        self.offset += size
        return value

    def read_bool(self) -> bool:
        return self._read("<B") != 0

    def read_u32(self) -> int:
        return self._read("<I")

    def read_u64(self) -> int:
        return self._read("<Q")

    def read_string(self) -> str:
        size = self.read_u32()
        if self.offset + size > len(self.data):
            raise ValueError(f"string overflow: need={size} offset={self.offset} size={len(self.data)}")
        raw = self.data[self.offset:self.offset + size]
        self.offset += size
        return raw.decode("utf-8", errors="replace")

    def ensure_consumed(self) -> None:
        if self.offset != len(self.data):
            raise ValueError(f"trailing bytes: offset={self.offset}, size={len(self.data)}")


def parse_login_response(payload: bytes) -> dict:
    reader = ReflectReader(payload)
    result = {
        "bSuccess": reader.read_bool(),
        "PlayerId": reader.read_u64(),
        "SessionKey": reader.read_u32(),
        "Error": reader.read_string(),
    }
    reader.ensure_consumed()
    return result


def parse_find_player_response(payload: bytes) -> dict:
    reader = ReflectReader(payload)
    result = {
        "bFound": reader.read_bool(),
        "PlayerId": reader.read_u64(),
        "GatewayConnectionId": reader.read_u64(),
        "SceneId": reader.read_u32(),
        "Error": reader.read_string(),
    }
    reader.ensure_consumed()
    return result


def parse_logout_response(payload: bytes) -> dict:
    reader = ReflectReader(payload)
    result = {
        "bSuccess": reader.read_bool(),
        "PlayerId": reader.read_u64(),
        "Error": reader.read_string(),
    }
    reader.ensure_consumed()
    return result


def parse_switch_scene_response(payload: bytes) -> dict:
    reader = ReflectReader(payload)
    result = {
        "bSuccess": reader.read_bool(),
        "PlayerId": reader.read_u64(),
        "SceneId": reader.read_u32(),
        "Error": reader.read_string(),
    }
    reader.ensure_consumed()
    return result


def wait_until(deadline: float, step: Callable[[], Optional[dict]]) -> Optional[dict]:
    while time.time() < deadline:
        value = step()
        if value is not None:
            return value
        time.sleep(0.2)
    return None


def try_login(sock: socket.socket, player_id: int) -> Optional[dict]:
    try:
        payload = call_client_function(sock, "Client_Login", struct.pack("<Q", player_id), timeout=3.0)
        return parse_login_response(payload)
    except (TimeoutError, ValueError, OSError):
        return None


def run_validation(
    build_dir: Path,
    timeout: float,
    zone_id: Optional[int],
    log_dir: Path,
    enable_mgo: bool,
    mongo_db: str,
    mongo_collection: str,
) -> bool:
    procs: List[subprocess.Popen] = []
    stop_lingering_servers()
    log_dir.mkdir(parents=True, exist_ok=True)

    base_env = {}
    if zone_id is not None:
        base_env["MESSION_ZONE_ID"] = str(zone_id)

    if enable_mgo:
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

    def cleanup() -> None:
        for proc in reversed(procs):
            try:
                proc.terminate()
                proc.wait(timeout=3)
            except Exception:
                try:
                    proc.kill()
                except Exception:
                    pass

    def launch(name: str, port: int) -> bool:
        proc = start_server(build_dir, name, dict(base_env), log_dir)
        if proc is None:
            return False
        procs.append(proc)
        if not wait_for_port("127.0.0.1", port, timeout):
            log(f"{name} did not become ready on port {port}")
            return False
        return True

    try:
        if not launch("RouterServer", ROUTER_PORT):
            return False
        if enable_mgo and not launch("MgoServer", MGO_PORT):
            return False
        if not launch("LoginServer", LOGIN_PORT):
            return False
        if not launch("WorldServer", WORLD_PORT):
            return False
        if not launch("SceneServer", SCENE_PORT):
            return False
        if not launch("GatewayServer", GATEWAY_PORT):
            return False

        log(f"Server logs: {log_dir}")
        time.sleep(2.0)

        player_id = 10001
        next_scene_id = 2

        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(5.0)
        try:
            sock.connect(("127.0.0.1", GATEWAY_PORT))

            log("Test 1: Client_Login...")
            login_deadline = time.time() + max(timeout, 10.0)
            login_response = wait_until(login_deadline, lambda: try_login(sock, player_id))
            if login_response is None:
                log("  Client_Login failed: no valid response before timeout")
                return False
            if not login_response["bSuccess"]:
                log(f"  Client_Login failed: error={login_response['Error']}")
                return False
            if login_response["PlayerId"] != player_id or login_response["SessionKey"] == 0:
                log(
                    "  Client_Login returned unexpected payload: "
                    f"playerId={login_response['PlayerId']} sessionKey={login_response['SessionKey']}"
                )
                return False
            log(
                "  Client_Login OK: "
                f"playerId={login_response['PlayerId']} sessionKey={login_response['SessionKey']}"
            )

            log("Test 2: Client_FindPlayer after login...")
            find_response = parse_find_player_response(
                call_client_function(sock, "Client_FindPlayer", struct.pack("<Q", player_id))
            )
            if not find_response["bFound"]:
                log(f"  Client_FindPlayer failed: error={find_response['Error']}")
                return False
            if find_response["PlayerId"] != player_id or find_response["GatewayConnectionId"] == 0:
                log(
                    "  Client_FindPlayer returned unexpected payload: "
                    f"playerId={find_response['PlayerId']} "
                    f"gatewayConnectionId={find_response['GatewayConnectionId']}"
                )
                return False
            log(
                "  Client_FindPlayer OK: "
                f"playerId={find_response['PlayerId']} "
                f"gatewayConnectionId={find_response['GatewayConnectionId']} "
                f"sceneId={find_response['SceneId']}"
            )

            log("Test 3: Client_SwitchScene...")
            switch_payload = struct.pack("<QI", player_id, next_scene_id)
            switch_response = parse_switch_scene_response(
                call_client_function(sock, "Client_SwitchScene", switch_payload)
            )
            if not switch_response["bSuccess"]:
                log(f"  Client_SwitchScene failed: error={switch_response['Error']}")
                return False
            if switch_response["PlayerId"] != player_id or switch_response["SceneId"] != next_scene_id:
                log(
                    "  Client_SwitchScene returned unexpected payload: "
                    f"playerId={switch_response['PlayerId']} sceneId={switch_response['SceneId']}"
                )
                return False
            log(
                "  Client_SwitchScene OK: "
                f"playerId={switch_response['PlayerId']} sceneId={switch_response['SceneId']}"
            )

            log("Test 4: Client_FindPlayer after switch...")
            find_after_switch = parse_find_player_response(
                call_client_function(sock, "Client_FindPlayer", struct.pack("<Q", player_id))
            )
            if not find_after_switch["bFound"] or find_after_switch["SceneId"] != next_scene_id:
                log(
                    "  Client_FindPlayer after switch returned unexpected payload: "
                    f"found={find_after_switch['bFound']} sceneId={find_after_switch['SceneId']} "
                    f"error={find_after_switch['Error']}"
                )
                return False
            log(
                "  Client_FindPlayer after switch OK: "
                f"playerId={find_after_switch['PlayerId']} sceneId={find_after_switch['SceneId']}"
            )

            log("Test 5: Client_Logout...")
            logout_response = parse_logout_response(
                call_client_function(sock, "Client_Logout", struct.pack("<Q", player_id))
            )
            if not logout_response["bSuccess"]:
                log(f"  Client_Logout failed: error={logout_response['Error']}")
                return False
            if logout_response["PlayerId"] != player_id:
                log(f"  Client_Logout returned unexpected playerId={logout_response['PlayerId']}")
                return False
            log(f"  Client_Logout OK: playerId={logout_response['PlayerId']}")

            log("Test 6: Client_FindPlayer after logout...")
            find_after_logout = parse_find_player_response(
                call_client_function(sock, "Client_FindPlayer", struct.pack("<Q", player_id))
            )
            if find_after_logout["bFound"]:
                log(
                    "  Client_FindPlayer after logout should not find player, "
                    f"but got sceneId={find_after_logout['SceneId']}"
                )
                return False
            log("  Client_FindPlayer after logout OK: player removed from World state")

            log("Validation PASSED")
            return True
        finally:
            try:
                sock.close()
            except OSError:
                pass
    finally:
        cleanup()


def main() -> int:
    parser = argparse.ArgumentParser(description="Mession skeleton validation")
    parser.add_argument("--build-dir", default="Build", help="Build directory (default: Build)")
    parser.add_argument("--no-build", action="store_true", help="Skip build step")
    parser.add_argument("--timeout", type=float, default=30.0, help="Startup timeout in seconds")
    parser.add_argument("--zone", type=int, default=None, metavar="ID", help="Set MESSION_ZONE_ID")
    parser.add_argument("--debug", action="store_true", help="Retained for compatibility; logs are always written")
    parser.add_argument("--stress", type=int, default=0, metavar="N", help="Reserved compatibility flag")
    parser.add_argument("--stress-moves", type=int, default=5, metavar="M", help="Reserved compatibility flag")
    parser.add_argument("--no-mgo", action="store_true", help="Do not start MgoServer")
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
    log_dir = build_dir / "validate_logs"

    if not build_dir.exists() and not args.no_build:
        build_dir.mkdir(parents=True, exist_ok=True)

    if not args.no_build and not build_project(build_dir):
        return 1

    if not get_executable_path(build_dir, "GatewayServer"):
        log(f"GatewayServer not found in {build_dir}")
        return 1
    if not get_executable_path(build_dir, "RouterServer"):
        log(f"RouterServer not found in {build_dir}")
        return 1
    if not get_executable_path(build_dir, "LoginServer"):
        log(f"LoginServer not found in {build_dir}")
        return 1
    if not get_executable_path(build_dir, "WorldServer"):
        log(f"WorldServer not found in {build_dir}")
        return 1
    if not get_executable_path(build_dir, "SceneServer"):
        log(f"SceneServer not found in {build_dir}")
        return 1
    if not args.no_mgo and not get_executable_path(build_dir, "MgoServer"):
        log(f"MgoServer not found in {build_dir}")
        return 1

    ok = run_validation(
        build_dir=build_dir,
        timeout=args.timeout,
        zone_id=args.zone,
        log_dir=log_dir,
        enable_mgo=(not args.no_mgo),
        mongo_db=args.mongo_db,
        mongo_collection=args.mongo_collection,
    )
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
