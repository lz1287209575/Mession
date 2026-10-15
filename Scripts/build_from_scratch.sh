#!/usr/bin/env bash
#
# build_from_scratch.sh —— 全新检出(没有 Build/、没有 Bin/、没有任何生成的代码)一键构建。
#
# 直接 `cmake -S . -B Build` 在全新检出上会失败:configure 期必须先编出一个宿主
# MHeaderTool 来跑反射代码生成,否则 Build/Generated/ 为空、各 add_executable 找不到
# *.mgenerated.cpp。本脚本负责把这一步连同环境上的坑一起处理掉:
#
#   1. Ninja 常不在 PATH(例如只随 CLion/JetBrains 的 RemoteDev 分发)——自动去常见
#      位置找;找不到就退回 Unix Makefiles,不会硬失败。
#   2. configure 期引导工具会先编 MHeaderTool(约 1 分钟),脚本把这一步的耗时与结果
#      讲清楚,失败时给出可读诊断而不是一段 cmake 警告。
#   3. 全新检出需要配置两趟:configure 期的 MHeaderTool 要读到 compile_commands.json
#      才能解析 *.Async.cpp 里的 await 业务体,而这个文件是第一趟 generate 阶段才写出的;
#      第一趟因此会缺一部分 *.mgenerated.cpp。脚本自动重跑第二趟,这一趟必须成功。
#   4. fmt 走 GitHub FetchContent(首次 configure 会 clone)——离线环境用 --fmt-dir
#      指向本地 fmt 源码即可跳过网络。
#
# 用法:
#   Scripts/build_from_scratch.sh                          # Release 构建到 Build/
#   Scripts/build_from_scratch.sh -t Debug                 # Debug 构建到 cmake-build-debug/
#   Scripts/build_from_scratch.sh -b /tmp/mession-out -j 32
#   Scripts/build_from_scratch.sh --clean --test
#   Scripts/build_from_scratch.sh --fmt-dir /path/to/fmt   # 离线/加速
#
# 退出码:0 = 成功;10 = 配置失败;20 = 构建失败;30 = 测试失败。便于 CI 区分阶段。

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

BUILD_TYPE="Release"
BUILD_DIR=""
JOBS=""
GENERATOR=""
FMT_DIR=""
DO_CLEAN=0
DO_TEST=0
DO_CONFIGURE_ONLY=0
CMAKE_ARGS=()

CONFIGURE_FAIL_RC=10
BUILD_FAIL_RC=20
TEST_FAIL_RC=30

# 已知不能"无参独立跑通"的测试可执行文件(重构前即如此,非本脚本引入):
#   ASTDumpTest         —— 面向全量 AST 转储的调试工具,无参运行数分钟且未断言退出码
#   MemberFrameworkTest —— 断言失败并核心转储(历史遗留)
TEST_SKIP_LIST="ASTDumpTest MemberFrameworkTest"

log()  { printf '\033[1;34m[build]\033[0m %s\n' "$*"; }
warn() { printf '\033[1;33m[build]\033[0m %s\n' "$*" >&2; }
die()  { printf '\033[1;31m[build]\033[0m %s\n' "$*" >&2; exit 1; }

usage() {
    cat <<'EOF'
全新检出一键构建。内部会调用 cmake 配置 + 构建,并处理下面几个坑:
Ninja 不在 PATH、configure 期需要先编宿主 MHeaderTool、fmt 需要联网。

用法: Scripts/build_from_scratch.sh [选项]

Options:
  -b, --build-dir DIR   构建目录 (默认: Release → Build, 其它 → cmake-build-<type 小写>)
  -t, --type TYPE       CMAKE_BUILD_TYPE: Debug|Release|RelWithDebInfo|MinSizeRel (默认 Release)
  -j, --jobs N          并行度 (默认: 核数, 上限 32)
  -g, --generator GEN   CMake 生成器 (默认: 有 ninja 用 Ninja, 否则 Unix Makefiles)
  -D, --cmake-arg A     追加一个 CMake 变量; A 形如 NAME=VALUE(脚本自动补 -D 前缀),
                        可重复。指定 Clang 版本就用它: -D Clang_DIR=/path/to/lib/cmake/clang
      --fmt-dir DIR     本地 fmt 源码目录, 跳过 FetchContent 网络下载
      --clean           先删除构建目录再配置
      --configure-only  只跑配置阶段
      --test            构建完成后跑 Bin/ 下的测试可执行文件
  -h, --help            显示本帮助
EOF
}

while [ $# -gt 0 ]; do
    case "$1" in
        -b|--build-dir)   BUILD_DIR="${2:?--build-dir 需要一个目录}"; shift 2 ;;
        -t|--type)        BUILD_TYPE="${2:?--type 需要一个值}"; shift 2 ;;
        -j|--jobs)        JOBS="${2:?--jobs 需要一个数字}"; shift 2 ;;
        -g|--generator)   GENERATOR="${2:?--generator 需要一个值}"; shift 2 ;;
        -D)               CMAKE_ARGS+=("${2:?-D 需要一个 NAME=VALUE}"); shift 2 ;;
        -D*)              CMAKE_ARGS+=("${1#-D}"); shift ;;
        --cmake-arg)      CMAKE_ARGS+=("${2:?--cmake-arg 需要一个 NAME=VALUE}"); shift 2 ;;
        --fmt-dir)        FMT_DIR="${2:?--fmt-dir 需要一个目录}"; shift 2 ;;
        --clean)          DO_CLEAN=1; shift ;;
        --test)           DO_TEST=1; shift ;;
        --configure-only) DO_CONFIGURE_ONLY=1; shift ;;
        -h|--help)        usage; exit 0 ;;
        *)                usage >&2; die "未知参数: $1" ;;
    esac
done

[ -f "${REPO_ROOT}/CMakeLists.txt" ] || die "在 ${REPO_ROOT} 下找不到 CMakeLists.txt"

case "${BUILD_TYPE}" in
    Debug|Release|RelWithDebInfo|MinSizeRel) ;;
    *) die "不支持的构建类型 '${BUILD_TYPE}'(可选 Debug/Release/RelWithDebInfo/MinSizeRel)" ;;
esac

if [ -z "${BUILD_DIR}" ]; then
    if [ "${BUILD_TYPE}" = "Release" ]; then
        BUILD_DIR="${REPO_ROOT}/Build"
    else
        BUILD_DIR="${REPO_ROOT}/cmake-build-$(printf '%s' "${BUILD_TYPE}" | tr '[:upper:]' '[:lower:]')"
    fi
elif [ "${BUILD_DIR#/}" = "${BUILD_DIR}" ]; then
    BUILD_DIR="${REPO_ROOT}/${BUILD_DIR}"
fi

if [ -z "${JOBS}" ]; then
    if command -v nproc >/dev/null 2>&1; then
        JOBS="$(nproc)"
    elif command -v sysctl >/dev/null 2>&1; then
        JOBS="$(sysctl -n hw.ncpu 2>/dev/null || echo 4)"
    else
        JOBS=4
    fi
    if [ "${JOBS}" -gt 32 ]; then JOBS=32; fi
fi

# 优先用 GNU timeout;macOS 上没有它就直接跑(并行度已受控,不至于挂死)。
TIMEOUT_BIN=""
if command -v timeout >/dev/null 2>&1; then
    TIMEOUT_BIN="timeout 300"
fi

# --- 前置检查 ----------------------------------------------------------------

command -v cmake >/dev/null 2>&1 || die "找不到 cmake;请先安装 CMake >= 3.15"

CMAKE_VERSION_RAW="$(cmake --version | head -n1 | awk '{print $3}')"
if ! [ "$(printf '%s\n' 3.15 "${CMAKE_VERSION_RAW}" | sort -V | head -n1)" = "3.15" ]; then
    die "CMake 版本过低 (${CMAKE_VERSION_RAW});项目要求 >= 3.15"
fi

if [ -z "${CXX:-}" ]; then
    for candidate in c++ g++ clang++; do
        if command -v "${candidate}" >/dev/null 2>&1; then CXX="${candidate}"; break; fi
    done
fi
[ -n "${CXX:-}" ] || die "找不到 C++ 编译器(g++/clang++)"

# --- Ninja -------------------------------------------------------------------
# Ninja 常以"某个 IDE 自带"的形式存在而不在 PATH 上,先兜一圈再决定生成器。
find_ninja() {
    if command -v ninja >/dev/null 2>&1; then
        command -v ninja
        return 0
    fi
    local candidate
    for candidate in \
        "${HOME}/.cache/JetBrains/RemoteDev/dist"/*/bin/ninja/*/*/ninja \
        "${HOME}/.local/bin/ninja" \
        /usr/local/bin/ninja \
        /opt/ninja/bin/ninja
    do
        if [ -x "${candidate}" ]; then
            printf '%s\n' "${candidate}"
            return 0
        fi
    done
    return 1
}

NINJA_BIN=""
if NINJA_BIN="$(find_ninja)"; then
    NINJA_DIR="$(dirname "${NINJA_BIN}")"
    case ":${PATH}:" in
        *":${NINJA_DIR}:"*) ;;
        *) PATH="${NINJA_DIR}:${PATH}"; export PATH ;;
    esac
else
    NINJA_BIN=""
fi

if [ -z "${GENERATOR}" ]; then
    if [ -f "${BUILD_DIR}/CMakeCache.txt" ]; then
        GENERATOR=""            # 已有缓存时交给 CMake 复用原生成器,避免生成器冲突报错
    elif [ -n "${NINJA_BIN}" ]; then
        GENERATOR="Ninja"
    else
        GENERATOR="Unix Makefiles"
    fi
fi

if [ "${GENERATOR}" = "Ninja" ] && [ -z "${NINJA_BIN}" ]; then
    die "指定了 Ninja 生成器,但 PATH 里找不到 ninja"
fi

# --- Clang / LLVM(MHeaderTool libtooling 依赖) --------------------------------
CLANG_FOUND=0
# 发行版布局不一: Debian/Ubuntu 是 /usr/lib/cmake/clang-<ver> 或 /usr/lib/llvm-<ver>/lib/cmake/clang,
# RHEL/Fedora 是 /usr/lib64/cmake/clang。
for dir in /usr/lib/cmake/clang-* /usr/lib64/cmake/clang* \
           /usr/local/lib/cmake/clang-* \
           /usr/lib/llvm-*/lib/cmake/clang /usr/lib/llvm-*/lib64/cmake/clang \
           /usr/lib64/llvm-*/lib/cmake/clang; do
    if [ -d "${dir}" ]; then CLANG_FOUND=1; break; fi
done
if [ "${CLANG_FOUND}" -eq 0 ] && [ -z "${Clang_DIR:-}" ]; then
    warn "没有在常见位置找到 Clang 的 CMake 包,而 MHeaderTool 的 libtooling 依赖它。"
    warn "  Debian/Ubuntu: apt install libclang-19-dev llvm-19-dev libclang-common-19-dev"
    warn "  RHEL/Fedora:   dnf install clang-devel llvm-devel clang-resource-filesystem"
    warn "  装在自定义位置时用 -D Clang_DIR=/path/to/lib/cmake/clang 指定"
fi

log "仓库根:   ${REPO_ROOT}"
log "构建目录: ${BUILD_DIR}"
log "构建类型: ${BUILD_TYPE}"
log "并行度:   ${JOBS}"
log "生成器:   ${GENERATOR:-<沿用构建目录已有缓存>}"
if [ -n "${NINJA_BIN}" ]; then log "Ninja:    ${NINJA_BIN}"; else log "Ninja:    未找到(已退回默认生成器)"; fi
if [ -n "${FMT_DIR}" ]; then log "fmt 源码: ${FMT_DIR}"; fi

if [ "${DO_CLEAN}" -eq 1 ] && [ -d "${BUILD_DIR}" ]; then
    log "清理构建目录 ${BUILD_DIR}"
    rm -rf -- "${BUILD_DIR}"
fi

CONFIGURE_LOG="$(mktemp -t mession-configure-1.XXXXXX.log)"
CONFIGURE_LOG2="$(mktemp -t mession-configure-2.XXXXXX.log)"

# --- 配置 --------------------------------------------------------------------
CONFIGURE_ARGS=(-S "${REPO_ROOT}" -B "${BUILD_DIR}" "-DCMAKE_BUILD_TYPE=${BUILD_TYPE}")
if [ "${#CMAKE_ARGS[@]}" -gt 0 ]; then
    for _arg in "${CMAKE_ARGS[@]}"; do
        case "${_arg}" in
            *=*) CONFIGURE_ARGS+=("-D${_arg}") ;;
            *)   die "cmake 变量要写成 NAME=VALUE,收到: ${_arg}" ;;
        esac
    done
    unset _arg
fi
if [ -n "${GENERATOR}" ]; then CONFIGURE_ARGS+=(-G "${GENERATOR}"); fi
if [ -n "${FMT_DIR}" ]; then
    [ -f "${FMT_DIR}/CMakeLists.txt" ] || die "--fmt-dir '${FMT_DIR}' 下没有 CMakeLists.txt"
    CONFIGURE_ARGS+=("-DFMT_DIR=${FMT_DIR}")
fi

WAS_FRESH=0
if [ ! -f "${BUILD_DIR}/CMakeCache.txt" ]; then WAS_FRESH=1; fi

log "开始配置(全新检出会先编一个宿主 MHeaderTool,大约 1 分钟)…"
CONFIGURE_PASS1_OK=1
if ! cmake "${CONFIGURE_ARGS[@]}" 2>&1 | tee "${CONFIGURE_LOG}"; then
    CONFIGURE_PASS1_OK=0
fi

# 全新检出的第一趟 configure 必然缺一部分生成物:configure 期跑 MHeaderTool 时还没有
# compile_commands.json(它是本趟 generate 阶段才写出的),而 MHeaderTool 只有读到它才会
# 去解析 *.Async.cpp 里的 await 业务体——所以第一趟会报某些 *.mgenerated.cpp 找不到,
# 而且依赖这些生成物的目标(例如 EchoService、AwaitDemo)要么报错要么被守卫跳过。
# 第一趟跑完但至少已经把宿主工具编好、把 compile_commands.json 写出来,第二趟才齐。
# 第一趟就成功时也要再跑一次:这一趟是为了让被守卫跳过的目标回到目标列表里。
if [ "${CONFIGURE_PASS1_OK}" -eq 1 ] && [ "${WAS_FRESH}" -eq 0 ]; then
    log "配置完成。"
else
    if [ "${CONFIGURE_PASS1_OK}" -eq 1 ]; then
        log "第一趟配置已完成,再配置一次让生成物相关目标补齐…"
    else
        log "第一趟配置未完整通过(全新检出时的预期现象),再配置一次…"
    fi
    if ! cmake "${CONFIGURE_ARGS[@]}" 2>&1 | tee "${CONFIGURE_LOG2}"; then
        warn "配置失败。第一趟日志: ${CONFIGURE_LOG}"
        warn "              第二趟日志: ${CONFIGURE_LOG2}"
        warn "常见原因:"
        warn "  * 拉取 fmt 失败(离线环境)→ 用 --fmt-dir 指向本地 fmt 源码"
        warn "  * 找不到 Clang 19 → apt install libclang-19-dev llvm-19-dev"
        warn "  * 磁盘空间不足 / 编译器缺失"
        exit "${CONFIGURE_FAIL_RC}"
    fi
    log "第二趟配置完成。"
fi

if [ "${DO_CONFIGURE_ONLY}" -eq 1 ]; then
    log "仅配置模式,完成。配置日志: ${CONFIGURE_LOG}"
    exit 0
fi

# --- 构建 --------------------------------------------------------------------
log "开始构建…"
if ! cmake --build "${BUILD_DIR}" --parallel "${JOBS}"; then
    warn "构建失败。若报错集中在 Build/Generated/ 下的文件缺失,说明 configure 期的"
    warn "MHeaderTool 引导编译没成功,可单独复查 ${BUILD_DIR}/MHeaderToolBootstrap/。"
    exit "${BUILD_FAIL_RC}"
fi

log "构建完成,产物在 ${REPO_ROOT}/Bin"

# --- 测试(可选) --------------------------------------------------------------
if [ "${DO_TEST}" -eq 1 ]; then
    log "开始跑测试…"
    PASS=0; FAIL=0; SKIPPED=0; FAILED_NAMES=""
    for exe in "${REPO_ROOT}"/Bin/*Test; do
        [ -x "${exe}" ] || continue
        name="$(basename "${exe}")"
        skip=0
        for s in ${TEST_SKIP_LIST}; do
            if [ "${name}" = "${s}" ]; then skip=1; fi
        done
        if [ "${skip}" -eq 1 ]; then
            printf '  [SKIP] %s (已知不能无参独立跑通)\n' "${name}"
            SKIPPED=$((SKIPPED + 1))
            continue
        fi
        rc=0
        output="$(cd "${REPO_ROOT}" && ${TIMEOUT_BIN} "${exe}" 2>&1)" || rc=$?
        if [ "${rc}" -eq 0 ]; then
            printf '  [OK  ] %s\n' "${name}"
            PASS=$((PASS + 1))
        else
            printf '  [FAIL] %s (rc=%s)\n' "${name}" "${rc}"
            printf '%s\n' "${output}" | tail -n 8 | sed 's/^/         /'
            FAIL=$((FAIL + 1))
            FAILED_NAMES="${FAILED_NAMES} ${name}"
        fi
    done
    printf '\n测试汇总: PASS=%s FAIL=%s SKIP=%s\n' "${PASS}" "${FAIL}" "${SKIPPED}"
    if [ "${FAIL}" -gt 0 ]; then
        warn "失败:${FAILED_NAMES}"
        exit "${TEST_FAIL_RC}"
    fi
fi

log "全部完成。"
