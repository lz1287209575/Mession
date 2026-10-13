/**
 * @file Property.cpp
 * @brief 实现 MProperty 的兜底虚函数:按 EPropertyType 把字段写入反射归档、导出为可读字符串,
 *   以及 JSON/二进制两个资产序列化入口的默认"不支持"桩。
 *
 * @detail 这里只在字段类型没有被模板派生类覆盖时才会被调用:
 *   - TProperty<T>(ReflectionPropertyTemplates.inl)覆盖 WriteValue 与全部
 *     Export 系列 / Import 系列方法,是运行期实际走的那条路;
 *   - 本文件的 WriteValue 走 MReflectArchive(按值语义,负责快照/复制);
 *     ExportBinaryValue 走资产侧的小端编码(MObjectAssetBinary);
 *     两者字节格式并不通用,不要混用。
 *   其余未被模板覆盖的类型会命中 default 分支:写快照时打告警且不落任何字节,
 *   导出字符串时返回 "<unsupported>" 之类的占位串。
 */
#include "Common/Runtime/HexUtils.h"
#include "Common/Runtime/Json.h"
#include "Common/Runtime/Log/Log.h"
#include "Common/Runtime/Reflect/Reflection.h"

namespace {
    /** @brief 把 SVector 格式化为 `{X=…, Y=…, Z=…}`,供字符串导出使用。 */
    MString FormatVector(const SVector& Value) {
        return "{X=" + MStringUtil::ToString(Value.X) + ", Y=" + MStringUtil::ToString(Value.Y) + ", Z=" + MStringUtil::ToString(Value.Z) + "}";
    }

    /** @brief 把 SRotator 格式化为 `{Pitch=…, Yaw=…, Roll=…}`,供字符串导出使用。 */
    MString FormatRotator(const SRotator& Value) {
        return "{Pitch=" + MStringUtil::ToString(Value.Pitch) + ", Yaw=" + MStringUtil::ToString(Value.Yaw) + ", Roll=" + MStringUtil::ToString(Value.Roll) + "}";
    }
} // namespace

/**
 * @brief WriteValue - 按 Type 把字段值送进/取出反射归档,方向由归档自己决定。
 * @param Object 字段所属对象的基址;为 nullptr 或字段 Size 为 0 时直接返回
 * @param Ar     读写双向归档:bWriting 为真时追加字节,bReading 为真时消费字节
 *
 * @detail 同一个函数兼顾写快照与读快照,调用方不需要区分方向。
 *   Struct/Enum 按 Size 字节整体 WriteBytes —— 这要求类型是 POD;带容器/字符串
 *   成员的嵌套类型必须由 TProperty<T> 的递归实现接管,否则会浅拷贝内部指针。
 *   Name/Object/Class/Array 没有分支,会落到 default 打告警且不写任何字节
 *   (容器走独立的 MVectorProperty / TPropertySnapshotOps 特化)。
 */
void MProperty::WriteValue(void* Object, MReflectArchive& Ar) const {
    if (!Object || Size == 0) { // 空对象或零宽字段:没有可序列化的内容
        return;
    }

    uint8* FieldPtr = static_cast<uint8*>(GetValueVoidPtr(Object));
    if (!FieldPtr) { // 访问器返回空:放弃本次序列化,不写任何字节
        return;
    }

    switch (Type) {
    case EPropertyType::Int8: {
        int8& Value = *reinterpret_cast<int8*>(FieldPtr);
        Ar << Value;
        break;
    }
    case EPropertyType::Int16: {
        int16& Value = *reinterpret_cast<int16*>(FieldPtr);
        Ar << Value;
        break;
    }
    case EPropertyType::Int32: {
        int32& Value = *reinterpret_cast<int32*>(FieldPtr);
        Ar << Value;
        break;
    }
    case EPropertyType::Int64: {
        int64& Value = *reinterpret_cast<int64*>(FieldPtr);
        Ar << Value;
        break;
    }
    case EPropertyType::UInt8: {
        uint8& Value = *reinterpret_cast<uint8*>(FieldPtr);
        Ar << Value;
        break;
    }
    case EPropertyType::UInt16: {
        uint16& Value = *reinterpret_cast<uint16*>(FieldPtr);
        Ar << Value;
        break;
    }
    case EPropertyType::UInt32: {
        uint32& Value = *reinterpret_cast<uint32*>(FieldPtr);
        Ar << Value;
        break;
    }
    case EPropertyType::UInt64: {
        uint64& Value = *reinterpret_cast<uint64*>(FieldPtr);
        Ar << Value;
        break;
    }
    case EPropertyType::Float: {
        float& Value = *reinterpret_cast<float*>(FieldPtr);
        Ar << Value;
        break;
    }
    case EPropertyType::Double: {
        double& Value = *reinterpret_cast<double*>(FieldPtr);
        Ar << Value;
        break;
    }
    case EPropertyType::Bool: {
        bool& Value = *reinterpret_cast<bool*>(FieldPtr);
        Ar << Value;
        break;
    }
    case EPropertyType::String: {
        MString& Value = *reinterpret_cast<MString*>(FieldPtr);
        Ar << Value;
        break;
    }
    case EPropertyType::Vector: {
        SVector& Value = *reinterpret_cast<SVector*>(FieldPtr);
        Ar << Value;
        break;
    }
    case EPropertyType::Rotator: {
        SRotator& Value = *reinterpret_cast<SRotator*>(FieldPtr);
        Ar << Value;
        break;
    }
    case EPropertyType::Struct: {
        Ar.WriteBytes(FieldPtr, Size); // POD 结构体按字节整体拷贝,不做递归
        break;
    }
    case EPropertyType::Enum: {
        Ar.WriteBytes(FieldPtr, Size); // 枚举按底层整数宽度(Size 字节)整体拷贝
        break;
    }
    default:
        LOG_WARN("Reflection snapshot write: unsupported property type %d for '%s'", (int)Type, Name.c_str());
        break;
    }
}

/**
 * @brief ExportValueToString - 把字段值渲染成人类可读的字符串(日志/CLI 展示用)。
 * @param Object 字段所属对象的基址
 * @return 渲染结果;对象或字段指针为空时返回 "<null-object>"/"<null-field>" 占位串
 *
 * @detail 输出格式是给人和 CLI 看的,不是解析格式,不要依赖它做反序列化。
 *   枚举优先查已注册的 MEnum(按 CppTypeIndex)输出 "枚举名::值名",
 *   查不到再退化为按 Size 决定的裸整数。
 *   结构体先特判 SVector/SRotator(有专用 `{X=…}` 格式),再查已注册结构体元数据
 *   递归导出,最后退化到十六进制串。
 *   Object/Class/Array 只返回占位串,说明这些类型在此路径上无意义。
 */
MString MProperty::ExportValueToString(const void* Object) const {
    if (!Object) {
        return "<null-object>";
    }

    const uint8* FieldPtr = static_cast<const uint8*>(GetValueVoidPtr(Object));
    if (!FieldPtr) {
        return "<null-field>";
    }

    switch (Type) {
    case EPropertyType::Int8:
        return MStringUtil::ToString(static_cast<int32>(*reinterpret_cast<const int8*>(FieldPtr)));
    case EPropertyType::Int16:
        return MStringUtil::ToString(static_cast<int32>(*reinterpret_cast<const int16*>(FieldPtr)));
    case EPropertyType::Int32:
        return MStringUtil::ToString(*reinterpret_cast<const int32*>(FieldPtr));
    case EPropertyType::Int64:
        return MStringUtil::ToString(*reinterpret_cast<const int64*>(FieldPtr));
    case EPropertyType::UInt8:
        return MStringUtil::ToString(static_cast<uint32>(*reinterpret_cast<const uint8*>(FieldPtr)));
    case EPropertyType::UInt16:
        return MStringUtil::ToString(static_cast<uint32>(*reinterpret_cast<const uint16*>(FieldPtr)));
    case EPropertyType::UInt32:
        return MStringUtil::ToString(*reinterpret_cast<const uint32*>(FieldPtr));
    case EPropertyType::UInt64:
        return MStringUtil::ToString(*reinterpret_cast<const uint64*>(FieldPtr));
    case EPropertyType::Float:
        return MStringUtil::ToString(*reinterpret_cast<const float*>(FieldPtr));
    case EPropertyType::Double:
        return MStringUtil::ToString(*reinterpret_cast<const double*>(FieldPtr));
    case EPropertyType::Bool:
        return *reinterpret_cast<const bool*>(FieldPtr) ? "true" : "false";
    case EPropertyType::String:
        return "\"" + *reinterpret_cast<const MString*>(FieldPtr) + "\"";
    case EPropertyType::Vector:
        return FormatVector(*reinterpret_cast<const SVector*>(FieldPtr));
    case EPropertyType::Rotator:
        return FormatRotator(*reinterpret_cast<const SRotator*>(FieldPtr));
    case EPropertyType::Enum: {
        if (const MEnum* EnumMeta = MObject::FindEnum(CppTypeIndex)) {
            // 枚举宽度可能是 1/2/4/8 字节,按 Size 取出再统一提升到 int64 查元数据
            int64 EnumValue = 0;
            switch (Size) {
            case 1:
                EnumValue = static_cast<int64>(*reinterpret_cast<const uint8*>(FieldPtr));
                break;
            case 2:
                EnumValue = static_cast<int64>(*reinterpret_cast<const uint16*>(FieldPtr));
                break;
            case 4:
                EnumValue = static_cast<int64>(*reinterpret_cast<const uint32*>(FieldPtr));
                break;
            case 8:
                EnumValue = static_cast<int64>(*reinterpret_cast<const uint64*>(FieldPtr));
                break;
            default:
                return "<enum-size-unsupported>";
            }

            if (const MEnumValue* ValueMeta = EnumMeta->FindValueByIntegral(EnumValue)) {
                return EnumMeta->GetName() + "::" + ValueMeta->Name;
            }
            // 未登记的枚举值:只输出 枚举名::数值,保留可读性
            return EnumMeta->GetName() + "::" + MStringUtil::ToString(EnumValue);
        }
        // 枚举未注册:退化为裸整数输出
        switch (Size) {
        case 1:
            return MStringUtil::ToString(static_cast<uint32>(*reinterpret_cast<const uint8*>(FieldPtr)));
        case 2:
            return MStringUtil::ToString(static_cast<uint32>(*reinterpret_cast<const uint16*>(FieldPtr)));
        case 4:
            return MStringUtil::ToString(*reinterpret_cast<const uint32*>(FieldPtr));
        case 8:
            return MStringUtil::ToString(*reinterpret_cast<const uint64*>(FieldPtr));
        default:
            return "<enum-size-unsupported>";
        }
    }
    case EPropertyType::Struct:
        if (CppTypeIndex == std::type_index(typeid(SVector))) { // 与 Vector 类型共用同一渲染格式
            return FormatVector(*reinterpret_cast<const SVector*>(FieldPtr));
        }
        if (CppTypeIndex == std::type_index(typeid(SRotator))) { // 与 Rotator 类型共用同一渲染格式
            return FormatRotator(*reinterpret_cast<const SRotator*>(FieldPtr));
        }
        if (MClass* StructMeta = MObject::FindStruct(CppTypeIndex)) {
            return StructMeta->ExportObjectToString(FieldPtr); // 已注册结构体:递归导出各字段
        }
        // 未注册结构体:退化为十六进制转储,保证信息不丢
        return "<struct:" + Name + " hex=" + Hex::BytesToHexString(FieldPtr, Size) + ">";
    case EPropertyType::Array:
        return "<array>";
    case EPropertyType::Object:
        return "<object>";
    case EPropertyType::Class:
        return "<class>";
    default:
        return "<unsupported>";
    }
}

/**
 * @brief ExportJsonValue - JSON 导出的基类默认实现:一律不支持。
 * @param Object   目标对象(此处未使用)
 * @param OutValue 输出 JSON 节点(此处未写入)
 * @param OutError 非空时写入 "json_export_unsupported:<字段名>"
 * @return 恒为 false
 *
 * @detail 真正的实现是 TPropertyJsonExporter<T>(ReflectionPropertyTemplates.inl),
 *   由 TProperty<T> 覆盖本虚函数后转发过去;只有生成器/手写代码直接构造裸
 *   MProperty 时才会命中这里。
 */
bool MProperty::ExportJsonValue(const void* /*Object*/, MJsonValue& /*OutValue*/, MString* OutError) const {
    if (OutError) {
        *OutError = "json_export_unsupported:" + Name;
    }
    return false;
}

/**
 * @brief ImportJsonValue - JSON 导入的基类默认实现:一律不支持。
 * @param Object   目标对象(此处未使用)
 * @param InValue  输入 JSON 节点(此处未读取)
 * @param OutError 非空时写入 "json_import_unsupported:<字段名>"
 * @return 恒为 false
 *
 * @detail 同 ExportJsonValue,实际路径由 TPropertyJsonImporter<T> 承载。
 */
bool MProperty::ImportJsonValue(void* /*Object*/, const MJsonValue& /*InValue*/, MString* OutError) const {
    if (OutError) {
        *OutError = "json_import_unsupported:" + Name;
    }
    return false;
}

/**
 * @brief ExportBinaryValue - 二进制(资产)导出的基类默认实现:一律不支持。
 * @param Object   目标对象(此处未使用)
 * @param OutData  输出字节缓冲(此处不写入)
 * @param OutError 非空时写入 "binary_export_unsupported:<字段名>"
 * @return 恒为 false
 *
 * @detail 实际路径是 TPropertyBinaryExporter<T> → ExportReflectValueToBinary,
 *   采用资产侧小端编码(与 WriteValue 的 MReflectArchive 格式不兼容)。
 */
bool MProperty::ExportBinaryValue(const void* /*Object*/, TByteArray& /*OutData*/, MString* OutError) const {
    if (OutError) {
        *OutError = "binary_export_unsupported:" + Name;
    }
    return false;
}

/**
 * @brief ImportBinaryValue - 二进制(资产)导入的基类默认实现:一律不支持。
 * @param Object   目标对象(此处未使用)
 * @param InData   输入字节缓冲(此处不读取)
 * @param OutError 非空时写入 "binary_import_unsupported:<字段名>"
 * @return 恒为 false
 *
 * @detail 实际路径是 TPropertyBinaryImporter<T> → ImportReflectValueFromBinary。
 */
bool MProperty::ImportBinaryValue(void* /*Object*/, const TByteArray& /*InData*/, MString* OutError) const {
    if (OutError) {
        *OutError = "binary_import_unsupported:" + Name;
    }
    return false;
}
