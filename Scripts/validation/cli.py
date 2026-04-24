from __future__ import annotations

from typing import Optional, Sequence

from .config import build_arg_parser, parse_cli_options
from .runner import ValidationRunner, ValidationRuntimeHooks


def main(hooks: ValidationRuntimeHooks, argv: Optional[Sequence[str]] = None) -> int:
    parser = build_arg_parser()
    args = parser.parse_args(argv)
    options = parse_cli_options(args)
    runner = ValidationRunner(hooks)
    return runner.run(options)
