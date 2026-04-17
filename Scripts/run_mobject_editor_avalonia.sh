#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SERVICE_URL="http://127.0.0.1:18081/api/status"
PROJECT_PATH="$ROOT_DIR/Source/Tools/MObjectEditorAvalonia/MObjectEditorAvalonia.csproj"
SERVICE_BIN="$ROOT_DIR/Bin/MObjectEditorService"

if ! command -v dotnet >/dev/null 2>&1; then
  echo "dotnet SDK 未安装，无法启动 Avalonia 编辑器。" >&2
  exit 1
fi

if ! curl -fsS "$SERVICE_URL" >/dev/null 2>&1; then
  if [[ ! -x "$SERVICE_BIN" ]]; then
    echo "未找到 $SERVICE_BIN，请先编译 MObjectEditorService。" >&2
    exit 1
  fi

  echo "启动本地 MObjectEditorService ..."
  "$SERVICE_BIN" --serve --port 18081 >/tmp/mobject_editor_service.log 2>&1 &
  sleep 1
fi

echo "启动 Avalonia 编辑器 ..."
exec dotnet run --project "$PROJECT_PATH"
