/**
 * @file ScriptBindEmitter.h
 * @brief ScriptBindEmitter —— 把带 `ScriptBind` MFUNCTION 的反射类生成 Lua 注册表与 Teal 类型声明。
 *
 * @detail 产物(每类两份,不是每函数一份):
 *   - `<OutDir>/<Class>.lua`    :Lua 侧的 `M_<Class>` 表,函数体统一转调 `M.InvokeStatic`,
 *                                 运行时的桥接由 C 端 MLuaBridge::Install 提供。
 *   - `<OutDir>/<Class>.d.tl`   :Teal record,镜像 MSTRUCT/MPROPERTY 的字段与方法签名,
 *                                 供 lua-language-server/Teal 做类型检查。
 *   只处理含 ScriptBind 函数的类;不含 ScriptBind 的类不产出任何文件。
 *   C++ 类型 → Teal 类型是**有限映射**,未覆盖的类型退化为 `any`(见 ScriptBindEmitter.cpp)。
 *
 *   后续要扩 Python/TS/C# 时,新增 PythonBindEmitter 等即可,共享同一 Transport 入口。
 */
#pragma once

#include <filesystem>
#include <vector>

namespace MHeaderTool {

    struct SParsedClass;
    struct SParsedProperty;
    /**
     * @brief ScriptBind 绑定代码生成器(无状态,全静态方法)。
     */
    class ScriptBindEmitter {
        public:
        /**
         * @brief 为每个含 ScriptBind 函数的类生成 .lua 与 .d.tl。
         * @param OutDir  输出目录;不存在时自动创建
         * @param Classes 解析出的反射类列表;只读取 Functions/Properties,不修改
         *
         * @detail 每个类覆盖式写两个文件(同名旧文件被直接截断),不做增量/时间戳比较 ——
         *         调用方负责决定要不要重新生成。
         */
        static void Run(const std::filesystem::path& OutDir, const TVector<SParsedClass>& Classes);
    };

} // namespace MHeaderTool