from __future__ import annotations

import struct
from typing import Optional

MT_FUNCTION_CALL = 13


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


def build_client_call_packet(function_id: int, call_id: int, payload: bytes) -> bytes:
    body = struct.pack("<BHQI", MT_FUNCTION_CALL, function_id, call_id, len(payload))
    body += payload
    return struct.pack("<I", len(body)) + body


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


def pack_combat_unit_ref(unit_kind: int, combat_entity_id: int, player_id: int) -> bytes:
    return struct.pack("<B7xQQ", unit_kind, combat_entity_id, player_id)


def pack_string(value: str) -> bytes:
    encoded = value.encode("utf-8")
    return struct.pack("<I", len(encoded)) + encoded
