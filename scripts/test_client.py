#!/usr/bin/env python3
"""
Mession 简易测试客户端

用法:
  python3 scripts/test_client.py [--host 127.0.0.1] [--port 8001] [--player-id 12345]

交互命令:
  login     - 登录
  move x y z - 发送移动 (如 move 1 2 3)
  quit      - 退出
"""

import argparse
import struct
import socket
import sys
from pathlib import Path

MT_LOGIN = 1
MT_LOGIN_RESPONSE = 2
MT_PLAYER_MOVE = 5


def send_packet(sock: socket.socket, payload: bytes) -> None:
    length = len(payload)
    packet = struct.pack("<I", length) + payload
    sock.sendall(packet)


def recv_packet(sock: socket.socket) -> tuple[int, bytes] | None:
    header = sock.recv(4)
    if len(header) < 4:
        return None
    length = struct.unpack("<I", header)[0]
    if length > 65535:
        return None
    body = b""
    while len(body) < length:
        chunk = sock.recv(length - len(body))
        if not chunk:
            return None
        body += chunk
    return (body[0], body[1:]) if body else None


def cmd_login(sock: socket.socket, player_id: int) -> bool:
    payload = struct.pack("<BQ", MT_LOGIN, player_id)
    send_packet(sock, payload)
    result = recv_packet(sock)
    if not result:
        print("Login failed: no response")
        return False
    msg_type, body = result
    if msg_type != MT_LOGIN_RESPONSE:
        print(f"Login failed: unexpected type {msg_type}")
        return False
    if len(body) < 12:
        print("Login failed: invalid response")
        return False
    session_key = struct.unpack("<I", body[0:4])[0]
    resp_player_id = struct.unpack("<Q", body[4:12])[0]
    print(f"Login OK: SessionKey={session_key}, PlayerId={resp_player_id}")
    return True


def cmd_move(sock: socket.socket, x: float, y: float, z: float) -> bool:
    payload = struct.pack("<Bfff", MT_PLAYER_MOVE, x, y, z)
    send_packet(sock, payload)
    print(f"Move sent: ({x}, {y}, {z})")
    return True


def main() -> int:
    parser = argparse.ArgumentParser(description="Mession test client")
    parser.add_argument("--host", default="127.0.0.1", help="Gateway host")
    parser.add_argument("--port", type=int, default=8001, help="Gateway port")
    parser.add_argument("--player-id", type=int, default=12345, help="Player ID for login")
    parser.add_argument("--no-interactive", action="store_true", help="Login and move once, then exit")
    args = parser.parse_args()

    print(f"Connecting to {args.host}:{args.port}...")
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.settimeout(10.0)
    try:
        sock.connect((args.host, args.port))
    except (socket.error, OSError) as e:
        print(f"Connect failed: {e}")
        return 1

    print("Connected. Commands: login, move x y z, quit")

    if args.no_interactive:
        if not cmd_login(sock, args.player_id):
            return 1
        cmd_move(sock, 1.0, 2.0, 3.0)
        sock.close()
        return 0

    while True:
        try:
            line = input("> ").strip()
        except EOFError:
            break

        if not line:
            continue
        parts = line.split()
        cmd = parts[0].lower()

        if cmd == "quit" or cmd == "exit":
            break
        if cmd == "login":
            if not cmd_login(sock, args.player_id):
                print("Login failed")
            continue
        if cmd == "move":
            if len(parts) < 4:
                print("Usage: move x y z")
                continue
            try:
                x, y, z = float(parts[1]), float(parts[2]), float(parts[3])
                cmd_move(sock, x, y, z)
            except ValueError:
                print("Invalid coordinates")
            continue
        print(f"Unknown command: {cmd}")

    sock.close()
    print("Bye")
    return 0


if __name__ == "__main__":
    sys.exit(main())
