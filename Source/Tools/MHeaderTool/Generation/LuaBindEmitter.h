#pragma once

#include <filesystem>
#include <vector>

namespace MHeaderTool {

struct SParsedClass;
struct SParsedProperty;

// LuaBindEmitter:把 MHeaderTool 解析出的反射类 emit 成 Lua 注册表 + Teal record
// 输出:
//   <OutDir>/<Class>.lua            (Lua 注册表 + InvokeClass / RegisterClass 包装)
//   <OutDir>/<Class>.d.tl           (Teal record 镜像 MSTRUCT / MPROPERTY)
class LuaBindEmitter
{
public:
    static void Run(
        const std::filesystem::path& OutDir,
        const std::vector<SParsedClass>& Classes);
};

} // namespace MHeaderTool