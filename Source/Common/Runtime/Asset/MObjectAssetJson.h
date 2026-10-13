/**
 * @file MObjectAssetJson.h
 * @brief MObjectAssetJson —— 资产对象的 JSON 导出/导入(也是 .mobj JsonBridge 负载的载体)。
 *
 * @detail 导出只写 Asset 域属性,并附加两个保留键供导入还原身份:
 *   - "$class":类名,导入时经 MObject::FindClass 反查反射类
 *   - "$name":对象名(仅非空时写)
 *   导入会跳过所有以 '$' 开头的键;MSTRUCT 级的
 *   ExportStructToJsonValue/ImportStructFromJsonValue/
 *   ImportAssetObjectFieldsFromJsonValue 三个入口在
 *   Reflect/ReflectionPropertyTemplates.inl 中声明。
 */
#pragma once

#include "Common/Runtime/Json.h"

class MObject;

namespace MObjectAssetJson {
    /**
     * @brief ExportAssetObjectToJsonValue - 对象 → JSON 值(含 $class/$name)。
     * @param Object 待导出对象;空则失败
     * @param OutValue 输出 JSON 节点
     * @param OutError 失败原因,可空
     * @return true 成功
     */
    bool ExportAssetObjectToJsonValue(const MObject* Object, MJsonValue& OutValue, MString* OutError = nullptr);
    /**
     * @brief ExportAssetObjectToJson - 对象 → 带缩进的 JSON 文本。
     * @param Object 待导出对象
     * @param OutJson 输出文本(两空格缩进)
     * @param OutError 失败原因,可空
     * @return true 成功
     */
    bool ExportAssetObjectToJson(const MObject* Object, MString& OutJson, MString* OutError = nullptr);

    /**
     * @brief ImportAssetObjectFromJsonValue - 由 JSON 值实例化对象。
     * @param InValue 根节点,必须含字符串型 "$class"
     * @param Outer 非空时设为新对象的 Outer,否则加入 RootSet
     * @param OutError 失败原因,可空
     * @return 新对象,调用方持有;失败返回 nullptr
     *
     * @detail 属性按 "$class" 指定的类绑定,未知属性会导致整体失败(不是忽略);
     *   失败路径会析构已创建的对象。
     */
    MObject* ImportAssetObjectFromJsonValue(const MJsonValue& InValue, MObject* Outer = nullptr, MString* OutError = nullptr);
    /**
     * @brief ImportAssetObjectFromJson - 解析 JSON 文本后转调 JsonValue 版本。
     * @param InJson JSON 文本
     * @param Outer 非空时设为新对象的 Outer,否则加入 RootSet
     * @param OutError 失败原因,可空
     * @return 新对象,调用方持有;失败返回 nullptr
     */
    MObject* ImportAssetObjectFromJson(const MString& InJson, MObject* Outer = nullptr, MString* OutError = nullptr);
} // namespace MObjectAssetJson
