#!/usr/bin/env python3
"""
Central registry service for Mession agents.
"""

from __future__ import annotations

import argparse
import json
import os
import threading
from dataclasses import dataclass, field
from datetime import datetime, timezone
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any, Optional
from urllib.parse import urlparse


DEFAULT_HOST = "127.0.0.1"
DEFAULT_PORT = 19080
DEFAULT_STALE_SECONDS = 15
AUTH_HEADER = "Authorization"
TOKEN_HEADER = "X-Mession-Token"


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


@dataclass
class RegistryNodeRecord:
    agent_name: str
    agent_id: str
    host: str
    project_root: str
    build_dir: str
    agent_started_at: Optional[str]
    first_seen_at: str
    heartbeat_at: str
    received_at: str
    groups: list[str] = field(default_factory=list)
    control_api: dict[str, Any] = field(default_factory=dict)
    topology: dict[str, Any] = field(default_factory=dict)
    status: dict[str, Any] = field(default_factory=dict)

    def to_dict(self, stale_seconds: int) -> dict[str, Any]:
        now = datetime.now(timezone.utc)
        heartbeat_dt = _parse_iso_datetime(self.received_at) or now
        age_seconds = max(0.0, (now - heartbeat_dt).total_seconds())
        heartbeat_state = "online" if age_seconds <= stale_seconds else "stale"
        payload = {
            "agent_name": self.agent_name,
            "agent_id": self.agent_id,
            "host": self.host,
            "project_root": self.project_root,
            "build_dir": self.build_dir,
            "agent_started_at": self.agent_started_at,
            "first_seen_at": self.first_seen_at,
            "heartbeat_at": self.heartbeat_at,
            "received_at": self.received_at,
            "heartbeat_state": heartbeat_state,
            "age_seconds": round(age_seconds, 3),
            "groups": self.groups,
            "control_api": self.control_api,
            "topology": self.topology,
            "status": self.status,
        }
        return payload


def _parse_iso_datetime(value: Optional[str]) -> Optional[datetime]:
    if not value:
        return None
    try:
        return datetime.fromisoformat(value)
    except ValueError:
        return None


class RegistryState:
    def __init__(self, auth_token: Optional[str], stale_seconds: int, state_file: Path):
        self.auth_token = auth_token
        self.stale_seconds = stale_seconds
        self.state_file = state_file
        self.started_at = utc_now_iso()
        self._lock = threading.Lock()
        self._nodes: dict[str, RegistryNodeRecord] = {}
        self._load_state()

    def _load_state(self) -> None:
        if not self.state_file.exists():
            return
        try:
            payload = json.loads(self.state_file.read_text(encoding="utf-8"))
        except Exception:
            return
        nodes = payload.get("nodes", {})
        for key, item in nodes.items():
            try:
                self._nodes[key] = RegistryNodeRecord(
                    agent_name=item["agent_name"],
                    agent_id=item["agent_id"],
                    host=item.get("host", ""),
                    project_root=item.get("project_root", ""),
                    build_dir=item.get("build_dir", ""),
                    agent_started_at=item.get("agent_started_at"),
                    first_seen_at=item.get("first_seen_at", item.get("received_at", "")),
                    heartbeat_at=item.get("heartbeat_at", ""),
                    received_at=item.get("received_at", ""),
                    groups=list(item.get("groups", [])),
                    control_api=dict(item.get("control_api", {})),
                    topology=dict(item.get("topology", {})),
                    status=dict(item.get("status", {})),
                )
            except KeyError:
                continue

    def _persist_state(self) -> None:
        self.state_file.parent.mkdir(parents=True, exist_ok=True)
        payload = {
            "started_at": self.started_at,
            "nodes": {
                key: {
                    "agent_name": value.agent_name,
                    "agent_id": value.agent_id,
                    "host": value.host,
                    "project_root": value.project_root,
                    "build_dir": value.build_dir,
                    "agent_started_at": value.agent_started_at,
                    "first_seen_at": value.first_seen_at,
                    "heartbeat_at": value.heartbeat_at,
                    "received_at": value.received_at,
                    "groups": value.groups,
                    "control_api": value.control_api,
                    "topology": value.topology,
                    "status": value.status,
                }
                for key, value in self._nodes.items()
            },
        }
        self.state_file.write_text(json.dumps(payload, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")

    def upsert_heartbeat(self, payload: dict[str, Any]) -> dict[str, Any]:
        agent_name = payload["agent_name"]
        now = utc_now_iso()
        existing = self._nodes.get(agent_name)
        record = RegistryNodeRecord(
            agent_name=agent_name,
            agent_id=payload["agent_id"],
            host=payload.get("host", ""),
            project_root=payload.get("project_root", ""),
            build_dir=payload.get("build_dir", ""),
            agent_started_at=payload.get("agent_started_at"),
            first_seen_at=existing.first_seen_at if existing and existing.first_seen_at else now,
            heartbeat_at=payload.get("heartbeat_at", utc_now_iso()),
            received_at=now,
            groups=list(payload.get("groups", [])),
            control_api=dict(payload.get("control_api", {})),
            topology=dict(payload.get("topology", {})),
            status=dict(payload.get("status", {})),
        )
        with self._lock:
            self._nodes[agent_name] = record
            self._persist_state()
            return record.to_dict(self.stale_seconds)

    def list_nodes(self) -> list[dict[str, Any]]:
        with self._lock:
            items = [record.to_dict(self.stale_seconds) for record in self._nodes.values()]
        items.sort(key=lambda item: item["agent_name"])
        return items

    def get_node(self, agent_name: str) -> Optional[dict[str, Any]]:
        with self._lock:
            record = self._nodes.get(agent_name)
            return record.to_dict(self.stale_seconds) if record else None

    def summary(self) -> dict[str, Any]:
        nodes = self.list_nodes()
        return {
            "started_at": self.started_at,
            "node_count": len(nodes),
            "online_count": sum(1 for node in nodes if node["heartbeat_state"] == "online"),
            "stale_count": sum(1 for node in nodes if node["heartbeat_state"] == "stale"),
            "state_file": str(self.state_file),
        }

    def liveness_status(self) -> dict[str, Any]:
        return {
            "ok": True,
            "service": "registry",
            "started_at": self.started_at,
            "time": utc_now_iso(),
        }

    def readiness_status(self) -> dict[str, Any]:
        state_parent = self.state_file.parent
        writable = state_parent.exists() and os.access(state_parent, os.W_OK)
        if not state_parent.exists():
            writable = os.access(state_parent.parent if state_parent.parent.exists() else Path("."), os.W_OK)
        return {
            "ok": writable,
            "service": "registry",
            "state_file": str(self.state_file),
            "state_dir": str(state_parent),
            "state_dir_writable": writable,
            "summary": self.summary(),
            "time": utc_now_iso(),
        }


class RegistryRequestHandler(BaseHTTPRequestHandler):
    server_version = "MessionRegistry/0.1"

    @property
    def state(self) -> RegistryState:
        return self.server.state  # type: ignore[attr-defined]

    def log_message(self, fmt: str, *args: Any) -> None:
        print(f"[server_registry] {self.address_string()} - {fmt % args}", flush=True)

    def do_OPTIONS(self) -> None:
        self.send_response(HTTPStatus.NO_CONTENT)
        self._send_common_headers()
        self.end_headers()

    def do_GET(self) -> None:
        parsed = urlparse(self.path)
        path = parsed.path.rstrip("/") or "/"

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
                    "name": "Mession central registry",
                    "summary": self.state.summary(),
                    "routes": [
                        "GET /healthz",
                        "GET /readyz",
                        "GET /api/registry/nodes",
                        "GET /api/registry/nodes/<agent_name>",
                        "POST /api/registry/heartbeat",
                    ],
                },
            )
            return

        if path == "/api/registry/nodes":
            self._write_json(
                HTTPStatus.OK,
                {
                    "summary": self.state.summary(),
                    "nodes": self.state.list_nodes(),
                },
            )
            return

        if path.startswith("/api/registry/nodes/"):
            agent_name = path.split("/")[-1]
            node = self.state.get_node(agent_name)
            if node is None:
                self._write_json(HTTPStatus.NOT_FOUND, {"error": "node_not_found"})
                return
            self._write_json(HTTPStatus.OK, node)
            return

        self._write_json(HTTPStatus.NOT_FOUND, {"error": "not_found"})

    def do_POST(self) -> None:
        if not self._check_authorization():
            return

        parsed = urlparse(self.path)
        path = parsed.path.rstrip("/")

        if path == "/api/registry/heartbeat":
            payload = self._read_json_body()
            if payload is None:
                self._write_json(HTTPStatus.BAD_REQUEST, {"error": "invalid_json"})
                return
            required = {"agent_name", "agent_id"}
            if not required.issubset(payload.keys()):
                self._write_json(HTTPStatus.BAD_REQUEST, {"error": "missing_required_fields"})
                return
            node = self.state.upsert_heartbeat(payload)
            self._write_json(HTTPStatus.OK, {"ok": True, "node": node})
            return

        self._write_json(HTTPStatus.NOT_FOUND, {"error": "not_found"})

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

    def _write_json(self, status: HTTPStatus, payload: dict[str, Any]) -> None:
        body = json.dumps(payload, ensure_ascii=False, indent=2).encode("utf-8")
        self.send_response(status)
        self._send_common_headers()
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def _send_common_headers(self) -> None:
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "Content-Type, Authorization, X-Mession-Token")


class RegistryServer(ThreadingHTTPServer):
    def __init__(self, server_address: tuple[str, int], state: RegistryState):
        super().__init__(server_address, RegistryRequestHandler)
        self.state = state


def main() -> int:
    parser = argparse.ArgumentParser(description="Mession central registry service")
    parser.add_argument("--host", default=env_or_default("MESSION_REGISTRY_HOST", DEFAULT_HOST), help=f"listen host (default: {DEFAULT_HOST})")
    parser.add_argument("--port", type=int, default=env_int("MESSION_REGISTRY_PORT", DEFAULT_PORT), help=f"listen port (default: {DEFAULT_PORT})")
    parser.add_argument("--auth-token", default=env_or_default("MESSION_REGISTRY_TOKEN"), help="Optional static bearer/token auth secret")
    parser.add_argument("--auth-token-file", type=Path, help="Optional file containing the auth token")
    parser.add_argument(
        "--stale-seconds",
        type=int,
        default=env_int("MESSION_REGISTRY_STALE_SECONDS", DEFAULT_STALE_SECONDS),
        help=f"Seconds after which a node becomes stale (default: {DEFAULT_STALE_SECONDS})",
    )
    parser.add_argument(
        "--state-file",
        type=Path,
        default=Path(env_or_default("MESSION_REGISTRY_STATE_FILE", str(Path("Build") / ".mession_registry_state.json")) or str(Path("Build") / ".mession_registry_state.json")),
        help="Registry state file path (default: Build/.mession_registry_state.json)",
    )
    args = parser.parse_args()

    auth_token = args.auth_token or ""
    if args.auth_token_file:
        auth_token = args.auth_token_file.read_text(encoding="utf-8").strip()
    if not auth_token:
        auth_token = os.environ.get("MESSION_REGISTRY_TOKEN", "")

    state = RegistryState(
        auth_token=auth_token or None,
        stale_seconds=args.stale_seconds,
        state_file=args.state_file,
    )
    server = RegistryServer((args.host, args.port), state=state)

    print(
        f"[server_registry] listening on http://{args.host}:{args.port}/ with state file {args.state_file}",
        flush=True,
    )

    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("[server_registry] shutting down", flush=True)
    finally:
        server.server_close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
