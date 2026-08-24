#pragma once

#include <filesystem>
#include <vector>

namespace MHeaderTool {

    struct SParsedClass;
    struct SParsedProperty;

    // ScriptBindEmitter — 把 MHeaderTool 解析出的反射类 emit 成 Lua 注册表 + Teal record
    // 输出:
    //   <OutDir>/<Class>.lua            (Lua 注册表 + M.InvokeStatic 包装)
    //   <OutDir>/<Class>.d.tl           (Teal record 镜像 MSTRUCT / MPROPERTY)
    // 后续扩 Python / TS / C# 时,加 PythonBindEmitter 等即可,共享同一 Transport 入口
    class ScriptBindEmitter {
        public:
        static void Run(const std::filesystem::path& OutDir, const TVector<SParsedClass>& Classes);
    };

} // namespace MHeaderTool