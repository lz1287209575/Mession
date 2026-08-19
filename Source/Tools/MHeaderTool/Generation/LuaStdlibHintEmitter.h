#pragma once

#include <filesystem>
#include <vector>

#include "Common/Runtime/MLib.h"

namespace MHeaderTool {

// One cfunction binding discovered from a MLua*.cpp annotation block.
struct SLuaParam {
    MString Name;       // "msg", "v", "size"
    MString TealType;   // "string", "MScalar", "MVector"
    bool    bOptional = false;
    bool    bSelf     = false;
};

struct SLuaReturnType {
    TVector<MString> TealTypes; // ["any","string"] for multi-return
    bool bIsVoid = false;
};

struct SLuaStdlibBinding {
    MString             NamespaceName;   // "Vector", "Log", "RPC", "Time", "Id"
    MString             FunctionName;    // "new", "get", "info", "call"
    MString             CppName;         // "FactoryNew", "MethodGet", "RpcCall"
    bool                bIsMethod        = false;
    TVector<SLuaParam>  Params;
    SLuaReturnType      Return;
    MString             SourceFile;      // relative path
    uint32              SourceLine = 0;
};

// LuaStdlibHintEmitter — reads MLua*.cpp annotations, emits Mession.lua + Mession.d.tl
//
// 替代手工维护的 Source/Common/Script/Lua/Resources/Mession.{lua,d.tl}。
// C++ 端 MLua*::Install 注册 + @lua-* 注解是唯一真理源。
class LuaStdlibHintEmitter {
public:
    // Main entry: scan Source/Common/Script/Lua/MLua*.cpp, extract bindings, emit two files.
    // SourceRoot 是项目 Source/ 根,内部拼出 MLua*.cpp 路径
    static void Run(const std::filesystem::path& SourceRoot,
                    const std::filesystem::path& LuaOutPath,
                    const std::filesystem::path& TealOutPath);

    // 测试可见:仅做解析 + emit,不写文件。返回 bindings 数量。
    static int ExtractBindings(const std::filesystem::path& SourceRoot,
                               TVector<SLuaStdlibBinding>& OutBindings);
};

} // namespace MHeaderTool