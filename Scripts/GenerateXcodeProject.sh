#!/usr/bin/env bash
#
# GenerateXcodeProject.sh —— 生成 Xcode 工程（不编译目标）。
#
# 直接调用 CMake 的 Xcode generator；不调用构建脚本、不编译目标。
#
# 示例：
#   Scripts/GenerateXcodeProject.sh
#   Scripts/GenerateXcodeProject.sh --clean -t Debug
#   Scripts/GenerateXcodeProject.sh -b Build-Xcode-Debug --fmt-dir /path/to/fmt

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="${REPO_ROOT}/Build-Xcode"
BUILD_TYPE="Release"
FMT_DIR=""
DO_CLEAN=0
CMAKE_ARGS=()

usage() {
    cat <<'EOF'
用法: Scripts/GenerateXcodeProject.sh [选项]

生成 Xcode 工程，不执行编译。默认输出到 Build-Xcode/。

选项:
  -b, --build-dir DIR   Xcode 工程目录（默认 Build-Xcode）
  -t, --type TYPE       默认构建类型：Debug|Release|RelWithDebInfo|MinSizeRel
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
        -t|--type)
            BUILD_TYPE="${2:?--type 需要一个值}"
            shift 2
            ;;
        --clean)
            DO_CLEAN=1
            shift
            ;;
        --fmt-dir)
            FMT_DIR="${2:?--fmt-dir 需要一个目录}"
            shift 2
            ;;
        -D)
            CMAKE_ARGS+=("${2:?-D 需要一个 NAME=VALUE}")
            shift 2
            ;;
        -D*)
            CMAKE_ARGS+=("${1#-D}")
            shift
            ;;
        --cmake-arg)
            CMAKE_ARGS+=("${2:?--cmake-arg 需要一个 NAME=VALUE}")
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
        *) usage >&2; echo "未知参数: $1" >&2; exit 2 ;;
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

case "${BUILD_TYPE}" in
    Debug|Release|RelWithDebInfo|MinSizeRel) ;;
    *) echo "[xcode] 不支持的构建类型: ${BUILD_TYPE}" >&2; exit 2 ;;
esac

if [ "${DO_CLEAN}" -eq 1 ] && [ -d "${BUILD_DIR}" ]; then
    echo "[xcode] 清理工程目录: ${BUILD_DIR}"
    rm -rf -- "${BUILD_DIR}"
fi

if [ -n "${FMT_DIR}" ] && [ ! -f "${FMT_DIR}/CMakeLists.txt" ]; then
    echo "[xcode] --fmt-dir 下没有 CMakeLists.txt: ${FMT_DIR}" >&2
    exit 2
fi

PROJECT_PATH="${BUILD_DIR}/Mession.xcodeproj"
echo "[xcode] 生成工程: ${PROJECT_PATH}"
CONFIGURE_ARGS=(
    -S "${REPO_ROOT}"
    -B "${BUILD_DIR}"
    -G Xcode
    "-DCMAKE_BUILD_TYPE=${BUILD_TYPE}"
)
if [ -n "${FMT_DIR}" ]; then
    CONFIGURE_ARGS+=("-DFMT_DIR=${FMT_DIR}")
fi
if [ "${#CMAKE_ARGS[@]}" -gt 0 ]; then
    for CMAKE_ARG in "${CMAKE_ARGS[@]}"; do
        case "${CMAKE_ARG}" in
            *=*) CONFIGURE_ARGS+=("-D${CMAKE_ARG}") ;;
            *) echo "[xcode] CMake 变量要写成 NAME=VALUE，收到: ${CMAKE_ARG}" >&2; exit 2 ;;
        esac
    done
fi

cmake "${CONFIGURE_ARGS[@]}"

if [ ! -d "${PROJECT_PATH}" ]; then
    echo "[xcode] 配置命令已结束，但未找到 Xcode 工程: ${PROJECT_PATH}" >&2
    echo "[xcode] 请使用 --clean 重试；若仍失败，请提供上方 CMake 输出。" >&2
    exit 1
fi

echo "[xcode] 工程已生成: ${PROJECT_PATH}"
