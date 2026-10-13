/**
 * @file ReflectionPropertyTemplates.inl
 * @brief 属性模板 TProperty<T> 与它的序列化/字符串导入辅助类,以及容器的特化入口。
 *
 * @detail 由 Reflection.h 在文件末尾包含,内容处于全局作用域。三层结构:
 *   1. TProperty<T> 覆写 MProperty 的全部虚函数,把工作转交给下面几个"策略类";
 *   2. 策略类按 T 特化:TPropertyStringImporter(字符串)、TPropertyStringExporter、
 *      TPropertyJsonExporter/Importer、TPropertyBinaryExporter/Importer,
 *      再加 TPropertySnapshotOps(快照,当前已无调用点,见文件末尾说明);
 *   3. 容器特化(TVector/TMap/TSet)放在两个被拆出去的 .inl 里,由文件末尾包含。
 *   文件中引用的 MObjectAssetJson / MObjectAssetBinary 函数只在这里前向声明,
 *   定义在 Source/Common/Runtime/Asset 下 —— 用"声明在前、定义在别处"来避免
 *   反射模块反向依赖资产模块的头文件。
 */

#include "Common/Runtime/Asset/MObjectAssetBinary.h"
#include "Common/Runtime/Json.h"

#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <limits>

// 资产侧序列化函数的前向声明:实现分别位于
// Asset/MObjectAssetJson.cpp 与 Asset/MObjectAssetBinary.cpp。
namespace MObjectAssetJson {
    bool ExportStructToJsonValue(const MClass* StructMeta, const void* StructData, MJsonValue& OutValue, MString* OutError);
    bool ImportStructFromJsonValue(const MClass* StructMeta, void* StructData, const MJsonValue& InValue, MString* OutError);
    bool ExportAssetObjectToJsonValue(const MObject* Object, MJsonValue& OutValue, MString* OutError);
    bool ImportAssetObjectFieldsFromJsonValue(MObject* Object, const MJsonValue& InValue, MString* OutError);
} // namespace MObjectAssetJson

namespace MObjectAssetBinary {
    bool EncodeStructFields(const MClass* StructMeta, const void* StructData, TByteArray& OutData, MString* OutError);
    bool DecodeStructFields(const MClass* StructMeta, void* StructData, const TByteArray& InData, MString* OutError);
} // namespace MObjectAssetBinary

// 策略类与自由函数的前向声明。必须出现在这里,因为下面的 TProperty<T> 定义会用到它们,
// 而它们的完整定义分散在本文件后半部与被包含的两个 .inl 里(容器特化)。
// 前向声明：用于属性模板特化
template <typename T> struct TPropertySnapshotOps;

template <typename T> struct TPropertyStringExporter;

template <typename T> struct TPropertyJsonExporter;

template <typename T> struct TPropertyJsonImporter;

template <typename T> struct TPropertyBinaryExporter;

template <typename T> struct TPropertyBinaryImporter;

template <typename TValue> inline MString ReflectValueToString(const TValue& Value);

template <typename TValue> inline bool ExportReflectValueToJson(const TValue& Value, MJsonValue& OutValue, MString* OutError);

template <typename TValue> inline bool ImportReflectValueFromJson(const MJsonValue& InValue, TValue& OutValue, const MProperty* Prop, void* OwnerObject, MString* OutError);

// TVector 的重载:元素逐个递归处理
template <typename TElement> inline bool ExportReflectValueToJson(const TVector<TElement>& Value, MJsonValue& OutValue, MString* OutError);

template <typename TElement> inline bool ImportReflectValueFromJson(const MJsonValue& InValue, TVector<TElement>& OutValue, const MProperty* Prop, void* OwnerObject, MString* OutError);

template <typename TValue> inline bool ExportReflectValueToBinary(const TValue& Value, TByteArray& OutData, MString* OutError);

template <typename TValue> inline bool ImportReflectValueFromBinary(const TByteArray& InData, TValue& OutValue, const MProperty* Prop, void* OwnerObject, MString* OutError);

template <typename TElement> inline bool ExportReflectValueToBinary(const TVector<TElement>& Value, TByteArray& OutData, MString* OutError);

template <typename TElement> inline bool ImportReflectValueFromBinary(const TByteArray& InData, TVector<TElement>& OutValue, const MProperty* Prop, void* OwnerObject, MString* OutError);


// ---------- SetValueFromString 特化（CLI 解析用） ----------
// MProperty::SetValueFromString 的默认实现一律返回 false;这里按类型给出可解析的实现,
// 由 TProperty<T> 覆写虚函数后转发进来(见本文件末尾的 TPropertySetValueFromStringDispatch)。

template <typename T> class TProperty;

/**
 * @brief TPropertyStringImporter - 字符串→字段值的策略类(主模板:不支持)。
 *
 * @detail 只有显式特化的类型才能从字符串赋值;未特化的类型会得到
 *   "string_import_unsupported_type" 错误。已支持:MString、bool、
 *   全部整型(int8..uint64)、float、double、TVector<TElement>,
 *   以及带 ImportFromCompactString 的 MSTRUCT 聚合类型。
 */
template <typename T> struct TPropertyStringImporter {
    static bool Import(const MProperty* /*Prop*/, void* /*Object*/, const MString& /*Value*/, MString* OutError) {
        if (OutError)
            *OutError = "string_import_unsupported_type";
        return false;
    }
};

/** @brief MString 特化:整串直接赋值,不做引号/转义处理。 */
template <> struct TPropertyStringImporter<MString> {
    static bool Import(const MProperty* Prop, void* Object, const MString& Value, MString* /*OutError*/) {
        if (!Prop || !Object)
            return false;
        auto* Ptr = Prop->GetValuePtr<MString>(Object);
        if (!Ptr)
            return false;
        *Ptr = Value;
        return true;
    }
};

/** @brief bool 特化:大小写不敏感地接受 true/1/yes/on 与 false/0/no/off。 */
template <> struct TPropertyStringImporter<bool> {
    static bool Import(const MProperty* Prop, void* Object, const MString& Value, MString* OutError) {
        if (!Prop || !Object)
            return false;
        auto* Ptr = Prop->GetValuePtr<bool>(Object);
        if (!Ptr)
            return false;
        MString Lower = Value;
        for (auto& C : Lower)
            C = static_cast<char>(std::tolower(static_cast<unsigned char>(C)));
        if (Lower == "true" || Lower == "1" || Lower == "yes" || Lower == "on") {
            *Ptr = true;
            return true;
        }
        if (Lower == "false" || Lower == "0" || Lower == "no" || Lower == "off") {
            *Ptr = false;
            return true;
        }
        if (OutError)
            *OutError = "string_import_invalid_bool:" + Value;
        return false;
    }
};

/**
 * @brief 整型通用策略:strtoull(以 0 为基数,故 "0x" 前缀按十六进制解析)后做范围检查。
 *
 * @detail 先转 unsigned long long 再比对目标类型上限,因此负数字面量在这里会
 *   被 strtoull 解析成一个很大的正数而溢出报错 —— 有符号类型的负值输入不可用,
 *   这是当前实现的已知限制。
 */
template <typename TInteger> struct TPropertyStringIntegerImporter {
    static bool Import(const MProperty* Prop, void* Object, const MString& Value, MString* OutError) {
        if (!Prop || !Object)
            return false;
        auto* Ptr = Prop->GetValuePtr<TInteger>(Object);
        if (!Ptr)
            return false;
        if (Value.empty()) {
            if (OutError)
                *OutError = "string_import_empty_integer";
            return false;
        }
        char* End              = nullptr;
        errno                  = 0;
        unsigned long long Raw = std::strtoull(Value.c_str(), &End, 0);
        if (End == Value.c_str() || (End != nullptr && *End != '\0') || errno != 0) { // 必须整串消费完
            if (OutError)
                *OutError = "string_import_invalid_integer:" + Value;
            return false;
        }
        if (Raw > static_cast<unsigned long long>((std::numeric_limits<TInteger>::max)())) {
            if (OutError)
                *OutError = "string_import_integer_out_of_range:" + Value;
            return false;
        }
        *Ptr = static_cast<TInteger>(Raw);
        return true;
    }
};

// 各整型只需继承通用实现,不必重复函数体
template <> struct TPropertyStringImporter<int8> : TPropertyStringIntegerImporter<int8> {};
template <> struct TPropertyStringImporter<int16> : TPropertyStringIntegerImporter<int16> {};
template <> struct TPropertyStringImporter<int32> : TPropertyStringIntegerImporter<int32> {};
template <> struct TPropertyStringImporter<int64> : TPropertyStringIntegerImporter<int64> {};
template <> struct TPropertyStringImporter<uint8> : TPropertyStringIntegerImporter<uint8> {};
template <> struct TPropertyStringImporter<uint16> : TPropertyStringIntegerImporter<uint16> {};
template <> struct TPropertyStringImporter<uint32> : TPropertyStringIntegerImporter<uint32> {};
template <> struct TPropertyStringImporter<uint64> : TPropertyStringIntegerImporter<uint64> {};

/** @brief float 特化:strtof,要求整串消费完且 errno 未被置位。 */
template <> struct TPropertyStringImporter<float> {
    static bool Import(const MProperty* Prop, void* Object, const MString& Value, MString* OutError) {
        if (!Prop || !Object)
            return false;
        auto* Ptr = Prop->GetValuePtr<float>(Object);
        if (!Ptr)
            return false;
        if (Value.empty()) {
            if (OutError)
                *OutError = "string_import_empty_float";
            return false;
        }
        char* End = nullptr;
        errno     = 0;
        float V   = std::strtof(Value.c_str(), &End);
        if (End == Value.c_str() || (End != nullptr && *End != '\0') || errno != 0) {
            if (OutError)
                *OutError = "string_import_invalid_float:" + Value;
            return false;
        }
        *Ptr = V;
        return true;
    }
};

/** @brief double 特化:与 float 相同,只是换用 strtod 保留双精度。 */
template <> struct TPropertyStringImporter<double> {
    static bool Import(const MProperty* Prop, void* Object, const MString& Value, MString* OutError) {
        if (!Prop || !Object)
            return false;
        auto* Ptr = Prop->GetValuePtr<double>(Object);
        if (!Ptr)
            return false;
        if (Value.empty()) {
            if (OutError)
                *OutError = "string_import_empty_float";
            return false;
        }
        char* End = nullptr;
        errno     = 0;
        double V  = std::strtod(Value.c_str(), &End);
        if (End == Value.c_str() || (End != nullptr && *End != '\0') || errno != 0) {
            if (OutError)
                *OutError = "string_import_invalid_float:" + Value;
            return false;
        }
        *Ptr = V;
        return true;
    }
};

/**
 * @brief MSTRUCT 聚合类型的字符串导入策略:把 `Type@addr:port` 这类紧凑串交给类型自己解析。
 *
 * @detail 适用于带 MSTRUCT 标注、并暴露静态 ImportFromCompactString 的类型,
 *   典型场景是 TVector 字段里的嵌套结构体元素(例如 SEchoServiceConfig::Peers
 *   是 TVector<SServicePeerConfig>)。当 TPropertyStringImporter<TVector<TElement>>
 *   递归到元素类型时(本文件后面的 TVector 特化),会走到这里。
 */
template <typename TAggregate> struct TPropertyStringImporterAggregate {
    static bool Import(const MProperty* Prop, void* Object, const MString& Value, MString* OutError) {
        if (!Prop || !Object)
            return false;
        auto* Ptr = Prop->template GetValuePtr<TAggregate>(Object);
        if (!Ptr)
            return false;
        if (!TAggregate::ImportFromCompactString(*Ptr, Value)) {
            if (OutError)
                *OutError = "string_import_aggregate_parse_failed:" + Value;
            return false;
        }
        return true;
    }
};

/**
 * @brief TPropertySetValueFromStringDispatch - 在"聚合导入"与"逐类型导入"之间按 SFINAE 二选一。
 *
 * @detail 这是 C++17 下对
 *   `if constexpr (requires { T::ImportFromCompactString; })` 的替代写法:
 *   `decltype(&T::ImportFromCompactString)` 良构时启用聚合版特化,否则落到主模板
 *   走普通的 TPropertyStringImporter<T>。特化总是成对出现(主模板 + enable_if 特化),
 *   只加一边会得到意料之外的分支。
 */
template <typename T, typename = void> struct TPropertySetValueFromStringDispatch {
    static bool Call(const MProperty* Prop, void* Object, const MString& Value, MString* OutError) {
        return TPropertyStringImporter<T>::Import(Prop, Object, Value, OutError);
    }
};

template <typename T> struct TPropertySetValueFromStringDispatch<T, std::enable_if_t<std::is_member_object_pointer_v<decltype(&T::ImportFromCompactString)>>> {
    static bool Call(const MProperty* Prop, void* Object, const MString& Value, MString* OutError) {
        return TPropertyStringImporterAggregate<T>::Import(Prop, Object, Value, OutError);
    }
};

/**
 * @brief TVector<TElement> 特化:把字符串按逗号切成若干 token,逐个解析成元素。
 *
 * @detail 语法是 `a,b,c`(无外层括号),每个 token 前后空白会被裁掉,空 token 跳过。
 *   元素解析借道一个临时的 TProperty<TElement> 实例(EPropertyType::None、
 *   offset 0),也就是复用同一套元素级解析逻辑;因此元素类型本身必须支持字符串导入。
 *   任一个元素失败则整体失败,但此时 OutValue 已经被 clear 且可能已有部分元素 ——
 *   失败后不要使用这个容器。
 */
template <typename TElement> struct TPropertyStringImporter<TVector<TElement>> {
    static bool Import(const MProperty* Prop, void* Object, const MString& Value, MString* OutError) {
        if (!Prop || !Object)
            return false;
        auto* Vec = Prop->GetValuePtr<TVector<TElement>>(Object);
        if (!Vec)
            return false;
        Vec->clear();

        size_t Pos = 0;
        while (Pos <= Value.size()) {
            size_t  Comma = Value.find(',', Pos);
            MString Token = (Comma == MString::npos) ? Value.substr(Pos) : Value.substr(Pos, Comma - Pos);
            while (!Token.empty() && std::isspace(static_cast<unsigned char>(Token.front())))
                Token.erase(Token.begin());
            while (!Token.empty() && std::isspace(static_cast<unsigned char>(Token.back())))
                Token.pop_back();

            if (!Token.empty()) {
                TElement            E{};
                TProperty<TElement> ElementProp(MString(), EPropertyType::None, 0, sizeof(TElement), EPropertyFlags::None);
                MString             ElemErr;
                if (!ElementProp.SetValueFromString(&E, Token, &ElemErr)) {
                    if (OutError)
                        *OutError = "string_import_array_element:" + ElemErr;
                    return false;
                }
                Vec->push_back(std::move(E));
            }
            if (Comma == MString::npos)
                break;
            Pos = Comma + 1;
        }
        return true;
    }
};

/**
 * @brief TProperty<T> - 带类型信息的属性描述符,反射系统实际使用的属性类型。
 *
 * @detail 它是 MProperty 的"知道 T"版本,两件事:
 *   1. 取值:把 T 与 typeid(T) 记进 CppTypeIndex,默认按 Offset 直接算地址;
 *      构造函数还接受一对访问器,用于成员指针场景(见 TMemberProperty);
 *   2. 转发:所有序列化虚函数都转交给上面对应的策略类,由它们按 T 特化。
 *   因此新增字段类型支持 = 新增一个策略特化,不需要改这个类的代码。
 */
template <typename T> class TProperty : public MProperty {
    public:
    /** @brief 按偏移取值的构造:offset 由 offsetof / 手工计算得来。 */
    TProperty(const MString& InName, EPropertyType InType, size_t InOffset, size_t InSize, EPropertyFlags InFlags) : MProperty(InName, InType, InOffset, InSize, std::type_index(typeid(T))) {
        Flags = InFlags;
    }

    /** @brief 带访问器的构造:适用于无法用 offsetof 的非标准布局成员。 */
    TProperty(const MString& InName, EPropertyType InType, size_t InOffset, size_t InSize, EPropertyFlags InFlags, MutableAccessor InMutableAccessor, ConstAccessor InConstAccessor)
        : MProperty(InName, InType, InOffset, InSize, std::type_index(typeid(T)), InMutableAccessor, InConstAccessor) {
        Flags = InFlags;
    }

    /**
     * @brief WriteValue - 覆写基类实现,改为按内容递归序列化。
     *
     * @detail 统一递归序列化(替代 TPropertySnapshotOps→基类 WriteBytes 浅拷贝):
     *   嵌套 MSTRUCT/vector/MString 字段按内容序列化,而不是拷贝内部指针
     *   (浅拷贝曾导致读回后与源对象共享 vector 缓冲 → 析构 double free)。
     */
    virtual void WriteValue(void* Object, MReflectArchive& Ar) const override {
        if (!Object) {
            return;
        }
        T* ValuePtr = GetValuePtr<T>(Object);
        if (!ValuePtr) {
            return;
        }
        SerializeArchiveValue(Ar, *ValuePtr);
    }

    virtual MString ExportValueToString(const void* Object) const override {
        return TPropertyStringExporter<T>::Export(this, Object);
    }

    virtual bool ExportJsonValue(const void* Object, MJsonValue& OutValue, MString* OutError = nullptr) const override {
        return TPropertyJsonExporter<T>::Export(this, Object, OutValue, OutError);
    }

    virtual bool ImportJsonValue(void* Object, const MJsonValue& InValue, MString* OutError = nullptr) const override {
        return TPropertyJsonImporter<T>::Import(this, Object, InValue, OutError);
    }

    virtual bool ExportBinaryValue(const void* Object, TByteArray& OutData, MString* OutError = nullptr) const override {
        return TPropertyBinaryExporter<T>::Export(this, Object, OutData, OutError);
    }

    virtual bool ImportBinaryValue(void* Object, const TByteArray& InData, MString* OutError = nullptr) const override {
        return TPropertyBinaryImporter<T>::Import(this, Object, InData, OutError);
    }

    /**
     * @brief SetValueFromString - 覆写基类的"不支持",分发到字符串导入策略。
     *
     * @detail 优先走 MSTRUCT 聚合导入(内部调 T::ImportFromCompactString),
     *   类型不带该静态方法时退回逐类型导入。
     *   C++17 写法:靠 `&T::ImportFromCompactString` 的良构性做重载选择
     *   (C++20 下本可写成 `if constexpr (requires { ... })`)。
     */
    virtual bool SetValueFromString(void* Object, const MString& Value, MString* OutError = nullptr) const override {
        return TPropertySetValueFromStringDispatch<T>::Call(this, Object, Value, OutError);
    }
};


/**
 * @brief TMemberProperty - 用成员对象指针定位字段的 TProperty<TValue>。
 *
 * @detail 编译期把 `&TObject::MemberPtr` 包成一对访问器传给基类,
 *   因此不依赖 offsetof,对非标准布局类型(有基类、有虚函数等)也成立;
 *   这是生成器注册字段时优先使用的方式(见 MREGISTER_PROPERTY)。
 */
template <typename TObject, typename TValue, TValue TObject::* MemberPtr> class TMemberProperty : public TProperty<TValue> {
    public:
    TMemberProperty(const MString& InName, EPropertyType InType, EPropertyFlags InFlags) : TProperty<TValue>(InName, InType, 0, sizeof(TValue), InFlags, &TMemberProperty::GetMutableValue, &TMemberProperty::GetConstValue) {
    }

    private:
    static void* GetMutableValue(void* Object) {
        return &(static_cast<TObject*>(Object)->*MemberPtr);
    }

    static const void* GetConstValue(const void* Object) {
        return &(static_cast<const TObject*>(Object)->*MemberPtr);
    }
};

/**
 * @brief TOffsetProperty - 按偏移定位字段的 TProperty<TValue>,用于裸布局(footprint)场景。
 *
 * @detail 偏移正确性完全由调用方保证;这里不做任何校验或边界检查。
 */
template <typename TValue> class TOffsetProperty : public TProperty<TValue> {
    public:
    TOffsetProperty(const MString& InName, EPropertyType InType, size_t InOffset, EPropertyFlags InFlags) : TProperty<TValue>(InName, InType, InOffset, sizeof(TValue), InFlags) {
    }
};

/**
 * @brief CreateOffsetProperty - 工厂:按偏移构造一个属性描述符。
 * @param InName   属性名
 * @param InType   EPropertyType 类型标签(容器用 None,由容器属性类另作说明)
 * @param InOffset 字段相对对象基址的字节偏移
 * @param InFlags  属性标志
 * @return 新建属性;所有权交给调用方(通常随即被 RegisterProperty 接管)
 */
template <typename TValue> inline MProperty* CreateOffsetProperty(const MString& InName, EPropertyType InType, size_t InOffset, EPropertyFlags InFlags = EPropertyFlags::None) {
    return new TOffsetProperty<TValue>(InName, InType, InOffset, InFlags);
}

// 默认序列化：退回到 MProperty 的基础实现
/**
 * @brief TPropertySnapshotOps - 快照策略(历史遗留):仅在未特化时回退到 MProperty::WriteValue。
 *
 * @detail 当前仓库内已无任何调用点:TProperty<T>::WriteValue 改为直接调用
 *   SerializeArchiveValue,不再经过本策略类。保留主模板与 TVector/TMap/TSet 特化
 *   只是为了兼容可能存在的旧调用点,新增代码不应再依赖它。
 */
template <typename T> struct TPropertySnapshotOps {
    static void Apply(const MProperty* Prop, void* Object, MReflectArchive& Ar) {
        if (!Prop) {
            return;
        }
        const_cast<MProperty*>(Prop)->MProperty::WriteValue(Object, Ar);
    }
};

/** @brief TPropertyStringExporter - 字符串导出策略(主模板):回退到 MProperty 的按类型渲染。 */
template <typename T> struct TPropertyStringExporter {
    static MString Export(const MProperty* Prop, const void* Object) {
        if (!Prop) {
            return "<null-prop>";
        }
        return Prop->MProperty::ExportValueToString(Object);
    }
};

/**
 * @brief TPropertyJsonExporter - JSON 导出策略(主模板):取值后交给 ExportReflectValueToJson。
 *
 * @detail 只做"把字段值取出来"这一段,具体如何变成 JSON 由自由函数按类型决定
 *   (容器有独立重载,见 StringExport.inl)。
 */
template <typename T> struct TPropertyJsonExporter {
    static bool Export(const MProperty* Prop, const void* Object, MJsonValue& OutValue, MString* OutError) {
        if (!Prop || !Object) {
            if (OutError) {
                *OutError = "json_export_invalid_object";
            }
            return false;
        }

        const T* ValuePtr = Prop->GetValuePtr<T>(Object);
        if (!ValuePtr) {
            if (OutError) {
                *OutError = "json_export_null_value_ptr:" + Prop->Name;
            }
            return false;
        }

        return ExportReflectValueToJson(*ValuePtr, OutValue, OutError);
    }
};

/** @brief TPropertyJsonImporter - JSON 导入策略(主模板):取值后交给 ImportReflectValueFromJson。 */
template <typename T> struct TPropertyJsonImporter {
    static bool Import(const MProperty* Prop, void* Object, const MJsonValue& InValue, MString* OutError) {
        if (!Prop || !Object) {
            if (OutError) {
                *OutError = "json_import_invalid_object";
            }
            return false;
        }

        T* ValuePtr = Prop->GetValuePtr<T>(Object);
        if (!ValuePtr) {
            if (OutError) {
                *OutError = "json_import_null_value_ptr:" + Prop->Name;
            }
            return false;
        }

        return ImportReflectValueFromJson(InValue, *ValuePtr, Prop, Object, OutError);
    }
};

/**
 * @brief TPropertyBinaryExporter - 二进制导出策略(主模板)。
 *
 * @detail 与 JSON 版的区别是会把 OutData 先清空(二进制是"独占缓冲"语义,
 *   而 JSON 是往已有节点里写)。
 */
template <typename T> struct TPropertyBinaryExporter {
    static bool Export(const MProperty* Prop, const void* Object, TByteArray& OutData, MString* OutError) {
        if (!Prop || !Object) {
            if (OutError) {
                *OutError = "binary_export_invalid_object";
            }
            return false;
        }

        const T* ValuePtr = Prop->GetValuePtr<T>(Object);
        if (!ValuePtr) {
            if (OutError) {
                *OutError = "binary_export_null_value_ptr:" + Prop->Name;
            }
            return false;
        }

        OutData.clear();
        return ExportReflectValueToBinary(*ValuePtr, OutData, OutError);
    }
};

// 本行不是笔误:它是"悬空模板头",专门用来给紧随其后的被包含文件里第一个声明
// (ReflectionPropertyTemplates.BinaryJson.inl 的 TPropertyBinaryImporter)提供模板参数。
// 两个文件必须成对修改 —— 在它们之间插入任何声明都会挪走模板头导致编译错误。
template <typename T>

// ---- 超大 ReflectionPropertyTemplates.inl 拆分(2026-08-20)----
// 内容:Binary/Json 序列化实现 + 容器特化
#include "Common/Runtime/Reflect/ReflectionPropertyTemplates.BinaryJson.inl"
// 内容:StringExporter 容器特化(TVector/TMap/TSet)
#include "Common/Runtime/Reflect/ReflectionPropertyTemplates.StringExport.inl"
