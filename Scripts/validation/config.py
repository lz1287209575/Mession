from __future__ import annotations

import argparse
from dataclasses import dataclass
from pathlib import Path
from typing import List, Optional, Sequence, Set

from build_systems import add_build_system_arguments

VALIDATE_MONGO_SANDBOX_DB = "mession_validate_sandbox"
VALIDATE_MONGO_SANDBOX_COLLECTION = "world_snapshots"

ALL_TEST_IDS = set(range(1, 44))
SUITE_TESTS = {
    "all": ALL_TEST_IDS,
    "player_state": set(range(1, 15)) | set(range(19, 27)) | {31, 32, 33},
    "runtime_social": {1, 2, 3, 4, 15, 34, 35, 36, 37},
    "scene_downlink": {1, 2, 3, 4, 5, 6, 15, 18, 19, 20},
    "combat_commit": {1, 2, 3, 4, 5, 6, 15, 16, 17, 30, 38, 39, 40, 41, 42, 43},
    "forward_errors": {1, 2, 3, 4, 27, 28, 29},
    "runtime_dispatch": set(range(1, 27)) | {30, 31, 32, 33, 34, 35, 36, 37, 38, 39},
}


@dataclass(frozen=True)
class ValidationCliOptions:
    build_dir: Path
    build_system: Optional[str]
    build_system_config: Optional[Path]
    no_build: bool
    list_suites: bool
    suite_inputs: List[str]
    timeout: float
    zone_id: Optional[int]
    no_mgo: bool
    mongo_db: str
    mongo_collection: str


def build_arg_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Mession skeleton validation")
    parser.add_argument("--build-dir", default="Build", help="Build directory (default: Build)")
    add_build_system_arguments(parser)
    parser.add_argument("--no-build", action="store_true", help="Skip build step")
    parser.add_argument("--list-suites", action="store_true", help="List available validation suites and exit")
    parser.add_argument(
        "--suite",
        action="append",
        default=[],
        help=(
            "Validation suite to run. Can be passed multiple times or as comma-separated names. "
            f"Available: {', '.join(sorted(SUITE_TESTS.keys()))}"
        ),
    )
    parser.add_argument("--timeout", type=float, default=30.0, help="Startup timeout in seconds")
    parser.add_argument("--zone", type=int, default=None, metavar="ID", help="Set MESSION_ZONE_ID")
    parser.add_argument("--debug", action="store_true", help="Retained for compatibility; logs are always written")
    parser.add_argument("--stress", type=int, default=0, metavar="N", help="Reserved compatibility flag")
    parser.add_argument("--stress-moves", type=int, default=5, metavar="M", help="Reserved compatibility flag")
    parser.add_argument("--no-mgo", action="store_true", help="Do not start MgoServer")
    parser.add_argument(
        "--mongo-db",
        default=VALIDATE_MONGO_SANDBOX_DB,
        help=f"Mongo database for validate MgoServer (default: {VALIDATE_MONGO_SANDBOX_DB})",
    )
    parser.add_argument(
        "--mongo-collection",
        default=VALIDATE_MONGO_SANDBOX_COLLECTION,
        help=f"Mongo collection for validate MgoServer (default: {VALIDATE_MONGO_SANDBOX_COLLECTION})",
    )
    return parser


def parse_cli_options(args: argparse.Namespace) -> ValidationCliOptions:
    return ValidationCliOptions(
        build_dir=Path(args.build_dir),
        build_system=getattr(args, 'build_system', None),
        build_system_config=getattr(args, 'build_system_config', None),
        no_build=bool(args.no_build),
        list_suites=bool(args.list_suites),
        suite_inputs=list(args.suite),
        timeout=float(args.timeout),
        zone_id=args.zone,
        no_mgo=bool(args.no_mgo),
        mongo_db=str(args.mongo_db),
        mongo_collection=str(args.mongo_collection),
    )


def parse_suite_names(raw_values: Sequence[str]) -> List[str]:
    suite_names: List[str] = []
    for raw_value in raw_values:
        for item in raw_value.split(','):
            name = item.strip()
            if name:
                suite_names.append(name)
    return suite_names or ["all"]


def resolve_enabled_tests(suite_names: Sequence[str]) -> Set[int]:
    enabled_tests: Set[int] = set()
    for suite_name in suite_names:
        test_ids = SUITE_TESTS.get(suite_name)
        if test_ids is None:
            valid = ", ".join(sorted(SUITE_TESTS.keys()))
            raise ValueError(f"unknown suite '{suite_name}', valid suites: {valid}")
        enabled_tests.update(test_ids)
    return enabled_tests
