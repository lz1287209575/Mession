from __future__ import annotations

import argparse
from dataclasses import dataclass
from pathlib import Path
from typing import List, Optional, Sequence, Set

from build_systems import add_build_system_arguments


# PoC 阶段链路:
#   - chain_local:        Client → Gateway → EchoService_A(命中本机 Actor)
#   - chain_remote:       Client → Gateway → EchoService_A → EchoService_B(跨进程 hop)
#   - chain_remote_async:  Client → Gateway → EchoService_A.EchoAwait(Frame/await)
#                          → EchoService_B.Echo → Actor 2001(P2)
#   - error_unknown:      Client → Gateway → 不存在的 ActorId → 错误回包
ALL_TEST_IDS = {1, 2, 3, 4, 5}
SUITE_TESTS = {
    "all": ALL_TEST_IDS,
    "chain_local": {1},
    "chain_remote": {1, 2},
    "chain_remote_async": {1, 2, 4},   # needs 1 + 2 setup + the new async test
    "error_unknown": {1, 3},
    "downlink": {1, 5},                # 下行通知端到端(MFUNCTION(CallClient))
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


def build_arg_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Mession PoC validation (Gateway + EchoService)")
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