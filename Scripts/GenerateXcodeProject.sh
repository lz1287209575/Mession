#!/usr/bin/env bash
#
# GenerateXcodeProject.sh —— 生成 Xcode 工程（不编译目标）。
#
# 复用 build_from_scratch.sh 的双趟 configure 与 MHeaderTool 引导逻辑，
# 确保首次生成时 Xcode 也能看到完整的反射生成目标。
#
# 示例：
#   Scripts/GenerateXcodeProject.sh
#   Scripts/GenerateXcodeProject.sh --clean -t Debug
#   Scripts/GenerateXcodeProject.sh -b Build-Xcode-Debug --fmt-dir /path/to/fmt

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="${REPO_ROOT}/Build-Xcode"
FORWARD_ARGS=()

usage() {
    cat <<'EOF'
用法: Scripts/GenerateXcodeProject.sh [选项]

生成 Xcode 工程，不执行编译。默认输出到 Build-Xcode/。

选项:
  -b, --build-dir DIR   Xcode 工程目录（默认 Build-Xcode）
  -t, --type TYPE       默认构建类型提示：Debug|Release|RelWithDebInfo|MinSizeRel
      --clean           生成前删除工程目录
      --fmt-dir DIR     本地 fmt 源码目录
  -D NAME=VALUE         传递 CMake 变量，可重复
      --cmake-arg A     同 -D NAME=VALUE
  -h, --help            显示帮助

Xcode 的 scheme/configuration 由 Xcode 自己选择；生成后打开 <构建目录>/Mession.xcodeproj。
EOF
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        -b|--build-dir)
            BUILD_DIR="${2:?--build-dir 需要一个目录}"
            shift 2
            ;;
        -g|--generator)
            echo "[xcode] 生成器固定为 Xcode，不能通过 $1 覆盖。" >&2
            exit 2
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            FORWARD_ARGS+=("$1")
            shift
            ;;
    esac
done

if [ "$(uname -s)" != "Darwin" ]; then
    echo "[xcode] Xcode 工程只能在 macOS 上生成。" >&2
    exit 2
fi

if ! command -v xcodebuild >/dev/null 2>&1; then
    echo "[xcode] 未找到 xcodebuild；请安装 Xcode 或 Command Line Tools。" >&2
    exit 2
fi

if [ "${BUILD_DIR#/}" = "${BUILD_DIR}" ]; then
    BUILD_DIR="${REPO_ROOT}/${BUILD_DIR}"
fi

PROJECT_PATH="${BUILD_DIR}/Mession.xcodeproj"
echo "[xcode] 生成工程: ${PROJECT_PATH}"
CONFIGURE_ARGS=(
    --configure-only \
    --generator Xcode \
    --build-dir "${BUILD_DIR}"
)
if [ "${#FORWARD_ARGS[@]}" -gt 0 ]; then
    CONFIGURE_ARGS+=("${FORWARD_ARGS[@]}")
fi

"${SCRIPT_DIR}/build_from_scratch.sh" "${CONFIGURE_ARGS[@]}"

if [ ! -d "${PROJECT_PATH}" ]; then
    echo "[xcode] 配置命令已结束，但未找到 Xcode 工程: ${PROJECT_PATH}" >&2
    echo "[xcode] 请使用 --clean 重试；若仍失败，请提供上方 CMake 输出。" >&2
    exit 1
fi

echo "[xcode] 工程已生成: ${PROJECT_PATH}"
