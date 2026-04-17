#!/usr/bin/env python3
"""
Cluster-aware control helpers for Mession server management.
"""

from __future__ import annotations

import json
import shlex
import subprocess
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Callable, Optional

SCRIPT_DIR = Path(__file__).resolve().parent
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))

import servers
from build_systems import build_system_cli_args
from server_control_api import ControlApiState, TaskManager, make_server_snapshot, resolve_build_dir, tail_text_file, utc_now_iso


ALL_SERVICES = [name for name, _port in servers.SERVER_ORDER]


@dataclass
class NodeConfig:
    name: str
    mode: str = "local"
    host: str = "127.0.0.1"
    agent_name: Optional[str] = None
    agent_scheme: str = "http"
    agent_port: int = 18080
    agent_base_url: Optional[str] = None
    auth_token: Optional[str] = None
    user: Optional[str] = None
    ssh_port: int = 22
    project_root: str = ""
    build_dir: str = "Build"
    build_system: Optional[str] = None
    build_system_config: Optional[str] = None
    services: list[str] = field(default_factory=lambda: list(ALL_SERVICES))


@dataclass
class TopologyConfig:
    version: str = "dev"
    node_services: dict[str, list[str]] = field(default_factory=dict)
    service_endpoints: dict[str, dict[str, Any]] = field(default_factory=dict)


@dataclass
class RegistryConfig:
    url: str
    auth_token: Optional[str] = None
    timeout: float = 3.0
    poll_interval: float = 2.0
    auto_discover: bool = True
    default_agent_scheme: str = "http"
    default_agent_port: int = 18080
    default_agent_auth_token: Optional[str] = None


@dataclass
class ClusterConfig:
    nodes: list[NodeConfig]
    topology: TopologyConfig
    registry: Optional[RegistryConfig] = None


@dataclass
class NodeRegistryEntry:
    node_name: str
    mode: str
    host: str
    heartbeat_state: str = "never_seen"
    first_seen_at: Optional[str] = None
    last_seen_at: Optional[str] = None
    last_error: Optional[str] = None
    agent_name: Optional[str] = None
    agent_id: Optional[str] = None
    agent_started_at: Optional[str] = None
    applied_topology_version: Optional[str] = None
    registry_source: str = "controller"
    groups: list[str] = field(default_factory=list)
    manageable: bool = False
    service_count: int = 0
    running_count: int = 0
    discovered: bool = False
    topology_issues: list[str] = field(default_factory=list)


def load_cluster_config(config_path: Optional[Path], default_local_node: Optional[NodeConfig] = None) -> ClusterConfig:
    project_root = servers.get_project_root()
    if config_path is None:
        nodes = [default_local_node] if default_local_node else [
            NodeConfig(
                name="local",
                mode="local",
                host="127.0.0.1",
                project_root=str(project_root),
                build_dir="Build",
                services=list(ALL_SERVICES),
            )
        ]
        local_node_name = nodes[0].name if nodes else "local"
        return ClusterConfig(
            nodes=nodes,
            topology=TopologyConfig(version="local-dev", node_services={local_node_name: list(ALL_SERVICES)}),
        )

    if not config_path.is_absolute():
        config_path = (project_root / config_path).resolve()

    content = json.loads(config_path.read_text(encoding="utf-8"))
    registry_raw = content.get("registry", {})
    nodes_raw = content.get("nodes", [])
    if not nodes_raw and not registry_raw.get("url"):
        raise ValueError(f"No nodes defined in cluster config: {config_path}")

    nodes: list[NodeConfig] = []
    for item in nodes_raw:
        node = NodeConfig(
            name=item["name"],
            mode=item.get("mode", "local"),
            host=item.get("host", "127.0.0.1"),
            agent_name=item.get("agent_name"),
            agent_scheme=item.get("agent_scheme", "http"),
            agent_port=int(item.get("agent_port", 18080)),
            agent_base_url=item.get("agent_base_url"),
            auth_token=item.get("auth_token"),
            user=item.get("user"),
            ssh_port=int(item.get("ssh_port", 22)),
            project_root=item.get("project_root", str(project_root)),
            build_dir=item.get("build_dir", "Build"),
            build_system=item.get("build_system"),
            build_system_config=item.get("build_system_config"),
            services=list(item.get("services", ALL_SERVICES)),
        )
        nodes.append(node)

    topology_raw = content.get("topology", {})
    topology = TopologyConfig(
        version=topology_raw.get("version", "dev"),
        node_services={
            key: list(value)
            for key, value in topology_raw.get("node_services", {}).items()
        },
        service_endpoints=dict(topology_raw.get("service_endpoints", {})),
    )

    if not topology.node_services:
        topology.node_services = {node.name: list(node.services) for node in nodes}

    registry = None
    if registry_raw.get("url"):
        registry = RegistryConfig(
            url=str(registry_raw["url"]).rstrip("/"),
            auth_token=registry_raw.get("auth_token"),
            timeout=float(registry_raw.get("timeout", 3.0)),
            poll_interval=float(registry_raw.get("poll_interval", 2.0)),
            auto_discover=bool(registry_raw.get("auto_discover", True)),
            default_agent_scheme=registry_raw.get("default_agent_scheme", "http"),
            default_agent_port=int(registry_raw.get("default_agent_port", 18080)),
            default_agent_auth_token=registry_raw.get("default_agent_auth_token"),
        )

    return ClusterConfig(nodes=nodes, topology=topology, registry=registry)


def load_cluster_nodes(config_path: Optional[Path], default_local_node: Optional[NodeConfig] = None) -> list[NodeConfig]:
    return load_cluster_config(config_path, default_local_node=default_local_node).nodes


def filter_snapshot_services(snapshot: dict[str, Any], service_names: Optional[list[str]]) -> dict[str, Any]:
    if service_names is None:
        return snapshot

    allowed = set(service_names)
    filtered = dict(snapshot)
    services_list = [item for item in snapshot.get("services", []) if item.get("name") in allowed]
    filtered["services"] = services_list
    filtered["service_count"] = len(services_list)
    filtered["running_count"] = sum(1 for item in services_list if item.get("state") == "Running")
    return filtered


def unknown_snapshot(node: NodeConfig, error_text: str) -> dict[str, Any]:
    services_list = []
    for name in node.services:
        services_list.append(
            {
                "name": name,
                "port": servers.get_server_port(name),
                "state": "Unknown",
                "tracked_pid": None,
                "tracked_pid_alive": False,
                "port_open": False,
                "log_path": "",
                "log_exists": False,
                "log_size": 0,
                "log_modified_at": None,
            }
        )
    return {
        "project_root": node.project_root,
        "build_dir": node.build_dir,
        "pid_file": "",
        "pid_file_exists": False,
        "server_log_dir": "",
        "running_count": 0,
        "service_count": len(services_list),
        "services": services_list,
        "updated_at": utc_now_iso(),
        "error": error_text,
    }


class BaseNodeController:
    def __init__(self, node: NodeConfig):
        self.node = node

    def snapshot(self) -> dict[str, Any]:
        raise NotImplementedError

    def read_log(self, server_name: str, lines: int) -> str:
        raise NotImplementedError

    def list_tasks(self) -> list[dict[str, Any]]:
        raise NotImplementedError

    def queue_action(self, action: str, server_name: Optional[str] = None):
        raise NotImplementedError

    def apply_topology(self, topology_payload: dict[str, Any]) -> dict[str, Any]:
        raise NotImplementedError

    def supports_actions(self) -> bool:
        return False

    def supports_logs(self) -> bool:
        return False

    def supports_tasks(self) -> bool:
        return False

    def supports_topology(self) -> bool:
        return False


class LocalNodeController(BaseNodeController):
    def __init__(self, node: NodeConfig):
        super().__init__(node)
        self.build_dir = resolve_build_dir(Path(node.build_dir))
        self.control_state = ControlApiState(
            build_dir=self.build_dir,
            build_system_name=node.build_system,
            build_system_config=Path(node.build_system_config) if node.build_system_config else None,
        )

    def snapshot(self) -> dict[str, Any]:
        snapshot = make_server_snapshot(self.build_dir)
        snapshot["node_name"] = self.node.name
        snapshot["node_mode"] = self.node.mode
        snapshot["node_host"] = self.node.host
        return filter_snapshot_services(snapshot, self.node.services)

    def read_log(self, server_name: str, lines: int) -> str:
        log_path = servers.get_server_log_dir() / f"{server_name}.log"
        return tail_text_file(log_path, lines)

    def list_tasks(self) -> list[dict[str, Any]]:
        return self.control_state.task_manager.list_tasks()

    def queue_action(self, action: str, server_name: Optional[str] = None):
        return self.control_state.queue_action(action, server_name=server_name)

    def apply_topology(self, topology_payload: dict[str, Any]) -> dict[str, Any]:
        return self.control_state.apply_topology(topology_payload)

    def supports_actions(self) -> bool:
        return True

    def supports_logs(self) -> bool:
        return True

    def supports_tasks(self) -> bool:
        return True

    def supports_topology(self) -> bool:
        return True


class SshNodeController(BaseNodeController):
    def __init__(self, node: NodeConfig):
        super().__init__(node)
        self.task_manager = TaskManager(
            build_dir=Path(node.build_dir),
            build_system_name=node.build_system,
            build_system_config=Path(node.build_system_config) if node.build_system_config else None,
        )

    def _ssh_target(self) -> str:
        if self.node.user:
            return f"{self.node.user}@{self.node.host}"
        return self.node.host

    def _ssh_base_command(self, remote_command: str) -> list[str]:
        return [
            "ssh",
            "-o",
            "BatchMode=yes",
            "-o",
            "ConnectTimeout=3",
            "-p",
            str(self.node.ssh_port),
            self._ssh_target(),
            remote_command,
        ]

    def _remote_python_command(self, python_code: str) -> list[str]:
        remote_command = (
            f"cd {shlex.quote(self.node.project_root)} && "
            f"python3 -c {shlex.quote(python_code)}"
        )
        return self._ssh_base_command(remote_command)

    def _run_remote_json(self, python_code: str) -> dict[str, Any]:
        result = subprocess.run(
            self._remote_python_command(python_code),
            capture_output=True,
            text=True,
            timeout=5,
        )
        if result.returncode != 0:
            raise RuntimeError((result.stderr or result.stdout or "remote command failed").strip())
        return json.loads(result.stdout)

    def snapshot(self) -> dict[str, Any]:
        code = (
            "import json; "
            "from pathlib import Path; "
            "import sys; "
            f"sys.path.insert(0, {self.node.project_root!r} + '/Scripts'); "
            "from server_control_api import make_server_snapshot, resolve_build_dir; "
            f"print(json.dumps(make_server_snapshot(resolve_build_dir(Path({self.node.build_dir!r}))), ensure_ascii=False))"
        )
        try:
            snapshot = self._run_remote_json(code)
        except Exception as exc:
            snapshot = unknown_snapshot(self.node, str(exc))
        snapshot["node_name"] = self.node.name
        snapshot["node_mode"] = self.node.mode
        snapshot["node_host"] = self.node.host
        return filter_snapshot_services(snapshot, self.node.services)

    def read_log(self, server_name: str, lines: int) -> str:
        code = (
            "import json; "
            "from pathlib import Path; "
            "import sys; "
            f"sys.path.insert(0, {self.node.project_root!r} + '/Scripts'); "
            "import servers; "
            "from server_control_api import tail_text_file; "
            f"log_path = servers.get_project_root() / 'Logs' / 'servers' / {server_name + '.log'!r}; "
            f"print(json.dumps({{'content': tail_text_file(log_path, {int(lines)})}}, ensure_ascii=False))"
        )
        try:
            payload = self._run_remote_json(code)
            return payload.get("content", "")
        except Exception as exc:
            return f"[remote log error] {exc}"

    def list_tasks(self) -> list[dict[str, Any]]:
        return self.task_manager.list_tasks()

    def queue_action(self, action: str, server_name: Optional[str] = None):
        build_dir = self.node.build_dir
        build_flags = build_system_cli_args(
            self.node.build_system,
            Path(self.node.build_system_config) if self.node.build_system_config else None,
        )
        if action == "build":
            remote_command = f"cd {shlex.quote(self.node.project_root)} && " + shlex.join(
                ["python3", "Scripts/build_project.py", "--build-dir", build_dir, *build_flags]
            )
        elif action == "start":
            remote_command = (
                f"cd {shlex.quote(self.node.project_root)} && "
                f"python3 Scripts/servers.py start --build-dir {shlex.quote(build_dir)}"
            )
        elif action == "stop":
            remote_command = (
                f"cd {shlex.quote(self.node.project_root)} && "
                f"python3 Scripts/servers.py stop --build-dir {shlex.quote(build_dir)}"
            )
        elif action == "validate":
            remote_command = f"cd {shlex.quote(self.node.project_root)} && " + shlex.join(
                ["python3", "Scripts/validate.py", "--build-dir", build_dir, *build_flags, "--no-build"]
            )
        elif action == "validate_with_build":
            remote_command = f"cd {shlex.quote(self.node.project_root)} && " + shlex.join(
                ["python3", "Scripts/validate.py", "--build-dir", build_dir, *build_flags]
            )
        elif action in {"start_server", "stop_server", "restart_server"}:
            if not server_name:
                raise ValueError(f"server_name is required for {action}")
            cli_action = action.replace("_", "-")
            remote_command = (
                f"cd {shlex.quote(self.node.project_root)} && "
                f"python3 Scripts/servers.py {cli_action} {shlex.quote(server_name)} --build-dir {shlex.quote(build_dir)}"
            )
        else:
            raise ValueError(f"unsupported action: {action}")

        return self.task_manager.run_subprocess_task(
            name=f"{action}:{server_name}" if server_name else action,
            command=self._ssh_base_command(remote_command),
            cwd=servers.get_project_root(),
        )

    def apply_topology(self, topology_payload: dict[str, Any]) -> dict[str, Any]:
        topology_json = json.dumps(topology_payload, ensure_ascii=False)
        python_code = (
            "import json, sys; "
            "from pathlib import Path; "
            "sys.path.insert(0, 'Scripts'); "
            "from server_control_api import ControlApiState, resolve_build_dir; "
            "payload = json.loads(sys.argv[1]); "
            f"state = ControlApiState(resolve_build_dir(Path({self.node.build_dir!r}))); "
            "print(json.dumps(state.apply_topology(payload), ensure_ascii=False))"
        )
        remote_command = (
            f"cd {shlex.quote(self.node.project_root)} && "
            f"python3 -c {shlex.quote(python_code)} "
            f"{shlex.quote(topology_json)}"
        )
        result = subprocess.run(
            self._ssh_base_command(remote_command),
            capture_output=True,
            text=True,
            timeout=10,
        )
        if result.returncode != 0:
            raise RuntimeError((result.stderr or result.stdout or 'remote topology apply failed').strip())
        return json.loads(result.stdout)

    def supports_actions(self) -> bool:
        return True

    def supports_logs(self) -> bool:
        return True

    def supports_tasks(self) -> bool:
        return True

    def supports_topology(self) -> bool:
        return True


class AgentNodeController(BaseNodeController):
    def __init__(self, node: NodeConfig):
        super().__init__(node)
        self.timeout = 3.0

    def _base_url(self) -> str:
        if self.node.agent_base_url:
            return self.node.agent_base_url.rstrip("/")
        return f"{self.node.agent_scheme}://{self.node.host}:{self.node.agent_port}"

    def _headers(self) -> dict[str, str]:
        headers = {"Accept": "application/json"}
        if self.node.auth_token:
            headers["Authorization"] = f"Bearer {self.node.auth_token}"
            headers["X-Mession-Token"] = self.node.auth_token
        return headers

    def _request_json(self, method: str, path: str, payload: Optional[dict[str, Any]] = None) -> dict[str, Any]:
        data = None
        headers = self._headers()
        if payload is not None:
            data = json.dumps(payload, ensure_ascii=False).encode("utf-8")
            headers["Content-Type"] = "application/json"
        request = urllib.request.Request(
            url=self._base_url() + path,
            method=method,
            headers=headers,
            data=data,
        )
        try:
            with urllib.request.urlopen(request, timeout=self.timeout) as response:
                data = response.read().decode("utf-8")
            return json.loads(data)
        except urllib.error.HTTPError as exc:
            body = exc.read().decode("utf-8", errors="ignore")
            raise RuntimeError(f"HTTP {exc.code}: {body or exc.reason}") from exc
        except Exception as exc:
            raise RuntimeError(str(exc)) from exc

    def snapshot(self) -> dict[str, Any]:
        try:
            snapshot = self._request_json("GET", "/api/status")
        except Exception as exc:
            snapshot = unknown_snapshot(self.node, str(exc))
        snapshot["node_name"] = self.node.name
        snapshot["node_mode"] = self.node.mode
        snapshot["node_host"] = self.node.host
        return filter_snapshot_services(snapshot, self.node.services)

    def read_log(self, server_name: str, lines: int) -> str:
        try:
            payload = self._request_json(
                "GET",
                f"/api/logs/{urllib.parse.quote(server_name)}?lines={int(lines)}",
            )
            return payload.get("content", "")
        except Exception as exc:
            return f"[agent log error] {exc}"

    def list_tasks(self) -> list[dict[str, Any]]:
        try:
            payload = self._request_json("GET", "/api/tasks")
            return list(payload.get("tasks", []))
        except Exception:
            return []

    def queue_action(self, action: str, server_name: Optional[str] = None):
        path = f"/api/actions/{action}"
        if server_name:
            path += f"/{urllib.parse.quote(server_name)}"
        request_payload: Optional[dict[str, Any]] = None
        if self.node.build_system or self.node.build_system_config:
            request_payload = {}
            if self.node.build_system:
                request_payload["build_system"] = self.node.build_system
            if self.node.build_system_config:
                request_payload["build_system_config"] = self.node.build_system_config
        payload = self._request_json("POST", path, payload=request_payload)
        task_data = payload.get("task")
        if not task_data:
            raise RuntimeError("agent did not return task payload")
        return RemoteTaskHandle(task_data)

    def apply_topology(self, topology_payload: dict[str, Any]) -> dict[str, Any]:
        payload = self._request_json("POST", "/api/topology/apply", payload=topology_payload)
        return dict(payload.get("topology", {}))

    def supports_actions(self) -> bool:
        return True

    def supports_logs(self) -> bool:
        return True

    def supports_tasks(self) -> bool:
        return True

    def supports_topology(self) -> bool:
        return True


def build_snapshot_from_registry_record(record: dict[str, Any], fallback_node: NodeConfig) -> dict[str, Any]:
    status = dict(record.get("status", {}))
    services_list = list(status.get("services", []))
    if not services_list:
        for name in record.get("topology", {}).get("services", []):
            services_list.append(
                {
                    "name": name,
                    "port": servers.get_server_port(name),
                    "state": "Unknown",
                    "tracked_pid": None,
                    "tracked_pid_alive": False,
                    "port_open": False,
                    "log_path": "",
                    "log_exists": False,
                    "log_size": 0,
                    "log_modified_at": None,
                }
            )
    return {
        "project_root": record.get("project_root", fallback_node.project_root),
        "build_dir": record.get("build_dir", fallback_node.build_dir),
        "build_system": record.get("build_system", fallback_node.build_system),
        "pid_file": "",
        "pid_file_exists": False,
        "server_log_dir": "",
        "running_count": status.get(
            "running_count",
            sum(1 for item in services_list if item.get("state") == "Running"),
        ),
        "service_count": status.get("service_count", len(services_list)),
        "services": services_list,
        "updated_at": record.get("received_at") or record.get("heartbeat_at") or utc_now_iso(),
        "agent_name": record.get("agent_name"),
        "agent_id": record.get("agent_id"),
        "agent_started_at": record.get("agent_started_at"),
        "groups": list(record.get("groups", [])),
        "topology": dict(record.get("topology", {})),
        "registry": {
            "source": "central",
            "heartbeat_state": record.get("heartbeat_state"),
        },
    }


class RegistryNodeController(BaseNodeController):
    def __init__(self, node: NodeConfig, record_provider: Callable[[], Optional[dict[str, Any]]]):
        super().__init__(node)
        self.record_provider = record_provider

    def snapshot(self) -> dict[str, Any]:
        record = self.record_provider()
        if not record:
            snapshot = unknown_snapshot(self.node, "registry record unavailable")
        else:
            snapshot = build_snapshot_from_registry_record(record, self.node)
        snapshot["node_name"] = self.node.name
        snapshot["node_mode"] = self.node.mode
        snapshot["node_host"] = self.node.host
        return filter_snapshot_services(snapshot, self.node.services)

    def read_log(self, server_name: str, lines: int) -> str:
        return "[registry-only node] live logs unavailable; add agent auth/defaults to enable direct control"

    def list_tasks(self) -> list[dict[str, Any]]:
        return []

    def queue_action(self, action: str, server_name: Optional[str] = None):
        raise RuntimeError("registry-only node is not directly manageable")

    def apply_topology(self, topology_payload: dict[str, Any]) -> dict[str, Any]:
        raise RuntimeError("registry-only node cannot receive topology pushes")


class RemoteTaskHandle:
    def __init__(self, task_data: dict[str, Any]):
        self._task_data = task_data

    def to_dict(self) -> dict[str, Any]:
        return dict(self._task_data)

    def __getitem__(self, key: str) -> Any:
        return self._task_data[key]

    def __getattr__(self, name: str) -> Any:
        try:
            return self._task_data[name]
        except KeyError as exc:
            raise AttributeError(name) from exc


class ClusterManager:
    def __init__(self, config_path: Optional[Path], default_local_node: Optional[NodeConfig] = None):
        self.config = load_cluster_config(config_path, default_local_node=default_local_node)
        self.controllers: list[BaseNodeController] = []
        self.controllers_by_name: dict[str, BaseNodeController] = {}
        self.registry: dict[str, NodeRegistryEntry] = {}
        self.snapshots: dict[str, dict[str, Any]] = {}
        self.central_registry_nodes: dict[str, dict[str, Any]] = {}
        self.central_registry_summary: dict[str, Any] = {}
        self.central_registry_last_error: Optional[str] = None
        self.last_poll_monotonic: dict[str, float] = {}
        self.last_registry_poll_monotonic = 0.0
        self.cluster_issues: list[str] = []
        self.discovered_node_names: set[str] = set()

        for node in self.config.nodes:
            self._register_controller(self._create_controller(node), discovered=False)

        self._refresh_cluster_issues()

    def _register_controller(self, controller: BaseNodeController, discovered: bool) -> None:
        node = controller.node
        if node.name in self.controllers_by_name:
            self.controllers_by_name[node.name] = controller
            for index, current in enumerate(self.controllers):
                if current.node.name == node.name:
                    self.controllers[index] = controller
                    break
        else:
            self.controllers.append(controller)
            self.controllers_by_name[node.name] = controller
        self.registry.setdefault(
            node.name,
            NodeRegistryEntry(
                node_name=node.name,
                mode=node.mode,
                host=node.host,
                discovered=discovered,
            ),
        )
        entry = self.registry[node.name]
        entry.mode = node.mode
        entry.host = node.host
        entry.discovered = discovered
        entry.manageable = controller.supports_actions()
        if discovered:
            self.discovered_node_names.add(node.name)

    def _create_controller(self, node: NodeConfig) -> BaseNodeController:
        if node.mode == "agent":
            return AgentNodeController(node)
        if node.mode == "ssh":
            return SshNodeController(node)
        if node.mode == "registry":
            return RegistryNodeController(node, lambda: self.central_registry_nodes.get(self._registry_agent_name(node)))
        return LocalNodeController(node)

    def _lookup_node(self, node_name: str) -> NodeConfig:
        for controller in self.controllers:
            if controller.node.name == node_name:
                return controller.node
        raise KeyError(node_name)

    def _compute_cluster_issues(self) -> list[str]:
        issues: list[str] = []
        service_to_nodes: dict[str, list[str]] = {}
        for controller in self.controllers:
            node_name = controller.node.name
            services_list = self.config.topology.node_services.get(node_name, controller.node.services)
            for service_name in services_list:
                service_to_nodes.setdefault(service_name, []).append(node_name)

        for service_name in ALL_SERVICES:
            owners = service_to_nodes.get(service_name, [])
            if not owners:
                issues.append(f"{service_name} is not assigned to any node")
            elif len(owners) > 1:
                issues.append(f"{service_name} is assigned to multiple nodes: {', '.join(owners)}")

        for controller in self.controllers:
            node = controller.node
            expected = set(self.config.topology.node_services.get(node.name, node.services))
            actual = set(node.services)
            if expected != actual:
                issues.append(
                    f"{node.name} services mismatch between node config and topology: "
                    f"node={sorted(actual)} topology={sorted(expected)}"
                )
        return issues

    def _refresh_cluster_issues(self) -> None:
        issues = self._compute_cluster_issues()
        if self.config.registry:
            if self.central_registry_last_error:
                issues.append(f"central registry unavailable: {self.central_registry_last_error}")
            configured_agents = {self._registry_agent_name(node) for node in self.config.nodes}
            unknown_agents = sorted(set(self.central_registry_nodes) - configured_agents)
            if unknown_agents and not self.config.registry.auto_discover:
                issues.append(f"registry has unknown agents: {', '.join(unknown_agents[:3])}")
        self.cluster_issues = issues

    def _registry_agent_name(self, node: NodeConfig) -> str:
        return node.agent_name or node.name

    def _registry_node_for_agent_name(self, agent_name: str) -> Optional[NodeConfig]:
        for controller in self.controllers:
            if self._registry_agent_name(controller.node) == agent_name:
                return controller.node
        return None

    def _registry_headers(self) -> dict[str, str]:
        headers = {"Accept": "application/json"}
        if self.config.registry and self.config.registry.auth_token:
            headers["Authorization"] = f"Bearer {self.config.registry.auth_token}"
            headers["X-Mession-Token"] = self.config.registry.auth_token
        return headers

    def _request_registry_json(self, path: str) -> dict[str, Any]:
        if not self.config.registry:
            raise RuntimeError("central registry is not configured")
        request = urllib.request.Request(
            url=self.config.registry.url + path,
            method="GET",
            headers=self._registry_headers(),
        )
        try:
            with urllib.request.urlopen(request, timeout=self.config.registry.timeout) as response:
                return json.loads(response.read().decode("utf-8"))
        except urllib.error.HTTPError as exc:
            body = exc.read().decode("utf-8", errors="ignore")
            raise RuntimeError(f"HTTP {exc.code}: {body or exc.reason}") from exc
        except Exception as exc:
            raise RuntimeError(str(exc)) from exc

    def _extract_registry_services(self, record: dict[str, Any]) -> list[str]:
        topology_services = list(record.get("topology", {}).get("services", []))
        if topology_services:
            return topology_services
        return [item.get("name") for item in record.get("status", {}).get("services", []) if item.get("name")]

    def _build_discovered_node(self, agent_name: str, record: dict[str, Any]) -> NodeConfig:
        control_api = dict(record.get("control_api", {}))
        registry_cfg = self.config.registry
        assert registry_cfg is not None
        base_url = (control_api.get("base_url") or "").rstrip("/")
        auth_required = bool(control_api.get("auth_required"))
        auth_token = registry_cfg.default_agent_auth_token
        scheme = control_api.get("scheme") or registry_cfg.default_agent_scheme
        host = control_api.get("host") or record.get("host") or "unknown"
        port = int(control_api.get("port") or registry_cfg.default_agent_port)
        manageable = bool(base_url) and (not auth_required or bool(auth_token))
        build_system = record.get("build_system")
        build_system_name = build_system.get("name") if isinstance(build_system, dict) else build_system
        build_system_config = build_system.get("config_path") if isinstance(build_system, dict) else None
        return NodeConfig(
            name=agent_name,
            mode="agent" if manageable else "registry",
            host=host,
            agent_name=agent_name,
            agent_scheme=scheme,
            agent_port=port,
            agent_base_url=base_url or None,
            auth_token=auth_token,
            project_root=record.get("project_root", ""),
            build_dir=record.get("build_dir", "Build"),
            build_system=build_system_name,
            build_system_config=build_system_config,
            services=self._extract_registry_services(record),
        )

    def _sync_discovered_nodes(self) -> None:
        if not self.config.registry or not self.config.registry.auto_discover:
            return
        configured_agents = {self._registry_agent_name(node) for node in self.config.nodes}
        for agent_name, record in sorted(self.central_registry_nodes.items()):
            if agent_name in configured_agents:
                continue
            node = self._build_discovered_node(agent_name, record)
            existing = self.controllers_by_name.get(node.name)
            if existing is not None:
                existing.node.host = node.host
                existing.node.services = list(node.services)
                existing.node.build_dir = node.build_dir
                existing.node.build_system = node.build_system
                existing.node.build_system_config = node.build_system_config
                existing.node.project_root = node.project_root
                existing.node.agent_base_url = node.agent_base_url
                existing.node.agent_port = node.agent_port
                existing.node.agent_scheme = node.agent_scheme
                existing.node.auth_token = node.auth_token
                if existing.node.mode != node.mode:
                    self._register_controller(self._create_controller(node), discovered=True)
                else:
                    entry = self.registry[node.name]
                    entry.mode = existing.node.mode
                    entry.host = existing.node.host
                    entry.manageable = existing.supports_actions()
                    entry.discovered = True
                continue
            self._register_controller(self._create_controller(node), discovered=True)

    def _refresh_central_registry(self, force: bool = False) -> None:
        if not self.config.registry:
            return
        now = time.monotonic()
        if not force and now - self.last_registry_poll_monotonic < self.config.registry.poll_interval:
            return
        self.last_registry_poll_monotonic = now
        try:
            payload = self._request_registry_json("/api/registry/nodes")
        except Exception as exc:
            self.central_registry_last_error = str(exc)
            self._refresh_cluster_issues()
            return

        self.central_registry_last_error = None
        self.central_registry_summary = dict(payload.get("summary", {}))
        self.central_registry_nodes = {
            item.get("agent_name", ""): item
            for item in payload.get("nodes", [])
            if item.get("agent_name")
        }
        self._sync_discovered_nodes()
        for controller in self.controllers:
            node = controller.node
            entry = self.registry[node.name]
            record = self.central_registry_nodes.get(self._registry_agent_name(node))
            self._merge_registry_record(entry, record)
            entry.topology_issues = self.validate_node(node.name, registry_record=record)
        self._refresh_cluster_issues()

    def _merge_registry_record(self, entry: NodeRegistryEntry, record: Optional[dict[str, Any]]) -> None:
        if not record:
            return
        entry.registry_source = "central"
        entry.heartbeat_state = record.get("heartbeat_state", entry.heartbeat_state)
        entry.first_seen_at = record.get("first_seen_at") or entry.first_seen_at
        entry.last_seen_at = record.get("received_at") or record.get("heartbeat_at") or entry.last_seen_at
        entry.agent_name = record.get("agent_name") or entry.agent_name
        entry.agent_id = record.get("agent_id") or entry.agent_id
        entry.agent_started_at = record.get("agent_started_at") or entry.agent_started_at
        entry.groups = list(record.get("groups", []))
        status = record.get("status", {}) or {}
        entry.service_count = int(status.get("service_count", entry.service_count or 0))
        entry.running_count = int(status.get("running_count", entry.running_count or 0))
        topology = record.get("topology", {}) or {}
        entry.applied_topology_version = topology.get("version") or entry.applied_topology_version

    def refresh_due(self, force: bool = False, selected_node_name: Optional[str] = None) -> None:
        self._refresh_central_registry(force=force)
        now = time.monotonic()
        for controller in self.controllers:
            node_name = controller.node.name
            interval = 1.0 if node_name == selected_node_name else 3.0
            last_polled = self.last_poll_monotonic.get(node_name, 0.0)
            if not force and now - last_polled < interval:
                continue
            self.last_poll_monotonic[node_name] = now
            snapshot = controller.snapshot()
            self.snapshots[node_name] = snapshot
            self._update_registry(controller.node, snapshot)
        self._refresh_cluster_issues()

    def _update_registry(self, node: NodeConfig, snapshot: dict[str, Any]) -> None:
        entry = self.registry[node.name]
        registry_record = self.central_registry_nodes.get(self._registry_agent_name(node))
        self._merge_registry_record(entry, registry_record)
        entry.topology_issues = self.validate_node(node.name, snapshot, registry_record=registry_record)

        if snapshot.get("error"):
            entry.last_error = snapshot.get("error")
            if not registry_record:
                entry.registry_source = "controller"
                entry.heartbeat_state = "stale" if entry.last_seen_at else "offline"
            return

        now = utc_now_iso()
        if not entry.first_seen_at:
            entry.first_seen_at = now
        if not registry_record:
            entry.registry_source = "controller"
            entry.last_seen_at = now
        entry.last_error = None
        entry.manageable = self.controllers_by_name[node.name].supports_actions()
        if not registry_record:
            entry.heartbeat_state = "online"
        entry.agent_name = snapshot.get("agent_name") or snapshot.get("node_name") or entry.agent_name
        entry.agent_id = snapshot.get("agent_id") or entry.agent_id
        entry.agent_started_at = snapshot.get("agent_started_at") or entry.agent_started_at
        entry.groups = list(snapshot.get("groups", entry.groups))
        entry.service_count = int(snapshot.get("service_count", entry.service_count or 0))
        entry.running_count = int(snapshot.get("running_count", entry.running_count or 0))
        topology = snapshot.get("topology", {})
        entry.applied_topology_version = topology.get("version") or entry.applied_topology_version

    def validate_node(
        self,
        node_name: str,
        snapshot: Optional[dict[str, Any]] = None,
        registry_record: Optional[dict[str, Any]] = None,
    ) -> list[str]:
        issues: list[str] = []
        try:
            node = self._lookup_node(node_name)
        except KeyError:
            return ["unknown node"]

        expected_services = set(self.config.topology.node_services.get(node_name, node.services))
        actual_services = set(node.services)
        if expected_services != actual_services:
            issues.append(
                f"configured services differ from topology: node={sorted(actual_services)} topology={sorted(expected_services)}"
            )

        if snapshot is None:
            snapshot = self.snapshots.get(node_name)
        if registry_record is None:
            registry_record = self.central_registry_nodes.get(self._registry_agent_name(node))

        if self.config.registry and node.mode in {"agent", "registry"} and not registry_record:
            issues.append("missing from central registry")
        elif registry_record and registry_record.get("heartbeat_state") == "stale":
            issues.append("central heartbeat is stale")

        if not snapshot and not registry_record:
            issues.append("no heartbeat received yet")
            return issues

        topology = {}
        if registry_record:
            topology = registry_record.get("topology", {}) or {}
        if snapshot and not snapshot.get("error"):
            topology = snapshot.get("topology", {}) or topology
        applied_version = topology.get("version")
        expected_version = self.config.topology.version
        if expected_version and applied_version != expected_version:
            issues.append(f"topology version mismatch: expected={expected_version} applied={applied_version or '-'}")

        applied_services = set(topology.get("services", []))
        if applied_services and applied_services != expected_services:
            issues.append(
                f"applied services differ from topology: applied={sorted(applied_services)} expected={sorted(expected_services)}"
            )

        if snapshot and snapshot.get("error"):
            issues.append(f"heartbeat error: {snapshot.get('error')}")

        return issues

    def build_topology_payload(self, node_name: str) -> dict[str, Any]:
        node = self._lookup_node(node_name)
        return {
            "version": self.config.topology.version,
            "node_name": node.name,
            "services": list(self.config.topology.node_services.get(node.name, node.services)),
            "service_endpoints": self.config.topology.service_endpoints,
        }

    def push_topology(self, node_name: str) -> dict[str, Any]:
        controller = self.controllers_by_name[node_name]
        return controller.apply_topology(self.build_topology_payload(node_name))

    def push_topology_all(self) -> dict[str, dict[str, Any]]:
        results: dict[str, dict[str, Any]] = {}
        for controller in self.controllers:
            try:
                results[controller.node.name] = controller.apply_topology(
                    self.build_topology_payload(controller.node.name)
                )
            except Exception as exc:
                results[controller.node.name] = {"error": str(exc)}
        return results

    def queue_action_many(
        self,
        node_names: list[str],
        action: str,
        server_name: Optional[str] = None,
    ) -> dict[str, dict[str, Any]]:
        results: dict[str, dict[str, Any]] = {}
        for node_name in node_names:
            controller = self.controllers_by_name.get(node_name)
            if controller is None:
                results[node_name] = {"error": "node_not_found"}
                continue
            try:
                task = controller.queue_action(action, server_name=server_name)
                task_id = getattr(task, "id", None)
                if task_id is None and hasattr(task, "to_dict"):
                    task_id = task.to_dict().get("id")
                results[node_name] = {"task_id": task_id}
            except Exception as exc:
                results[node_name] = {"error": str(exc)}
        return results

    def push_topology_many(self, node_names: list[str]) -> dict[str, dict[str, Any]]:
        results: dict[str, dict[str, Any]] = {}
        for node_name in node_names:
            try:
                results[node_name] = self.push_topology(node_name)
            except Exception as exc:
                results[node_name] = {"error": str(exc)}
        return results

    def get_snapshot(self, node_name: str) -> dict[str, Any]:
        if node_name in self.snapshots:
            return self.snapshots[node_name]
        node = self._lookup_node(node_name)
        return unknown_snapshot(node, "no snapshot")

    def list_registry_entries(self) -> list[NodeRegistryEntry]:
        return [self.registry[controller.node.name] for controller in self.controllers]

    def get_controller(self, node_name: str) -> BaseNodeController:
        return self.controllers_by_name[node_name]

    def registry_mode_label(self) -> str:
        if self.config.registry and self.config.registry.auto_discover:
            return "central+discover"
        return "central" if self.config.registry else "controller"


def build_node_controllers(config_path: Optional[Path], default_local_node: Optional[NodeConfig] = None) -> list[BaseNodeController]:
    nodes = load_cluster_nodes(config_path, default_local_node=default_local_node)
    controllers: list[BaseNodeController] = []
    for node in nodes:
        if node.mode == "agent":
            controllers.append(AgentNodeController(node))
        elif node.mode == "ssh":
            controllers.append(SshNodeController(node))
        elif node.mode == "registry":
            controllers.append(RegistryNodeController(node, lambda: None))
        else:
            controllers.append(LocalNodeController(node))
    return controllers
