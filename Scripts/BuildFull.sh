#!/usr/bin/env bash
#
# BuildFull.sh —— 删除构建目录后重新配置并编译。
#
# 这是 build_from_scratch.sh 的全量构建入口：它保留后者对首次检出、
# MHeaderTool 引导、fmt 与测试选项的完整处理，同时强制 --clean。
#
# 示例：
#   Scripts/BuildFull.sh
#   Scripts/BuildFull.sh -t Debug -b /tmp/mession-debug -j 16 --test

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

exec "${SCRIPT_DIR}/build_from_scratch.sh" --clean "$@"
