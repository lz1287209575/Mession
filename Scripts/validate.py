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

import argparse
import os
import shutil
import socket
import struct
import subprocess
import sys
import time
from pathlib import Path
from typing import Callable, List, Optional, Set

from build_systems import add_build_system_arguments, run_build

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

ALL_TEST_IDS = set(range(1, 44))
SUITE_TESTS = {
    "all": ALL_TEST_IDS,
    "player_state": set(range(1, 15)) | set(range(19, 27)) | {31, 32, 33},
    "runtime_social": {1, 2, 3, 4, 15, 34, 35, 36, 37},
    "scene_downlink": {1, 2, 3, 4, 5, 6, 15, 18, 19, 20},
    "combat_commit": {1, 2, 3, 4, 5, 6, 15, 16, 17, 30, 38, 39, 40, 41, 42, 43},
    "forward_errors": {1, 2, 3, 4, 27, 28, 29},
    "runtime_dispatch": set(range(1, 27)) | {30, 31, 32, 33, 34, 35, 36, 37, 38, 39},
}


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


def compute_stable_id(scope_name: str, member_name: str) -> int:
    offset_basis = 2166136261
    prime = 16777619
    h = offset_basis

    def mix(text: str) -> None:
        nonlocal h
        for ch in text.encode("utf-8"):
            h ^= ch
            h = (h * prime) & 0xFFFFFFFF

    mix(scope_name)
    h ^= ord(":")
    h = (h * prime) & 0xFFFFFFFF
    h ^= ord(":")
    h = (h * prime) & 0xFFFFFFFF
    mix(member_name)

    folded = ((h >> 16) ^ (h & 0xFFFF)) & 0xFFFF
    return folded if folded != 0 else 1


def compute_stable_client_function_id(client_api_name: str) -> int:
    return compute_stable_id("MClientApi", client_api_name)


def compute_stable_downlink_function_id(function_name: str) -> int:
    return compute_stable_id("MClientDownlink", function_name)


def next_call_id() -> int:
    global _NEXT_CALL_ID
    call_id = _NEXT_CALL_ID
    _NEXT_CALL_ID += 1
    return call_id


def build_client_call_packet(function_id: int, call_id: int, payload: bytes) -> bytes:
    body = struct.pack("<BHQI", MT_FUNCTION_CALL, function_id, call_id, len(payload))
    body += payload
    return struct.pack("<I", len(body)) + body


def send_client_call(sock: socket.socket, function_name: str, payload: bytes) -> tuple[int, int]:
    function_id = compute_stable_client_function_id(function_name)
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


def decode_client_function_packet(payload: bytes) -> Optional[tuple[int, bytes]]:
    if len(payload) < 2 + 4:
        return None
    function_id = struct.unpack("<H", payload[:2])[0]
    payload_size = struct.unpack("<I", payload[2:6])[0]
    if 6 + payload_size > len(payload):
        return None
    return function_id, payload[6:6 + payload_size]


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


def recv_client_downlink(
    sock: socket.socket,
    expected_function_name: str,
    timeout: float,
) -> bytes:
    expected_function_id = compute_stable_downlink_function_id(expected_function_name)
    deadline = time.time() + timeout
    while time.time() < deadline:
        remaining = max(0.1, deadline - time.time())
        packet = recv_one_packet_raw(sock, timeout=min(remaining, 1.0))
        if packet is None:
            continue

        msg_type, payload = packet
        if msg_type != MT_FUNCTION_CALL:
            continue

        decoded = decode_client_function_packet(payload)
        if decoded is None:
            continue

        function_id, response_payload = decoded
        if function_id == expected_function_id:
            return response_payload

    raise TimeoutError(f"timeout waiting for client downlink: function_id={expected_function_id}")


def call_client_function(
    sock: socket.socket,
    function_name: str,
    request_payload: bytes,
    timeout: float = 5.0,
) -> bytes:
    function_id, call_id = send_client_call(sock, function_name, request_payload)
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

    def read_u8(self) -> int:
        return self._read("<B")

    def read_u32(self) -> int:
        return self._read("<I")

    def read_u16(self) -> int:
        return self._read("<H")

    def read_u64(self) -> int:
        return self._read("<Q")

    def read_bytes(self, size: int) -> bytes:
        if self.offset + size > len(self.data):
            raise ValueError(f"bytes overflow: need={size} offset={self.offset} size={len(self.data)}")
        raw = self.data[self.offset:self.offset + size]
        self.offset += size
        return raw

    def read_string(self) -> str:
        size = self.read_u32()
        if self.offset + size > len(self.data):
            raise ValueError(f"string overflow: need={size} offset={self.offset} size={len(self.data)}")
        raw = self.data[self.offset:self.offset + size]
        self.offset += size
        return raw.decode("utf-8", errors="replace")

    def read_u64_vector(self) -> List[int]:
        size = self.read_u32()
        result: List[int] = []
        for _ in range(size):
            result.append(self.read_u64())
        return result

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


def parse_change_gold_response(payload: bytes) -> dict:
    reader = ReflectReader(payload)
    result = {
        "bSuccess": reader.read_bool(),
        "PlayerId": reader.read_u64(),
        "Gold": reader.read_u32(),
        "Error": reader.read_string(),
    }
    reader.ensure_consumed()
    return result


def parse_query_profile_response(payload: bytes) -> dict:
    reader = ReflectReader(payload)
    result = {
        "bSuccess": reader.read_bool(),
        "PlayerId": reader.read_u64(),
        "CurrentSceneId": reader.read_u32(),
        "Gold": reader.read_u32(),
        "EquippedItem": reader.read_string(),
        "Level": reader.read_u32(),
        "Experience": reader.read_u32(),
        "Health": reader.read_u32(),
        "Error": reader.read_string(),
    }
    reader.ensure_consumed()
    return result


def parse_query_inventory_response(payload: bytes) -> dict:
    reader = ReflectReader(payload)
    result = {
        "bSuccess": reader.read_bool(),
        "PlayerId": reader.read_u64(),
        "Gold": reader.read_u32(),
        "EquippedItem": reader.read_string(),
        "Error": reader.read_string(),
    }
    reader.ensure_consumed()
    return result


def parse_query_progression_response(payload: bytes) -> dict:
    reader = ReflectReader(payload)
    result = {
        "bSuccess": reader.read_bool(),
        "PlayerId": reader.read_u64(),
        "Level": reader.read_u32(),
        "Experience": reader.read_u32(),
        "Health": reader.read_u32(),
        "Error": reader.read_string(),
    }
    reader.ensure_consumed()
    return result


def parse_query_combat_profile_response(payload: bytes) -> dict:
    reader = ReflectReader(payload)
    result = {
        "bSuccess": reader.read_bool(),
        "PlayerId": reader.read_u64(),
        "BaseAttack": reader.read_u32(),
        "BaseDefense": reader.read_u32(),
        "MaxHealth": reader.read_u32(),
        "PrimarySkillId": reader.read_u32(),
        "LastResolvedSceneId": reader.read_u32(),
        "LastResolvedHealth": reader.read_u32(),
        "Error": reader.read_string(),
    }
    reader.ensure_consumed()
    return result


def parse_set_primary_skill_response(payload: bytes) -> dict:
    reader = ReflectReader(payload)
    result = {
        "bSuccess": reader.read_bool(),
        "PlayerId": reader.read_u64(),
        "PrimarySkillId": reader.read_u32(),
        "Error": reader.read_string(),
    }
    reader.ensure_consumed()
    return result


def parse_create_party_response(payload: bytes) -> dict:
    reader = ReflectReader(payload)
    result = {
        "bSuccess": reader.read_bool(),
        "PlayerId": reader.read_u64(),
        "PartyId": reader.read_u64(),
        "LeaderPlayerId": reader.read_u64(),
        "MemberCount": reader.read_u32(),
        "Error": reader.read_string(),
    }
    reader.ensure_consumed()
    return result


def parse_invite_party_response(payload: bytes) -> dict:
    reader = ReflectReader(payload)
    result = {
        "bSuccess": reader.read_bool(),
        "PlayerId": reader.read_u64(),
        "PartyId": reader.read_u64(),
        "TargetPlayerId": reader.read_u64(),
        "Error": reader.read_string(),
    }
    reader.ensure_consumed()
    return result


def parse_accept_party_invite_response(payload: bytes) -> dict:
    reader = ReflectReader(payload)
    result = {
        "bSuccess": reader.read_bool(),
        "PlayerId": reader.read_u64(),
        "PartyId": reader.read_u64(),
        "LeaderPlayerId": reader.read_u64(),
        "MemberCount": reader.read_u32(),
        "Error": reader.read_string(),
    }
    reader.ensure_consumed()
    return result


def parse_kick_party_member_response(payload: bytes) -> dict:
    reader = ReflectReader(payload)
    result = {
        "bSuccess": reader.read_bool(),
        "PlayerId": reader.read_u64(),
        "PartyId": reader.read_u64(),
        "TargetPlayerId": reader.read_u64(),
        "MemberCount": reader.read_u32(),
        "Error": reader.read_string(),
    }
    reader.ensure_consumed()
    return result


def parse_party_created_notify(payload: bytes) -> dict:
    reader = ReflectReader(payload)
    result = {
        "PartyId": reader.read_u64(),
        "LeaderPlayerId": reader.read_u64(),
        "MemberPlayerIds": reader.read_u64_vector(),
    }
    reader.ensure_consumed()
    return result


def parse_party_invite_received_notify(payload: bytes) -> dict:
    reader = ReflectReader(payload)
    result = {
        "PartyId": reader.read_u64(),
        "LeaderPlayerId": reader.read_u64(),
        "TargetPlayerId": reader.read_u64(),
    }
    reader.ensure_consumed()
    return result


def parse_party_member_joined_notify(payload: bytes) -> dict:
    reader = ReflectReader(payload)
    result = {
        "PartyId": reader.read_u64(),
        "LeaderPlayerId": reader.read_u64(),
        "JoinedPlayerId": reader.read_u64(),
        "MemberPlayerIds": reader.read_u64_vector(),
    }
    reader.ensure_consumed()
    return result


def parse_party_member_removed_notify(payload: bytes) -> dict:
    reader = ReflectReader(payload)
    result = {
        "PartyId": reader.read_u64(),
        "LeaderPlayerId": reader.read_u64(),
        "RemovedPlayerId": reader.read_u64(),
        "MemberPlayerIds": reader.read_u64_vector(),
        "Reason": reader.read_string(),
    }
    reader.ensure_consumed()
    return result


def parse_pawn_state_response(payload: bytes) -> dict:
    reader = ReflectReader(payload)
    result = {
        "bSuccess": reader.read_bool(),
        "PlayerId": reader.read_u64(),
        "SceneId": reader.read_u32(),
        "X": reader._read("<f"),
        "Y": reader._read("<f"),
        "Z": reader._read("<f"),
        "Health": reader.read_u32(),
        "Error": reader.read_string(),
    }
    reader.ensure_consumed()
    return result


def parse_scene_state_message(payload: bytes) -> dict:
    reader = ReflectReader(payload)
    result = {
        "PlayerId": reader.read_u64(),
        "SceneId": reader.read_u16(),
        "X": reader._read("<f"),
        "Y": reader._read("<f"),
        "Z": reader._read("<f"),
    }
    reader.ensure_consumed()
    return result


def parse_scene_leave_message(payload: bytes) -> dict:
    reader = ReflectReader(payload)
    result = {
        "PlayerId": reader.read_u64(),
        "SceneId": reader.read_u16(),
    }
    reader.ensure_consumed()
    return result


def parse_equip_item_response(payload: bytes) -> dict:
    reader = ReflectReader(payload)
    result = {
        "bSuccess": reader.read_bool(),
        "PlayerId": reader.read_u64(),
        "EquippedItem": reader.read_string(),
        "Error": reader.read_string(),
    }
    reader.ensure_consumed()
    return result


def parse_grant_experience_response(payload: bytes) -> dict:
    reader = ReflectReader(payload)
    result = {
        "bSuccess": reader.read_bool(),
        "PlayerId": reader.read_u64(),
        "Level": reader.read_u32(),
        "Experience": reader.read_u32(),
        "Error": reader.read_string(),
    }
    reader.ensure_consumed()
    return result


def parse_modify_health_response(payload: bytes) -> dict:
    reader = ReflectReader(payload)
    result = {
        "bSuccess": reader.read_bool(),
        "PlayerId": reader.read_u64(),
        "Health": reader.read_u32(),
        "Error": reader.read_string(),
    }
    reader.ensure_consumed()
    return result


def parse_cast_skill_response(payload: bytes) -> dict:
    reader = ReflectReader(payload)
    result = {
        "bSuccess": reader.read_bool(),
        "PlayerId": reader.read_u64(),
        "TargetPlayerId": reader.read_u64(),
        "SkillId": reader.read_u32(),
        "SceneId": reader.read_u32(),
        "AppliedDamage": reader.read_u32(),
        "TargetHealth": reader.read_u32(),
        "Error": reader.read_string(),
    }
    reader.ensure_consumed()
    return result


def parse_combat_unit_ref(reader: ReflectReader) -> dict:
    # Reflection currently serializes FCombatUnitRef as raw struct bytes, so the
    # uint8 enum keeps its native 7-byte padding before the uint64 fields.
    unit_kind = reader.read_u8()
    reader.read_bytes(7)
    return {
        "UnitKind": unit_kind,
        "CombatEntityId": reader.read_u64(),
        "PlayerId": reader.read_u64(),
    }


def parse_debug_spawn_monster_response(payload: bytes) -> dict:
    reader = ReflectReader(payload)
    result = {
        "bSuccess": reader.read_bool(),
        "PlayerId": reader.read_u64(),
        "SceneId": reader.read_u32(),
        "MonsterUnit": parse_combat_unit_ref(reader),
        "MonsterTemplateId": reader.read_u32(),
        "Error": reader.read_string(),
    }
    reader.ensure_consumed()
    return result


def parse_cast_skill_at_unit_response(payload: bytes) -> dict:
    reader = ReflectReader(payload)
    result = {
        "bSuccess": reader.read_bool(),
        "PlayerId": reader.read_u64(),
        "TargetUnit": parse_combat_unit_ref(reader),
        "SkillId": reader.read_u32(),
        "SceneId": reader.read_u32(),
        "AppliedDamage": reader.read_u32(),
        "TargetHealth": reader.read_u32(),
        "bTargetDefeated": reader.read_bool(),
        "ExperienceReward": reader.read_u32(),
        "GoldReward": reader.read_u32(),
        "Error": reader.read_string(),
    }
    reader.ensure_consumed()
    return result


def pack_combat_unit_ref(unit_kind: int, combat_entity_id: int, player_id: int) -> bytes:
    return struct.pack("<B7xQQ", unit_kind, combat_entity_id, player_id)


def pack_string(value: str) -> bytes:
    encoded = value.encode("utf-8")
    return struct.pack("<I", len(encoded)) + encoded


def wait_until(deadline: float, step: Callable[[], Optional[dict]]) -> Optional[dict]:
    while time.time() < deadline:
        value = step()
        if value is not None:
            return value
        time.sleep(0.2)
    return None


def parse_suite_names(raw_values: List[str]) -> List[str]:
    suite_names: List[str] = []
    for raw_value in raw_values:
        for item in raw_value.split(","):
            name = item.strip()
            if name:
                suite_names.append(name)
    return suite_names or ["all"]


def resolve_enabled_tests(suite_names: List[str]) -> Set[int]:
    enabled_tests: Set[int] = set()
    for suite_name in suite_names:
        test_ids = SUITE_TESTS.get(suite_name)
        if test_ids is None:
            valid = ", ".join(sorted(SUITE_TESTS.keys()))
            raise ValueError(f"unknown suite '{suite_name}', valid suites: {valid}")
        enabled_tests.update(test_ids)
    return enabled_tests


def try_login(sock: socket.socket, player_id: int) -> Optional[dict]:
    try:
        payload = call_client_function(sock, "Client_Login", struct.pack("<Q", player_id), timeout=3.0)
        return parse_login_response(payload)
    except (TimeoutError, ValueError, OSError):
        return None


def try_find_player(sock: socket.socket, payload: bytes, timeout: float = 3.0) -> Optional[dict]:
    try:
        return parse_find_player_response(call_client_function(sock, "Client_FindPlayer", payload, timeout=timeout))
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
    enabled_tests: Set[int],
) -> bool:
    procs: List[subprocess.Popen] = []
    proc_by_name: dict[str, subprocess.Popen] = {}
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
        proc_by_name[name] = proc
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

        player_id = int(time.time() * 1000) % 4000000000 + 10001
        other_player_id = player_id + 1
        next_scene_id = 2
        initial_session_key = 0
        party_id = 0

        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(5.0)
        sock2 = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock2.settimeout(5.0)
        try:
            sock.connect(("127.0.0.1", GATEWAY_PORT))
            sock2.connect(("127.0.0.1", GATEWAY_PORT))

            if 1 in enabled_tests:
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
                initial_session_key = login_response["SessionKey"]
                log(
                    "  Client_Login OK: "
                    f"playerId={login_response['PlayerId']} sessionKey={login_response['SessionKey']}"
                )

            if 2 in enabled_tests:
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

            if 3 in enabled_tests:
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

            if 4 in enabled_tests:
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

            if 5 in enabled_tests:
                log("Test 5: Client_Move...")
                move_payload = struct.pack("<Qfff", player_id, 128.5, 64.25, 7.0)
                move_response = parse_pawn_state_response(
                    call_client_function(sock, "Client_Move", move_payload)
                )
                if not move_response["bSuccess"]:
                    log(f"  Client_Move failed: error={move_response['Error']}")
                    return False
                if (
                    move_response["PlayerId"] != player_id
                    or move_response["SceneId"] != next_scene_id
                    or abs(move_response["X"] - 128.5) > 0.001
                    or abs(move_response["Y"] - 64.25) > 0.001
                    or abs(move_response["Z"] - 7.0) > 0.001
                    or move_response["Health"] != 100
                ):
                    log(f"  Client_Move returned unexpected payload: {move_response}")
                    return False
                log(f"  Client_Move OK: {move_response}")

            if 6 in enabled_tests:
                log("Test 6: Client_QueryPawn...")
                query_pawn_response = parse_pawn_state_response(
                    call_client_function(sock, "Client_QueryPawn", struct.pack("<Q", player_id))
                )
                if (
                    not query_pawn_response["bSuccess"]
                    or query_pawn_response["PlayerId"] != player_id
                    or query_pawn_response["SceneId"] != next_scene_id
                    or abs(query_pawn_response["X"] - 128.5) > 0.001
                    or abs(query_pawn_response["Y"] - 64.25) > 0.001
                    or abs(query_pawn_response["Z"] - 7.0) > 0.001
                    or query_pawn_response["Health"] != 100
                ):
                    log(f"  Client_QueryPawn returned unexpected payload: {query_pawn_response}")
                    return False
                log(f"  Client_QueryPawn OK: {query_pawn_response}")

            if 7 in enabled_tests:
                log("Test 7: Client_ChangeGold...")
                change_gold_response = parse_change_gold_response(
                    call_client_function(sock, "Client_ChangeGold", struct.pack("<Qi", player_id, 50))
                )
                if not change_gold_response["bSuccess"]:
                    log(f"  Client_ChangeGold failed: error={change_gold_response['Error']}")
                    return False
                if change_gold_response["PlayerId"] != player_id or change_gold_response["Gold"] != 50:
                    log(
                        "  Client_ChangeGold returned unexpected payload: "
                        f"playerId={change_gold_response['PlayerId']} gold={change_gold_response['Gold']}"
                    )
                    return False
                log(
                    "  Client_ChangeGold OK: "
                    f"playerId={change_gold_response['PlayerId']} gold={change_gold_response['Gold']}"
                )

            if 8 in enabled_tests:
                log("Test 8: Client_EquipItem...")
                equip_item_response = parse_equip_item_response(
                    call_client_function(
                        sock,
                        "Client_EquipItem",
                        struct.pack("<Q", player_id) + pack_string("iron_sword"),
                    )
                )
                if not equip_item_response["bSuccess"]:
                    log(f"  Client_EquipItem failed: error={equip_item_response['Error']}")
                    return False
                if equip_item_response["PlayerId"] != player_id or equip_item_response["EquippedItem"] != "iron_sword":
                    log(
                        "  Client_EquipItem returned unexpected payload: "
                        f"playerId={equip_item_response['PlayerId']} "
                        f"equippedItem={equip_item_response['EquippedItem']}"
                    )
                    return False
                log(
                    "  Client_EquipItem OK: "
                    f"playerId={equip_item_response['PlayerId']} "
                    f"equippedItem={equip_item_response['EquippedItem']}"
                )

            if 9 in enabled_tests:
                log("Test 9: Client_GrantExperience...")
                grant_experience_response = parse_grant_experience_response(
                    call_client_function(sock, "Client_GrantExperience", struct.pack("<QI", player_id, 250))
                )
                if not grant_experience_response["bSuccess"]:
                    log(f"  Client_GrantExperience failed: error={grant_experience_response['Error']}")
                    return False
                if (
                    grant_experience_response["PlayerId"] != player_id
                    or grant_experience_response["Level"] != 3
                    or grant_experience_response["Experience"] != 250
                ):
                    log(
                        "  Client_GrantExperience returned unexpected payload: "
                        f"playerId={grant_experience_response['PlayerId']} "
                        f"level={grant_experience_response['Level']} "
                        f"experience={grant_experience_response['Experience']}"
                    )
                    return False
                log(
                    "  Client_GrantExperience OK: "
                    f"playerId={grant_experience_response['PlayerId']} "
                    f"level={grant_experience_response['Level']} "
                    f"experience={grant_experience_response['Experience']}"
                )

            if 10 in enabled_tests:
                log("Test 10: Client_ModifyHealth...")
                modify_health_response = parse_modify_health_response(
                    call_client_function(sock, "Client_ModifyHealth", struct.pack("<Qi", player_id, -25))
                )
                if not modify_health_response["bSuccess"]:
                    log(f"  Client_ModifyHealth failed: error={modify_health_response['Error']}")
                    return False
                if modify_health_response["PlayerId"] != player_id or modify_health_response["Health"] != 75:
                    log(
                        "  Client_ModifyHealth returned unexpected payload: "
                        f"playerId={modify_health_response['PlayerId']} health={modify_health_response['Health']}"
                    )
                    return False
                log(
                    "  Client_ModifyHealth OK: "
                    f"playerId={modify_health_response['PlayerId']} health={modify_health_response['Health']}"
                )

            if 11 in enabled_tests:
                log("Test 11: Client_QueryPawn after health change...")
                query_pawn_after_health = parse_pawn_state_response(
                    call_client_function(sock, "Client_QueryPawn", struct.pack("<Q", player_id))
                )
                if (
                    not query_pawn_after_health["bSuccess"]
                    or query_pawn_after_health["PlayerId"] != player_id
                    or query_pawn_after_health["SceneId"] != next_scene_id
                    or abs(query_pawn_after_health["X"] - 128.5) > 0.001
                    or abs(query_pawn_after_health["Y"] - 64.25) > 0.001
                    or abs(query_pawn_after_health["Z"] - 7.0) > 0.001
                    or query_pawn_after_health["Health"] != 75
                ):
                    log(
                        "  Client_QueryPawn after health change returned unexpected payload: "
                        f"{query_pawn_after_health}"
                    )
                    return False
                log(f"  Client_QueryPawn after health change OK: {query_pawn_after_health}")

            if 12 in enabled_tests:
                log("Test 12: Client_QueryProfile after writes...")
                query_profile_response = parse_query_profile_response(
                    call_client_function(sock, "Client_QueryProfile", struct.pack("<Q", player_id))
                )
                if (
                    not query_profile_response["bSuccess"]
                    or query_profile_response["PlayerId"] != player_id
                    or query_profile_response["CurrentSceneId"] != next_scene_id
                    or query_profile_response["Gold"] != 50
                    or query_profile_response["EquippedItem"] != "iron_sword"
                    or query_profile_response["Level"] != 3
                    or query_profile_response["Experience"] != 250
                    or query_profile_response["Health"] != 75
                ):
                    log(
                        "  Client_QueryProfile returned unexpected payload: "
                        f"{query_profile_response}"
                    )
                    return False
                log(f"  Client_QueryProfile OK: {query_profile_response}")

            if 13 in enabled_tests:
                log("Test 13: Client_QueryInventory after writes...")
                query_inventory_response = parse_query_inventory_response(
                    call_client_function(sock, "Client_QueryInventory", struct.pack("<Q", player_id))
                )
                if (
                    not query_inventory_response["bSuccess"]
                    or query_inventory_response["PlayerId"] != player_id
                    or query_inventory_response["Gold"] != 50
                    or query_inventory_response["EquippedItem"] != "iron_sword"
                ):
                    log(
                        "  Client_QueryInventory returned unexpected payload: "
                        f"{query_inventory_response}"
                    )
                    return False
                log(f"  Client_QueryInventory OK: {query_inventory_response}")

            if 14 in enabled_tests:
                log("Test 14: Client_QueryProgression after writes...")
                query_progression_response = parse_query_progression_response(
                    call_client_function(sock, "Client_QueryProgression", struct.pack("<Q", player_id))
                )
                if (
                    not query_progression_response["bSuccess"]
                    or query_progression_response["PlayerId"] != player_id
                    or query_progression_response["Level"] != 3
                    or query_progression_response["Experience"] != 250
                    or query_progression_response["Health"] != 75
                ):
                    log(
                        "  Client_QueryProgression returned unexpected payload: "
                        f"{query_progression_response}"
                    )
                    return False
                log(f"  Client_QueryProgression OK: {query_progression_response}")

            if 31 in enabled_tests:
                log("Test 31: Client_SetPrimarySkill...")
                set_primary_skill_response = parse_set_primary_skill_response(
                    call_client_function(sock, "Client_SetPrimarySkill", struct.pack("<QI", player_id, 2002))
                )
                if not set_primary_skill_response["bSuccess"]:
                    log(f"  Client_SetPrimarySkill failed: error={set_primary_skill_response['Error']}")
                    return False
                if (
                    set_primary_skill_response["PlayerId"] != player_id
                    or set_primary_skill_response["PrimarySkillId"] != 2002
                ):
                    log(
                        "  Client_SetPrimarySkill returned unexpected payload: "
                        f"{set_primary_skill_response}"
                    )
                    return False
                log(f"  Client_SetPrimarySkill OK: {set_primary_skill_response}")

            if 32 in enabled_tests:
                log("Test 32: Client_QueryCombatProfile after writes...")
                query_combat_profile_after_write = parse_query_combat_profile_response(
                    call_client_function(sock, "Client_QueryCombatProfile", struct.pack("<Q", player_id))
                )
                if (
                    not query_combat_profile_after_write["bSuccess"]
                    or query_combat_profile_after_write["PlayerId"] != player_id
                    or query_combat_profile_after_write["BaseAttack"] != 10
                    or query_combat_profile_after_write["BaseDefense"] != 5
                    or query_combat_profile_after_write["MaxHealth"] != 100
                    or query_combat_profile_after_write["PrimarySkillId"] != 2002
                    or query_combat_profile_after_write["LastResolvedSceneId"] != 0
                    or query_combat_profile_after_write["LastResolvedHealth"] != 100
                ):
                    log(
                        "  Client_QueryCombatProfile after writes returned unexpected payload: "
                        f"{query_combat_profile_after_write}"
                    )
                    return False
                log(f"  Client_QueryCombatProfile after writes OK: {query_combat_profile_after_write}")

            if 15 in enabled_tests:
                log("Test 15: second player enter same scene and trigger downlink...")
                other_login_response = wait_until(
                    time.time() + max(timeout, 10.0),
                    lambda: try_login(sock2, other_player_id),
                )
                if other_login_response is None:
                    log("  second player login failed: no valid response before timeout")
                    return False
                if not other_login_response["bSuccess"]:
                    log(f"  second player login failed: error={other_login_response['Error']}")
                    return False

                other_switch_response = parse_switch_scene_response(
                    call_client_function(sock2, "Client_SwitchScene", struct.pack("<QI", other_player_id, next_scene_id))
                )
                if not other_switch_response["bSuccess"]:
                    log(f"  second player switch failed: error={other_switch_response['Error']}")
                    return False

                enter_downlink = parse_scene_state_message(
                    recv_client_downlink(sock, "Client_ScenePlayerEnter", 5.0)
                )
                if (
                    enter_downlink["PlayerId"] != other_player_id
                    or enter_downlink["SceneId"] != next_scene_id
                    or abs(enter_downlink["X"]) > 0.001
                    or abs(enter_downlink["Y"]) > 0.001
                    or abs(enter_downlink["Z"]) > 0.001
                ):
                    log(f"  Client_ScenePlayerEnter returned unexpected payload: {enter_downlink}")
                    return False
                log(f"  Client_ScenePlayerEnter OK: {enter_downlink}")

            if 34 in enabled_tests:
                log("Test 34: Client_CreateParty...")
                create_party_response = parse_create_party_response(
                    call_client_function(sock, "Client_CreateParty", struct.pack("<Q", player_id))
                )
                if not create_party_response["bSuccess"]:
                    log(f"  Client_CreateParty failed: error={create_party_response['Error']}")
                    return False
                if (
                    create_party_response["PlayerId"] != player_id
                    or create_party_response["LeaderPlayerId"] != player_id
                    or create_party_response["MemberCount"] != 1
                    or create_party_response["PartyId"] == 0
                ):
                    log(f"  Client_CreateParty returned unexpected payload: {create_party_response}")
                    return False
                party_id = create_party_response["PartyId"]
                log(f"  Client_CreateParty OK: {create_party_response}")

            if 35 in enabled_tests:
                log("Test 35: Client_InviteParty...")
                invite_party_response = parse_invite_party_response(
                    call_client_function(sock, "Client_InviteParty", struct.pack("<QQ", player_id, other_player_id))
                )
                if not invite_party_response["bSuccess"]:
                    log(f"  Client_InviteParty failed: error={invite_party_response['Error']}")
                    return False
                if (
                    invite_party_response["PlayerId"] != player_id
                    or invite_party_response["PartyId"] != party_id
                    or invite_party_response["TargetPlayerId"] != other_player_id
                ):
                    log(f"  Client_InviteParty returned unexpected payload: {invite_party_response}")
                    return False
                party_invite_notify = parse_party_invite_received_notify(
                    recv_client_downlink(sock2, "Client_PartyInviteReceived", 5.0)
                )
                if (
                    party_invite_notify["PartyId"] != party_id
                    or party_invite_notify["LeaderPlayerId"] != player_id
                    or party_invite_notify["TargetPlayerId"] != other_player_id
                ):
                    log(f"  Client_PartyInviteReceived returned unexpected payload: {party_invite_notify}")
                    return False
                log(f"  Client_InviteParty OK: {invite_party_response}")

            if 36 in enabled_tests:
                log("Test 36: Client_AcceptPartyInvite...")
                accept_party_invite_response = parse_accept_party_invite_response(
                    call_client_function(sock2, "Client_AcceptPartyInvite", struct.pack("<QQ", other_player_id, party_id))
                )
                if not accept_party_invite_response["bSuccess"]:
                    log(f"  Client_AcceptPartyInvite failed: error={accept_party_invite_response['Error']}")
                    return False
                if (
                    accept_party_invite_response["PlayerId"] != other_player_id
                    or accept_party_invite_response["PartyId"] != party_id
                    or accept_party_invite_response["LeaderPlayerId"] != player_id
                    or accept_party_invite_response["MemberCount"] != 2
                ):
                    log(
                        "  Client_AcceptPartyInvite returned unexpected payload: "
                        f"{accept_party_invite_response}"
                    )
                    return False
                party_member_joined_notify = parse_party_member_joined_notify(
                    recv_client_downlink(sock, "Client_PartyMemberJoined", 5.0)
                )
                if (
                    party_member_joined_notify["PartyId"] != party_id
                    or party_member_joined_notify["LeaderPlayerId"] != player_id
                    or party_member_joined_notify["JoinedPlayerId"] != other_player_id
                    or party_member_joined_notify["MemberPlayerIds"] != [player_id, other_player_id]
                ):
                    log(f"  Client_PartyMemberJoined returned unexpected payload: {party_member_joined_notify}")
                    return False
                log(f"  Client_AcceptPartyInvite OK: {accept_party_invite_response}")

            if 37 in enabled_tests:
                log("Test 37: Client_KickPartyMember...")
                kick_party_member_response = parse_kick_party_member_response(
                    call_client_function(sock, "Client_KickPartyMember", struct.pack("<QQQ", player_id, party_id, other_player_id))
                )
                if not kick_party_member_response["bSuccess"]:
                    log(f"  Client_KickPartyMember failed: error={kick_party_member_response['Error']}")
                    return False
                if (
                    kick_party_member_response["PlayerId"] != player_id
                    or kick_party_member_response["PartyId"] != party_id
                    or kick_party_member_response["TargetPlayerId"] != other_player_id
                    or kick_party_member_response["MemberCount"] != 1
                ):
                    log(
                        "  Client_KickPartyMember returned unexpected payload: "
                        f"{kick_party_member_response}"
                    )
                    return False
                party_member_removed_notify = parse_party_member_removed_notify(
                    recv_client_downlink(sock2, "Client_PartyMemberRemoved", 5.0)
                )
                if (
                    party_member_removed_notify["PartyId"] != party_id
                    or party_member_removed_notify["LeaderPlayerId"] != player_id
                    or party_member_removed_notify["RemovedPlayerId"] != other_player_id
                    or party_member_removed_notify["MemberPlayerIds"] != [player_id]
                    or party_member_removed_notify["Reason"] != "member_kicked"
                ):
                    log(f"  Client_PartyMemberRemoved returned unexpected payload: {party_member_removed_notify}")
                    return False
                log(f"  Client_KickPartyMember OK: {kick_party_member_response}")

            if 16 in enabled_tests:
                log("Test 16: Client_CastSkill...")
                cast_skill_response = parse_cast_skill_response(
                    call_client_function(sock, "Client_CastSkill", struct.pack("<QQI", player_id, other_player_id, 1001))
                )
                if not cast_skill_response["bSuccess"]:
                    log(f"  Client_CastSkill failed: error={cast_skill_response['Error']}")
                    return False
                if (
                    cast_skill_response["PlayerId"] != player_id
                    or cast_skill_response["TargetPlayerId"] != other_player_id
                    or cast_skill_response["SkillId"] != 1001
                    or cast_skill_response["SceneId"] != next_scene_id
                    or cast_skill_response["AppliedDamage"] != 5
                    or cast_skill_response["TargetHealth"] != 95
                ):
                    log(f"  Client_CastSkill returned unexpected payload: {cast_skill_response}")
                    return False
                log(f"  Client_CastSkill OK: {cast_skill_response}")

            if 17 in enabled_tests:
                log("Test 17: target Client_QueryPawn after skill...")
                target_pawn_after_skill = parse_pawn_state_response(
                    call_client_function(sock2, "Client_QueryPawn", struct.pack("<Q", other_player_id))
                )
                if (
                    not target_pawn_after_skill["bSuccess"]
                    or target_pawn_after_skill["PlayerId"] != other_player_id
                    or target_pawn_after_skill["SceneId"] != next_scene_id
                    or target_pawn_after_skill["Health"] != 95
                ):
                    log(f"  target Client_QueryPawn after skill returned unexpected payload: {target_pawn_after_skill}")
                    return False
                log(f"  target Client_QueryPawn after skill OK: {target_pawn_after_skill}")

            if 30 in enabled_tests:
                log("Test 30: target Client_QueryCombatProfile after skill...")
                target_combat_profile_after_skill = parse_query_combat_profile_response(
                    call_client_function(sock2, "Client_QueryCombatProfile", struct.pack("<Q", other_player_id))
                )
                if (
                    not target_combat_profile_after_skill["bSuccess"]
                    or target_combat_profile_after_skill["PlayerId"] != other_player_id
                    or target_combat_profile_after_skill["BaseAttack"] != 10
                    or target_combat_profile_after_skill["BaseDefense"] != 5
                    or target_combat_profile_after_skill["MaxHealth"] != 100
                    or target_combat_profile_after_skill["PrimarySkillId"] != 1001
                    or target_combat_profile_after_skill["LastResolvedSceneId"] != next_scene_id
                    or target_combat_profile_after_skill["LastResolvedHealth"] != 95
                ):
                    log(
                        "  target Client_QueryCombatProfile after skill returned unexpected payload: "
                        f"{target_combat_profile_after_skill}"
                    )
                    return False
                log(f"  target Client_QueryCombatProfile after skill OK: {target_combat_profile_after_skill}")

            if 38 in enabled_tests:
                log("Test 38: target Client_QueryProfile after skill...")
                target_profile_after_skill = parse_query_profile_response(
                    call_client_function(sock2, "Client_QueryProfile", struct.pack("<Q", other_player_id))
                )
                if (
                    not target_profile_after_skill["bSuccess"]
                    or target_profile_after_skill["PlayerId"] != other_player_id
                    or target_profile_after_skill["CurrentSceneId"] != next_scene_id
                    or target_profile_after_skill["Level"] != 1
                    or target_profile_after_skill["Experience"] != 0
                    or target_profile_after_skill["Health"] != 95
                ):
                    log(
                        "  target Client_QueryProfile after skill returned unexpected payload: "
                        f"{target_profile_after_skill}"
                    )
                    return False
                log(f"  target Client_QueryProfile after skill OK: {target_profile_after_skill}")

            if 39 in enabled_tests:
                log("Test 39: target Client_QueryProgression after skill...")
                target_progression_after_skill = parse_query_progression_response(
                    call_client_function(sock2, "Client_QueryProgression", struct.pack("<Q", other_player_id))
                )
                if (
                    not target_progression_after_skill["bSuccess"]
                    or target_progression_after_skill["PlayerId"] != other_player_id
                    or target_progression_after_skill["Level"] != 1
                    or target_progression_after_skill["Experience"] != 0
                    or target_progression_after_skill["Health"] != 95
                ):
                    log(
                        "  target Client_QueryProgression after skill returned unexpected payload: "
                        f"{target_progression_after_skill}"
                    )
                    return False
                log(f"  target Client_QueryProgression after skill OK: {target_progression_after_skill}")

            spawned_monster_unit = None
            if 40 in enabled_tests:
                log("Test 40: Client_DebugSpawnMonster...")
                spawn_monster_payload = (
                    struct.pack("<QI", player_id, 9001)
                    + pack_string("validate_slime")
                    + struct.pack("<IIIIIII", 50, 50, 3, 0, 1001, 120, 25)
                )
                spawn_monster_response = parse_debug_spawn_monster_response(
                    call_client_function(sock, "Client_DebugSpawnMonster", spawn_monster_payload)
                )
                if not spawn_monster_response["bSuccess"]:
                    log(f"  Client_DebugSpawnMonster failed: error={spawn_monster_response['Error']}")
                    return False
                monster_unit = spawn_monster_response["MonsterUnit"]
                if (
                    spawn_monster_response["PlayerId"] != player_id
                    or spawn_monster_response["SceneId"] != next_scene_id
                    or spawn_monster_response["MonsterTemplateId"] != 9001
                    or monster_unit["UnitKind"] != 2
                    or monster_unit["CombatEntityId"] == 0
                    or monster_unit["PlayerId"] != 0
                ):
                    log(f"  Client_DebugSpawnMonster returned unexpected payload: {spawn_monster_response}")
                    return False
                spawned_monster_unit = monster_unit
                log(f"  Client_DebugSpawnMonster OK: {spawn_monster_response}")

            if 41 in enabled_tests:
                log("Test 41: Client_CastSkillAtUnit on monster...")
                if spawned_monster_unit is None:
                    log("  Client_CastSkillAtUnit skipped: monster was not spawned")
                    return False
                cast_skill_at_unit_payload = (
                    struct.pack("<Q", player_id)
                    + pack_combat_unit_ref(
                        spawned_monster_unit["UnitKind"],
                        spawned_monster_unit["CombatEntityId"],
                        spawned_monster_unit["PlayerId"],
                    )
                    + struct.pack("<I", 1001)
                )
                cast_skill_at_unit_response = parse_cast_skill_at_unit_response(
                    call_client_function(sock, "Client_CastSkillAtUnit", cast_skill_at_unit_payload)
                )
                if not cast_skill_at_unit_response["bSuccess"]:
                    log(f"  Client_CastSkillAtUnit failed: error={cast_skill_at_unit_response['Error']}")
                    return False
                if (
                    cast_skill_at_unit_response["PlayerId"] != player_id
                    or cast_skill_at_unit_response["TargetUnit"]["UnitKind"] != 2
                    or cast_skill_at_unit_response["TargetUnit"]["CombatEntityId"] != spawned_monster_unit["CombatEntityId"]
                    or cast_skill_at_unit_response["SkillId"] != 1001
                    or cast_skill_at_unit_response["SceneId"] != next_scene_id
                    or cast_skill_at_unit_response["AppliedDamage"] != 10
                    or cast_skill_at_unit_response["TargetHealth"] != 40
                    or cast_skill_at_unit_response["bTargetDefeated"]
                    or cast_skill_at_unit_response["ExperienceReward"] != 0
                    or cast_skill_at_unit_response["GoldReward"] != 0
                ):
                    log(f"  Client_CastSkillAtUnit returned unexpected payload: {cast_skill_at_unit_response}")
                    return False
                log(f"  Client_CastSkillAtUnit OK: {cast_skill_at_unit_response}")

            if 42 in enabled_tests:
                log("Test 42: kill monster and apply rewards...")
                if spawned_monster_unit is None:
                    log("  monster kill test skipped: monster was not spawned")
                    return False

                final_monster_kill_response = None
                expected_healths = [30, 20, 10, 0]
                for cast_index, expected_target_health in enumerate(expected_healths, start=1):
                    cast_skill_at_unit_payload = (
                        struct.pack("<Q", player_id)
                        + pack_combat_unit_ref(
                            spawned_monster_unit["UnitKind"],
                            spawned_monster_unit["CombatEntityId"],
                            spawned_monster_unit["PlayerId"],
                        )
                        + struct.pack("<I", 1001)
                    )
                    cast_skill_at_unit_response = parse_cast_skill_at_unit_response(
                        call_client_function(sock, "Client_CastSkillAtUnit", cast_skill_at_unit_payload)
                    )
                    if not cast_skill_at_unit_response["bSuccess"]:
                        log(
                            "  monster kill cast failed on step "
                            f"{cast_index}: error={cast_skill_at_unit_response['Error']}"
                        )
                        return False
                    if (
                        cast_skill_at_unit_response["PlayerId"] != player_id
                        or cast_skill_at_unit_response["TargetUnit"]["UnitKind"] != 2
                        or cast_skill_at_unit_response["TargetUnit"]["CombatEntityId"] != spawned_monster_unit["CombatEntityId"]
                        or cast_skill_at_unit_response["SkillId"] != 1001
                        or cast_skill_at_unit_response["SceneId"] != next_scene_id
                        or cast_skill_at_unit_response["AppliedDamage"] != 10
                        or cast_skill_at_unit_response["TargetHealth"] != expected_target_health
                    ):
                        log(
                            "  monster kill cast returned unexpected payload on step "
                            f"{cast_index}: {cast_skill_at_unit_response}"
                        )
                        return False
                    if expected_target_health != 0:
                        if (
                            cast_skill_at_unit_response["bTargetDefeated"]
                            or cast_skill_at_unit_response["ExperienceReward"] != 0
                            or cast_skill_at_unit_response["GoldReward"] != 0
                        ):
                            log(
                                "  monster kill cast returned premature reward payload on step "
                                f"{cast_index}: {cast_skill_at_unit_response}"
                            )
                            return False
                    else:
                        if (
                            not cast_skill_at_unit_response["bTargetDefeated"]
                            or cast_skill_at_unit_response["ExperienceReward"] != 120
                            or cast_skill_at_unit_response["GoldReward"] != 25
                        ):
                            log(f"  monster final kill payload unexpected: {cast_skill_at_unit_response}")
                            return False
                        final_monster_kill_response = cast_skill_at_unit_response

                if final_monster_kill_response is None:
                    log("  monster final kill response missing")
                    return False

                caster_profile_after_kill = parse_query_profile_response(
                    call_client_function(sock, "Client_QueryProfile", struct.pack("<Q", player_id))
                )
                if (
                    not caster_profile_after_kill["bSuccess"]
                    or caster_profile_after_kill["PlayerId"] != player_id
                    or caster_profile_after_kill["CurrentSceneId"] != next_scene_id
                    or caster_profile_after_kill["Gold"] != 25
                    or caster_profile_after_kill["Level"] != 2
                    or caster_profile_after_kill["Experience"] != 120
                    or caster_profile_after_kill["Health"] != 100
                ):
                    log(f"  Client_QueryProfile after monster kill returned unexpected payload: {caster_profile_after_kill}")
                    return False
                log(f"  Client_QueryProfile after monster kill OK: {caster_profile_after_kill}")

                caster_inventory_after_kill = parse_query_inventory_response(
                    call_client_function(sock, "Client_QueryInventory", struct.pack("<Q", player_id))
                )
                if (
                    not caster_inventory_after_kill["bSuccess"]
                    or caster_inventory_after_kill["PlayerId"] != player_id
                    or caster_inventory_after_kill["Gold"] != 25
                    or caster_inventory_after_kill["EquippedItem"] != "starter_sword"
                ):
                    log(
                        "  Client_QueryInventory after monster kill returned unexpected payload: "
                        f"{caster_inventory_after_kill}"
                    )
                    return False
                log(f"  Client_QueryInventory after monster kill OK: {caster_inventory_after_kill}")

                caster_progression_after_kill = parse_query_progression_response(
                    call_client_function(sock, "Client_QueryProgression", struct.pack("<Q", player_id))
                )
                if (
                    not caster_progression_after_kill["bSuccess"]
                    or caster_progression_after_kill["PlayerId"] != player_id
                    or caster_progression_after_kill["Level"] != 2
                    or caster_progression_after_kill["Experience"] != 120
                    or caster_progression_after_kill["Health"] != 100
                ):
                    log(
                        "  Client_QueryProgression after monster kill returned unexpected payload: "
                        f"{caster_progression_after_kill}"
                    )
                    return False
                log(f"  Client_QueryProgression after monster kill OK: {caster_progression_after_kill}")

            if 43 in enabled_tests:
                log("Test 43: dead monster cannot be targeted again...")
                if spawned_monster_unit is None:
                    log("  dead monster target test skipped: monster was not spawned")
                    return False
                cast_skill_at_unit_payload = (
                    struct.pack("<Q", player_id)
                    + pack_combat_unit_ref(
                        spawned_monster_unit["UnitKind"],
                        spawned_monster_unit["CombatEntityId"],
                        spawned_monster_unit["PlayerId"],
                    )
                    + struct.pack("<I", 1001)
                )
                dead_monster_response = parse_cast_skill_at_unit_response(
                    call_client_function(sock, "Client_CastSkillAtUnit", cast_skill_at_unit_payload)
                )
                if dead_monster_response["bSuccess"] or dead_monster_response["Error"] != "scene_combat_target_not_found":
                    log(f"  dead monster retarget returned unexpected payload: {dead_monster_response}")
                    return False
                log(f"  dead monster retarget OK: {dead_monster_response}")

            if 18 in enabled_tests:
                log("Test 18: second player receives first player move update...")
                move_again_response = parse_pawn_state_response(
                    call_client_function(sock, "Client_Move", struct.pack("<Qfff", player_id, 256.0, 96.0, 11.0))
                )
                if not move_again_response["bSuccess"]:
                    log(f"  second move failed: error={move_again_response['Error']}")
                    return False

                update_downlink = parse_scene_state_message(
                    recv_client_downlink(sock2, "Client_ScenePlayerUpdate", 5.0)
                )
                if (
                    update_downlink["PlayerId"] != player_id
                    or update_downlink["SceneId"] != next_scene_id
                    or abs(update_downlink["X"] - 256.0) > 0.001
                    or abs(update_downlink["Y"] - 96.0) > 0.001
                    or abs(update_downlink["Z"] - 11.0) > 0.001
                ):
                    log(f"  Client_ScenePlayerUpdate returned unexpected payload: {update_downlink}")
                    return False
                log(f"  Client_ScenePlayerUpdate OK: {update_downlink}")

            if 19 in enabled_tests:
                log("Test 19: Client_Logout...")
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

            if 20 in enabled_tests:
                log("Test 20: second player receives leave downlink...")
                leave_downlink = parse_scene_leave_message(
                    recv_client_downlink(sock2, "Client_ScenePlayerLeave", 5.0)
                )
                if leave_downlink["PlayerId"] != player_id or leave_downlink["SceneId"] != next_scene_id:
                    log(f"  Client_ScenePlayerLeave returned unexpected payload: {leave_downlink}")
                    return False
                log(f"  Client_ScenePlayerLeave OK: {leave_downlink}")

            if 21 in enabled_tests:
                log("Test 21: Client_FindPlayer after logout...")
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

            if 22 in enabled_tests:
                log("Test 22: Client_Login again after logout...")
                relogin_response = parse_login_response(
                    call_client_function(sock, "Client_Login", struct.pack("<Q", player_id))
                )
                if not relogin_response["bSuccess"]:
                    log(f"  second Client_Login failed: error={relogin_response['Error']}")
                    return False
                if relogin_response["PlayerId"] != player_id or relogin_response["SessionKey"] == 0:
                    log(
                        "  second Client_Login returned unexpected payload: "
                        f"playerId={relogin_response['PlayerId']} sessionKey={relogin_response['SessionKey']}"
                    )
                    return False
                if initial_session_key != 0 and relogin_response["SessionKey"] == initial_session_key:
                    log(
                        "  second Client_Login should allocate a fresh session key, "
                        f"but reused {relogin_response['SessionKey']}"
                    )
                    return False
                log(
                    "  second Client_Login OK: "
                    f"playerId={relogin_response['PlayerId']} sessionKey={relogin_response['SessionKey']}"
                )

            if 23 in enabled_tests:
                log("Test 23: Client_FindPlayer after relogin...")
                find_after_relogin = parse_find_player_response(
                    call_client_function(sock, "Client_FindPlayer", struct.pack("<Q", player_id))
                )
                if not find_after_relogin["bFound"] or find_after_relogin["SceneId"] != next_scene_id:
                    log(
                        "  Client_FindPlayer after relogin returned unexpected payload: "
                        f"found={find_after_relogin['bFound']} sceneId={find_after_relogin['SceneId']} "
                        f"error={find_after_relogin['Error']}"
                    )
                    return False
                log(
                    "  Client_FindPlayer after relogin OK: "
                    f"playerId={find_after_relogin['PlayerId']} sceneId={find_after_relogin['SceneId']}"
                )

            if 24 in enabled_tests:
                log("Test 24: Client_QueryProfile after relogin...")
                query_profile_after_relogin = parse_query_profile_response(
                    call_client_function(sock, "Client_QueryProfile", struct.pack("<Q", player_id))
                )
                if (
                    not query_profile_after_relogin["bSuccess"]
                    or query_profile_after_relogin["PlayerId"] != player_id
                    or query_profile_after_relogin["CurrentSceneId"] != next_scene_id
                    or query_profile_after_relogin["Gold"] != 50
                    or query_profile_after_relogin["EquippedItem"] != "iron_sword"
                    or query_profile_after_relogin["Level"] != 3
                    or query_profile_after_relogin["Experience"] != 250
                    or query_profile_after_relogin["Health"] != 75
                ):
                    log(
                        "  Client_QueryProfile after relogin returned unexpected payload: "
                        f"{query_profile_after_relogin}"
                    )
                    return False
                log(f"  Client_QueryProfile after relogin OK: {query_profile_after_relogin}")

            if 25 in enabled_tests:
                log("Test 25: Client_QueryInventory after relogin...")
                query_inventory_after_relogin = parse_query_inventory_response(
                    call_client_function(sock, "Client_QueryInventory", struct.pack("<Q", player_id))
                )
                if (
                    not query_inventory_after_relogin["bSuccess"]
                    or query_inventory_after_relogin["PlayerId"] != player_id
                    or query_inventory_after_relogin["Gold"] != 50
                    or query_inventory_after_relogin["EquippedItem"] != "iron_sword"
                ):
                    log(
                        "  Client_QueryInventory after relogin returned unexpected payload: "
                        f"{query_inventory_after_relogin}"
                    )
                    return False
                log(f"  Client_QueryInventory after relogin OK: {query_inventory_after_relogin}")

            if 26 in enabled_tests:
                log("Test 26: Client_QueryProgression after relogin...")
                query_progression_after_relogin = parse_query_progression_response(
                    call_client_function(sock, "Client_QueryProgression", struct.pack("<Q", player_id))
                )
                if (
                    not query_progression_after_relogin["bSuccess"]
                    or query_progression_after_relogin["PlayerId"] != player_id
                    or query_progression_after_relogin["Level"] != 3
                    or query_progression_after_relogin["Experience"] != 250
                    or query_progression_after_relogin["Health"] != 75
                ):
                    log(
                        "  Client_QueryProgression after relogin returned unexpected payload: "
                        f"{query_progression_after_relogin}"
                    )
                    return False
                log(f"  Client_QueryProgression after relogin OK: {query_progression_after_relogin}")

            if 33 in enabled_tests:
                log("Test 33: Client_QueryCombatProfile after relogin...")
                query_combat_profile_after_relogin = parse_query_combat_profile_response(
                    call_client_function(sock, "Client_QueryCombatProfile", struct.pack("<Q", player_id))
                )
                if (
                    not query_combat_profile_after_relogin["bSuccess"]
                    or query_combat_profile_after_relogin["PlayerId"] != player_id
                    or query_combat_profile_after_relogin["BaseAttack"] != 10
                    or query_combat_profile_after_relogin["BaseDefense"] != 5
                    or query_combat_profile_after_relogin["MaxHealth"] != 100
                    or query_combat_profile_after_relogin["PrimarySkillId"] != 2002
                    or query_combat_profile_after_relogin["LastResolvedSceneId"] != 0
                    or query_combat_profile_after_relogin["LastResolvedHealth"] != 100
                ):
                    log(
                        "  Client_QueryCombatProfile after relogin returned unexpected payload: "
                        f"{query_combat_profile_after_relogin}"
                    )
                    return False
                log(f"  Client_QueryCombatProfile after relogin OK: {query_combat_profile_after_relogin}")

            if 27 in enabled_tests:
                log("Test 27: forwarded Client_FindPlayer invalid payload...")
                invalid_payload_response = try_find_player(sock, struct.pack("<I", player_id))
                if invalid_payload_response is None:
                    log("  invalid payload test failed: no response")
                    return False
                if invalid_payload_response["Error"] != "client_call_param_binding_failed":
                    log(
                        "  invalid payload returned unexpected error: "
                        f"{invalid_payload_response['Error']}"
                    )
                    return False
                log("  invalid payload OK: binder error surfaced through Gateway")

            if 28 in enabled_tests:
                log("Test 28: forwarded Client_FindPlayer validation error...")
                zero_player_response = try_find_player(sock, struct.pack("<Q", 0))
                if zero_player_response is None:
                    log("  zero player id test failed: no response")
                    return False
                if zero_player_response["Error"] != "player_id_required":
                    log(
                        "  zero player id returned unexpected error: "
                        f"{zero_player_response['Error']}"
                    )
                    return False
                log("  validation error OK: world business error reached client")

            if 29 in enabled_tests:
                log("Test 29: World unavailable after forward route...")
                world_proc = proc_by_name.get("WorldServer")
                if world_proc is None:
                    log("  WorldServer process handle missing")
                    return False
                try:
                    world_proc.terminate()
                    world_proc.wait(timeout=5)
                except Exception:
                    try:
                        world_proc.kill()
                    except Exception:
                        pass
                backend_unavailable_response = wait_until(
                    time.time() + 5.0,
                    lambda: try_find_player(sock, struct.pack("<Q", player_id), timeout=1.0),
                )
                if backend_unavailable_response is None:
                    log("  backend unavailable test failed: no response")
                    return False
                if backend_unavailable_response["Error"] not in {
                    "client_route_backend_unavailable",
                    "server_call_disconnected",
                    "server_call_send_failed",
                }:
                    log(
                        "  backend unavailable returned unexpected error: "
                        f"{backend_unavailable_response['Error']}"
                    )
                    return False
                log("  backend unavailable OK: Gateway returned reflected route error")

            log("Validation PASSED")
            return True
        finally:
            try:
                sock.close()
            except OSError:
                pass
            try:
                sock2.close()
            except OSError:
                pass
    finally:
        cleanup()


def main() -> int:
    parser = argparse.ArgumentParser(description="Mession skeleton validation")
    parser.add_argument("--build-dir", default="Build", help="Build directory (default: Build)")
    add_build_system_arguments(parser)
    parser.add_argument("--no-build", action="store_true", help="Skip build step")
    parser.add_argument("--list-suites", action="store_true", help="List available validation suites and exit")
    parser.add_argument(
        "--suite",
        action="append",
        default=[],
        help=(
            "Validation suite to run. Can be passed multiple times or as comma-separated names. "
            f"Available: {', '.join(sorted(SUITE_TESTS.keys()))}"
        ),
    )
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
    if args.list_suites:
        for suite_name in sorted(SUITE_TESTS.keys()):
            print(suite_name)
        return 0

    try:
        suite_names = parse_suite_names(args.suite)
        enabled_tests = resolve_enabled_tests(suite_names)
    except ValueError as exc:
        log(str(exc))
        return 1

    log(f"Selected suites: {', '.join(suite_names)}")
    log(f"Enabled tests: {', '.join(str(test_id) for test_id in sorted(enabled_tests))}")

    project_root = Path(__file__).resolve().parent.parent
    build_dir = (project_root / args.build_dir).resolve()
    log_dir = build_dir / "validate_logs"

    if not build_dir.exists() and not args.no_build:
        build_dir.mkdir(parents=True, exist_ok=True)

    if not args.no_build and not build_project(
        build_dir=build_dir,
        build_system_name=args.build_system,
        build_system_config=args.build_system_config,
    ):
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
        enabled_tests=enabled_tests,
    )
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
