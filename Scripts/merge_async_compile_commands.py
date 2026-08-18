#!/usr/bin/env python3
"""merge_async_compile_commands.py — 把 *.Async.cpp 以 codegen 视角命令注入 compile_commands.json。

背景:*.Async.cpp 是 codegen 专用源(业务编译不编译,见
Docs/superpowers/specs/2026-07-24-cpp17-async-await.md §7.2.1),
CMake 不会为它生成编译命令 → 基于 compile_commands 的 IDE(clangd/VS Code/CLion)
不索引该文件,无代码补全/跳转/诊断。

本脚本扫描 SourceRoot 下所有 *.Async.cpp,为每个生成与
MHeaderTool::MCodegenSourceCompilationDatabase 一致的 fallback 命令
(-fsyntax-only -x c++ -I<SourceRoot> -DMESSION_AWAIT_CODEGEN_SOURCE <file>)
并合并进 Build/compile_commands.json(已存在的 file 条目跳过)。

用法:python3 Scripts/merge_async_compile_commands.py [BuildDir] [SourceRoot]
默认:BuildDir=Build, SourceRoot=Source(相对仓库根)。
"""

import json
import os
import sys


def collect_async_sources(source_root: str):
    out = []
    for dirpath, dirnames, filenames in os.walk(source_root):
        dirnames[:] = [d for d in dirnames if d not in ("Build", "Tests", "Tools", ".git")]
        for fn in filenames:
            if fn.endswith(".Async.cpp"):
                out.append(os.path.join(dirpath, fn))
    return sorted(out)


def main():
    repo_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    build_dir = sys.argv[1] if len(sys.argv) > 1 else os.path.join(repo_root, "Build")
    source_root = sys.argv[2] if len(sys.argv) > 2 else os.path.join(repo_root, "Source")

    cc_path = os.path.join(build_dir, "compile_commands.json")
    if not os.path.exists(cc_path):
        print(f"error: {cc_path} 不存在(需先 cmake 配置并开启 CMAKE_EXPORT_COMPILE_COMMANDS)")
        sys.exit(1)

    with open(cc_path, "r", encoding="utf-8") as f:
        db = json.load(f)

    existing = {os.path.normpath(e["file"]) for e in db}
    added = 0
    for async_file in collect_async_sources(source_root):
        norm = os.path.normpath(async_file)
        if norm in existing:
            continue
        cmd = (
            "-fsyntax-only -x c++ "
            f"-I{source_root} "
            "-DMESSION_AWAIT_CODEGEN_SOURCE "
            f"{async_file}"
        )
        db.append({"directory": repo_root, "command": cmd, "file": async_file})
        existing.add(norm)
        added += 1

    with open(cc_path, "w", encoding="utf-8") as f:
        json.dump(db, f, indent=2)
    print(f"合并完成:新增 {added} 条 *.Async.cpp 条目(共 {len(db)} 条)")


if __name__ == "__main__":
    main()
