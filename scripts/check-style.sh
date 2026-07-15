#!/usr/bin/env bash
# scripts/check-style.sh - CI 用的 clang-format 检查脚本
# Spec: Docs/superpowers/specs/2026-07-14-coding-style/design.md
#
# 退出码:
#   0 - 全部合规
#   1 - clang-format 缺失或检查失败
#   2 - 用法错误

set -uo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO_ROOT"

# --- Reflected-ABI guard (Q2: machine-enforced) ---
# 反射字段名(`MPROPERTY()` 后紧跟的字段名)是公开 ABI.
# 任何 +/- 行触及 ABI 表中的字段名 → 视为破坏 ABI → 阻断 CI.
# 当前 ABI 列表手维护,后续 PR 可扩展.
ABI_HEADERS=(
    "Source/Common/Net/ServiceDiscovery/Endpoint.h"
    "Source/Servers/EchoService/EchoService.h"
)
ABI_FIELDS=(
    # Endpoint.h
    "EServerType" "ServerType"
    "ServerId"
    "Address" "Port"
    "PublisherCount" "PublisherServerType" "PublisherServerId"
    "Topic" "Tick" "Endpoints"
    "Ack"
    # EchoService.h
    "SEchoServiceConfig" "InstId" "ActorCount" "ListenPort" "ServiceName"
    "FEchoRequest" "Payload"
    "FEchoResponse" "ReplyPayload" "EchoActorId"
)
if git rev-parse --verify HEAD >/dev/null 2>&1; then
    ABIPATTERN='\b('"$(IFS='|'; echo "${ABI_FIELDS[*]}")"')\b'
    for HDR in "${ABI_HEADERS[@]}"; do
        if [ -f "$HDR" ]; then
            git diff --unified=0 HEAD -- "$HDR" 2>/dev/null \
            | grep -E '^[-+]\s*[A-Za-z]' \
            | grep -E "$ABIPATTERN" \
            | grep -v '^[-+]{3}' \
            | grep -vE '^[-+]\s*//' > /tmp/abi-diff-$$.txt || true
            if [ -s /tmp/abi-diff-$$.txt ]; then
                echo "[check-style] ABI violation in $HDR:" >&2
                cat /tmp/abi-diff-$$.txt >&2
                rm -f /tmp/abi-diff-$$.txt
                exit 1
            fi
        fi
    done
fi
rm -f /tmp/abi-diff-$$.txt
# --- ABI guard END ---

if ! command -v clang-format >/dev/null 2>&1; then
    echo "[check-style] clang-format not found. Install with:" >&2
    echo "  apt-get install clang-format   # Debian/Ubuntu" >&2
    echo "  brew install clang-format     # macOS" >&2
    echo "  pip install clang-format      # via pip" >&2
    exit 1
fi

# 只检查 Source/Protocol/Tests,跳过 Build/Generated
FILES=$(find Source Protocol Tests \
    -type f \( -name '*.h' -o -name '*.hpp' -o -name '*.cpp' -o -name '*.cc' -o -name '*.c' \) \
    2>/dev/null | sort)

if [ -z "$FILES" ]; then
    echo "[check-style] no C++ files found under Source/ Protocol/ Tests/"
    exit 0
fi

echo "[check-style] checking $(echo "$FILES" | wc -l) files with clang-format..."

# --dry-run --Werror - 有违规就把退出码拉成非零;xargs 把大量文件分批
echo "$FILES" | xargs -n 50 clang-format --dry-run --Werror --style=file

STATUS=$?

if [ $STATUS -ne 0 ]; then
    echo "" >&2
    echo "[check-style] FAIL - some files need formatting." >&2
    echo "[check-style] fix locally with:" >&2
    echo "  find Source Protocol Tests \\" >&2
    echo "      -type f \\( -name '*.h' -o -name '*.cpp' \\) \\" >&2
    echo "      -exec clang-format -i {} \;" >&2
    exit $STATUS
fi

echo "[check-style] OK - all files conform to .clang-format"
exit 0
