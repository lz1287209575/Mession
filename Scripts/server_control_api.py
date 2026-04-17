#!/usr/bin/env python3
"""
Mession local server control API.

This script exposes a small HTTP JSON API for local tooling:

- query server status
- start / stop all servers
- run build / validate tasks
- inspect recent task output
- tail per-server log files

It is meant to sit behind a future UE plugin, desktop app, or web UI.
"""

from __future__ import annotations

import argparse
import contextlib
import json
import io
import os
import socket
import subprocess
import sys
import threading
import time
import traceback
import uuid
import urllib.error
import urllib.request
from collections import deque
from dataclasses import dataclass, field
from datetime import datetime, timezone
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any, Callable, Optional
from urllib.parse import parse_qs, urlparse

SCRIPT_DIR = Path(__file__).resolve().parent
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))

import servers
from build_systems import add_build_system_arguments, build_system_cli_args, describe_build_system, resolve_build_system_config_path


DEFAULT_HOST = "127.0.0.1"
DEFAULT_PORT = 18080
MAX_TASK_OUTPUT_CHARS = 64 * 1024
DEFAULT_LOG_LINES = 200
AUTH_HEADER = "Authorization"
TOKEN_HEADER = "X-Mession-Token"
TOPOLOGY_STATE_FILE = ".mession_agent_topology.json"


def utc_now_iso() -> str:
    return datetime.now(timezone.utc).isoformat()


def env_or_default(name: str, default: Optional[str] = None) -> Optional[str]:
    value = os.environ.get(name)
    if value is None or value == "":
        return default
    return value


def env_int(name: str, default: int) -> int:
    value = env_or_default(name)
    if value is None:
        return default
    try:
        return int(value)
    except ValueError:
        return default


def env_float(name: str, default: float) -> float:
    value = env_or_default(name)
    if value is None:
        return default
    try:
        return float(value)
    except ValueError:
        return default


def env_csv_list(name: str) -> list[str]:
    value = env_or_default(name, "")
    if not value:
        return []
    return [item.strip() for item in value.split(",") if item.strip()]


def resolve_build_dir(build_dir: Path) -> Path:
    if build_dir.is_absolute():
        return build_dir.resolve()
    return (servers.get_project_root() / build_dir).resolve()


def is_port_open(host: str, port: int, timeout: float = 0.25) -> bool:
    sock = None
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(timeout)
        sock.connect((host, port))
        return True
    except OSError:
        return False
    finally:
        if sock is not None:
            try:
                sock.close()
            except OSError:
                pass


def tail_text_file(path: Path, max_lines: int) -> str:
    if max_lines <= 0:
        return ""

    try:
        with path.open("r", encoding="utf-8", errors="ignore") as handle:
            return "".join(deque(handle, maxlen=max_lines))
    except OSError:
        return ""


def make_server_snapshot(build_dir: Path) -> dict[str, Any]:
    pid_file = servers.get_pid_file_path(build_dir)
    pid_map = servers.read_pid_registry(build_dir)

    log_dir = servers.get_server_log_dir()
    service_items: list[dict[str, Any]] = []
    running_count = 0

    for name, port in servers.SERVER_ORDER:
        tracked_pid = pid_map.get(name)
        tracked_pid_alive = bool(tracked_pid and servers.is_process_alive(tracked_pid))
        port_open = is_port_open("127.0.0.1", port) if port > 0 else False
        log_path = log_dir / f"{name}.log"
        log_exists = log_path.exists()

        if tracked_pid_alive and port_open:
            state = "Running"
        elif tracked_pid_alive:
            state = "Starting"
        elif port_open:
            state = "Running"
        else:
            state = "Stopped"

        if state == "Running":
            running_count += 1

        service_items.append(
            {
                "name": name,
                "port": port,
                "state": state,
                "tracked_pid": tracked_pid,
                "tracked_pid_alive": tracked_pid_alive,
                "port_open": port_open,
                "log_path": str(log_path),
                "log_exists": log_exists,
                "log_size": log_path.stat().st_size if log_exists else 0,
                "log_modified_at": datetime.fromtimestamp(log_path.stat().st_mtime, timezone.utc).isoformat()
                if log_exists
                else None,
            }
        )

    return {
        "project_root": str(servers.get_project_root()),
        "build_dir": str(build_dir),
        "pid_file": str(pid_file),
        "pid_file_exists": pid_file.exists(),
        "server_log_dir": str(log_dir),
        "running_count": running_count,
        "service_count": len(service_items),
        "services": service_items,
        "updated_at": utc_now_iso(),
    }


class RegistryHeartbeatReporter:
    def __init__(
        self,
        state: "ControlApiState",
        registry_url: str,
        registry_token: Optional[str],
        heartbeat_interval: float,
    ):
        self.state = state
        self.registry_url = registry_url.rstrip("/")
        self.registry_token = registry_token
        self.heartbeat_interval = max(1.0, heartbeat_interval)
        self.stop_event = threading.Event()
        self.thread: Optional[threading.Thread] = None
        self.last_heartbeat_at: Optional[str] = None
        self.last_error: Optional[str] = None

    def start(self) -> None:
        if self.thread is not None:
            return
        self.thread = threading.Thread(target=self._run_loop, daemon=True)
        self.thread.start()

    def stop(self) -> None:
        self.stop_event.set()
        if self.thread and self.thread.is_alive():
            self.thread.join(timeout=2.0)

    def get_status(self) -> dict[str, Any]:
        return {
            "registry_url": self.registry_url,
            "heartbeat_interval": self.heartbeat_interval,
            "last_heartbeat_at": self.last_heartbeat_at,
            "last_error": self.last_error,
        }

    def _run_loop(self) -> None:
        while not self.stop_event.is_set():
            self._send_once()
            self.stop_event.wait(self.heartbeat_interval)

    def _send_once(self) -> None:
        payload = self.state.build_registry_heartbeat_payload()
        data = json.dumps(payload, ensure_ascii=False).encode("utf-8")
        headers = {"Content-Type": "application/json", "Accept": "application/json"}
        if self.registry_token:
            headers[AUTH_HEADER] = f"Bearer {self.registry_token}"
            headers[TOKEN_HEADER] = self.registry_token
        request = urllib.request.Request(
            url=self.registry_url + "/api/registry/heartbeat",
            method="POST",
            headers=headers,
            data=data,
        )
        try:
            with urllib.request.urlopen(request, timeout=3.0) as response:
                response.read()
            self.last_heartbeat_at = utc_now_iso()
            self.last_error = None
        except Exception as exc:
            self.last_error = str(exc)


@dataclass
class TaskRecord:
    id: str
    name: str
    status: str = "pending"
    created_at: str = field(default_factory=utc_now_iso)
    started_at: Optional[str] = None
    finished_at: Optional[str] = None
    return_code: Optional[int] = None
    command: list[str] = field(default_factory=list)
    output: str = ""
    error: Optional[str] = None

    def append_output(self, text: str) -> None:
        if not text:
            return
        self.output = (self.output + text)[-MAX_TASK_OUTPUT_CHARS:]

    def to_dict(self) -> dict[str, Any]:
        return {
            "id": self.id,
            "name": self.name,
            "status": self.status,
            "created_at": self.created_at,
            "started_at": self.started_at,
            "finished_at": self.finished_at,
            "return_code": self.return_code,
            "command": self.command,
            "output": self.output,
            "error": self.error,
        }


class TaskManager:
    def __init__(
        self,
        build_dir: Path,
        build_system_name: Optional[str] = None,
        build_system_config: Optional[Path] = None,
    ):
        self.build_dir = build_dir
        self.build_system_name = build_system_name
        self.build_system_config = build_system_config
        self._lock = threading.Lock()
        self._tasks: dict[str, TaskRecord] = {}

    def list_tasks(self) -> list[dict[str, Any]]:
        with self._lock:
            tasks = sorted(self._tasks.values(), key=lambda item: item.created_at, reverse=True)
            return [task.to_dict() for task in tasks]

    def get_task(self, task_id: str) -> Optional[dict[str, Any]]:
        with self._lock:
            task = self._tasks.get(task_id)
            return task.to_dict() if task else None

    def _create_task(self, name: str, command: list[str]) -> TaskRecord:
        task = TaskRecord(id=uuid.uuid4().hex, name=name, command=command)
        with self._lock:
            self._tasks[task.id] = task
        return task

    def _append_task_output(self, task: TaskRecord, text: str) -> None:
        with self._lock:
            task.append_output(text)

    def _finish_task(self, task: TaskRecord, return_code: int, error: Optional[str] = None) -> None:
        with self._lock:
            task.return_code = return_code
            task.status = "completed" if return_code == 0 else "failed"
            task.error = error
            task.finished_at = utc_now_iso()

    def _mark_task_running(self, task: TaskRecord) -> None:
        with self._lock:
            task.status = "running"
            task.started_at = utc_now_iso()

    def run_callable_task(
        self,
        name: str,
        command: list[str],
        func: Callable[[], int],
    ) -> TaskRecord:
        task = self._create_task(name=name, command=command)

        manager = self

        class TaskStream(io.TextIOBase):
            def write(self, text: str) -> int:
                manager._append_task_output(task, text)
                return len(text)

            def flush(self) -> None:
                return None

        def worker() -> None:
            self._mark_task_running(task)
            stream = TaskStream()
            try:
                with contextlib.redirect_stdout(stream), contextlib.redirect_stderr(stream):
                    rc = func()
                self._finish_task(task, rc)
            except Exception as exc:
                self._append_task_output(task, traceback.format_exc())
                self._finish_task(task, 1, str(exc))

        threading.Thread(target=worker, daemon=True).start()
        return task

    def run_subprocess_task(
        self,
        name: str,
        command: list[str],
        cwd: Path,
    ) -> TaskRecord:
        task = self._create_task(name=name, command=command)

        def worker() -> None:
            self._mark_task_running(task)

            try:
                process = subprocess.Popen(
                    command,
                    cwd=str(cwd),
                    stdout=subprocess.PIPE,
                    stderr=subprocess.STDOUT,
                    text=True,
                    bufsize=1,
                )

                assert process.stdout is not None
                for line in process.stdout:
                    self._append_task_output(task, line)

                process.wait()
                self._finish_task(task, process.returncode)
            except Exception as exc:
                self._append_task_output(task, traceback.format_exc())
                self._finish_task(task, 1, str(exc))

        threading.Thread(target=worker, daemon=True).start()
        return task

    def run_build(self) -> TaskRecord:
        return self.run_build_with_options(self.build_system_name, self.build_system_config)

    def run_build_with_options(
        self,
        build_system_name: Optional[str],
        build_system_config: Optional[Path],
    ) -> TaskRecord:
        return self.run_subprocess_task(
            name="build",
            command=[
                sys.executable,
                str(servers.get_project_root() / "Scripts" / "build_project.py"),
                "--build-dir",
                str(self.build_dir),
                *build_system_cli_args(build_system_name, build_system_config),
            ],
            cwd=servers.get_project_root(),
        )


class ControlApiState:
    def __init__(
        self,
        build_dir: Path,
        build_system_name: Optional[str] = None,
        build_system_config: Optional[Path] = None,
        auth_token: Optional[str] = None,
        agent_name: Optional[str] = None,
        registry_url: Optional[str] = None,
        registry_token: Optional[str] = None,
        heartbeat_interval: float = 5.0,
        control_api_advertisement: Optional[dict[str, Any]] = None,
        agent_groups: Optional[list[str]] = None,
    ):
        self.build_dir = build_dir
        self.build_system_name = build_system_name
        self.build_system_config = resolve_build_system_config_path(build_system_config)
        self.build_system_info = describe_build_system(
            build_dir=build_dir,
            build_system_name=build_system_name,
            config_path=self.build_system_config,
        )
        self.task_manager = TaskManager(
            build_dir=build_dir,
            build_system_name=build_system_name,
            build_system_config=self.build_system_config,
        )
        self.auth_token = auth_token
        self.agent_name = agent_name or socket.gethostname()
        self.agent_id = uuid.uuid4().hex
        self.started_at = utc_now_iso()
        self.control_api_advertisement = dict(control_api_advertisement or {})
        self.agent_groups = list(agent_groups or [])
        self.topology_state_path = self.build_dir / TOPOLOGY_STATE_FILE
        self.applied_topology = self._load_topology_state()
        self.registry_reporter: Optional[RegistryHeartbeatReporter] = None
        if registry_url:
            self.registry_reporter = RegistryHeartbeatReporter(
                state=self,
                registry_url=registry_url,
                registry_token=registry_token,
                heartbeat_interval=heartbeat_interval,
            )

    def _load_topology_state(self) -> dict[str, Any]:
        if not self.topology_state_path.exists():
            return {}
        try:
            return json.loads(self.topology_state_path.read_text(encoding="utf-8"))
        except Exception:
            return {}

    def get_topology_summary(self) -> dict[str, Any]:
        topology = self.applied_topology or {}
        return {
            "version": topology.get("version"),
            "node_name": topology.get("node_name"),
            "services": list(topology.get("services", [])),
            "applied_at": topology.get("applied_at"),
            "path": str(self.topology_state_path),
        }

    def apply_topology(self, payload: dict[str, Any]) -> dict[str, Any]:
        stored = dict(payload)
        stored["applied_at"] = utc_now_iso()
        self.topology_state_path.parent.mkdir(parents=True, exist_ok=True)
        self.topology_state_path.write_text(
            json.dumps(stored, ensure_ascii=False, indent=2) + "\n",
            encoding="utf-8",
        )
        self.applied_topology = stored
        return self.get_topology_summary()

    def build_registry_heartbeat_payload(self) -> dict[str, Any]:
        snapshot = make_server_snapshot(self.build_dir)
        return {
            "agent_name": self.agent_name,
            "agent_id": self.agent_id,
            "agent_started_at": self.started_at,
            "project_root": str(servers.get_project_root()),
            "build_dir": str(self.build_dir),
            "build_system": dict(self.build_system_info),
            "host": socket.gethostname(),
            "heartbeat_at": utc_now_iso(),
            "groups": list(self.agent_groups),
            "control_api": dict(self.control_api_advertisement),
            "topology": self.get_topology_summary(),
            "status": {
                "running_count": snapshot.get("running_count", 0),
                "service_count": snapshot.get("service_count", 0),
                "services": snapshot.get("services", []),
            },
        }

    def get_registry_status(self) -> Optional[dict[str, Any]]:
        if not self.registry_reporter:
            return None
        return self.registry_reporter.get_status()

    def liveness_status(self) -> dict[str, Any]:
        return {
            "ok": True,
            "service": "control_api",
            "agent_name": self.agent_name,
            "started_at": self.started_at,
            "time": utc_now_iso(),
        }

    def readiness_status(self) -> dict[str, Any]:
        build_dir_exists = self.build_dir.exists()
        registry = self.get_registry_status()
        return {
            "ok": build_dir_exists,
            "service": "control_api",
            "agent_name": self.agent_name,
            "build_dir": str(self.build_dir),
            "build_dir_exists": build_dir_exists,
            "build_system": dict(self.build_system_info),
            "registry": registry,
            "time": utc_now_iso(),
        }

    def queue_action(
        self,
        action: str,
        server_name: Optional[str] = None,
        build_system_name: Optional[str] = None,
        build_system_config: Optional[Path] = None,
    ) -> TaskRecord:
        project_root = servers.get_project_root()
        selected_build_system = build_system_name if build_system_name is not None else self.build_system_name
        selected_build_system_config = (
            resolve_build_system_config_path(build_system_config)
            if build_system_config is not None
            else self.build_system_config
        )

        if action == "start":
            return self.task_manager.run_callable_task(
                name="start",
                command=["servers.py", "start", "--build-dir", str(self.build_dir)],
                func=lambda: servers.start_servers(self.build_dir),
            )
        if action == "stop":
            return self.task_manager.run_callable_task(
                name="stop",
                command=["servers.py", "stop", "--build-dir", str(self.build_dir)],
                func=lambda: servers.stop_servers(self.build_dir),
            )
        if action == "start_server":
            if not server_name:
                raise ValueError("server_name is required for start_server")
            return self.task_manager.run_callable_task(
                name=f"start_server:{server_name}",
                command=["servers.py", "start-server", server_name, "--build-dir", str(self.build_dir)],
                func=lambda: servers.start_server(self.build_dir, server_name),
            )
        if action == "stop_server":
            if not server_name:
                raise ValueError("server_name is required for stop_server")
            return self.task_manager.run_callable_task(
                name=f"stop_server:{server_name}",
                command=["servers.py", "stop-server", server_name, "--build-dir", str(self.build_dir)],
                func=lambda: servers.stop_server(self.build_dir, server_name),
            )
        if action == "restart_server":
            if not server_name:
                raise ValueError("server_name is required for restart_server")
            return self.task_manager.run_callable_task(
                name=f"restart_server:{server_name}",
                command=["servers.py", "restart-server", server_name, "--build-dir", str(self.build_dir)],
                func=lambda: servers.restart_server(self.build_dir, server_name),
            )
        if action == "validate":
            return self.task_manager.run_subprocess_task(
                name="validate",
                command=[
                    sys.executable,
                    str(project_root / "Scripts" / "validate.py"),
                    "--build-dir",
                    str(self.build_dir),
                    *build_system_cli_args(selected_build_system, selected_build_system_config),
                    "--no-build",
                ],
                cwd=project_root,
            )
        if action == "validate_with_build":
            return self.task_manager.run_subprocess_task(
                name="validate_with_build",
                command=[
                    sys.executable,
                    str(project_root / "Scripts" / "validate.py"),
                    "--build-dir",
                    str(self.build_dir),
                    *build_system_cli_args(selected_build_system, selected_build_system_config),
                ],
                cwd=project_root,
            )
        if action == "build":
            return self.task_manager.run_build_with_options(selected_build_system, selected_build_system_config)
        raise ValueError(f"unsupported action: {action}")


class ControlRequestHandler(BaseHTTPRequestHandler):
    server_version = "MessionControlApi/0.1"

    def log_message(self, fmt: str, *args: Any) -> None:
        print(f"[server_control_api] {self.address_string()} - {fmt % args}", flush=True)

    @property
    def state(self) -> ControlApiState:
        return self.server.state  # type: ignore[attr-defined]

    def do_OPTIONS(self) -> None:
        self.send_response(HTTPStatus.NO_CONTENT)
        self._send_common_headers("application/json; charset=utf-8")
        self.end_headers()

    def do_GET(self) -> None:
        parsed = urlparse(self.path)
        path = parsed.path.rstrip("/") or "/"
        query = parse_qs(parsed.query)

        if path == "/healthz":
            self._write_json(HTTPStatus.OK, self.state.liveness_status())
            return

        if path == "/readyz":
            payload = self.state.readiness_status()
            self._write_json(HTTPStatus.OK if payload.get("ok") else HTTPStatus.SERVICE_UNAVAILABLE, payload)
            return

        if not self._check_authorization():
            return

        if path == "/":
            self._write_json(
                HTTPStatus.OK,
                {
                    "name": "Mession local server control API",
                    "agent_name": self.state.agent_name,
                    "agent_id": self.state.agent_id,
                    "started_at": self.state.started_at,
                    "routes": [
                        "GET /healthz",
                        "GET /readyz",
                        "GET /api/status",
                        "GET /api/agent/info",
                        "GET /api/topology",
                        "GET /api/tasks",
                        "GET /api/tasks/<task_id>",
                        "GET /api/logs/<server>?lines=200",
                        "POST /api/actions/<build|start|stop|validate|validate_with_build>",
                        "POST /api/actions/<start_server|stop_server|restart_server>/<server>",
                        "POST /api/topology/apply",
                    ],
                },
            )
            return

        if path == "/api/agent/info":
            self._write_json(
                HTTPStatus.OK,
                {
                    "agent_name": self.state.agent_name,
                    "agent_id": self.state.agent_id,
                    "started_at": self.state.started_at,
                    "build_dir": str(self.state.build_dir),
                    "build_system": dict(self.state.build_system_info),
                    "project_root": str(servers.get_project_root()),
                    "auth_enabled": bool(self.state.auth_token),
                    "groups": list(self.state.agent_groups),
                    "control_api": dict(self.state.control_api_advertisement),
                    "topology": self.state.get_topology_summary(),
                    "registry": self.state.get_registry_status(),
                },
            )
            return

        if path == "/api/status":
            payload = make_server_snapshot(self.state.build_dir)
            payload["agent_name"] = self.state.agent_name
            payload["agent_id"] = self.state.agent_id
            payload["agent_started_at"] = self.state.started_at
            payload["build_system"] = dict(self.state.build_system_info)
            payload["groups"] = list(self.state.agent_groups)
            payload["control_api"] = dict(self.state.control_api_advertisement)
            payload["topology"] = self.state.get_topology_summary()
            payload["registry"] = self.state.get_registry_status()
            self._write_json(HTTPStatus.OK, payload)
            return

        if path == "/api/topology":
            self._write_json(
                HTTPStatus.OK,
                {
                    "agent_name": self.state.agent_name,
                    "agent_id": self.state.agent_id,
                    "topology": self.state.get_topology_summary(),
                },
            )
            return

        if path == "/api/tasks":
            self._write_json(HTTPStatus.OK, {"tasks": self.state.task_manager.list_tasks()})
            return

        if path.startswith("/api/tasks/"):
            task_id = path.split("/")[-1]
            task = self.state.task_manager.get_task(task_id)
            if task is None:
                self._write_json(HTTPStatus.NOT_FOUND, {"error": "task_not_found"})
                return
            self._write_json(HTTPStatus.OK, task)
            return

        if path.startswith("/api/logs/"):
            server_name = path.split("/")[-1]
            allowed = {name for name, _port in servers.SERVER_ORDER}
            if server_name not in allowed:
                self._write_json(HTTPStatus.NOT_FOUND, {"error": "unknown_server"})
                return

            try:
                lines = int(query.get("lines", [str(DEFAULT_LOG_LINES)])[0])
            except ValueError:
                lines = DEFAULT_LOG_LINES
            lines = max(1, min(lines, 2000))

            log_path = servers.get_server_log_dir() / f"{server_name}.log"
            if not log_path.exists():
                self._write_json(
                    HTTPStatus.OK,
                    {"server": server_name, "log_path": str(log_path), "content": "", "exists": False},
                )
                return

            self._write_json(
                HTTPStatus.OK,
                {
                    "server": server_name,
                    "log_path": str(log_path),
                    "exists": True,
                    "content": tail_text_file(log_path, lines),
                },
            )
            return

        self._write_json(HTTPStatus.NOT_FOUND, {"error": "not_found"})

    def do_POST(self) -> None:
        if not self._check_authorization():
            return

        parsed = urlparse(self.path)
        path = parsed.path.rstrip("/")

        if path == "/api/topology/apply":
            payload = self._read_json_body()
            if payload is None:
                self._write_json(HTTPStatus.BAD_REQUEST, {"error": "invalid_json"})
                return
            topology_summary = self.state.apply_topology(payload)
            self._write_json(
                HTTPStatus.OK,
                {
                    "agent_name": self.state.agent_name,
                    "agent_id": self.state.agent_id,
                    "topology": topology_summary,
                },
            )
            return

        if path.startswith("/api/actions/"):
            parts = [part for part in path.split("/") if part]
            action = parts[-1]
            server_name = None
            if len(parts) >= 4:
                action = parts[2]
                server_name = parts[3]
            payload = self._read_json_body()
            if payload is None:
                self._write_json(HTTPStatus.BAD_REQUEST, {"error": "invalid_json"})
                return
            build_system_name = payload.get("build_system")
            build_system_config = payload.get("build_system_config")
            try:
                task = self.state.queue_action(
                    action,
                    server_name=server_name,
                    build_system_name=str(build_system_name) if build_system_name is not None else None,
                    build_system_config=Path(str(build_system_config)) if build_system_config else None,
                )
            except (ValueError, FileNotFoundError) as exc:
                self._write_json(HTTPStatus.BAD_REQUEST, {"error": str(exc)})
                return
            self._write_json(HTTPStatus.ACCEPTED, {"task": task.to_dict()})
            return

        self._write_json(HTTPStatus.NOT_FOUND, {"error": "not_found"})

    def _write_json(self, status: HTTPStatus, payload: dict[str, Any]) -> None:
        body = json.dumps(payload, ensure_ascii=False, indent=2).encode("utf-8")
        self.send_response(status)
        self._send_common_headers("application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def _send_common_headers(self, content_type: str) -> None:
        self.send_header("Content-Type", content_type)
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "Content-Type")

    def _check_authorization(self) -> bool:
        expected = self.state.auth_token
        if not expected:
            return True

        bearer = self.headers.get(AUTH_HEADER, "")
        token = self.headers.get(TOKEN_HEADER, "")

        authorized = token == expected
        if bearer.startswith("Bearer "):
            authorized = authorized or (bearer[len("Bearer "):] == expected)

        if authorized:
            return True

        self._write_json(HTTPStatus.UNAUTHORIZED, {"error": "unauthorized"})
        return False

    def _read_json_body(self) -> Optional[dict[str, Any]]:
        length_header = self.headers.get("Content-Length", "0")
        try:
            content_length = int(length_header)
        except ValueError:
            return None
        if content_length <= 0:
            return {}
        try:
            raw = self.rfile.read(content_length)
            return json.loads(raw.decode("utf-8"))
        except Exception:
            return None


class ControlApiServer(ThreadingHTTPServer):
    def __init__(self, server_address: tuple[str, int], state: ControlApiState):
        super().__init__(server_address, ControlRequestHandler)
        self.state = state


def build_control_api_advertisement(
    listen_host: str,
    listen_port: int,
    advertise_host: Optional[str],
    advertise_port: Optional[int],
    advertise_url: Optional[str],
    auth_required: bool,
) -> dict[str, Any]:
    if advertise_url:
        parsed = urlparse(advertise_url)
        return {
            "base_url": advertise_url.rstrip("/"),
            "scheme": parsed.scheme or "http",
            "host": parsed.hostname or "",
            "port": parsed.port or advertise_port or listen_port,
            "auth_required": auth_required,
        }

    host = advertise_host
    if not host and listen_host not in {"0.0.0.0", "::", ""}:
        host = listen_host
    if not host:
        return {}

    port = advertise_port or listen_port
    return {
        "base_url": f"http://{host}:{port}",
        "scheme": "http",
        "host": host,
        "port": port,
        "auth_required": auth_required,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description="Mession local server control API")
    parser.add_argument("--host", default=env_or_default("MESSION_CONTROL_API_HOST", DEFAULT_HOST), help=f"listen host (default: {DEFAULT_HOST})")
    parser.add_argument("--port", type=int, default=env_int("MESSION_CONTROL_API_PORT", DEFAULT_PORT), help=f"listen port (default: {DEFAULT_PORT})")
    parser.add_argument("--build-dir", type=Path, default=Path(env_or_default("MESSION_BUILD_DIR", "Build") or "Build"), help="Build directory (default: Build)")
    add_build_system_arguments(parser)
    parser.add_argument("--agent-name", default=env_or_default("MESSION_AGENT_NAME"), help="Optional logical agent name shown in status output")
    parser.add_argument(
        "--group",
        action="append",
        default=list(env_csv_list("MESSION_AGENT_GROUPS")),
        help="Optional registry/TUI group label; may be provided multiple times",
    )
    parser.add_argument("--auth-token", default=env_or_default("MESSION_CONTROL_API_TOKEN"), help="Optional static bearer/token auth secret")
    parser.add_argument("--auth-token-file", type=Path, help="Optional file containing the auth token")
    parser.add_argument("--advertise-host", default=env_or_default("MESSION_CONTROL_API_ADVERTISE_HOST"), help="Optional host name advertised to the central registry")
    parser.add_argument("--advertise-port", type=int, default=env_int("MESSION_CONTROL_API_ADVERTISE_PORT", 0) or None, help="Optional advertised control API port")
    parser.add_argument("--advertise-url", default=env_or_default("MESSION_CONTROL_API_ADVERTISE_URL"), help="Optional full control API URL advertised to the central registry")
    parser.add_argument("--registry-url", default=env_or_default("MESSION_REGISTRY_URL"), help="Optional central registry URL for agent heartbeats")
    parser.add_argument("--registry-token", default=env_or_default("MESSION_REGISTRY_TOKEN"), help="Optional auth token for the central registry")
    parser.add_argument(
        "--heartbeat-interval",
        type=float,
        default=env_float("MESSION_REGISTRY_HEARTBEAT_INTERVAL", 5.0),
        help="Registry heartbeat interval in seconds when --registry-url is set (default: 5.0)",
    )
    args = parser.parse_args()

    build_dir = resolve_build_dir(args.build_dir)
    auth_token = args.auth_token or ""
    if args.auth_token_file:
        auth_token = args.auth_token_file.read_text(encoding="utf-8").strip()
    if not auth_token:
        auth_token = os.environ.get("MESSION_CONTROL_API_TOKEN", "")

    registry_token = args.registry_token or os.environ.get("MESSION_REGISTRY_TOKEN", "")
    control_api_advertisement = build_control_api_advertisement(
        listen_host=args.host,
        listen_port=args.port,
        advertise_host=args.advertise_host,
        advertise_port=args.advertise_port,
        advertise_url=args.advertise_url,
        auth_required=bool(auth_token),
    )

    state = ControlApiState(
        build_dir=build_dir,
        build_system_name=args.build_system,
        build_system_config=args.build_system_config,
        auth_token=auth_token or None,
        agent_name=args.agent_name,
        registry_url=args.registry_url,
        registry_token=registry_token or None,
        heartbeat_interval=args.heartbeat_interval,
        control_api_advertisement=control_api_advertisement,
        agent_groups=args.group,
    )
    server = ControlApiServer((args.host, args.port), state=state)

    print(
        f"[server_control_api] listening on http://{args.host}:{args.port}/ with build dir {build_dir}"
        f" using build system {state.build_system_info['name']}",
        flush=True,
    )

    if state.registry_reporter:
        state.registry_reporter.start()
        print(
            f"[server_control_api] registry heartbeat enabled -> {args.registry_url}",
            flush=True,
        )

    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("[server_control_api] shutting down", flush=True)
    finally:
        if state.registry_reporter:
            state.registry_reporter.stop()
        server.server_close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
