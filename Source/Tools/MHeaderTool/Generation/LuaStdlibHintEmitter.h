/**
 * @file LuaStdlibHintEmitter.h
 * @brief LuaStdlibHintEmitter —— 从 MLua*.cpp 的 `/// @lua-*` 注解生成 Mession.lua 与 Mession.d.tl。
 *
 * @detail 真理源是 C++ 侧:MLuaXxx::Install 的注册代码 + 紧邻的 `@lua-*` 注释块。
 *         本工具把注解抽出来,替代手工维护的
 *         `Source/Common/Script/Lua/Resources/Mession.{lua,d.tl}` —— 手写副本必然会漂移。
 *         无状态:不缓存、不增量,每次运行全量重扫、全量重写两个文件。
 */
#pragma once

#include <filesystem>
#include <vector>

#include "Common/Runtime/MLib.h"

namespace MHeaderTool {

    // 从 MLua*.cpp 注解块里解析出的一个 cfunction 形参。
    struct SLuaParam {
        MString Name;     // "msg", "v", "size"
        MString TealType; // "string", "MScalar", "MVector"
        bool    bOptional = false;
        bool    bSelf     = false;
    };

    /**
     * @brief 一个 cfunction 的返回值类型(多返回值时为多个 Teal 类型)。
     */
    struct SLuaReturnType {
        TVector<MString> TealTypes;       // 多返回值时按顺序排列,如 {"any","string"}
        bool             bIsVoid = false; // 是否无返回值(@lua-return 缺省)
    };

    /**
     * @brief 一条 stdlib 绑定:Lua 侧命名空间/函数名 与 C++ 侧实现名 的对应关系。
     */
    struct SLuaStdlibBinding {
        MString            NamespaceName;     // Lua 命名空间,如 "Vector" / "Log" / "RPC"
        MString            FunctionName;      // Lua 函数名,如 "new" / "get" / "info"
        MString            CppName;           // 对应的 C++ cfunction 名,如 "FactoryNew"
        bool               bIsMethod = false; // true = 以 `obj:fn(...)` 形式调用(有 self 参数)
        TVector<SLuaParam> Params;            // 形参,首个可能是 self
        SLuaReturnType     Return;            // 返回值类型
        MString            SourceFile;        // 注解所在文件名(只有文件名,不含目录 —— 见 ParseFile)
        uint32             SourceLine = 0;    // 注解块起始行
    };

    /**
     * @brief Lua 标准库提示生成器(无状态,全静态方法)。
     *
     * @detail C++ 端 `MLuaXxx::Install` 的注册代码 + 紧邻的 `@lua-*` 注解是唯一真理源;
     *         本类只做"注解 → .lua/.d.tl"的单向投影,不反向校验 C++ 实现是否真的存在。
     */
    class LuaStdlibHintEmitter {
        public:
        /**
         * @brief 主入口:扫注解 → 生成两个文件(覆盖式写入)。
         * @param SourceRoot  项目 Source/ 根目录,内部拼出 Common/Script/Lua/MLua*.cpp
         * @param LuaOutPath  输出的 .lua 路径
         * @param TealOutPath 输出的 .d.tl 路径
         *
         * @detail 两个输出文件都是**直接覆盖**,不做增量。会在 stderr 打印一行绑定条数摘要。
         */
        static void Run(const std::filesystem::path& SourceRoot, const std::filesystem::path& LuaOutPath, const std::filesystem::path& TealOutPath);

        /**
         * @brief 只做"扫描 + 解析",不写文件(供测试使用)。
         * @param SourceRoot   项目 Source/ 根目录
         * @param OutBindings  [out] 追加解析结果(不清理旧内容)
         * @return 本次解析出的绑定条数
         *
         * @detail 只扫 Common/Script/Lua 这一层目录(非递归),且只认文件名以 "MLua" 开头的 .cpp。
         */
        static int ExtractBindings(const std::filesystem::path& SourceRoot, TVector<SLuaStdlibBinding>& OutBindings);
    };

} // namespace MHeaderTool