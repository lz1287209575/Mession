from __future__ import annotations

import socket
import struct
import time
from typing import Optional

from .packets import MT_FUNCTION_CALL, build_client_call_packet, compute_stable_client_function_id, compute_stable_downlink_function_id, decode_client_call_packet, decode_client_function_packet

_NEXT_CALL_ID = 1


def next_call_id() -> int:
    global _NEXT_CALL_ID
    call_id = _NEXT_CALL_ID
    _NEXT_CALL_ID += 1
    return call_id


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


def send_client_call(sock: socket.socket, function_name: str, payload: bytes) -> tuple[int, int]:
    function_id = compute_stable_client_function_id(function_name)
    call_id = next_call_id()
    sock.sendall(build_client_call_packet(function_id, call_id, payload))
    return function_id, call_id


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
