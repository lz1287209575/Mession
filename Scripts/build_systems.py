#!/usr/bin/env python3
"""
Shared build system definitions for local tooling.
"""

from __future__ import annotations

import argparse
import json
import os
import shlex
import subprocess
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Optional


DEFAULT_BUILD_SYSTEM_NAME = "cmake"
DEFAULT_BUILD_SYSTEM_CONFIG_PATH = Path("Config") / "build_systems.json"
BUILD_SYSTEM_ENV_VAR = "MESSION_BUILD_SYSTEM"
BUILD_SYSTEM_CONFIG_ENV_VAR = "MESSION_BUILD_SYSTEM_CONFIG"


def get_project_root() -> Path:
    return Path(__file__).resolve().parent.parent


def resolve_project_relative_path(path: Optional[Path], project_root: Optional[Path] = None) -> Optional[Path]:
    if path is None:
        return None
    root = project_root or get_project_root()
    if path.is_absolute():
        return path.resolve()
    return (root / path).resolve()


def resolve_build_system_config_path(
    config_path: Optional[Path],
    project_root: Optional[Path] = None,
) -> Optional[Path]:
    root = project_root or get_project_root()
    selected = config_path
    if selected is None:
        env_value = os.environ.get(BUILD_SYSTEM_CONFIG_ENV_VAR, "").strip()
        if env_value:
            selected = Path(env_value)
        else:
            default_path = resolve_project_relative_path(DEFAULT_BUILD_SYSTEM_CONFIG_PATH, root)
            if default_path and default_path.exists():
                selected = default_path
    if selected is None:
        return None
    resolved = resolve_project_relative_path(selected, root)
    if resolved is None or not resolved.exists():
        raise FileNotFoundError(f"Build system config not found: {selected}")
    return resolved


@dataclass(frozen=True)
class BuildSystemSpec:
    name: str
    commands: list[list[str]]
    cwd: str = "."
    env: dict[str, str] = field(default_factory=dict)
    description: str = ""


@dataclass(frozen=True)
class BuildPlan:
    spec: BuildSystemSpec
    commands: list[list[str]]
    cwd: Path
    env: dict[str, str]


@dataclass(frozen=True)
class BuildSystemCatalog:
    systems: dict[str, BuildSystemSpec]
    default_name: Optional[str] = None
    source_path: Optional[Path] = None


def _normalize_token_command(value: Any, field_name: str) -> list[str]:
    if isinstance(value, str):
        tokens = shlex.split(value)
        if not tokens:
            raise ValueError(f"{field_name} must not be empty")
        return tokens
    if isinstance(value, list) and value and all(isinstance(part, str) for part in value):
        return list(value)
    raise ValueError(f"{field_name} must be a non-empty string or string array")


def _normalize_commands(raw: dict[str, Any], system_name: str) -> list[list[str]]:
    commands_raw = raw.get("commands")
    if commands_raw is not None:
        if not isinstance(commands_raw, list) or not commands_raw:
            raise ValueError(f"build system '{system_name}' field 'commands' must be a non-empty array")
        commands: list[list[str]] = []
        for index, item in enumerate(commands_raw):
            commands.append(_normalize_token_command(item, f"build system '{system_name}' commands[{index}]"))
        return commands

    commands = []
    for field_name in ("configure", "build"):
        value = raw.get(field_name)
        if value is None:
            continue
        commands.append(_normalize_token_command(value, f"build system '{system_name}' field '{field_name}'"))
    if not commands:
        raise ValueError(f"build system '{system_name}' must define 'commands' or 'configure'/'build'")
    return commands


def _parse_build_system_spec(raw: dict[str, Any], fallback_name: Optional[str] = None) -> BuildSystemSpec:
    name = str(raw.get("name") or fallback_name or "").strip()
    if not name:
        raise ValueError("build system entry is missing a name")
    env = raw.get("env", {})
    if env is None:
        env = {}
    if not isinstance(env, dict) or not all(isinstance(key, str) and isinstance(value, str) for key, value in env.items()):
        raise ValueError(f"build system '{name}' field 'env' must be an object of string pairs")
    return BuildSystemSpec(
        name=name,
        commands=_normalize_commands(raw, name),
        cwd=str(raw.get("cwd", ".")),
        env=dict(env),
        description=str(raw.get("description", "")),
    )


def _builtin_catalog() -> BuildSystemCatalog:
    systems = {
        DEFAULT_BUILD_SYSTEM_NAME: BuildSystemSpec(
            name=DEFAULT_BUILD_SYSTEM_NAME,
            description="Default CMake configure + build pipeline",
            cwd=".",
            commands=[
                ["cmake", "-S", ".", "-B", "{build_dir}", "-DCMAKE_BUILD_TYPE=Release"],
                ["cmake", "--build", "{build_dir}", "-j4"],
            ],
        )
    }
    return BuildSystemCatalog(systems=systems, default_name=DEFAULT_BUILD_SYSTEM_NAME, source_path=None)


def load_build_system_catalog(
    config_path: Optional[Path] = None,
    project_root: Optional[Path] = None,
) -> BuildSystemCatalog:
    root = project_root or get_project_root()
    catalog = _builtin_catalog()
    resolved_config_path = resolve_build_system_config_path(config_path, root)
    if resolved_config_path is None:
        return catalog

    payload = json.loads(resolved_config_path.read_text(encoding="utf-8"))
    raw_systems = payload.get("systems", [])
    if isinstance(raw_systems, dict):
        items = []
        for system_name, value in raw_systems.items():
            if not isinstance(value, dict):
                raise ValueError(f"build system '{system_name}' must be an object")
            merged = dict(value)
            merged.setdefault("name", system_name)
            items.append(merged)
        raw_systems = items

    if not isinstance(raw_systems, list):
        raise ValueError("build systems config field 'systems' must be an array or object")

    systems = dict(catalog.systems)
    for item in raw_systems:
        if not isinstance(item, dict):
            raise ValueError("each build system config entry must be an object")
        spec = _parse_build_system_spec(item)
        systems[spec.name] = spec

    default_name = payload.get("default")
    if default_name is not None:
        default_name = str(default_name).strip()
        if default_name and default_name not in systems:
            raise ValueError(f"default build system '{default_name}' is not defined")
    else:
        default_name = catalog.default_name

    return BuildSystemCatalog(systems=systems, default_name=default_name, source_path=resolved_config_path)


def resolve_build_system(
    build_system_name: Optional[str] = None,
    config_path: Optional[Path] = None,
    project_root: Optional[Path] = None,
) -> tuple[BuildSystemSpec, BuildSystemCatalog]:
    root = project_root or get_project_root()
    catalog = load_build_system_catalog(config_path=config_path, project_root=root)
    requested_name = (build_system_name or os.environ.get(BUILD_SYSTEM_ENV_VAR, "")).strip()
    selected_name = requested_name or catalog.default_name or DEFAULT_BUILD_SYSTEM_NAME
    spec = catalog.systems.get(selected_name)
    if spec is None:
        available = ", ".join(sorted(catalog.systems))
        raise ValueError(f"Unknown build system '{selected_name}'. Available: {available}")
    return spec, catalog


class _FormatContext(dict[str, str]):
    def __missing__(self, key: str) -> str:
        raise ValueError(f"Unsupported build system placeholder: {key}")


def _format_value(template: str, context: dict[str, str]) -> str:
    try:
        return template.format_map(_FormatContext(context))
    except ValueError:
        raise
    except Exception as exc:
        raise ValueError(f"Failed to expand build system template '{template}': {exc}") from exc


def render_build_plan(
    build_dir: Path,
    build_system_name: Optional[str] = None,
    config_path: Optional[Path] = None,
    project_root: Optional[Path] = None,
) -> BuildPlan:
    root = (project_root or get_project_root()).resolve()
    resolved_build_dir = resolve_project_relative_path(build_dir, root)
    if resolved_build_dir is None:
        raise ValueError("build_dir is required")

    spec, _catalog = resolve_build_system(
        build_system_name=build_system_name,
        config_path=config_path,
        project_root=root,
    )
    context = {
        "project_root": str(root),
        "build_dir": str(resolved_build_dir),
        "python": sys.executable,
    }
    commands = [[_format_value(part, context) for part in command] for command in spec.commands]
    cwd_text = _format_value(spec.cwd, context)
    cwd = resolve_project_relative_path(Path(cwd_text), root)
    if cwd is None:
        raise ValueError("build system cwd could not be resolved")
    env = os.environ.copy()
    env.update({key: _format_value(value, context) for key, value in spec.env.items()})
    return BuildPlan(spec=spec, commands=commands, cwd=cwd, env=env)


def format_command(command: list[str]) -> str:
    return shlex.join(command)


def describe_build_system(
    build_dir: Path,
    build_system_name: Optional[str] = None,
    config_path: Optional[Path] = None,
    project_root: Optional[Path] = None,
) -> dict[str, Any]:
    root = (project_root or get_project_root()).resolve()
    plan = render_build_plan(
        build_dir=build_dir,
        build_system_name=build_system_name,
        config_path=config_path,
        project_root=root,
    )
    _spec, catalog = resolve_build_system(
        build_system_name=build_system_name,
        config_path=config_path,
        project_root=root,
    )
    return {
        "name": plan.spec.name,
        "description": plan.spec.description,
        "cwd": str(plan.cwd),
        "commands": [list(command) for command in plan.commands],
        "config_path": str(catalog.source_path) if catalog.source_path else None,
        "available": sorted(catalog.systems),
    }


def build_system_cli_args(
    build_system_name: Optional[str],
    config_path: Optional[Path],
) -> list[str]:
    args: list[str] = []
    if build_system_name:
        args.extend(["--build-system", build_system_name])
    if config_path:
        args.extend(["--build-system-config", str(config_path)])
    return args


def add_build_system_arguments(parser: argparse.ArgumentParser) -> None:
    parser.add_argument(
        "--build-system",
        default=os.environ.get(BUILD_SYSTEM_ENV_VAR, "").strip() or None,
        help=f"Build system name (default: config default or {DEFAULT_BUILD_SYSTEM_NAME})",
    )
    parser.add_argument(
        "--build-system-config",
        type=Path,
        default=Path(os.environ[BUILD_SYSTEM_CONFIG_ENV_VAR]) if os.environ.get(BUILD_SYSTEM_CONFIG_ENV_VAR) else None,
        help="Optional build systems config JSON path",
    )


def run_build(
    build_dir: Path,
    build_system_name: Optional[str] = None,
    config_path: Optional[Path] = None,
    project_root: Optional[Path] = None,
) -> int:
    root = (project_root or get_project_root()).resolve()
    resolved_build_dir = resolve_project_relative_path(build_dir, root)
    if resolved_build_dir is None:
        raise ValueError("build_dir is required")
    plan = render_build_plan(
        build_dir=build_dir,
        build_system_name=build_system_name,
        config_path=config_path,
        project_root=root,
    )
    resolved_build_dir.mkdir(parents=True, exist_ok=True)
    print(f"[build] using build system: {plan.spec.name}", flush=True)
    for command in plan.commands:
        print(f"$ {format_command(command)}", flush=True)
        process = subprocess.Popen(
            command,
            cwd=str(plan.cwd),
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            bufsize=1,
            env=plan.env,
        )
        assert process.stdout is not None
        for line in process.stdout:
            print(line, end="", flush=True)
        process.wait()
        if process.returncode != 0:
            return process.returncode
    return 0
