/**
 * @file MObjectAssetJson.cpp
 * @brief MObjectAssetJson 实现:属性集 ↔ JSON 对象,以及自带的缩进序列化。
 *
 * @detail 属性集导出/导入都只覆盖 Asset 域(bAssetOnly=true 用于对象路径,
 *   false 用于 MSTRUCT 路径)。JSON 节点用 TMap 存对象成员,所以导出文本里
 *   键序是字典序而非反射声明序("$class"/"$name" 因 '$' 排在字母前而居首),
 *   这一点与属性声明顺序无关,不宜依赖。
 */
#include "Common/Runtime/Asset/MObjectAssetJson.h"

#include "Common/Runtime/Object/Object.h"
#include "Common/Runtime/Reflect/Reflection.h"

namespace MObjectAssetJson {
    namespace {
        /**
         * @brief 递归收集属性,顺序为父类 → 本类(每层保持注册顺序)。
         * @param ClassMeta 起始类;空则直接返回
         * @param bAssetOnly true 时只收带 Asset 域的属性
         * @param OutProperties 输出(追加,不清空)
         */
        void GatherClassProperties(const MClass* ClassMeta, bool bAssetOnly, TVector<const MProperty*>& OutProperties) {
            if (!ClassMeta) {
                return;
            }

            GatherClassProperties(ClassMeta->GetParentClass(), bAssetOnly, OutProperties);
            for (const MProperty* Prop : ClassMeta->GetProperties()) {
                if (!Prop) {
                    continue;
                }
                if (bAssetOnly && !Prop->HasAnyDomains(EPropertyDomainFlags::Asset)) {
                    continue;
                }
                OutProperties.push_back(Prop);
            }
        }

        /**
         * @brief 把一组属性导出成 JSON object 节点(键 = 属性名)。
         * @param ClassMeta 目标的类/结构反射信息;与 ObjectData 任一为空即失败
         * @param ObjectData 实例(读值)
         * @param bAssetOnly 是否限定 Asset 域
         * @param OutValue 输出节点(会被重置为 Object)
         * @param OutError 失败原因,可空
         * @return true 成功;任一属性导出失败即整体失败
         */
        bool ExportPropertySetToJsonObject(const MClass* ClassMeta, const void* ObjectData, bool bAssetOnly, MJsonValue& OutValue, MString* OutError) {
            if (!ClassMeta || !ObjectData) {
                if (OutError) {
                    *OutError = "asset_json_export_invalid_target";
                }
                return false;
            }

            OutValue      = MJsonValue{};
            OutValue.Type = EJsonType::Object;

            TVector<const MProperty*> Properties;
            GatherClassProperties(ClassMeta, bAssetOnly, Properties);
            for (const MProperty* Prop : Properties) {
                MJsonValue PropertyValue;
                MString    PropertyError;
                if (!Prop->ExportJsonValue(ObjectData, PropertyValue, &PropertyError)) {
                    if (OutError) {
                        *OutError = "asset_json_export_property_failed:" + Prop->Name + ":" + PropertyError;
                    }
                    return false;
                }
                OutValue.ObjectValue[Prop->Name] = std::move(PropertyValue);
            }

            return true;
        }

        /**
         * @brief 把 JSON object 节点里的成员写回一组属性。
         * @param ClassMeta 目标的类/结构反射信息;与 ObjectData 任一为空即失败
         * @param ObjectData 实例(写值)
         * @param InValue 输入节点,必须是 object
         * @param bAssetOnly true 时非 Asset 域成员直接报错(而非忽略)
         * @param OutError 失败原因,可空
         * @return true 成功
         *
         * @detail 语义是"严格模式":未知属性名、非 Asset 域属性、单属性解析失败
         *   都会立即失败并中止,避免半截对象被当成合法结果使用。以 '$' 开头的
         *   键是元数据(如 $class/$name/$type),一律跳过。
         */
        bool ImportPropertySetFromJsonObject(const MClass* ClassMeta, void* ObjectData, const MJsonValue& InValue, bool bAssetOnly, MString* OutError) {
            if (!ClassMeta || !ObjectData) {
                if (OutError) {
                    *OutError = "asset_json_import_invalid_target";
                }
                return false;
            }
            if (!InValue.IsObject()) {
                if (OutError) {
                    *OutError = "asset_json_import_expected_object";
                }
                return false;
            }

            for (const auto& [Key, Value] : InValue.ObjectValue) {
                // '$' 前缀为元数据(如 $class/$name),由调用方单独处理,不绑定属性。
                if (!Key.empty() && Key[0] == '$') {
                    continue;
                }

                MProperty* Prop = ClassMeta->FindProperty(Key);
                if (!Prop) {
                    if (OutError) {
                        *OutError = "asset_json_unknown_property:" + Key;
                    }
                    return false;
                }
                if (bAssetOnly && !Prop->HasAnyDomains(EPropertyDomainFlags::Asset)) {
                    if (OutError) {
                        *OutError = "asset_json_non_asset_property:" + Key;
                    }
                    return false;
                }

                MString PropertyError;
                if (!Prop->ImportJsonValue(ObjectData, Value, &PropertyError)) {
                    if (OutError) {
                        *OutError = "asset_json_import_property_failed:" + Key + ":" + PropertyError;
                    }
                    return false;
                }
            }

            return true;
        }

        /** @brief 追加 Depth*2 个空格(与 MJsonWriter 的两空格缩进保持一致)。 */
        void AppendIndent(MString& Out, int Depth) {
            Out.append(static_cast<size_t>(Depth) * 2u, ' ');
        }

        /**
         * @brief 递归把 JSON 节点写成带缩进的文本。
         * @param Value 待输出节点
         * @param Out 输出缓冲(追加)
         * @param Depth 当前嵌套深度,决定缩进量
         *
         * @detail 空数组/空对象输出成单行 "[]"/"{}" (不换行),非空才多行展开;
         *   字符串统一交给 MJsonWriter 转义,数字/布尔/空值直接拼字面量。
         */
        void StringifyJsonValue(const MJsonValue& Value, MString& Out, int Depth) {
            switch (Value.Type) {
            case EJsonType::Null:
                Out += "null";
                return;
            case EJsonType::Boolean:
                Out += Value.BoolValue ? "true" : "false";
                return;
            case EJsonType::Number:
                Out += MStringUtil::ToString(Value.NumberValue);
                return;
            case EJsonType::String: {
                MJsonWriter Writer;
                Writer.Value(Value.StringValue);
                Out += Writer.ToString();
                return;
            }
            case EJsonType::Array: {
                Out += "[";
                if (!Value.ArrayValue.empty()) {
                    Out += "\n";
                    for (size_t Index = 0; Index < Value.ArrayValue.size(); ++Index) {
                        AppendIndent(Out, Depth + 1);
                        StringifyJsonValue(Value.ArrayValue[Index], Out, Depth + 1);
                        if (Index + 1 < Value.ArrayValue.size()) {
                            Out += ",";
                        }
                        Out += "\n";
                    }
                    AppendIndent(Out, Depth);
                }
                Out += "]";
                return;
            }
            case EJsonType::Object: {
                Out += "{";
                if (!Value.ObjectValue.empty()) {
                    Out += "\n";
                    size_t Index = 0;
                    for (const auto& [Key, ObjectValue] : Value.ObjectValue) {
                        AppendIndent(Out, Depth + 1);
                        MJsonWriter Writer;
                        Writer.Value(Key);
                        Out += Writer.ToString();
                        Out += ": ";
                        StringifyJsonValue(ObjectValue, Out, Depth + 1);
                        if (Index + 1 < Value.ObjectValue.size()) {
                            Out += ",";
                        }
                        Out += "\n";
                        ++Index;
                    }
                    AppendIndent(Out, Depth);
                }
                Out += "}";
                return;
            }
            }
        }

        /** @brief 节点 → 文本(顶层深度 0)。 */
        MString JsonValueToString(const MJsonValue& Value) {
            MString Result;
            StringifyJsonValue(Value, Result, 0);
            return Result;
        }

        /**
         * @brief 由根节点解析出目标反射类。
         * @param InValue 根节点:必须是 object 且含字符串型 "$class"
         * @param OutError 失败原因,可空
         * @return 反射类;节点形态不对或类名未注册时返回 nullptr
         */
        MClass* ResolveClassForImport(const MJsonValue& InValue, MString* OutError) {
            if (!InValue.IsObject()) {
                if (OutError) {
                    *OutError = "asset_json_root_not_object";
                }
                return nullptr;
            }

            const auto ClassIt = InValue.ObjectValue.find("$class");
            if (ClassIt == InValue.ObjectValue.end() || !ClassIt->second.IsString()) {
                if (OutError) {
                    *OutError = "asset_json_missing_class";
                }
                return nullptr;
            }

            MClass* ClassMeta = MObject::FindClass(ClassIt->second.StringValue);
            if (!ClassMeta) {
                if (OutError) {
                    *OutError = "asset_json_unknown_class:" + ClassIt->second.StringValue;
                }
                return nullptr;
            }
            return ClassMeta;
        }
    } // namespace

    /**
     * @brief MSTRUCT 导出(bAssetOnly=false):不限属性域,导出全部字段。
     *
     * @detail 该入口供 ReflectionPropertyTemplates.inl 的 JSON 导出器调用。
     */
    bool ExportStructToJsonValue(const MClass* StructMeta, const void* StructData, MJsonValue& OutValue, MString* OutError) {
        return ExportPropertySetToJsonObject(StructMeta, StructData, false, OutValue, OutError);
    }

    /** @brief MSTRUCT 导入(bAssetOnly=false):与 ExportStructToJsonValue 对称。 */
    bool ImportStructFromJsonValue(const MClass* StructMeta, void* StructData, const MJsonValue& InValue, MString* OutError) {
        return ImportPropertySetFromJsonObject(StructMeta, StructData, InValue, false, OutError);
    }

    /**
     * @brief 对象 → JSON 值:先导出 Asset 域属性,再补 "$class"/"$name" 元数据。
     *
     * @detail "$name" 仅在对象名为非空时写入,因此空名对象往返后会保持空名。
     *   属性键与元数据键同放在一个 object 里,由 '$' 前缀区分。
     */
    bool ExportAssetObjectToJsonValue(const MObject* Object, MJsonValue& OutValue, MString* OutError) {
        if (!Object) {
            if (OutError) {
                *OutError = "asset_json_export_null_object";
            }
            return false;
        }

        MClass* ClassMeta = Object->GetClass();
        if (!ClassMeta) {
            if (OutError) {
                *OutError = "asset_json_export_missing_class";
            }
            return false;
        }

        if (!ExportPropertySetToJsonObject(ClassMeta, Object, true, OutValue, OutError)) {
            return false;
        }

        OutValue.ObjectValue["$class"]             = MJsonValue{};
        OutValue.ObjectValue["$class"].Type        = EJsonType::String;
        OutValue.ObjectValue["$class"].StringValue = ClassMeta->GetName();

        if (!Object->GetName().empty()) {
            OutValue.ObjectValue["$name"]             = MJsonValue{};
            OutValue.ObjectValue["$name"].Type        = EJsonType::String;
            OutValue.ObjectValue["$name"].StringValue = Object->GetName();
        }

        return true;
    }

    /** @brief 对象 → JSON 文本:导出为节点后交给内置缩进器,不经过 MJsonWriter。 */
    bool ExportAssetObjectToJson(const MObject* Object, MString& OutJson, MString* OutError) {
        MJsonValue RootValue;
        if (!ExportAssetObjectToJsonValue(Object, RootValue, OutError)) {
            return false;
        }

        OutJson = JsonValueToString(RootValue);
        return true;
    }

    /**
     * @brief 把已有对象的 Asset 域字段从 JSON 值里刷新一遍。
     * @param Object 已存在且已设置 Class 的对象
     * @param InValue 源节点(通常来自外层对象的嵌套字段)
     * @param OutError 失败原因,可空
     * @return true 成功
     *
     * @detail 不新建对象,用于 Instanced 嵌套对象的字段回填(见
     *   ReflectionPropertyTemplates.BinaryJson.inl 中指针分支)。
     */
    bool ImportAssetObjectFieldsFromJsonValue(MObject* Object, const MJsonValue& InValue, MString* OutError) {
        if (!Object) {
            if (OutError) {
                *OutError = "asset_json_import_null_object";
            }
            return false;
        }

        MClass* ClassMeta = Object->GetClass();
        if (!ClassMeta) {
            if (OutError) {
                *OutError = "asset_json_import_missing_class";
            }
            return false;
        }

        return ImportPropertySetFromJsonObject(ClassMeta, Object, InValue, true, OutError);
    }

    /**
     * @brief 由 JSON 值创建对象:定类 → 建实例 → 设名/Outer → 灌字段。
     *
     * @detail Outer 为空时把对象加入 RootSet(由全局根集托管生命期);任何一步
     *   失败都会 delete 已创建的实例,不留半成品。
     */
    MObject* ImportAssetObjectFromJsonValue(const MJsonValue& InValue, MObject* Outer, MString* OutError) {
        MClass* ClassMeta = ResolveClassForImport(InValue, OutError);
        if (!ClassMeta) {
            return nullptr;
        }

        MObject* Object = static_cast<MObject*>(ClassMeta->CreateInstance());
        if (!Object) {
            if (OutError) {
                *OutError = "asset_json_create_instance_failed:" + ClassMeta->GetName();
            }
            return nullptr;
        }

        const auto NameIt = InValue.ObjectValue.find("$name");
        if (NameIt != InValue.ObjectValue.end() && NameIt->second.IsString()) {
            Object->SetName(NameIt->second.StringValue);
        }

        if (Outer) {
            Object->SetOuter(Outer);
        } else {
            Object->AddToRoot();
        }

        MString ImportError;
        if (!ImportAssetObjectFieldsFromJsonValue(Object, InValue, &ImportError)) {
            delete Object;
            if (OutError) {
                *OutError = ImportError;
            }
            return nullptr;
        }

        return Object;
    }

    /**
     * @brief 解析 JSON 文本后转调 JsonValue 版本。
     *
     * @detail 解析错误直接透传(MJsonReader 的错误串),不额外包装,
     *   便于调用方区分"文本坏了"和"内容不合法"。
     */
    MObject* ImportAssetObjectFromJson(const MString& InJson, MObject* Outer, MString* OutError) {
        MJsonValue RootValue;
        MString    ParseError;
        if (!MJsonReader::Parse(InJson, RootValue, ParseError)) {
            if (OutError) {
                *OutError = ParseError;
            }
            return nullptr;
        }

        return ImportAssetObjectFromJsonValue(RootValue, Outer, OutError);
    }
} // namespace MObjectAssetJson
