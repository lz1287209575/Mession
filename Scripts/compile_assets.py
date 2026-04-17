#!/usr/bin/env python3
"""
Compile .mobj.json assets into .mob with stable naming.
"""

from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
from pathlib import Path

from build_systems import add_build_system_arguments, get_project_root, run_build


def log(message: str) -> None:
    print(f"[compile_assets] {message}", flush=True)


def get_smoke_tool_path(project_root: Path) -> Path:
    exe_name = "MObjectAssetSmokeTool.exe" if sys.platform == "win32" else "MObjectAssetSmokeTool"
    return project_root / "Bin" / exe_name


def get_asset_output_name(asset_path: Path, suffix: str) -> str:
    if asset_path.name.endswith(".mobj.json"):
        return asset_path.name[:-10] + suffix
    return asset_path.stem + suffix


def get_asset_relative_path(project_root: Path, asset_path: Path) -> Path:
    game_data_root = (project_root / "GameData").resolve()
    try:
        return asset_path.resolve().relative_to(game_data_root)
    except ValueError:
        pass

    try:
        return asset_path.resolve().relative_to(project_root.resolve())
    except ValueError:
        return Path(asset_path.name)


def get_generated_asset_dir(project_root: Path, asset_path: Path) -> Path:
    asset_rel = get_asset_relative_path(project_root, asset_path)
    return project_root / "Build" / "Generated" / "Assets" / asset_rel.parent


def get_generated_mob_path(project_root: Path, asset_path: Path) -> Path:
    return get_generated_asset_dir(project_root, asset_path) / get_asset_output_name(asset_path, ".mob")


def get_generated_roundtrip_path(project_root: Path, asset_path: Path) -> Path:
    return get_generated_asset_dir(project_root, asset_path) / get_asset_output_name(asset_path, ".roundtrip.json")


def get_publish_mob_path(project_root: Path, asset_path: Path) -> Path:
    asset_rel = get_asset_relative_path(project_root, asset_path)
    return project_root / "GameData" / asset_rel.parent / get_asset_output_name(asset_path, ".mob")


def clean_asset_outputs(project_root: Path, asset_path: Path) -> None:
    for output_path in (
        get_generated_mob_path(project_root, asset_path),
        get_generated_roundtrip_path(project_root, asset_path),
    ):
        if output_path.exists():
            output_path.unlink()


def discover_assets(project_root: Path, inputs: list[str]) -> list[Path]:
    if not inputs:
        return sorted((project_root / "GameData").rglob("*.mobj.json"))

    results: list[Path] = []
    for raw in inputs:
        if any(token in raw for token in ("*", "?", "[")):
            results.extend(sorted(project_root.glob(raw)))
            continue

        candidate = Path(raw)
        if not candidate.is_absolute():
            candidate = (project_root / candidate).resolve()

        if candidate.is_dir():
            results.extend(sorted(candidate.rglob("*.mobj.json")))
        else:
            results.append(candidate)

    deduped: list[Path] = []
    seen: set[Path] = set()
    for path in results:
        resolved = path.resolve()
        if resolved not in seen:
            deduped.append(resolved)
            seen.add(resolved)
    return deduped


def run_smoke_tool(
    project_root: Path,
    smoke_tool: Path,
    asset_path: Path,
    emit_roundtrip: bool,
    publish: bool,
) -> int:
    mob_path = get_generated_mob_path(project_root, asset_path)
    roundtrip_path = get_generated_roundtrip_path(project_root, asset_path)
    mob_path.parent.mkdir(parents=True, exist_ok=True)
    cmd = [str(smoke_tool), str(asset_path), str(mob_path)]
    if emit_roundtrip:
        roundtrip_path.parent.mkdir(parents=True, exist_ok=True)
        cmd.append(str(roundtrip_path))
    else:
        cmd.append("--no-roundtrip")

    try:
        display_path = asset_path.relative_to(project_root)
    except ValueError:
        display_path = asset_path

    log(f"compile {display_path}")
    completed = subprocess.run(cmd, check=False, cwd=project_root)
    if completed.returncode != 0:
        return completed.returncode

    if publish:
        publish_path = get_publish_mob_path(project_root, asset_path)
        publish_path.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(mob_path, publish_path)
        log(f"publish {publish_path.relative_to(project_root)}")

    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="Compile .mobj.json assets into .mob")
    parser.add_argument("inputs", nargs="*", help="Asset file, directory, or glob; default scans GameData/**/*.mobj.json")
    parser.add_argument("--build-dir", type=Path, default=Path("Build"), help="Build directory (default: Build)")
    parser.add_argument("--build", action="store_true", help="Build MObjectAssetSmokeTool before compiling assets")
    parser.add_argument("--no-roundtrip", action="store_true", help="Do not emit .roundtrip.json validation output")
    parser.add_argument("--publish", action="store_true", help="Copy generated .mob into GameData after compile")
    add_build_system_arguments(parser)
    args = parser.parse_args()

    project_root = get_project_root()
    smoke_tool = get_smoke_tool_path(project_root)

    if args.build:
        rc = run_build(
            build_dir=args.build_dir,
            build_system_name=args.build_system,
            config_path=args.build_system_config,
        )
        if rc != 0:
            log(f"build_failed rc={rc}")
            return rc

    if not smoke_tool.exists():
        log(f"smoke_tool_missing: {smoke_tool}")
        log("run `python3 Scripts/compile_assets.py --build` first")
        return 2

    assets = discover_assets(project_root, args.inputs)
    if not assets:
        log("no_assets_found")
        return 0

    emit_roundtrip = not args.no_roundtrip
    failures = 0
    for asset_path in assets:
        if not asset_path.exists():
            log(f"missing_asset: {asset_path}")
            failures += 1
            continue

        clean_asset_outputs(project_root, asset_path)
        rc = run_smoke_tool(project_root, smoke_tool, asset_path, emit_roundtrip, args.publish)
        if rc != 0:
            log(f"failed rc={rc}: {asset_path}")
            failures += 1

    if failures > 0:
        log(f"done_with_failures count={failures}")
        return 3

    log(f"done ok={len(assets)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
