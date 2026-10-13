/**
 * @file MObjectAssetCompiler.h
 * @brief MObjectAssetCompiler —— 资产编译入口:对象/JSON → .mobj 字节或文件。
 *
 * @detail 本命名空间是 MObjectAssetBinary 的薄封装,额外提供
 *   "JSON 文件 → 输出文件"的整条流水线,供构建脚本与 CLI 复用;
 *   自身不持有状态,全部接口都是纯函数。
 */
#pragma once

#include "Common/Runtime/MLib.h"

class MObject;

namespace MObjectAssetCompiler {
    /**
     * @brief CompileBytesFromObject - 对象 → .mobj 字节。
     * @param RootObject 根对象(只带 Asset 域属性参与编码)
     * @param OutBytes 输出(含文件头)
     * @param OutError 失败原因,可空
     * @return true 成功
     */
    bool CompileBytesFromObject(const MObject* RootObject, TByteArray& OutBytes, MString* OutError = nullptr);
    /**
     * @brief CompileBytesFromJson - 资产 JSON → .mobj 字节。
     * @param JsonText 资产 JSON 文本
     * @param OutBytes 输出(含文件头)
     * @param OutError 失败原因,可空
     * @return true 成功
     */
    bool CompileBytesFromJson(const MString& JsonText, TByteArray& OutBytes, MString* OutError = nullptr);

    /**
     * @brief CompileFileFromJson - 读取 JSON 文件并写出 .mobj 文件。
     * @param JsonFilePath 输入文件路径(按二进制整体读入)
     * @param OutputFilePath 输出文件路径(存在则截断重写)
     * @param OutError 失败原因,可空
     * @return true 成功
     *
     * @detail 三个步骤(读、编译、写)任一失败立即返回;写入阶段失败时输出文件
     *   可能已被创建或截断,调用方不要把它当成有效产物。
     */
    bool CompileFileFromJson(const MString& JsonFilePath, const MString& OutputFilePath, MString* OutError = nullptr);
} // namespace MObjectAssetCompiler
