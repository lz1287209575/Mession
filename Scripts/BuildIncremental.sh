#!/usr/bin/env bash
#
# BuildIncremental.sh —— 复用已有 CMake 构建目录进行增量编译。
#
# CMake 会在输入或构建系统文件更新时自动重新配置；本脚本不删除缓存、
# 生成物或目标文件。首次构建请使用 BuildFull.sh。
#
# 示例：
#   Scripts/BuildIncremental.sh
#   Scripts/BuildIncremental.sh -b Build -j 16 --target AwaitableTest
#   Scripts/BuildIncremental.sh --config Debug       # 多配置生成器（VS/Xcode）

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="${REPO_ROOT}/Build"
JOBS=""
TARGET=""
CONFIG=""

usage() {
    cat <<'EOF'
用法: Scripts/BuildIncremental.sh [选项]

选项:
  -b, --build-dir DIR  CMake 构建目录（默认 Build）
  -j, --jobs N         并行任务数（默认由 CMake 决定）
      --target NAME    仅构建指定目标
      --config NAME    多配置生成器的配置名（如 Debug/Release）
  -h, --help           显示帮助

首次构建或需要从零开始时，请使用 Scripts/BuildFull.sh。
EOF
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        -b|--build-dir) BUILD_DIR="${2:?--build-dir 需要一个目录}"; shift 2 ;;
        -j|--jobs)      JOBS="${2:?--jobs 需要一个数字}"; shift 2 ;;
        --target)       TARGET="${2:?--target 需要一个目标名}"; shift 2 ;;
        --config)       CONFIG="${2:?--config 需要一个配置名}"; shift 2 ;;
        -h|--help)      usage; exit 0 ;;
        *)              usage >&2; echo "未知参数: $1" >&2; exit 2 ;;
    esac
done

if [ "${BUILD_DIR#/}" = "${BUILD_DIR}" ]; then
    BUILD_DIR="${REPO_ROOT}/${BUILD_DIR}"
fi

if [ ! -f "${BUILD_DIR}/CMakeCache.txt" ]; then
    echo "[build] 未找到构建缓存: ${BUILD_DIR}" >&2
    echo "[build] 请先运行 Scripts/BuildFull.sh，或通过 --build-dir 指定已有构建目录。" >&2
    exit 2
fi

BUILD_ARGS=(--build "${BUILD_DIR}")
if [ -n "${TARGET}" ]; then BUILD_ARGS+=(--target "${TARGET}"); fi
if [ -n "${CONFIG}" ]; then BUILD_ARGS+=(--config "${CONFIG}"); fi
if [ -n "${JOBS}" ]; then BUILD_ARGS+=(--parallel "${JOBS}"); fi

echo "[build] 增量编译: ${BUILD_DIR}"
exec cmake "${BUILD_ARGS[@]}"
