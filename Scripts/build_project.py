#!/usr/bin/env python3
"""
Project build entrypoint with pluggable build systems.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

from build_systems import add_build_system_arguments, run_build


def main() -> int:
    parser = argparse.ArgumentParser(description="Build Mession with the selected build system")
    parser.add_argument("--build-dir", type=Path, default=Path("Build"), help="Build directory (default: Build)")
    add_build_system_arguments(parser)
    args = parser.parse_args()
    return run_build(
        build_dir=args.build_dir,
        build_system_name=args.build_system,
        config_path=args.build_system_config,
    )


if __name__ == "__main__":
    sys.exit(main())
