from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
from typing import Callable, Optional, Sequence, Set

from .config import SUITE_TESTS, ValidationCliOptions, parse_suite_names, resolve_enabled_tests


@dataclass(frozen=True)
class ValidationRuntimeHooks:
    log: Callable[[str], None]
    build_project: Callable[[Path, Optional[str], Optional[Path]], bool]
    get_executable_path: Callable[[Path, str], Optional[Path]]
    run_validation: Callable[[Path, float, Optional[int], Path, bool, str, str, Set[int]], bool]


class ValidationRunner:
    def __init__(self, hooks: ValidationRuntimeHooks):
        self.hooks = hooks

    def list_suites(self) -> list[str]:
        return sorted(SUITE_TESTS.keys())

    def run(self, options: ValidationCliOptions) -> int:
        if options.list_suites:
            for suite_name in self.list_suites():
                print(suite_name)
            return 0

        try:
            suite_names = parse_suite_names(options.suite_inputs)
            enabled_tests = resolve_enabled_tests(suite_names)
        except ValueError as exc:
            self.hooks.log(str(exc))
            return 1

        self.hooks.log(f"Selected suites: {', '.join(suite_names)}")
        self.hooks.log(f"Enabled tests: {', '.join(str(test_id) for test_id in sorted(enabled_tests))}")

        project_root = Path(__file__).resolve().parents[2]
        build_dir = (project_root / options.build_dir).resolve()
        log_dir = build_dir / "validate_logs"

        if not build_dir.exists() and not options.no_build:
            build_dir.mkdir(parents=True, exist_ok=True)

        if not options.no_build and not self.hooks.build_project(
            build_dir=build_dir,
            build_system_name=options.build_system,
            build_system_config=options.build_system_config,
        ):
            return 1

        if not self._validate_binaries(build_dir, enable_mgo=not options.no_mgo):
            return 1

        ok = self.hooks.run_validation(
            build_dir=build_dir,
            timeout=options.timeout,
            zone_id=options.zone_id,
            log_dir=log_dir,
            enable_mgo=(not options.no_mgo),
            mongo_db=options.mongo_db,
            mongo_collection=options.mongo_collection,
            enabled_tests=enabled_tests,
        )
        return 0 if ok else 1

    def _validate_binaries(self, build_dir: Path, enable_mgo: bool) -> bool:
        required = ["GatewayServer", "RouterServer", "LoginServer", "WorldServer", "SceneServer"]
        if enable_mgo:
            required.append("MgoServer")
        for name in required:
            if not self.hooks.get_executable_path(build_dir, name):
                self.hooks.log(f"{name} not found in {build_dir}")
                return False
        return True
