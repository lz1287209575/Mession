#!/usr/bin/env bash
# Scripts/install-hooks.sh - 安装 clang-format 工具链与 pre-commit hook
# Spec: Docs/superpowers/specs/2026-07-14-coding-style/design.md
#
# 用法:
#   bash Scripts/install-hooks.sh

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
LOCAL_BIN="$HOME/local/bin"
mkdir -p "$LOCAL_BIN"

install_clang_format() {
    local CF_VER="${CLANG_FORMAT_VERSION:-18.1.8}"

    if command -v clang-format >/dev/null 2>&1; then
        echo "[install-hooks] clang-format already available: $(command -v clang-format)"
        return 0
    fi

    echo "[install-hooks] installing clang-format ${CF_VER} to $LOCAL_BIN ..."

    if command -v pip3 >/dev/null 2>&1; then
        pip3 install --target "$LOCAL_BIN" clang-format==${CF_VER}
        echo "[install-hooks] installed via pip3 - add $LOCAL_BIN to PATH"
    elif command -v apt-get >/dev/null 2>&1; then
        echo "[install-hooks] please run: apt-get install clang-format"
        exit 1
    else
        echo "[install-hooks] cannot auto-install. Please install clang-format manually." >&2
        exit 1
    fi
}

install_precommit() {
    if ! command -v pre-commit >/dev/null 2>&1; then
        echo "[install-hooks] installing pre-commit via pip3..."
        pip3 install --user pre-commit
    fi
    echo "[install-hooks] running 'pre-commit install' in $REPO_ROOT"
    (cd "$REPO_ROOT" && pre-commit install)
}

install_clang_format
install_precommit

echo "[install-hooks] done."
echo "[install-hooks] next steps:"
echo "  1. add $LOCAL_BIN to PATH:  export PATH=\"$LOCAL_BIN:\$PATH\""
echo "  2. test with:               bash Scripts/check-style.sh"
