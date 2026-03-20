#!/usr/bin/env python3
"""
ServerMessages 协议组包/解包小验证（与 Common/Net/ServerMessages.h + MessageUtils.h 约定一致）。

约定：
- 本脚本覆盖的首批跨服消息整数载荷使用网络字节序（大端）
- 客户端协议与未迁移消息仍保持现状，不在此脚本中验证
- 字符串为 uint16 长度 + 裸字节（无结尾 \\0）
不依赖 C++ 或运行中服务，仅校验 Python 侧与文档约定一致的编解码回合。
"""

import struct
import sys
from typing import Any

def write_u8(b: list[int], v: int) -> None:
    b.append(v & 0xFF)

def write_u16_le(b: list[int], v: int) -> None:
    b.extend(struct.pack("<H", v & 0xFFFF))

def write_u32_le(b: list[int], v: int) -> None:
    b.extend(struct.pack("<I", v & 0xFFFFFFFF))

def write_u64_le(b: list[int], v: int) -> None:
    b.extend(struct.pack("<Q", v & 0xFFFFFFFFFFFFFFFF))

def write_f32_le(b: list[int], v: float) -> None:
    b.extend(struct.pack("<f", v))

def write_u16_be(b: list[int], v: int) -> None:
    b.extend(struct.pack(">H", v & 0xFFFF))

def write_u32_be(b: list[int], v: int) -> None:
    b.extend(struct.pack(">I", v & 0xFFFFFFFF))

def write_u64_be(b: list[int], v: int) -> None:
    b.extend(struct.pack(">Q", v & 0xFFFFFFFFFFFFFFFF))

def write_string(b: list[int], s: str) -> None:
    raw = s.encode("utf-8")
    write_u16_le(b, len(raw))
    b.extend(raw)

def read_u8(data: bytes, offset: int) -> tuple[int, int]:
    if offset + 1 > len(data):
        raise ValueError("read u8 past end")
    return data[offset], offset + 1

def read_u16_le(data: bytes, offset: int) -> tuple[int, int]:
    if offset + 2 > len(data):
        raise ValueError("read u16 past end")
    return struct.unpack_from("<H", data, offset)[0], offset + 2

def read_u32_le(data: bytes, offset: int) -> tuple[int, int]:
    if offset + 4 > len(data):
        raise ValueError("read u32 past end")
    return struct.unpack_from("<I", data, offset)[0], offset + 4

def read_u64_le(data: bytes, offset: int) -> tuple[int, int]:
    if offset + 8 > len(data):
        raise ValueError("read u64 past end")
    return struct.unpack_from("<Q", data, offset)[0], offset + 8

def read_f32_le(data: bytes, offset: int) -> tuple[float, int]:
    if offset + 4 > len(data):
        raise ValueError("read f32 past end")
    return struct.unpack_from("<f", data, offset)[0], offset + 4

def read_u16_be(data: bytes, offset: int) -> tuple[int, int]:
    if offset + 2 > len(data):
        raise ValueError("read u16 past end")
    return struct.unpack_from(">H", data, offset)[0], offset + 2

def read_u32_be(data: bytes, offset: int) -> tuple[int, int]:
    if offset + 4 > len(data):
        raise ValueError("read u32 past end")
    return struct.unpack_from(">I", data, offset)[0], offset + 4

def read_u64_be(data: bytes, offset: int) -> tuple[int, int]:
    if offset + 8 > len(data):
        raise ValueError("read u64 past end")
    return struct.unpack_from(">Q", data, offset)[0], offset + 8

def read_string(data: bytes, offset: int) -> tuple[str, int]:
    n, offset = read_u16_le(data, offset)
    if offset + n > len(data):
        raise ValueError("read string past end")
    s = data[offset : offset + n].decode("utf-8")
    return s, offset + n


# --- SPlayerLoginResponseMessage: ConnectionId(u64), PlayerId(u64), SessionKey(u32) [BE]
def serialize_player_login_response(conn_id: int, player_id: int, session_key: int) -> bytes:
    b: list[int] = []
    write_u64_be(b, conn_id)
    write_u64_be(b, player_id)
    write_u32_be(b, session_key)
    return bytes(b)

def deserialize_player_login_response(data: bytes) -> tuple[int, int, int]:
    o = 0
    conn_id, o = read_u64_be(data, o)
    player_id, o = read_u64_be(data, o)
    session_key, o = read_u32_be(data, o)
    if o != len(data):
        raise ValueError("trailing bytes")
    return conn_id, player_id, session_key


# --- SSessionValidateRequestMessage: ConnectionId(u64), PlayerId(u64), SessionKey(u32) [BE]
def serialize_session_validate_request(conn_id: int, player_id: int, session_key: int) -> bytes:
    b: list[int] = []
    write_u64_be(b, conn_id)
    write_u64_be(b, player_id)
    write_u32_be(b, session_key)
    return bytes(b)

def deserialize_session_validate_request(data: bytes) -> tuple[int, int, int]:
    o = 0
    conn_id, o = read_u64_be(data, o)
    player_id, o = read_u64_be(data, o)
    session_key, o = read_u32_be(data, o)
    if o != len(data):
        raise ValueError("trailing bytes")
    return conn_id, player_id, session_key


# --- SPlayerClientSyncMessage: PlayerId(u64), DataSize(u32), Data(bytes) [BE header]
def serialize_player_client_sync(player_id: int, payload: bytes) -> bytes:
    b: list[int] = []
    write_u64_be(b, player_id)
    write_u32_be(b, len(payload))
    b.extend(payload)
    return bytes(b)

def deserialize_player_client_sync(data: bytes) -> tuple[int, bytes]:
    o = 0
    player_id, o = read_u64_be(data, o)
    size, o = read_u32_be(data, o)
    if o + size > len(data):
        raise ValueError("payload past end")
    payload = data[o : o + size]
    o += size
    if o != len(data):
        raise ValueError("trailing bytes")
    return player_id, payload


# --- SPlayerLogoutMessage: PlayerId(u64) [BE]
def serialize_player_logout(player_id: int) -> bytes:
    b: list[int] = []
    write_u64_be(b, player_id)
    return bytes(b)

def deserialize_player_logout(data: bytes) -> int:
    o = 0
    player_id, o = read_u64_be(data, o)
    if o != len(data):
        raise ValueError("trailing bytes")
    return player_id


def main() -> int:
    failed = 0

    # Round-trip: SPlayerLoginResponseMessage
    c1, p1, s1 = 10001, 20002, 0x12345678
    blob = serialize_player_login_response(c1, p1, s1)
    c2, p2, s2 = deserialize_player_login_response(blob)
    if (c2, p2, s2) != (c1, p1, s1):
        print("FAIL SPlayerLoginResponseMessage round-trip")
        failed += 1
    else:
        print("OK  SPlayerLoginResponseMessage round-trip")

    # Round-trip: SSessionValidateRequestMessage
    blob = serialize_session_validate_request(1, 10001, 999)
    a, b, c = deserialize_session_validate_request(blob)
    if (a, b, c) != (1, 10001, 999):
        print("FAIL SSessionValidateRequestMessage round-trip")
        failed += 1
    else:
        print("OK  SSessionValidateRequestMessage round-trip")

    # Round-trip: SPlayerClientSyncMessage
    payload = bytes([6, 0, 0, 0, 0, 0, 0, 0, 0, 13, 0, 0, 0])  # 示例 PlayerClientSync payload
    blob = serialize_player_client_sync(10001, payload)
    pid, out_payload = deserialize_player_client_sync(blob)
    if pid != 10001 or out_payload != payload:
        print("FAIL SPlayerClientSyncMessage round-trip")
        failed += 1
    else:
        print("OK  SPlayerClientSyncMessage round-trip")

    # Round-trip: SPlayerLogoutMessage
    blob = serialize_player_logout(10002)
    if deserialize_player_logout(blob) != 10002:
        print("FAIL SPlayerLogoutMessage round-trip")
        failed += 1
    else:
        print("OK  SPlayerLogoutMessage round-trip")

    # Fixed blob: SPlayerLoginResponse 已知字节（大端）
    # ConnectionId=1, PlayerId=10001, SessionKey=0x01020304
    fixed = bytes([
        0, 0, 0, 0, 0, 0, 0, 1,
        0, 0, 0, 0, 0, 0, 0x27, 0x11,
        1, 2, 3, 4,
    ])
    fc, fp, fs = deserialize_player_login_response(fixed)
    if (fc, fp, fs) != (1, 10001, 0x01020304):
        print("FAIL SPlayerLoginResponse fixed blob")
        failed += 1
    else:
        print("OK  SPlayerLoginResponse fixed blob")

    if failed:
        print(f"\n{failed} test(s) failed")
        return 1
    print("\nAll protocol checks passed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
