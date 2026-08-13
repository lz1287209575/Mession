#!/usr/bin/env python3
"""
Mession PoC 端到端验证（MServiceRegistry + Gateway + 同质多进程 EchoService）

链路：
  - 链路 1 (chain_local):   Client → Gateway → EchoService_A(7001) → 本机 Actor 1001
  - 链路 2 (chain_remote):  Client → Gateway → EchoService_A → EchoService_B(7002) → Actor 2001
  - 链路 3 (error_unknown): Client → Gateway → EchoService_A → 不存在 ActorId

启动顺序由 Scripts/servers.py 控制；本脚本负责：
  1. 启动 4 个进程（Registry + EchoA + EchoB + Gateway）
  2. 发 MT_FunctionCall(13) 给 Gateway:8001
  3. 解析 FSampleEchoResponse 回包
  4. 按 enabled tests 决定跑哪些链路
"""

import argparse
import os
import socket
import struct
import subprocess
import sys
import time
from pathlib import Path
from typing import Optional, Sequence, Set

from build_systems import run_build


PROJECT_ROOT = Path(__file__).resolve().parent.parent
DEFAULT_BUILD_DIR = PROJECT_ROOT / "Build"
BIN_DIR = PROJECT_ROOT / "Bin"
VALIDATE_LOG_DIR = Path("Logs") / "validate"

GATEWAY_PORT = 8001
ECHO_A_PORT = 7001
ECHO_B_PORT = 7002
REGISTRY_PORT = 18000


def log(msg: str) -> None:
    print(f"[validate] {msg}", flush=True)


def get_executable_path(name: str) -> Optional[Path]:
    for suffix in ("", ".exe"):
        candidate = BIN_DIR / (name + suffix)
        if candidate.exists():
            return candidate
    return None


def wait_for_port(host: str, port: int, timeout: float) -> bool:
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
                sock.settimeout(0.5)
                sock.connect((host, port))
                return True
        except OSError:
            time.sleep(0.1)
    return False


def kill_poc_processes() -> None:
    """按 binary 名清理可能残留的 GatewayServer / EchoService / MServiceRegistry 进程。"""
    for name in ("GatewayServer", "EchoService", "MServiceRegistry"):
        try:
            subprocess.run(
                ["pkill", "-f", f"/{name}"],
                check=False,
                capture_output=True,
                timeout=3,
            )
        except (subprocess.SubprocessError, FileNotFoundError):
            pass

    if sys.platform != "win32":
        for candidate in ("/usr/bin/fuser", "/bin/fuser"):
            if Path(candidate).exists():
                fuser = candidate
                break
        else:
            fuser = None
        if fuser:
            for port in (GATEWAY_PORT, ECHO_A_PORT, ECHO_B_PORT, REGISTRY_PORT):
                try:
                    subprocess.run(
                        [fuser, "-k", f"{port}/tcp"],
                        check=False,
                        capture_output=True,
                        timeout=3,
                    )
                except (subprocess.SubprocessError, FileNotFoundError):
                    pass

    time.sleep(0.5)


# ===== 协议编解码（与 Common/Net/Rpc/RpcTransport.cpp 一致） =====

# step-1: Client↔Gateway envelope 改成无 MessageType 字节:
#   wire = [Length:4B][RequestId:8B][FunctionId:2B][PayloadSize:4B][Payload:N]
# 这一步仅删除 `MT_FUNCTION_CALL = 13` 那一字节,客户端 stub 跟 Gateway
# 现在的 BuildClientCallPacket / ParseClientCallPacket 暂时仍兼容,直到
# step-2 把 Gateway 切到 BuildClientEnvelopePacket 才彻底丢掉 1 字节头。
# 现在 validate.py 客户端发出的包仍带 1 字节头(暂态),Gateway 用旧路径
# 解包,链路保持通。
MT_FUNCTION_CALL = 13

REQUEST_ID_DEFAULT = 1  # UE 实际运行时会用单调递增 id;validate.py 一次性用 1


def compute_stable_id(scope: str, member: str) -> int:
    # 与 Common/Runtime/Reflect/Class.h:43 ComputeStableReflectId 一致
    OFFSET_BASIS = 2166136261
    PRIME = 16777619

    def mix_to(h: int, text: str) -> int:
        for ch in text.encode("utf-8"):
            h ^= ch
            h = (h * PRIME) & 0xFFFFFFFF
        return h

    h = OFFSET_BASIS
    h = mix_to(h, scope)
    h = mix_to(h, "::")
    h = mix_to(h, member)
    folded = ((h >> 16) ^ (h & 0xFFFF)) & 0xFFFF
    return folded if folded != 0 else 1


ECHO_FUNCTION_ID = compute_stable_id("MEchoService", "Echo")
ECHOAWAIT_FUNCTION_ID = compute_stable_id("MEchoService", "EchoAwait")


def pack_string(value: str) -> bytes:
    encoded = value.encode("utf-8")
    return struct.pack("<I", len(encoded)) + encoded


def build_client_call_packet(function_id: int, call_id: int, payload: bytes) -> bytes:
    # step-2 新格式（Gateway ParseClientEnvelopePacket）:
    #   [RequestId:8][FunctionId:2][PayloadSize:4][Payload]（无 MessageType 字节）
    return build_client_envelope_packet(function_id, call_id, payload)


# step-1 引入:新 envelope builder / parser。先在 validate.py 这边准备好,
# step-2 把 Gateway 切到 ParseClientEnvelopePacket 后开始用。
def build_client_envelope_packet(function_id: int, request_id: int, payload: bytes) -> bytes:
    body = struct.pack("<Q", request_id)
    body += struct.pack("<H", function_id)
    body += struct.pack("<I", len(payload))
    body += payload
    return struct.pack("<I", len(body)) + body


def parse_client_envelope(payload: bytes) -> Optional[dict]:
    #   [RequestId:8B][FunctionId:2B][PayloadSize:4B][Payload:N]
    if len(payload) < 8 + 2 + 4:
        return None
    request_id, function_id, payload_size = struct.unpack_from("<QHI", payload, 0)
    header_size = 8 + 2 + 4
    if len(payload) < header_size + payload_size:
        return None
    return {
        "request_id": request_id,
        "function_id": function_id,
        "payload": payload[header_size:header_size + payload_size],
    }


def recv_exact(sock: socket.socket, size: int) -> Optional[bytes]:
    if size <= 0:
        return b""
    buf = bytearray()
    while len(buf) < size:
        try:
            chunk = sock.recv(size - len(buf))
        except (socket.timeout, OSError):
            return None
        if not chunk:
            return None
        buf.extend(chunk)
    return bytes(buf)


def recv_one_packet(sock: socket.socket, timeout: float) -> Optional[bytes]:
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
    return body


def parse_client_response(payload: bytes) -> Optional[dict]:
    # step-2 新格式（Gateway BuildClientEnvelopePacket）:
    #   [RequestId:8][FunctionId:2][PayloadSize:4][Payload]
    if len(payload) < 8 + 2 + 4:
        return None
    request_id, function_id, payload_size = struct.unpack_from("<QHI", payload, 0)
    header_size = 8 + 2 + 4
    if len(payload) < header_size + payload_size:
        return None
    response_body = payload[header_size:header_size + payload_size]
    return {
        "request_id": request_id,
        "function_id": function_id,
        "payload": response_body,
    }


def parse_echo_response(payload: bytes) -> Optional[dict]:
    if len(payload) < 4:
        return None
    offset = 0
    echo_len = struct.unpack_from("<I", payload, offset)[0]
    offset += 4
    if len(payload) < offset + echo_len:
        return None
    echo = payload[offset:offset + echo_len].decode("utf-8", errors="replace")
    offset += echo_len
    if len(payload) < offset + 8:
        return None
    source_actor_id = struct.unpack_from("<Q", payload, offset)[0]
    offset += 8
    if len(payload) < offset + 4:
        return None
    name_len = struct.unpack_from("<I", payload, offset)[0]
    offset += 4
    if len(payload) < offset + name_len:
        return None
    source_server_name = payload[offset:offset + name_len].decode("utf-8", errors="replace")
    return {
        "echo": echo,
        "source_actor_id": source_actor_id,
        "source_server_name": source_server_name,
    }


def parse_fapp_error(payload: bytes) -> Optional[dict]:
    if len(payload) < 4:
        return None
    code_len = struct.unpack_from("<I", payload, 0)[0]
    offset = 4
    if len(payload) < offset + code_len:
        return None
    code = payload[offset:offset + code_len].decode("utf-8", errors="replace")
    offset += code_len
    if len(payload) < offset + 4:
        return {"code": code, "message": ""}
    msg_len = struct.unpack_from("<I", payload, offset)[0]
    offset += 4
    if len(payload) < offset + msg_len:
        return {"code": code, "message": ""}
    message = payload[offset:offset + msg_len].decode("utf-8", errors="replace")
    return {"code": code, "message": message}


def make_echo_request(target_actor_id: int, message: str) -> bytes:
    # 反射字段注册顺序（FSampleEchoMessages.h）：Message 先、TargetActorId 后——
    # MReflectArchive 按注册顺序读写，payload 必须同序。
    return pack_string(message) + struct.pack("<Q", target_actor_id)


# ActorId 编码：[ServiceId: high 32][InstId: low 32]；与 ServiceId.h::MServiceId::Make 一致。
def make_actor_id(server_type_name: str, inst_id: int) -> int:
    type_map = {
        "Gateway": 1,
        "Login": 2,
        "World": 3,
        "Scene": 4,
        "Router": 5,
        "Mgo": 6,
        "Echo": 7,
    }
    service_id = type_map.get(server_type_name, 0)
    return (service_id << 32) | (inst_id & 0xFFFFFFFF)


def call_echo(sock: socket.socket, function_id: int, call_id: int, payload: bytes, timeout: float = 5.0) -> dict:
    sock.sendall(build_client_call_packet(function_id, call_id, payload))
    deadline = time.time() + timeout
    while time.time() < deadline:
        remaining = max(0.1, deadline - time.time())
        body = recv_one_packet(sock, timeout=min(remaining, 1.0))
        if body is None:
            continue
        if not body:
            continue
        response = parse_client_response(body)
        if response is None:
            continue
        # PushClientDownlink uses BuildClientFunctionPacket (no CallId echoed back).
        # Match only on FunctionId; the downlink frames any echo response.
        if response["function_id"] != function_id:
            continue
        # b_success：成功响应 = payload 能解出 FSampleEchoResponse；
        # FAppError 错误响应解不出（parse_fapp_error 对 FSampleEchoResponse 会
        # 贪心误判——首 4 字节 Message size 会被当成 FAppError code_len）。
        response["b_success"] = parse_echo_response(response["payload"]) is not None
        return response
    raise TimeoutError(f"timeout waiting for response to function_id={function_id}")


# ===== Test cases =====


def run_test_chain_local(sock: socket.socket) -> bool:
    log("Test 1 (chain_local): Client -> Gateway -> EchoService_A -> Actor 1001")
    request = make_echo_request(target_actor_id=make_actor_id("Echo", 1001), message="hello local")
    response = call_echo(sock, ECHO_FUNCTION_ID, call_id=1, payload=request)
    if not response["b_success"]:
        log(f"  FAIL: response not successful: {response}")
        return False
    echo = parse_echo_response(response["payload"])
    if echo is None:
        log("  FAIL: payload not parseable as FSampleEchoResponse")
        return False
    if echo["echo"] != "hello local [echoed]":
        log(f"  FAIL: echo mismatch: {echo}")
        return False
    if echo["source_server_name"] != "MEchoService":
        log(f"  FAIL: source server mismatch: {echo}")
        return False
    log(f"  OK: {echo}")
    return True


def run_test_chain_remote(sock: socket.socket) -> bool:
    log("Test 2 (chain_remote): Client -> Gateway -> EchoService_A -> EchoService_B -> Actor 2001")
    request = make_echo_request(target_actor_id=make_actor_id("Echo", 2001), message="hello remote")
    response = call_echo(sock, ECHO_FUNCTION_ID, call_id=2, payload=request)
    if not response["b_success"]:
        log(f"  FAIL: response not successful: {response}")
        return False
    echo = parse_echo_response(response["payload"])
    if echo is None:
        log("  FAIL: payload not parseable as FSampleEchoResponse")
        return False
    if echo["echo"] != "hello remote [echoed]":
        log(f"  FAIL: echo mismatch: {echo}")
        return False
    if echo["source_server_name"] != "MEchoService":
        log(f"  FAIL: source server mismatch: {echo}")
        return False
    log(f"  OK: {echo}")
    return True


def run_test_chain_remote_async(sock: socket.socket) -> bool:
    """P2: same chain as chain_remote but goes through EchoAwait (the
    Frame-based async handler). Exercises AWAIT_OK(CallToActor) over the wire.

    Note: EchoAwait forwards to Echo (the same target Actor + class+method),
    then chains the response back to the client through the same wire envelope.
    """
    log("Test 2b (chain_remote_async): Client -> Gateway -> EchoService_A.EchoAwait -> EchoService_B.Echo -> Actor 2001")
    request = make_echo_request(target_actor_id=make_actor_id("Echo", 2001), message="hello remote async")
    response = call_echo(sock, ECHOAWAIT_FUNCTION_ID, call_id=2, payload=request)
    if not response["b_success"]:
        log(f"  FAIL: response not successful: {response}")
        return False
    echo = parse_echo_response(response["payload"])
    if echo is None:
        log("  FAIL: payload not parseable as FSampleEchoResponse")
        return False
    if echo["echo"] != "hello remote async [echoed]":
        log(f"  FAIL: echo mismatch: {echo}")
        return False
    if echo["source_server_name"] != "MEchoService":
        log(f"  FAIL: source server mismatch: {echo}")
        return False
    log(f"  OK: {echo}")
    return True


def run_test_error_unknown(sock: socket.socket) -> bool:
    log("Test 3 (error_unknown): Client -> Gateway -> EchoService -> Actor 9999 (not registered)")
    request = make_echo_request(target_actor_id=9999, message="hello ghost")
    response = call_echo(sock, ECHO_FUNCTION_ID, call_id=3, payload=request)
    if response["b_success"]:
        log("  FAIL: expected b_success=False, got success")
        return False
    err = parse_fapp_error(response["payload"])
    if err is None:
        log(f"  FAIL: error payload not parseable: {response}")
        return False
    log(f"  OK: error code={err['code']}, message={err['message']}")
    return True


# ===== Process orchestration =====


def start_process(exe: Path, argv: Sequence[str], log_dir: Path) -> subprocess.Popen:
    log_dir.mkdir(parents=True, exist_ok=True)
    port = argv[0].split("=")[-1]
    log_path = log_dir / f"{exe.name}_{port}.log"
    handle = open(log_path, "w", encoding="utf-8")
    return subprocess.Popen(
        [str(exe), *argv],
        cwd=str(PROJECT_ROOT),
        stdout=handle,
        stderr=handle,
        start_new_session=True,
    )


def run_validation(
    build_dir: Path,
    timeout: float,
    enabled_tests: Set[int],
    log_dir: Path,
) -> bool:
    log_dir.mkdir(parents=True, exist_ok=True)
    log("prepare: clean residual processes")
    kill_poc_processes()

    procs: list[subprocess.Popen] = []

    def spawn(name: str, port: int, argv: Sequence[str]) -> bool:
        exe = get_executable_path(name)
        if exe is None:
            log(f"  missing executable: {name} (run `cmake --build {build_dir}` first)")
            return False
        proc = start_process(exe, argv, log_dir)
        procs.append(proc)
        if not wait_for_port("127.0.0.1", port, timeout=timeout):
            log(f"  {name} on port {port} did not bind within {timeout}s")
            return False
        log(f"  started {name} on port {port} (pid={proc.pid})")
        return True

    try:
        # 起 MServiceRegistry 在最前——Echo/Gateway 都依赖 Registry 否则起不来。
        if not spawn(
            "MServiceRegistry",
            REGISTRY_PORT,
            [
                f"--listen={REGISTRY_PORT}",
            ],
        ):
            return False
        if not spawn(
            "EchoService",
            ECHO_A_PORT,
            [
                f"--listen={ECHO_A_PORT}",
                "--server-id=2",
                "--inst=1",
                "--actors=1001,1002",
                "--service=MEchoService",
                f"--registry=127.0.0.1:{REGISTRY_PORT}",
            ],
        ):
            return False
        if not spawn(
            "EchoService",
            ECHO_B_PORT,
            [
                f"--listen={ECHO_B_PORT}",
                "--server-id=3",
                "--inst=2",
                "--actors=2001,2002",
                "--service=MEchoService",
                f"--registry=127.0.0.1:{REGISTRY_PORT}",
            ],
        ):
            return False
        # 给两个 EchoService 一小段时间注册到 Registry + 推 EndpointChange。
        time.sleep(1.0)
        if not spawn(
            "GatewayServer",
            GATEWAY_PORT,
            [
                f"--listen={GATEWAY_PORT}",
                f"--registry=127.0.0.1:{REGISTRY_PORT}",
            ],
        ):
            return False

        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
            sock.settimeout(5.0)
            sock.connect(("127.0.0.1", GATEWAY_PORT))

            ok = True
            if 1 in enabled_tests and not run_test_chain_local(sock):
                ok = False
            if ok and 2 in enabled_tests and not run_test_chain_remote(sock):
                ok = False
            if ok and 4 in enabled_tests and not run_test_chain_remote_async(sock):
                ok = False
            if ok and 3 in enabled_tests and not run_test_error_unknown(sock):
                ok = False

            return ok
    finally:
        for proc in reversed(procs):
            try:
                proc.terminate()
                proc.wait(timeout=3)
            except Exception:
                try:
                    proc.kill()
                except Exception:
                    pass


# ===== CLI =====


def parse_args(argv: Optional[Sequence[str]] = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Mession PoC validation (Gateway + EchoService)")
    parser.add_argument("--build-dir", default="Build", help="Build directory (default: Build)")
    parser.add_argument("--no-build", action="store_true", help="Skip build step")
    parser.add_argument("--list-suites", action="store_true", help="List available validation suites and exit")
    parser.add_argument(
        "--suite",
        action="append",
        default=[],
        help=(
            "Validation suite to run. Can be passed multiple times or as comma-separated names. "
            "Available: all, chain_local, chain_remote, error_unknown"
        ),
    )
    parser.add_argument("--timeout", type=float, default=30.0, help="Per-port startup timeout in seconds")
    return parser.parse_args(argv)


def build_project(build_dir: Path) -> bool:
    log(f"Building project ({build_dir})...")
    rc = run_build(build_dir=build_dir)
    if rc == 0:
        log("Build OK")
        return True
    log(f"Build failed with exit code {rc}")
    return False


def resolve_enabled_tests(suite_inputs: Sequence[str]) -> Set[int]:
    sys.path.insert(0, str(PROJECT_ROOT / "Scripts"))
    from validation.config import parse_suite_names, resolve_enabled_tests
    suite_names = parse_suite_names(list(suite_inputs))
    return resolve_enabled_tests(suite_names)


def main(argv: Optional[Sequence[str]] = None) -> int:
    args = parse_args(argv)

    if args.list_suites:
        sys.path.insert(0, str(PROJECT_ROOT / "Scripts"))
        from validation.config import SUITE_TESTS
        for name in sorted(SUITE_TESTS.keys()):
            print(name)
        return 0

    build_dir = (PROJECT_ROOT / args.build_dir).resolve()
    log_dir = VALIDATE_LOG_DIR

    enabled_tests = resolve_enabled_tests(args.suite)
    log(f"Selected tests: {sorted(enabled_tests)}")

    if not args.no_build:
        if not build_dir.exists():
            build_dir.mkdir(parents=True, exist_ok=True)
        if not build_project(build_dir):
            return 1

    ok = run_validation(
        build_dir=build_dir,
        timeout=args.timeout,
        enabled_tests=enabled_tests,
        log_dir=log_dir,
    )

    if ok:
        log("Validation PASSED")
        return 0
    log("Validation FAILED")
    return 1


if __name__ == "__main__":
    sys.exit(main())