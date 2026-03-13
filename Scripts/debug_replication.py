#!/usr/bin/env python3
"""最小复现：连 Gateway，登录一次，然后循环收包并打印前几字节。需先手动启动各服。"""
import socket
import struct
import sys
import time

GATEWAY_PORT = 8001
MT_LOGIN = 1
MT_LOGIN_RESPONSE = 2

def main():
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.settimeout(15.0)
    try:
        sock.connect(("127.0.0.1", GATEWAY_PORT))
    except Exception as e:
        print("Connect failed:", e, file=sys.stderr)
        return 1

    # 登录
    payload = struct.pack("<BQ", MT_LOGIN, 10001)
    packet = struct.pack("<I", len(payload)) + payload
    sock.sendall(packet)

    header = sock.recv(4)
    if len(header) < 4:
        print("No login response", file=sys.stderr)
        return 1
    resp_len = struct.unpack("<I", header)[0]
    body = b""
    while len(body) < resp_len:
        body += sock.recv(resp_len - len(body))
    if body[0] != MT_LOGIN_RESPONSE:
        print("Unexpected login response type", body[0], file=sys.stderr)
        return 1
    print("Login OK, player 10001")

    # 收包 5 秒，打印每个包的类型与前 20 字节
    sock.settimeout(0.5)
    deadline = time.time() + 5.0
    count = 0
    while time.time() < deadline:
        try:
            header = sock.recv(4)
            if len(header) < 4:
                continue
            length = struct.unpack("<I", header)[0]
            if length > 0 and length < 1000000:
                body = b""
                while len(body) < length:
                    body += sock.recv(length - len(body))
                count += 1
                first = body[0] if body else 0
                preview = body[:20].hex() if len(body) <= 20 else body[:20].hex() + "..."
                print(f"  packet #{count} type={first} len={length} body_hex={preview}")
        except socket.timeout:
            continue
        except Exception as e:
            print("Recv error:", e, file=sys.stderr)
            break

    print(f"Total packets after login: {count}")
    sock.close()
    return 0

if __name__ == "__main__":
    sys.exit(main())
