#!/usr/bin/env python3
"""
start_service.py — 同质多进程 Service 的统一启动器

按 --services 参数启动一组 Service 进程（gateway / echo / 后续扩展的任意 service）。
子进程 PID 记录到 ./Logs/<svc>.pid；日志到 ./Logs/<svc>.log。

用法:
  ./Scripts/start_service.py start --services=gateway,echo
  ./Scripts/start_service.py status
  ./Scripts/start_service.py stop
  ./Scripts/start_service.py restart --services=echo

设计:
  - SERVICES dict 集中所有 service 的启动参数 + bin 路径
  - 加新 service 只在 dict 里加 1 项；start() 自动处理 --peers 拼接
  - --peers 字段根据 peers_field 标识动态注入：
      "service_peers" → 连 Gateway + 其它 service_peers
      "gateway_peers"  → 连所有其它 service（仅 Gateway 用）
"""
import argparse
import os
import signal
import subprocess
import time
from pathlib import Path

ROOT = Path(__file__).parent.parent
BIN = ROOT / "Bin"
LOG_DIR = ROOT / "Logs"
PID_DIR = LOG_DIR


def parse_listen_port(args):
    """从 args 里提取监听端口：优先 --listen=N，其次 -p N。"""
    for i, a in enumerate(args):
        if a.startswith("--listen="):
            return int(a.split("=", 1)[1])
        if a in ("-p", "--port") and i + 1 < len(args):
            return int(args[i + 1])
    return 0


# 各 Service 启动参数集中配置——加新 Service 只改这里
SERVICES = {
    "gateway": {
        "bin": BIN / "GatewayServer",
        "args": [
            "-p",
            "8001",
            "--local-type=Gateway",
        ],
        "peers_field": "gateway_peers",
        "log_name": "gateway",
    },
    "echo": {
        "bin": BIN / "EchoService",
        "args": [
            "--listen=7001",
            "--service=MEchoService",
            "--local-type=Echo",
            "--inst=1",
            "--actors=1,2",
        ],
        "peers_field": "service_peers",
        "log_name": "echo",
    },
    # 第 2 步扩展示例：再加一个 service
    # "inventory": {
    #     "bin": BIN / "EchoService",  # 同一二进制，靠 --local-type 区分业务
    #     "args": ["--listen=7003", "--local-type=Inventory", "--inst=1"],
    #     "peers_field": "service_peers",
    #     "log_name": "inventory",
    # },
}


def build_service_args(svc_name, all_services):
    """根据选定的 services 集合动态拼接 --peers 参数"""
    spec = SERVICES[svc_name]
    args = list(spec["args"])

    # 收集其它需要作为 peer 的服务
    other_service_peers = [
        (name.upper(), parse_listen_port(SERVICES[name]["args"]))
        for name in all_services
        if name != svc_name
    ]

    if spec.get("peers_field") == "service_peers":
        # 普通 service：连 Gateway + 其它 service
        if other_service_peers:
            peers_str = ",".join(f"{n}@127.0.0.1:{p}" for n, p in other_service_peers)
            args.append(f"--peers={peers_str}")
    elif spec.get("peers_field") == "gateway_peers":
        # Gateway：连所有其它 service
        if other_service_peers:
            peers_str = ",".join(f"{n}@127.0.0.1:{p}" for n, p in other_service_peers)
            args.append(f"--peers={peers_str}")

    return args


procs = {}  # svc_name -> (Popen, log_fh)


def parse_services_arg(arg):
    return [s.strip() for s in arg.split(",") if s.strip()]


def start(services):
    if not services:
        services = list(SERVICES.keys())
        print(f"  (未指定 --services，默认启动全部: {services})")

    unknown = [s for s in services if s not in SERVICES]
    if unknown:
        print(f"  [ERROR] 未知 service: {unknown}；已知: {list(SERVICES.keys())}")
        return False

    LOG_DIR.mkdir(parents=True, exist_ok=True)

    for name in services:
        spec = SERVICES[name]
        if not spec["bin"].exists():
            print(f"  [ERROR] {name}: binary 不存在: {spec['bin']}")
            print(f"          请先编译: cmake --build Build -j4")
            return False

        args = [str(spec["bin"])] + build_service_args(name, services)
        log_file = LOG_DIR / f"{spec['log_name']}.log"
        pid_file = PID_DIR / f"{spec['log_name']}.pid"
        log_fh = open(log_file, "w")
        proc = subprocess.Popen(args, cwd=str(ROOT), stdout=log_fh, stderr=subprocess.STDOUT)
        procs[name] = (proc, log_fh)
        pid_file.write_text(str(proc.pid))
        print(f"  [+] {name} pid={proc.pid} log={log_file}")
        time.sleep(0.5)

    return True


def stop():
    if not procs:
        for name, spec in SERVICES.items():
            pid_file = PID_DIR / f"{spec['log_name']}.pid"
            if pid_file.exists():
                try:
                    pid = int(pid_file.read_text().strip())
                    os.kill(pid, signal.SIGTERM)
                    print(f"  [-] {name} pid={pid} (from pidfile)")
                except (ProcessLookupError, ValueError):
                    pass
                pid_file.unlink(missing_ok=True)
        return

    for name, (proc, log_fh) in procs.items():
        if proc.poll() is None:
            proc.send_signal(signal.SIGTERM)
            try:
                proc.wait(timeout=3)
            except subprocess.TimeoutExpired:
                proc.kill()
        log_fh.close()
        (PID_DIR / f"{SERVICES[name]['log_name']}.pid").unlink(missing_ok=True)
        print(f"  [-] {name} pid={proc.pid}")
    procs.clear()


def status():
    if not procs:
        for name, spec in SERVICES.items():
            pid_file = PID_DIR / f"{spec['log_name']}.pid"
            if pid_file.exists():
                pid = pid_file.read_text().strip()
                print(f"  {name}: pid={pid} (pidfile only)")
        return

    for name, (proc, _) in procs.items():
        rc = proc.poll()
        alive = "running" if rc is None else f"exited(rc={rc})"
        print(f"  {name}: {alive} pid={proc.pid}")


def main():
    parser = argparse.ArgumentParser(description="同质多进程 Service 启动器")
    sub = parser.add_subparsers(dest="cmd", required=True)

    p_start = sub.add_parser("start", help="启动一组 service")
    p_start.add_argument("--services", type=str,
                         help="逗号分隔的 service 名（不传则启动全部）")

    sub.add_parser("stop", help="停止所有已启动的 service")
    sub.add_parser("status", help="查看运行状态")
    p_restart = sub.add_parser("restart", help="stop + start")
    p_restart.add_argument("--services", type=str)

    args = parser.parse_args()

    if args.cmd == "start":
        services = parse_services_arg(args.services) if args.services else None
        if not start(services or []):
            exit(1)
    elif args.cmd == "restart":
        services = parse_services_arg(args.services) if args.services else None
        stop()
        time.sleep(0.5)
        if not start(services or []):
            exit(1)
    elif args.cmd == "stop":
        stop()
    elif args.cmd == "status":
        status()


if __name__ == "__main__":
    main()
