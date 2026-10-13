/**
 * @file ReflectionPropertyTemplates.StringExport.inl
 * @brief 容器的字符串导出特化,以及 TVector/TMap/TSet 的 JSON、二进制序列化与快照特化。
 *
 * @detail 由 ReflectionPropertyTemplates.inl 在文件末尾包含,内容处于全局作用域。
 *   注意:本文件第一个声明 `TPropertyStringExporter<TVector<TElement>>` 的
 *   `template <typename TElement>` 头并不在本文件里 —— 它来自父文件的包含行之前
 *   (实际沿用了 BinaryJson.inl 末尾那一行悬空模板头),三者改动要一起看。
 *   容器编码统一采用"先个数、后元素"的布局:
 *   - JSON:数组,元素递归;
 *   - Binary:VarUInt 个数,随后每个元素再带自己的 VarUInt 长度前缀
 *     (便于逐元素独立解码并做长度校验)。
 *   文件末尾的 MVectorProperty / TMemberVectorProperty / REGISTER_TVECTOR_PROPERTY
 *   目前在整个仓库内没有调用点(容器字段实际走 TProperty<TVector<...>>),
 *   保留为按偏移注册容器属性的兼容实现。
 */

/**
 * @brief TPropertyStringExporter<TVector<TElement>> - 把容器渲染成 `[a, b, c]`。
 *
 * @detail 元素渲染复用 ReflectValueToString,因此字符串元素会带引号。
 *   容器为空时得到 "[]";Prop 或 Object 为空时返回占位串 "<null-array>"。
 */
struct TPropertyStringExporter<TVector<TElement>> {
    static MString Export(const MProperty* Prop, const void* Object) {
        if (!Prop || !Object) {
            return "<null-array>";
        }

        const auto* Vec = Prop->GetValuePtr<TVector<TElement>>(Object);
        if (!Vec) {
            return "<null-array>";
        }

        MString Result = "[";
        for (size_t Index = 0; Index < Vec->size(); ++Index) {
            if (Index > 0) {
                Result += ", ";
            }
            Result += ReflectValueToString((*Vec)[Index]);
        }
        Result += "]";
        return Result;
    }
};

/**
 * @brief 容器 → JSON 数组:逐元素递归调用标量版本。
 *
 * @detail 任意一个元素失败即整体失败,但此时 OutValue 已经写了部分元素 ——
 *   失败后不要使用它。OutError 会被元素级调用覆盖为更具体的原因。
 */
template <typename TElement> inline bool ExportReflectValueToJson(const TVector<TElement>& Value, MJsonValue& OutValue, MString* OutError) {
    OutValue.Type = EJsonType::Array;
    OutValue.ArrayValue.clear();
    OutValue.ArrayValue.reserve(Value.size());
    for (const TElement& Element : Value) {
        MJsonValue ElementValue;
        if (!ExportReflectValueToJson(Element, ElementValue, OutError)) {
            return false;
        }
        OutValue.ArrayValue.push_back(std::move(ElementValue));
    }
    return true;
}

/**
 * @brief JSON 数组 → 容器:先清空目标,再逐元素构造并 push_back。
 *
 * @detail 元素先默认构造再导入,所以元素类型必须有默认构造函数。
 *   Prop/OwnerObject 原样透传,用于元素内部的 Instanced 对象实例化。
 */
template <typename TElement> inline bool ImportReflectValueFromJson(const MJsonValue& InValue, TVector<TElement>& OutValue, const MProperty* Prop, void* OwnerObject, MString* OutError) {
    if (!InValue.IsArray()) {
        if (OutError) {
            *OutError = "json_import_expected_array";
        }
        return false;
    }

    OutValue.clear();
    OutValue.reserve(InValue.ArrayValue.size());
    for (const MJsonValue& ElementValue : InValue.ArrayValue) {
        TElement Element{};
        if (!ImportReflectValueFromJson(ElementValue, Element, Prop, OwnerObject, OutError)) {
            return false;
        }
        OutValue.push_back(std::move(Element));
    }
    return true;
}

/**
 * @brief 容器 → 二进制:VarUInt 元素个数,随后每个元素带 VarUInt 长度前缀。
 *
 * @detail 逐元素先编码到临时缓冲再拼进去:多付出一次拷贝,换来
 *   "长度可预知"这一优点,读侧才能做边界校验。
 *   与 JSON 不同,这里入口就 clear,不依赖调用方先清空。
 */
template <typename TElement> inline bool ExportReflectValueToBinary(const TVector<TElement>& Value, TByteArray& OutData, MString* OutError) {
    OutData.clear();
    MObjectAssetBinary::AppendVarUInt(OutData, static_cast<uint64>(Value.size()));

    for (const TElement& Element : Value) {
        TByteArray ElementPayload;
        if (!ExportReflectValueToBinary(Element, ElementPayload, OutError)) {
            return false;
        }

        MObjectAssetBinary::AppendVarUInt(OutData, static_cast<uint64>(ElementPayload.size()));
        OutData.insert(OutData.end(), ElementPayload.begin(), ElementPayload.end());
    }

    return true;
}

/**
 * @brief 二进制 → 容器:按个数循环,每个元素先读长度再切出子缓冲独立解码。
 *
 * @detail 安全要点:元素长度会先与剩余字节数比较,避免用超长长度构造越界区间;
 *   元素解码在独立的子缓冲上进行,因此元素内部也能享受"必须完整消费"的检查。
 *   最后仍要求外层字节被完整消费(可能有计数之外的尾字节)。
 */
template <typename TElement> inline bool ImportReflectValueFromBinary(const TByteArray& InData, TVector<TElement>& OutValue, const MProperty* Prop, void* OwnerObject, MString* OutError) {
    size_t Offset = 0;
    uint64 Count  = 0;
    if (!MObjectAssetBinary::ReadVarUInt(InData, Offset, Count)) {
        if (OutError) {
            *OutError = "binary_import_expected_array";
        }
        return false;
    }

    OutValue.clear();
    OutValue.reserve(static_cast<size_t>(Count));
    for (uint64 Index = 0; Index < Count; ++Index) {
        uint64 ElementSize = 0;
        if (!MObjectAssetBinary::ReadVarUInt(InData, Offset, ElementSize) || ElementSize > static_cast<uint64>(InData.size() - Offset)) { // 长度先行校验
            if (OutError) {
                *OutError = "binary_import_truncated_array_element";
            }
            return false;
        }

        TByteArray ElementPayload(InData.begin() + static_cast<std::ptrdiff_t>(Offset), InData.begin() + static_cast<std::ptrdiff_t>(Offset + static_cast<size_t>(ElementSize)));
        Offset += static_cast<size_t>(ElementSize);

        TElement Element{};
        if (!ImportReflectValueFromBinary(ElementPayload, Element, Prop, OwnerObject, OutError)) {
            return false;
        }
        OutValue.push_back(std::move(Element));
    }

    return Detail::EnsureFullyConsumed(InData, Offset, OutError);
}

// TMap<K, V> 容器专用序列化
/**
 * @brief TPropertySnapshotOps<TMap<K, V, Compare>> - map 的快照序列化特化(历史遗留,已无调用点)。
 *
 * @detail 布局:uint32 个数 + 键值交替。两点注意:
 *   - 写方向键会先拷一份(`K KeyCopy = Pair.first;`),因为 operator<< 取非 const 引用,
 *     而 map 的 key 是 const;值则直接按引用写。
 *   - 遍历顺序就是容器自身的顺序(std::map 按键序,哈希 map 则不确定),
 *     所以字节流只在同一次往返内可解释,不要当稳定格式落盘或跨端比对。
 *   读方向先 clear 再 emplace,键重复时后者被丢弃(emplace 不覆盖)。
 */
template <typename K, typename V, typename Compare> struct TPropertySnapshotOps<TMap<K, V, Compare>> {
    static void Apply(const MProperty* Prop, void* Object, MReflectArchive& Ar) {
        if (!Prop || !Object) {
            return;
        }

        auto* MapPtr = Prop->GetValuePtr<TMap<K, V, Compare>>(Object);
        if (!MapPtr) {
            return;
        }
        uint32 Count = 0;
        if (Ar.bWriting) {
            Count = static_cast<uint32>(MapPtr->size());
        }

        Ar << Count;

        if (Ar.bReading) {
            MapPtr->clear();
            for (uint32 Index = 0; Index < Count; ++Index) {
                K Key{};
                V Value{};
                Ar << Key;
                Ar << Value;
                MapPtr->emplace(std::move(Key), std::move(Value));
            }
        } else {
            for (auto& Pair : *MapPtr) {
                K  KeyCopy = Pair.first; // key 是 const,operator<< 要非 const 引用,故拷贝
                V& Value   = Pair.second;
                Ar << KeyCopy;
                Ar << Value;
            }
        }
    }
};

/**
 * @brief TPropertyStringExporter<TMap<K, V, Compare>> - 渲染成 `{key: value, …}`。
 *
 * @detail 与 JSON 的对象字面量形状相同,但键没有引号(键靠 ReflectValueToString
 *   自己渲染:字符串类型会带引号,整型不带),因此这不是可直接解析的 JSON。
 */
template <typename K, typename V, typename Compare> struct TPropertyStringExporter<TMap<K, V, Compare>> {
    static MString Export(const MProperty* Prop, const void* Object) {
        if (!Prop || !Object) {
            return "<null-map>";
        }

        const auto* MapPtr = Prop->GetValuePtr<TMap<K, V, Compare>>(Object);
        if (!MapPtr) {
            return "<null-map>";
        }

        MString Result = "{";
        bool    bFirst = true;
        for (const auto& Pair : *MapPtr) {
            if (!bFirst) {
                Result += ", ";
            }
            Result += ReflectValueToString(Pair.first);
            Result += ": ";
            Result += ReflectValueToString(Pair.second);
            bFirst = false;
        }
        Result += "}";
        return Result;
    }
};

// TSet<T> 容器专用序列化
/**
 * @brief TPropertySnapshotOps<TSet<T, Compare>> - set 的快照序列化特化(历史遗留,已无调用点)。
 *
 * @detail 布局:uint32 个数 + 元素序列(注意与 map 不同,没有键值交替)。
 *   写方向先拷一份元素(`T Copy = Value;`),原因同 map:range-for 给出的是 const 引用。
 *   读方向 clear 后逐个 insert,重复元素会被去重,因此读回的元素个数可能少于计数 ——
 *   此时不会报错,剩余字节也不会被消费检查(本特化没有 EnsureFullyConsumed)。
 */
template <typename T, typename Compare> struct TPropertySnapshotOps<TSet<T, Compare>> {
    static void Apply(const MProperty* Prop, void* Object, MReflectArchive& Ar) {
        if (!Prop || !Object) {
            return;
        }

        auto* SetPtr = Prop->GetValuePtr<TSet<T, Compare>>(Object);
        if (!SetPtr) {
            return;
        }
        uint32 Count = 0;
        if (Ar.bWriting) {
            Count = static_cast<uint32>(SetPtr->size());
        }

        Ar << Count;

        if (Ar.bReading) {
            SetPtr->clear();
            for (uint32 Index = 0; Index < Count; ++Index) {
                T Value{};
                Ar << Value;
                SetPtr->insert(std::move(Value));
            }
        } else {
            for (const T& Value : *SetPtr) {
                T Copy = Value; // range-for 给的是 const 引用,operator<< 需要非 const
                Ar << Copy;
            }
        }
    }
};

/** @brief TPropertyStringExporter<TSet<T, Compare>> - 渲染成 `{a, b}`(集合用大括号,与 map 的 `{k: v}` 形近但语义不同)。 */
template <typename T, typename Compare> struct TPropertyStringExporter<TSet<T, Compare>> {
    static MString Export(const MProperty* Prop, const void* Object) {
        if (!Prop || !Object) {
            return "<null-set>";
        }

        const auto* SetPtr = Prop->GetValuePtr<TSet<T, Compare>>(Object);
        if (!SetPtr) {
            return "<null-set>";
        }

        MString Result = "{";
        bool    bFirst = true;
        for (const T& Value : *SetPtr) {
            if (!bFirst) {
                Result += ", ";
            }
            Result += ReflectValueToString(Value);
            bFirst = false;
        }
        Result += "}";
        return Result;
    }
};

/**
 * @brief MVectorProperty - 按偏移定位 TVector 字段的属性实现(当前无调用点)。
 *
 * @detail 与 TProperty<TVector<...>> 的区别,也是不应再选它的原因:
 *   - 直接 `Object + Offset` 取址,不支持访问器,故只适用于标准布局的偏移;
 *   - WriteValue 对元素一律逐个走 operator<<,没有平凡类型的整块拷贝快路径;
 *   - EPropertyType 记为 None,因此字符串/JSON/二进制导出仍由它自己的虚函数给出,
 *     与 MProperty 的类型标签体系是两套。
 *   当前仓库内没有实例化点;容器字段走 TProperty 模板路线即可。
 */
template <typename TElement> class MVectorProperty : public MProperty {
    public:
    MVectorProperty(const MString& InName, size_t InOffset, EPropertyFlags InFlags) : MProperty(InName, EPropertyType::None, InOffset, sizeof(TVector<TElement>), std::type_index(typeid(TVector<TElement>))) {
        Flags = InFlags;
    }

    /** @brief 读写容器:uint32 个数 + 逐元素 operator<<;读方向按个数 resize 后原地覆盖。 */
    virtual void WriteValue(void* Object, MReflectArchive& Ar) const override {
        if (!Object) {
            return;
        }

        auto*  Vec   = reinterpret_cast<TVector<TElement>*>(reinterpret_cast<uint8*>(Object) + Offset);
        uint32 Count = 0;
        if (Ar.bWriting) {
            Count = static_cast<uint32>(Vec->size());
        }

        Ar << Count;

        if (Ar.bReading) {
            Vec->resize(Count);
        }

        for (uint32 Index = 0; Index < Count; ++Index) {
            TElement& Element = (*Vec)[Index];
            Ar << Element;
        }
    }

    /** @brief 渲染成 `[a, b, c]`;Vec 由 Offset 算出,理论上非空,故第二个判空分支实际不可达。 */
    virtual MString ExportValueToString(const void* Object) const override {
        if (!Object) {
            return "<null-array>";
        }

        const auto* Vec = reinterpret_cast<const TVector<TElement>*>(reinterpret_cast<const uint8*>(Object) + Offset);
        if (!Vec) {
            return "<null-array>";
        }

        MString Result = "[";
        for (size_t Index = 0; Index < Vec->size(); ++Index) {
            if (Index > 0) {
                Result += ", ";
            }
            Result += ReflectValueToString((*Vec)[Index]);
        }
        Result += "]";
        return Result;
    }
};

/**
 * @brief TMemberVectorProperty - 用成员指针定位 TVector 字段的容器属性(当前无调用点)。
 *
 * @detail 相比 MVectorProperty 改用访问器取址(通过 GetValuePtr),
 *   因此对非标准布局成员也成立 —— 这也是唯一值得保留的版本。
 *   序列化与字符串导出逻辑与 MVectorProperty 相同。
 */
template <typename TObject, typename TElement, TVector<TElement> TObject::* MemberPtr> class TMemberVectorProperty : public MProperty {
    public:
    TMemberVectorProperty(const MString& InName, EPropertyFlags InFlags)
        : MProperty(InName, EPropertyType::None, 0, sizeof(TVector<TElement>), std::type_index(typeid(TVector<TElement>)), &TMemberVectorProperty::GetMutableValue, &TMemberVectorProperty::GetConstValue) {
        Flags = InFlags;
    }

    /** @brief 读写容器:uint32 个数 + 逐元素 operator<<。 */
    virtual void WriteValue(void* Object, MReflectArchive& Ar) const override {
        auto* Vec = GetValuePtr<TVector<TElement>>(Object);
        if (!Vec) {
            return;
        }

        uint32 Count = 0;
        if (Ar.bWriting) {
            Count = static_cast<uint32>(Vec->size());
        }

        Ar << Count;

        if (Ar.bReading) {
            Vec->resize(Count);
        }

        for (uint32 Index = 0; Index < Count; ++Index) {
            TElement& Element = (*Vec)[Index];
            Ar << Element;
        }
    }

    /** @brief 渲染成 `[a, b, c]`。 */
    virtual MString ExportValueToString(const void* Object) const override {
        const auto* Vec = GetValuePtr<TVector<TElement>>(Object);
        if (!Vec) {
            return "<null-array>";
        }

        MString Result = "[";
        for (size_t Index = 0; Index < Vec->size(); ++Index) {
            if (Index > 0) {
                Result += ", ";
            }
            Result += ReflectValueToString((*Vec)[Index]);
        }
        Result += "]";
        return Result;
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
 * @brief REGISTER_TVECTOR_PROPERTY - 用成员指针注册一个 TVector 容器字段。
 * @param ElementCppType 元素类型(仅用于满足模板参数,不产生代码)
 * @param PropName       字段名
 * @param PropFlags      EPropertyFlags 位组合
 *
 * @detail 依赖 ThisClass 与 InClass(须在 RegisterAllFunctions/Properties 内使用)。
 *   元素类型必须已被 MReflectArchive 支持:元素不支持时不会在这里报错,
 *   而是运行时静默丢字节(见 ReflectionArchiveInvoke.inl 中 operator<< 的说明)。
 *   当前仓库内无调用点。
 */
#define REGISTER_TVECTOR_PROPERTY(ElementCppType, PropName, PropFlags)                                                 \
    do {                                                                                                               \
        auto* Prop = new TMemberVectorProperty<ThisClass, ElementCppType, &ThisClass::PropName>(#PropName, PropFlags); \
        InClass->RegisterProperty(Prop);                                                                               \
    } while (0)
