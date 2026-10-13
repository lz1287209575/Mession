/**
 * @file ReflectionPropertyTemplates.BinaryJson.inl
 * @brief 值的 JSON 与二进制序列化实现(按类型分派的 if constexpr 链),以及 TVector 的快照特化。
 *
 * @detail 本文件由 ReflectionPropertyTemplates.inl 在文件末尾包含,内容处于全局作用域。
 *   注意:文件第一个声明 `TPropertyBinaryImporter` 的 `template <typename T>` 头
 *   并不在本文件里 —— 它来自父文件末尾那一行悬空的模板头,两个文件必须配对修改。
 *
 *   两套编码习惯不同,不要混用:
 *   - JSON:数字统一走 double(整型有专门的精度/范围检查后再落回整数),
 *     枚举优先写成"值名"字符串,读入时也接受字符串或数字;
 *   - Binary:显式小端 + VarUInt 变长整数(见 MObjectAssetBinary),
 *     结构体走资产侧字段编码;反序列化后要求字节被**完整消费**
 *     (Detail::EnsureFullyConsumed),多出任何尾字节都算失败。
 *   所有 Export/Import 都是"返回 bool + 可选 OutError 字符串"的风格,不抛异常。
 */
/**
 * @brief TPropertyBinaryImporter - 二进制导入策略:把字节还原到属性所指字段。
 *
 * @detail 类模板参数 T 由父文件末尾的悬空 `template <typename T>` 提供。
 *   只负责"取字段地址"这一步,真正的解码由 ImportReflectValueFromBinary 按 T 分派。
 */
struct TPropertyBinaryImporter {
    static bool Import(const MProperty* Prop, void* Object, const TByteArray& InData, MString* OutError) {
        if (!Prop || !Object) {
            if (OutError) {
                *OutError = "binary_import_invalid_object";
            }
            return false;
        }

        T* ValuePtr = Prop->GetValuePtr<T>(Object);
        if (!ValuePtr) {
            if (OutError) {
                *OutError = "binary_import_null_value_ptr:" + Prop->Name;
            }
            return false;
        }

        return ImportReflectValueFromBinary(InData, *ValuePtr, Prop, Object, OutError);
    }
};

/**
 * @brief Detail - 本文件内部的转接与校验辅助(不属于公开接口)。
 */
namespace Detail {
    /** @brief 结构体 → JSON:转交给资产模块的 ExportStructToJsonValue,缺少结构体元数据时直接失败。 */
    inline bool ExportStructLikeValueToJson(const MClass* StructMeta, const void* StructData, MJsonValue& OutValue, MString* OutError) {
        if (!StructMeta) {
            if (OutError) {
                *OutError = "json_export_missing_struct_meta";
            }
            return false;
        }

        return MObjectAssetJson::ExportStructToJsonValue(StructMeta, StructData, OutValue, OutError);
    }

    /** @brief JSON → 结构体:转交给 ImportStructFromJsonValue;缺少元数据即失败。 */
    inline bool ImportStructLikeValueFromJson(const MClass* StructMeta, void* StructData, const MJsonValue& InValue, MString* OutError) {
        if (!StructMeta) {
            if (OutError) {
                *OutError = "json_import_missing_struct_meta";
            }
            return false;
        }

        return MObjectAssetJson::ImportStructFromJsonValue(StructMeta, StructData, InValue, OutError);
    }

    /** @brief 结构体 → 二进制:转交给 EncodeStructFields(资产侧字段编码)。 */
    inline bool ExportStructLikeValueToBinary(const MClass* StructMeta, const void* StructData, TByteArray& OutData, MString* OutError) {
        if (!StructMeta) {
            if (OutError) {
                *OutError = "binary_export_missing_struct_meta";
            }
            return false;
        }

        return MObjectAssetBinary::EncodeStructFields(StructMeta, StructData, OutData, OutError);
    }

    /** @brief 二进制 → 结构体:转交给 DecodeStructFields;缺少元数据即失败。 */
    inline bool ImportStructLikeValueFromBinary(const MClass* StructMeta, void* StructData, const TByteArray& InData, MString* OutError) {
        if (!StructMeta) {
            if (OutError) {
                *OutError = "binary_import_missing_struct_meta";
            }
            return false;
        }

        return MObjectAssetBinary::DecodeStructFields(StructMeta, StructData, InData, OutError);
    }

    /**
     * @brief EnsureFullyConsumed - 校验字节被恰好消费完。
     * @param InData  输入字节
     * @param Offset  已消费到的偏移
     * @param OutError 未消费完时写入 "binary_trailing_bytes"
     * @return true 表示 Offset == InData.size()
     *
     * @detail 这是二进制导入的"严格模式":多出尾字节不忽略、直接判失败,
     *   避免旧格式残留字段被静默忽略而读出错位的数据。
     */
    inline bool EnsureFullyConsumed(const TByteArray& InData, size_t Offset, MString* OutError) {
        if (Offset == InData.size()) {
            return true;
        }

        if (OutError) {
            *OutError = "binary_trailing_bytes";
        }
        return false;
    }
} // namespace Detail

/**
 * @brief ExportReflectValueToJson - 把任意反射值转成 MJsonValue。
 * @param Value    待转换的值(按 const 引用取,内部 using TDecayed 去掉 cv/引用)
 * @param OutValue 输出 JSON 节点(会被整体赋值)
 * @param OutError 失败原因;成功时不写
 * @return true 转换成功
 *
 * @detail 分派靠 if constexpr 链,顺序体现优先级:MName/MString → bool → 整型 → 浮点
 *   → SVector → SRotator → 枚举 → MObject 指针 → TByteArray → 平凡类型 → 其他。
 *   注意整型也是写成 JSON number(double):超过 2^53 的 int64/uint64 会丢精度,
 *   这是当前 JSON 表示的已知限制。
 *   最后两级都需要"已注册的结构体元数据",没注册就返回 false 并把原因写进 OutError。
 */
template <typename TValue> inline bool ExportReflectValueToJson(const TValue& Value, MJsonValue& OutValue, MString* OutError) {
    using TDecayed = std::remove_cv_t<std::remove_reference_t<TValue>>;
    if constexpr (std::is_same_v<TDecayed, MString> || std::is_same_v<TDecayed, MName>) {
        OutValue.Type        = EJsonType::String;
        OutValue.StringValue = Value;
        return true;
    } else if constexpr (std::is_same_v<TDecayed, bool>) {
        OutValue.Type      = EJsonType::Boolean;
        OutValue.BoolValue = Value;
        return true;
    } else if constexpr (std::is_integral_v<TDecayed>) {
        OutValue.Type        = EJsonType::Number;
        OutValue.NumberValue = static_cast<double>(Value); // 大整数会丢精度,见 @detail
        return true;
    } else if constexpr (std::is_floating_point_v<TDecayed>) {
        OutValue.Type        = EJsonType::Number;
        OutValue.NumberValue = static_cast<double>(Value);
        return true;
    } else if constexpr (std::is_same_v<TDecayed, SVector>) {
        OutValue.Type             = EJsonType::Object; // 向量表示为 {"X":…,"Y":…,"Z":…}
        OutValue.ObjectValue["X"] = MJsonValue{EJsonType::Number, false, static_cast<double>(Value.X)};
        OutValue.ObjectValue["Y"] = MJsonValue{EJsonType::Number, false, static_cast<double>(Value.Y)};
        OutValue.ObjectValue["Z"] = MJsonValue{EJsonType::Number, false, static_cast<double>(Value.Z)};
        return true;
    } else if constexpr (std::is_same_v<TDecayed, SRotator>) {
        OutValue.Type                 = EJsonType::Object; // 旋转表示为 {"Pitch":…,"Yaw":…,"Roll":…}
        OutValue.ObjectValue["Pitch"] = MJsonValue{EJsonType::Number, false, static_cast<double>(Value.Pitch)};
        OutValue.ObjectValue["Yaw"]   = MJsonValue{EJsonType::Number, false, static_cast<double>(Value.Yaw)};
        OutValue.ObjectValue["Roll"]  = MJsonValue{EJsonType::Number, false, static_cast<double>(Value.Roll)};
        return true;
    } else if constexpr (std::is_enum_v<TDecayed>) {
        using TUnderlying = std::underlying_type_t<TDecayed>;
        if (const MEnum* EnumMeta = MObject::FindEnum(std::type_index(typeid(TDecayed)))) {
            const int64 EnumValue = static_cast<int64>(static_cast<TUnderlying>(Value));
            if (const MEnumValue* ValueMeta = EnumMeta->FindValueByIntegral(EnumValue)) {
                OutValue.Type        = EJsonType::String; // 已登记的值名:输出可读名字
                OutValue.StringValue = ValueMeta->Name;
                return true;
            }
        }

        OutValue.Type        = EJsonType::Number; // 未注册或未登记的值:退化为数值
        OutValue.NumberValue = static_cast<double>(static_cast<TUnderlying>(Value));
        return true;
    } else if constexpr (std::is_pointer_v<TDecayed> && std::is_base_of_v<MObject, std::remove_pointer_t<TDecayed>>) {
        if (!Value) {
            OutValue = MJsonValue{}; // 空指针 -> JSON null
            return true;
        }

        return MObjectAssetJson::ExportAssetObjectToJsonValue(Value, OutValue, OutError); // 内嵌对象递归导出
    } else if constexpr (std::is_same_v<TDecayed, TByteArray>) {
        OutValue.Type = EJsonType::Array; // 字节数组写成 [0,1,2,…] 的数字数组
        OutValue.ArrayValue.clear();
        OutValue.ArrayValue.reserve(Value.size());
        for (uint8 Byte : Value) {
            MJsonValue Element;
            Element.Type        = EJsonType::Number;
            Element.NumberValue = static_cast<double>(Byte);
            OutValue.ArrayValue.push_back(std::move(Element));
        }
        return true;
    } else if constexpr (std::is_trivially_copyable_v<TDecayed>) {
        if (const MClass* StructMeta = MObject::FindStruct(std::type_index(typeid(TDecayed)))) {
            return Detail::ExportStructLikeValueToJson(StructMeta, &Value, OutValue, OutError);
        }
        if (OutError) {
            *OutError = "json_export_unsupported_trivial_type";
        }
        return false;
    } else {
        if (const MClass* StructMeta = MObject::FindStruct(std::type_index(typeid(TDecayed)))) {
            return Detail::ExportStructLikeValueToJson(StructMeta, &Value, OutValue, OutError);
        }
        if (OutError) {
            *OutError = "json_export_unsupported_type";
        }
        return false;
    }
}

/**
 * @brief Detail - JSON 导入方向的辅助函数(内部使用)。
 */
namespace Detail {
    /**
     * @brief ImportIntegerFromJsonNumber - 把 JSON number 落到整型字段,做三重校验。
     * @param NumberValue JSON 读出的 double
     * @param OutValue    输出整型
     * @param OutError    失败原因
     * @return true 表示值可无损表示成 TInteger
     *
     * @detail 校验依次为:1) 必须是有限值(NaN/Inf 直接拒绝);2) 必须是整数
     *   (用 trunc 后自比,3.5 会被拒绝而不是截断);3) 落在目标类型范围内 ——
     *   有符号/无符号分开比边界,无符号额外拒绝负数。
     *   因为 JSON 只能给出 double,超过 2^53 的整数在到达这里之前就已经丢精度。
     */
    template <typename TInteger> inline bool ImportIntegerFromJsonNumber(double NumberValue, TInteger& OutValue, MString* OutError) {
        if (!std::isfinite(NumberValue)) {
            if (OutError) {
                *OutError = "json_import_non_finite_number";
            }
            return false;
        }

        const double Truncated = std::trunc(NumberValue);
        if (Truncated != NumberValue) {
            if (OutError) {
                *OutError = "json_import_expected_integer";
            }
            return false;
        }

        if constexpr (std::is_signed_v<TInteger>) {
            const double MinValue = static_cast<double>((std::numeric_limits<TInteger>::min)());
            const double MaxValue = static_cast<double>((std::numeric_limits<TInteger>::max)());
            if (NumberValue < MinValue || NumberValue > MaxValue) {
                if (OutError) {
                    *OutError = "json_import_integer_out_of_range";
                }
                return false;
            }
            OutValue = static_cast<TInteger>(static_cast<int64>(NumberValue));
        } else {
            const double MaxValue = static_cast<double>((std::numeric_limits<TInteger>::max)());
            if (NumberValue < 0.0 || NumberValue > MaxValue) {
                if (OutError) {
                    *OutError = "json_import_integer_out_of_range";
                }
                return false;
            }
            OutValue = static_cast<TInteger>(static_cast<uint64>(NumberValue));
        }
        return true;
    }

    /**
     * @brief FindRequiredObjectMember - 在 JSON 对象里按键取值,缺一不可。
     * @param ObjectValue 必须是一个 JSON object
     * @param Key         成员名
     * @param OutError    失败原因
     * @return 指向成员节点的指针;不是 object 或成员缺失时返回 nullptr
     *
     * @detail 返回的是 JSON 树内部的指针,生命周期与 ObjectValue 相同,
     *   调用方不要保存到 ObjectValue 之外。
     */
    inline const MJsonValue* FindRequiredObjectMember(const MJsonValue& ObjectValue, const MString& Key, MString* OutError) {
        if (!ObjectValue.IsObject()) {
            if (OutError) {
                *OutError = "json_import_expected_object";
            }
            return nullptr;
        }

        const auto It = ObjectValue.ObjectValue.find(Key);
        if (It == ObjectValue.ObjectValue.end()) {
            if (OutError) {
                *OutError = "json_import_missing_member:" + Key;
            }
            return nullptr;
        }
        return &It->second;
    }
} // namespace Detail

/**
 * @brief ImportReflectValueFromJson - 把 MJsonValue 还原到任意反射值。
 * @param InValue     输入 JSON 节点
 * @param OutValue    输出值;失败时可能已被部分修改
 * @param Prop        字段描述符:MObject 指针字段要靠它判断是否允许实例化
 * @param OwnerObject 所属对象:导入内嵌对象时用作新对象的 Outer
 * @param OutError    失败原因
 * @return true 导入成功
 *
 * @detail 与导出的分支一一对应,但更严格:类型不符时直接失败,不做隐式转换
 *   (如字符串不给数字、数字不给布尔)。几个值得注意的点:
 *   - 枚举既接受"值名"字符串也接受数字,字符串必须是已登记的值名;
 *   - SVector/SRotator 要求 X/Y/Z(或 Pitch/Yaw/Roll)三个成员齐全且都是数字;
 *   - MObject 指针字段只有带 EPropertyFlags::Instanced 才允许从 JSON 实例化,
 *     且需要 OwnerObject 作为新对象的 Outer(见下面分支的详细说明);
 *   - TByteArray 与结构体分别递归处理。
 */
template <typename TValue> inline bool ImportReflectValueFromJson(const MJsonValue& InValue, TValue& OutValue, const MProperty* Prop, void* OwnerObject, MString* OutError) {
    using TDecayed = std::remove_cv_t<std::remove_reference_t<TValue>>;
    if constexpr (std::is_same_v<TDecayed, MString> || std::is_same_v<TDecayed, MName>) {
        if (!InValue.IsString()) {
            if (OutError) {
                *OutError = "json_import_expected_string";
            }
            return false;
        }
        OutValue = InValue.StringValue;
        return true;
    } else if constexpr (std::is_same_v<TDecayed, bool>) {
        if (!InValue.IsBool()) {
            if (OutError) {
                *OutError = "json_import_expected_bool";
            }
            return false;
        }
        OutValue = InValue.BoolValue;
        return true;
    } else if constexpr (std::is_integral_v<TDecayed>) {
        if (!InValue.IsNumber()) {
            if (OutError) {
                *OutError = "json_import_expected_number";
            }
            return false;
        }
        return Detail::ImportIntegerFromJsonNumber(InValue.NumberValue, OutValue, OutError);
    } else if constexpr (std::is_floating_point_v<TDecayed>) {
        if (!InValue.IsNumber()) {
            if (OutError) {
                *OutError = "json_import_expected_number";
            }
            return false;
        }
        OutValue = static_cast<TDecayed>(InValue.NumberValue);
        return true;
    } else if constexpr (std::is_same_v<TDecayed, SVector>) {
        const MJsonValue* X = Detail::FindRequiredObjectMember(InValue, "X", OutError);
        const MJsonValue* Y = Detail::FindRequiredObjectMember(InValue, "Y", OutError);
        const MJsonValue* Z = Detail::FindRequiredObjectMember(InValue, "Z", OutError);
        if (!X || !Y || !Z || !X->IsNumber() || !Y->IsNumber() || !Z->IsNumber()) {
            if (OutError && OutError->empty()) { // 前一步可能已写入更具体的原因,不覆盖
                *OutError = "json_import_invalid_vector";
            }
            return false;
        }
        OutValue.X = static_cast<float>(X->NumberValue);
        OutValue.Y = static_cast<float>(Y->NumberValue);
        OutValue.Z = static_cast<float>(Z->NumberValue);
        return true;
    } else if constexpr (std::is_same_v<TDecayed, SRotator>) {
        const MJsonValue* Pitch = Detail::FindRequiredObjectMember(InValue, "Pitch", OutError);
        const MJsonValue* Yaw   = Detail::FindRequiredObjectMember(InValue, "Yaw", OutError);
        const MJsonValue* Roll  = Detail::FindRequiredObjectMember(InValue, "Roll", OutError);
        if (!Pitch || !Yaw || !Roll || !Pitch->IsNumber() || !Yaw->IsNumber() || !Roll->IsNumber()) {
            if (OutError && OutError->empty()) {
                *OutError = "json_import_invalid_rotator";
            }
            return false;
        }
        OutValue.Pitch = static_cast<float>(Pitch->NumberValue);
        OutValue.Yaw   = static_cast<float>(Yaw->NumberValue);
        OutValue.Roll  = static_cast<float>(Roll->NumberValue);
        return true;
    } else if constexpr (std::is_enum_v<TDecayed>) {
        using TUnderlying = std::underlying_type_t<TDecayed>;
        if (InValue.IsString()) {
            if (const MEnum* EnumMeta = MObject::FindEnum(std::type_index(typeid(TDecayed)))) {
                if (const MEnumValue* EnumValue = EnumMeta->FindValue(InValue.StringValue)) {
                    OutValue = static_cast<TDecayed>(static_cast<TUnderlying>(EnumValue->Value));
                    return true;
                }
            }
            if (OutError) {
                *OutError = "json_import_unknown_enum_value";
            }
            return false;
        }
        if (!InValue.IsNumber()) {
            if (OutError) {
                *OutError = "json_import_expected_enum";
            }
            return false;
        }
        TUnderlying RawValue{};
        if (!Detail::ImportIntegerFromJsonNumber(InValue.NumberValue, RawValue, OutError)) {
            return false;
        }
        OutValue = static_cast<TDecayed>(RawValue);
        return true;
    } else if constexpr (std::is_pointer_v<TDecayed> && std::is_base_of_v<MObject, std::remove_pointer_t<TDecayed>>) {
        using TObject = std::remove_pointer_t<TDecayed>;
        if (InValue.IsNull()) {
            OutValue = nullptr; // 显式 null -> 置空,是唯一不需要 Instanced 的写法
            return true;
        }
        if (!InValue.IsObject()) {
            if (OutError) {
                *OutError = "json_import_expected_object_node";
            }
            return false;
        }
        // 内嵌对象会新建实例并挂到 OwnerObject 名下,所以必须显式声明该字段可实例化,
        // 否则会被当成"普通对象引用"而拒绝(引用语义没有实现)。
        if (!Prop || !Prop->HasAnyFlags(EPropertyFlags::Instanced)) {
            if (OutError) {
                *OutError = "json_import_object_reference_unsupported";
            }
            return false;
        }
        // 需要 OwnerObject 才能把新实例挂进对象树;没有的话无法确定 Outer,直接失败。
        if (!OwnerObject) {
            if (OutError) {
                *OutError = "json_import_missing_owner_object";
            }
            return false;
        }

        MClass* TargetClass = TObject::StaticClass();
        // 可选的 $class 字段:只用于校验,必须是目标类名本身(不支持多态),写错即失败。
        if (const auto ClassIt = InValue.ObjectValue.find("$class"); ClassIt != InValue.ObjectValue.end()) {
            if (!ClassIt->second.IsString()) {
                if (OutError) {
                    *OutError = "json_import_invalid_object_class";
                }
                return false;
            }

            if (ClassIt->second.StringValue != TargetClass->GetName()) {
                if (OutError) {
                    *OutError = "json_import_object_class_mismatch:" + ClassIt->second.StringValue;
                }
                return false;
            }
        }

        auto* NewObject = static_cast<MObject*>(TargetClass->CreateInstance());
        if (!NewObject) {
            if (OutError) {
                *OutError = "json_import_object_create_failed";
            }
            return false;
        }

        if (const auto NameIt = InValue.ObjectValue.find("$name"); NameIt != InValue.ObjectValue.end() && NameIt->second.IsString()) {
            NewObject->SetName(NameIt->second.StringValue); // 可选的 $name
        }
        NewObject->SetOuter(static_cast<MObject*>(OwnerObject));

        MString NestedError;
        if (!MObjectAssetJson::ImportAssetObjectFieldsFromJsonValue(NewObject, InValue, &NestedError)) {
            delete NewObject; // 递归导入失败:回收刚建的对象,不给调用方留半成品
            if (OutError) {
                *OutError = NestedError;
            }
            return false;
        }

        OutValue = static_cast<TDecayed>(NewObject);
        return true;
    } else if constexpr (std::is_same_v<TDecayed, TByteArray>) {
        if (!InValue.IsArray()) {
            if (OutError) {
                *OutError = "json_import_expected_array";
            }
            return false;
        }
        OutValue.clear();
        OutValue.reserve(InValue.ArrayValue.size());
        for (const MJsonValue& Element : InValue.ArrayValue) {
            uint8 Byte = 0;
            if (!ImportReflectValueFromJson(Element, Byte, Prop, OwnerObject, OutError)) {
                return false; // 元素级复用同一函数,故仍然走整型校验(0..255 由范围检查保证)
            }
            OutValue.push_back(Byte);
        }
        return true;
    } else if constexpr (std::is_trivially_copyable_v<TDecayed>) {
        if (const MClass* StructMeta = MObject::FindStruct(std::type_index(typeid(TDecayed)))) {
            return Detail::ImportStructLikeValueFromJson(StructMeta, &OutValue, InValue, OutError);
        }
        if (OutError) {
            *OutError = "json_import_unsupported_trivial_type";
        }
        return false;
    } else {
        if (const MClass* StructMeta = MObject::FindStruct(std::type_index(typeid(TDecayed)))) {
            return Detail::ImportStructLikeValueFromJson(StructMeta, &OutValue, InValue, OutError);
        }
        if (OutError) {
            *OutError = "json_import_unsupported_type";
        }
        return false;
    }
}

/**
 * @brief ExportReflectValueToBinary - 把任意反射值编码成资产侧二进制字节。
 * @param Value    待编码的值
 * @param OutData  输出字节;函数入口会先 clear
 * @param OutError 失败原因
 * @return true 编码成功
 *
 * @detail 与 JSON 版的分支结构一致,但编码是显式小端:
 *   整型/枚举走 AppendFixedLE(定长小端),float 走 AppendFloat32LE、
 *   double 走 AppendFloat64LE,字符串走 AppendString(bool 走单字节)。
 *   SVector/SRotator 是三个 float32 顺序写出。
 *   TByteArray 写成 VarUInt 长度 + 原始字节。
 *   结构性差异:对象指针不支持导出(与 JSON 不同,JSON 会给 null),直接失败。
 */
template <typename TValue> inline bool ExportReflectValueToBinary(const TValue& Value, TByteArray& OutData, MString* OutError) {
    using TDecayed = std::remove_cv_t<std::remove_reference_t<TValue>>;
    OutData.clear();

    if constexpr (std::is_same_v<TDecayed, MString> || std::is_same_v<TDecayed, MName>) {
        MObjectAssetBinary::AppendString(OutData, Value);
        return true;
    } else if constexpr (std::is_same_v<TDecayed, bool>) {
        MObjectAssetBinary::AppendByte(OutData, Value ? 1u : 0u);
        return true;
    } else if constexpr (std::is_integral_v<TDecayed>) {
        MObjectAssetBinary::AppendFixedLE(OutData, Value);
        return true;
    } else if constexpr (std::is_floating_point_v<TDecayed>) {
        if constexpr (std::is_same_v<TDecayed, float>) {
            MObjectAssetBinary::AppendFloat32LE(OutData, Value);
        } else {
            MObjectAssetBinary::AppendFloat64LE(OutData, Value);
        }
        return true;
    } else if constexpr (std::is_same_v<TDecayed, SVector>) {
        MObjectAssetBinary::AppendFloat32LE(OutData, Value.X);
        MObjectAssetBinary::AppendFloat32LE(OutData, Value.Y);
        MObjectAssetBinary::AppendFloat32LE(OutData, Value.Z);
        return true;
    } else if constexpr (std::is_same_v<TDecayed, SRotator>) {
        MObjectAssetBinary::AppendFloat32LE(OutData, Value.Pitch);
        MObjectAssetBinary::AppendFloat32LE(OutData, Value.Yaw);
        MObjectAssetBinary::AppendFloat32LE(OutData, Value.Roll);
        return true;
    } else if constexpr (std::is_enum_v<TDecayed>) {
        using TUnderlying = std::underlying_type_t<TDecayed>;
        MObjectAssetBinary::AppendFixedLE(OutData, static_cast<TUnderlying>(Value));
        return true;
    } else if constexpr (std::is_pointer_v<TDecayed> && std::is_base_of_v<MObject, std::remove_pointer_t<TDecayed>>) {
        if (OutError) {
            *OutError = "binary_export_object_reference_unsupported";
        }
        return false;
    } else if constexpr (std::is_same_v<TDecayed, TByteArray>) {
        MObjectAssetBinary::AppendVarUInt(OutData, static_cast<uint64>(Value.size()));
        OutData.insert(OutData.end(), Value.begin(), Value.end());
        return true;
    } else if constexpr (std::is_trivially_copyable_v<TDecayed>) {
        if (const MClass* StructMeta = MObject::FindStruct(std::type_index(typeid(TDecayed)))) {
            return Detail::ExportStructLikeValueToBinary(StructMeta, &Value, OutData, OutError);
        }
        if (OutError) {
            *OutError = "binary_export_unsupported_trivial_type";
        }
        return false;
    } else {
        if (const MClass* StructMeta = MObject::FindStruct(std::type_index(typeid(TDecayed)))) {
            return Detail::ExportStructLikeValueToBinary(StructMeta, &Value, OutData, OutError);
        }
        if (OutError) {
            *OutError = "binary_export_unsupported_type";
        }
        return false;
    }
}

/**
 * @brief ImportReflectValueFromBinary - 把资产侧二进制字节还原到反射值。
 * @param InData      输入字节
 * @param OutValue    输出值
 * @param Prop        字段描述符(对象指针字段未实现,此处忽略)
 * @param OwnerObject 所属对象(对象指针字段未实现,此处忽略)
 * @param OutError    失败原因
 * @return true 解码成功且字节被完整消费
 *
 * @detail 每个标量分支解码后都会调 Detail::EnsureFullyConsumed —— 也就是说
 *   "给一个 int32 字段 8 个字节"会失败,而不是忽略后 4 个字节。
 *   对象指针字段直接返回 "binary_import_object_reference_unsupported"
 *   (内嵌对象只支持 JSON 导入,二进制侧没有实现),因此 Prop/OwnerObject 被显式丢弃。
 *   TByteArray 分支额外做长度前置校验,避免用超大 Count 构造越界区间。
 */
template <typename TValue> inline bool ImportReflectValueFromBinary(const TByteArray& InData, TValue& OutValue, const MProperty* Prop, void* OwnerObject, MString* OutError) {
    using TDecayed = std::remove_cv_t<std::remove_reference_t<TValue>>;
    size_t Offset  = 0;

    if constexpr (std::is_same_v<TDecayed, MString> || std::is_same_v<TDecayed, MName>) {
        return MObjectAssetBinary::ReadString(InData, Offset, OutValue) && Detail::EnsureFullyConsumed(InData, Offset, OutError);
    } else if constexpr (std::is_same_v<TDecayed, bool>) {
        uint8 Raw = 0;
        if (!MObjectAssetBinary::ReadByte(InData, Offset, Raw)) {
            if (OutError) {
                *OutError = "binary_import_expected_bool";
            }
            return false;
        }
        OutValue = (Raw != 0);
        return Detail::EnsureFullyConsumed(InData, Offset, OutError);
    } else if constexpr (std::is_integral_v<TDecayed>) {
        if (!MObjectAssetBinary::ReadFixedLE(InData, Offset, OutValue)) {
            if (OutError) {
                *OutError = "binary_import_expected_integral";
            }
            return false;
        }
        return Detail::EnsureFullyConsumed(InData, Offset, OutError);
    } else if constexpr (std::is_floating_point_v<TDecayed>) {
        const bool bOk = [&]() -> bool {
            if constexpr (std::is_same_v<TDecayed, float>) {
                return MObjectAssetBinary::ReadFloat32LE(InData, Offset, OutValue);
            } else {
                return MObjectAssetBinary::ReadFloat64LE(InData, Offset, OutValue);
            }
        }();
        if (!bOk) {
            if (OutError) {
                *OutError = "binary_import_expected_float";
            }
            return false;
        }
        return Detail::EnsureFullyConsumed(InData, Offset, OutError);
    } else if constexpr (std::is_same_v<TDecayed, SVector>) {
        if (!MObjectAssetBinary::ReadFloat32LE(InData, Offset, OutValue.X) || !MObjectAssetBinary::ReadFloat32LE(InData, Offset, OutValue.Y) || !MObjectAssetBinary::ReadFloat32LE(InData, Offset, OutValue.Z)) {
            if (OutError) {
                *OutError = "binary_import_expected_vector";
            }
            return false;
        }
        return Detail::EnsureFullyConsumed(InData, Offset, OutError);
    } else if constexpr (std::is_same_v<TDecayed, SRotator>) {
        if (!MObjectAssetBinary::ReadFloat32LE(InData, Offset, OutValue.Pitch) || !MObjectAssetBinary::ReadFloat32LE(InData, Offset, OutValue.Yaw) || !MObjectAssetBinary::ReadFloat32LE(InData, Offset, OutValue.Roll)) {
            if (OutError) {
                *OutError = "binary_import_expected_rotator";
            }
            return false;
        }
        return Detail::EnsureFullyConsumed(InData, Offset, OutError);
    } else if constexpr (std::is_enum_v<TDecayed>) {
        using TUnderlying = std::underlying_type_t<TDecayed>;
        TUnderlying RawValue{};
        if (!MObjectAssetBinary::ReadFixedLE(InData, Offset, RawValue)) {
            if (OutError) {
                *OutError = "binary_import_expected_enum";
            }
            return false;
        }
        OutValue = static_cast<TDecayed>(RawValue);
        return Detail::EnsureFullyConsumed(InData, Offset, OutError);
    } else if constexpr (std::is_pointer_v<TDecayed> && std::is_base_of_v<MObject, std::remove_pointer_t<TDecayed>>) {
        (void)Prop; // 二进制侧不支持对象指针:显式丢弃形参,避免 -Wunused 告警
        (void)OwnerObject;
        if (OutError) {
            *OutError = "binary_import_object_reference_unsupported";
        }
        return false;
    } else if constexpr (std::is_same_v<TDecayed, TByteArray>) {
        uint64 Count = 0;
        if (!MObjectAssetBinary::ReadVarUInt(InData, Offset, Count)) {
            if (OutError) {
                *OutError = "binary_import_expected_byte_array";
            }
            return false;
        }
        if (Count > static_cast<uint64>(InData.size() - Offset)) { // 先校验再取区间,防止 Count 撑爆迭代器
            if (OutError) {
                *OutError = "binary_import_truncated_byte_array";
            }
            return false;
        }
        OutValue.assign(InData.begin() + static_cast<std::ptrdiff_t>(Offset), InData.begin() + static_cast<std::ptrdiff_t>(Offset + static_cast<size_t>(Count)));
        Offset += static_cast<size_t>(Count);
        return Detail::EnsureFullyConsumed(InData, Offset, OutError);
    } else if constexpr (std::is_trivially_copyable_v<TDecayed>) {
        if (const MClass* StructMeta = MObject::FindStruct(std::type_index(typeid(TDecayed)))) {
            return Detail::ImportStructLikeValueFromBinary(StructMeta, &OutValue, InData, OutError);
        }
        if (OutError) {
            *OutError = "binary_import_unsupported_trivial_type";
        }
        return false;
    } else {
        if (const MClass* StructMeta = MObject::FindStruct(std::type_index(typeid(TDecayed)))) {
            return Detail::ImportStructLikeValueFromBinary(StructMeta, &OutValue, InData, OutError);
        }
        if (OutError) {
            *OutError = "binary_import_unsupported_type";
        }
        return false;
    }
}

/**
 * @brief ReflectValueToString - 把单个值渲染成可读文本(容器字符串导出的元素级工具)。
 * @param Value 待渲染的值
 * @return 渲染结果;不支持的平凡类型会退化为 "<struct hex=…>"
 *
 * @detail 与 MProperty::ExportValueToString 的取向一致但覆盖面不同:
 *   这里没有 Object/Class/Array 分支,多了"任意 MObject 派生类型调 ToString()"。
 *   MString 会带引号输出。整型按有/无符号分别转换后再转字符串,避免负数被当成巨大无符号数。
 */
template <typename TValue> inline MString ReflectValueToString(const TValue& Value) {
    using TDecayed = std::remove_cv_t<std::remove_reference_t<TValue>>;
    if constexpr (std::is_same_v<TDecayed, MString>) {
        return "\"" + Value + "\"";
    } else if constexpr (std::is_same_v<TDecayed, bool>) {
        return Value ? "true" : "false";
    } else if constexpr (std::is_same_v<TDecayed, SVector>) {
        return "{X=" + MStringUtil::ToString(Value.X) + ", Y=" + MStringUtil::ToString(Value.Y) + ", Z=" + MStringUtil::ToString(Value.Z) + "}";
    } else if constexpr (std::is_same_v<TDecayed, SRotator>) {
        return "{Pitch=" + MStringUtil::ToString(Value.Pitch) + ", Yaw=" + MStringUtil::ToString(Value.Yaw) + ", Roll=" + MStringUtil::ToString(Value.Roll) + "}";
    } else if constexpr (std::is_base_of_v<MObject, TDecayed>) {
        return Value.ToString();
    } else if constexpr (std::is_enum_v<TDecayed>) {
        using TUnderlying = std::underlying_type_t<TDecayed>;
        if (const MEnum* EnumMeta = MObject::FindEnum(std::type_index(typeid(TDecayed)))) {
            const int64 EnumValue = static_cast<int64>(static_cast<TUnderlying>(Value));
            if (const MEnumValue* ValueMeta = EnumMeta->FindValueByIntegral(EnumValue)) {
                return EnumMeta->GetName() + "::" + ValueMeta->Name;
            }
            return EnumMeta->GetName() + "::" + MStringUtil::ToString(EnumValue);
        }
        return MStringUtil::ToString(static_cast<TUnderlying>(Value));
    } else if constexpr (std::is_integral_v<TDecayed>) {
        if constexpr (std::is_signed_v<TDecayed>) {
            return MStringUtil::ToString(static_cast<int64>(Value));
        } else {
            return MStringUtil::ToString(static_cast<uint64>(Value));
        }
    } else if constexpr (std::is_floating_point_v<TDecayed>) {
        return MStringUtil::ToString(static_cast<double>(Value));
    } else if constexpr (std::is_trivially_copyable_v<TDecayed>) {
        const uint8*       Bytes     = reinterpret_cast<const uint8*>(&Value);
        MString            Result    = "<struct hex=";
        static const char* HexDigits = "0123456789ABCDEF";
        for (size_t Index = 0; Index < sizeof(TDecayed); ++Index) {
            const uint8 Byte = Bytes[Index];
            Result.push_back(HexDigits[(Byte >> 4) & 0x0F]);
            Result.push_back(HexDigits[Byte & 0x0F]);
        }
        Result += ">";
        return Result;
    } else {
        return "<unsupported>";
    }
}

// TVector 容器专用序列化
/**
 * @brief TPropertySnapshotOps<TVector<TElement>> - 容器的快照序列化特化(历史遗留)。
 *
 * @detail 与主模板一样已无调用点(见 ReflectionPropertyTemplates.inl 中的说明);
 *   保留是因为它演示了容器的"计数 + 数据"布局,且与 MVectorProperty 的实现相互印证。
 *   实现要点:
 *   - 先写 uint32 元素个数;读方向据此 resize 一次,再原地覆盖元素(比逐个 push_back 快);
 *   - 元素平凡可拷贝时整块 WriteBytes,否则逐个走 operator<<。
 *   约束:元素类型必须能 resize 默认构造,且元素序列化语义要与整块拷贝一致。
 */
template <typename TElement> struct TPropertySnapshotOps<TVector<TElement>> {
    static void Apply(const MProperty* Prop, void* Object, MReflectArchive& Ar) {
        if (!Prop || !Object) {
            return;
        }

        auto* Vec = Prop->GetValuePtr<TVector<TElement>>(Object);
        if (!Vec) {
            return;
        }
        uint32 Count = 0;
        if (Ar.bWriting) {
            Count = static_cast<uint32>(Vec->size());
        }

        Ar << Count;

        if (Ar.bReading) {
            Vec->resize(Count); // 读方向先按计数分配,再逐元素覆盖
        }

        if (Count == 0) {
            return;
        }

        if constexpr (std::is_trivially_copyable_v<TElement>) {
            // POD 元素：按字节批量序列化整个数组
            Ar.WriteBytes(Vec->data(), sizeof(TElement) * Count);
        } else {
            // 非 POD：逐个元素走各自的 operator<<
            for (uint32 Index = 0; Index < Count; ++Index) {
                TElement& Element = (*Vec)[Index];
                Ar << Element;
            }
        }
    }
};

// 同父文件末尾的说明:这一行是悬空模板头,给被包含文件
// (ReflectionPropertyTemplates.StringExport.inl)里第一个声明
// `TPropertyStringExporter<TVector<TElement>>` 提供 TElement。两个文件必须配对修改。
template <typename TElement>
