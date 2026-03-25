// ============================================
// 属性模板：默认行为 + 容器特化
// ============================================

// 前向声明：用于属性模板特化
template<typename T>
struct TPropertySnapshotOps;

template<typename T>
struct TPropertyStringExporter;

template<typename TValue>
inline MString ReflectValueToString(const TValue& Value);

template<typename T>
class TProperty : public MProperty
{
public:
    TProperty(const MString& InName, EPropertyType InType, size_t InOffset, size_t InSize, EPropertyFlags InFlags)
        : MProperty(InName, InType, InOffset, InSize, std::type_index(typeid(T)))
    {
        Flags = InFlags;
    }

    TProperty(
        const MString& InName,
        EPropertyType InType,
        size_t InOffset,
        size_t InSize,
        EPropertyFlags InFlags,
        MutableAccessor InMutableAccessor,
        ConstAccessor InConstAccessor)
        : MProperty(InName, InType, InOffset, InSize, std::type_index(typeid(T)), InMutableAccessor, InConstAccessor)
    {
        Flags = InFlags;
    }

    virtual void WriteValue(void* Object, MReflectArchive& Ar) const override
    {
        TPropertySnapshotOps<T>::Apply(this, Object, Ar);
    }

    virtual MString ExportValueToString(const void* Object) const override
    {
        return TPropertyStringExporter<T>::Export(this, Object);
    }
};

template<typename TObject, typename TValue, TValue TObject::* MemberPtr>
class TMemberProperty : public TProperty<TValue>
{
public:
    TMemberProperty(const MString& InName, EPropertyType InType, EPropertyFlags InFlags)
        : TProperty<TValue>(
            InName,
            InType,
            0,
            sizeof(TValue),
            InFlags,
            &TMemberProperty::GetMutableValue,
            &TMemberProperty::GetConstValue)
    {
    }

private:
    static void* GetMutableValue(void* Object)
    {
        return &(static_cast<TObject*>(Object)->*MemberPtr);
    }

    static const void* GetConstValue(const void* Object)
    {
        return &(static_cast<const TObject*>(Object)->*MemberPtr);
    }
};

template<typename TValue>
class TOffsetProperty : public TProperty<TValue>
{
public:
    TOffsetProperty(const MString& InName, EPropertyType InType, size_t InOffset, EPropertyFlags InFlags)
        : TProperty<TValue>(InName, InType, InOffset, sizeof(TValue), InFlags)
    {
    }
};

template<typename TValue>
inline MProperty* CreateOffsetProperty(const MString& InName, EPropertyType InType, size_t InOffset, EPropertyFlags InFlags = EPropertyFlags::None)
{
    return new TOffsetProperty<TValue>(InName, InType, InOffset, InFlags);
}

// 默认序列化：退回到 MProperty 的基础实现
template<typename T>
struct TPropertySnapshotOps
{
    static void Apply(const MProperty* Prop, void* Object, MReflectArchive& Ar)
    {
        if (!Prop)
        {
            return;
        }
        const_cast<MProperty*>(Prop)->MProperty::WriteValue(Object, Ar);
    }
};

template<typename T>
struct TPropertyStringExporter
{
    static MString Export(const MProperty* Prop, const void* Object)
    {
        if (!Prop)
        {
            return "<null-prop>";
        }
        return Prop->MProperty::ExportValueToString(Object);
    }
};

template<typename TValue>
inline MString ReflectValueToString(const TValue& Value)
{
    using TDecayed = std::remove_cv_t<std::remove_reference_t<TValue>>;
    if constexpr (std::is_same_v<TDecayed, MString>)
    {
        return "\"" + Value + "\"";
    }
    else if constexpr (std::is_same_v<TDecayed, bool>)
    {
        return Value ? "true" : "false";
    }
    else if constexpr (std::is_same_v<TDecayed, SVector>)
    {
        return "{X=" + MStringUtil::ToString(Value.X) +
               ", Y=" + MStringUtil::ToString(Value.Y) +
               ", Z=" + MStringUtil::ToString(Value.Z) + "}";
    }
    else if constexpr (std::is_same_v<TDecayed, SRotator>)
    {
        return "{Pitch=" + MStringUtil::ToString(Value.Pitch) +
               ", Yaw=" + MStringUtil::ToString(Value.Yaw) +
               ", Roll=" + MStringUtil::ToString(Value.Roll) + "}";
    }
    else if constexpr (std::is_base_of_v<MObject, TDecayed>)
    {
        return Value.ToString();
    }
    else if constexpr (std::is_enum_v<TDecayed>)
    {
        using TUnderlying = std::underlying_type_t<TDecayed>;
        if (const MEnum* EnumMeta = MObject::FindEnum(std::type_index(typeid(TDecayed))))
        {
            const int64 EnumValue = static_cast<int64>(static_cast<TUnderlying>(Value));
            if (const MEnumValue* ValueMeta = EnumMeta->FindValueByIntegral(EnumValue))
            {
                return EnumMeta->GetName() + "::" + ValueMeta->Name;
            }
            return EnumMeta->GetName() + "::" + MStringUtil::ToString(EnumValue);
        }
        return MStringUtil::ToString(static_cast<TUnderlying>(Value));
    }
    else if constexpr (std::is_integral_v<TDecayed>)
    {
        if constexpr (std::is_signed_v<TDecayed>)
        {
            return MStringUtil::ToString(static_cast<int64>(Value));
        }
        else
        {
            return MStringUtil::ToString(static_cast<uint64>(Value));
        }
    }
    else if constexpr (std::is_floating_point_v<TDecayed>)
    {
        return MStringUtil::ToString(static_cast<double>(Value));
    }
    else if constexpr (std::is_trivially_copyable_v<TDecayed>)
    {
        const uint8* Bytes = reinterpret_cast<const uint8*>(&Value);
        MString Result = "<struct hex=";
        static const char* HexDigits = "0123456789ABCDEF";
        for (size_t Index = 0; Index < sizeof(TDecayed); ++Index)
        {
            const uint8 Byte = Bytes[Index];
            Result.push_back(HexDigits[(Byte >> 4) & 0x0F]);
            Result.push_back(HexDigits[Byte & 0x0F]);
        }
        Result += ">";
        return Result;
    }
    else
    {
        return "<unsupported>";
    }
}

// TVector 容器专用序列化
template<typename TElement>
struct TPropertySnapshotOps<TVector<TElement>>
{
    static void Apply(const MProperty* Prop, void* Object, MReflectArchive& Ar)
    {
        if (!Prop || !Object)
        {
            return;
        }

        auto* Vec = Prop->GetValuePtr<TVector<TElement>>(Object);
        if (!Vec)
        {
            return;
        }
        uint32 Count = 0;
        if (Ar.bWriting)
        {
            Count = static_cast<uint32>(Vec->size());
        }

        Ar << Count;

        if (Ar.bReading)
        {
            Vec->resize(Count);
        }

        if (Count == 0)
        {
            return;
        }

        if constexpr (std::is_trivially_copyable_v<TElement>)
        {
            // POD 元素：按字节批量序列化整个数组
            Ar.WriteBytes(Vec->data(), sizeof(TElement) * Count);
        }
        else
        {
            // 非 POD：逐个元素走各自的 operator<<
            for (uint32 Index = 0; Index < Count; ++Index)
            {
                TElement& Element = (*Vec)[Index];
                Ar << Element;
            }
        }
    }
};

template<typename TElement>
struct TPropertyStringExporter<TVector<TElement>>
{
    static MString Export(const MProperty* Prop, const void* Object)
    {
        if (!Prop || !Object)
        {
            return "<null-array>";
        }

        const auto* Vec = Prop->GetValuePtr<TVector<TElement>>(Object);
        if (!Vec)
        {
            return "<null-array>";
        }

        MString Result = "[";
        for (size_t Index = 0; Index < Vec->size(); ++Index)
        {
            if (Index > 0)
            {
                Result += ", ";
            }
            Result += ReflectValueToString((*Vec)[Index]);
        }
        Result += "]";
        return Result;
    }
};

// TMap<K, V> 容器专用序列化
template<typename K, typename V, typename Compare>
struct TPropertySnapshotOps<TMap<K, V, Compare>>
{
    static void Apply(const MProperty* Prop, void* Object, MReflectArchive& Ar)
    {
        if (!Prop || !Object)
        {
            return;
        }

        auto* MapPtr = Prop->GetValuePtr<TMap<K, V, Compare>>(Object);
        if (!MapPtr)
        {
            return;
        }
        uint32 Count = 0;
        if (Ar.bWriting)
        {
            Count = static_cast<uint32>(MapPtr->size());
        }

        Ar << Count;

        if (Ar.bReading)
        {
            MapPtr->clear();
            for (uint32 Index = 0; Index < Count; ++Index)
            {
                K Key{};
                V Value{};
                Ar << Key;
                Ar << Value;
                MapPtr->emplace(std::move(Key), std::move(Value));
            }
        }
        else
        {
            for (auto& Pair : *MapPtr)
            {
                K KeyCopy = Pair.first;
                V& Value = Pair.second;
                Ar << KeyCopy;
                Ar << Value;
            }
        }
    }
};

template<typename K, typename V, typename Compare>
struct TPropertyStringExporter<TMap<K, V, Compare>>
{
    static MString Export(const MProperty* Prop, const void* Object)
    {
        if (!Prop || !Object)
        {
            return "<null-map>";
        }

        const auto* MapPtr = Prop->GetValuePtr<TMap<K, V, Compare>>(Object);
        if (!MapPtr)
        {
            return "<null-map>";
        }

        MString Result = "{";
        bool bFirst = true;
        for (const auto& Pair : *MapPtr)
        {
            if (!bFirst)
            {
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
template<typename T, typename Compare>
struct TPropertySnapshotOps<TSet<T, Compare>>
{
    static void Apply(const MProperty* Prop, void* Object, MReflectArchive& Ar)
    {
        if (!Prop || !Object)
        {
            return;
        }

        auto* SetPtr = Prop->GetValuePtr<TSet<T, Compare>>(Object);
        if (!SetPtr)
        {
            return;
        }
        uint32 Count = 0;
        if (Ar.bWriting)
        {
            Count = static_cast<uint32>(SetPtr->size());
        }

        Ar << Count;

        if (Ar.bReading)
        {
            SetPtr->clear();
            for (uint32 Index = 0; Index < Count; ++Index)
            {
                T Value{};
                Ar << Value;
                SetPtr->insert(std::move(Value));
            }
        }
        else
        {
            for (const T& Value : *SetPtr)
            {
                T Copy = Value;
                Ar << Copy;
            }
        }
    }
};

template<typename T, typename Compare>
struct TPropertyStringExporter<TSet<T, Compare>>
{
    static MString Export(const MProperty* Prop, const void* Object)
    {
        if (!Prop || !Object)
        {
            return "<null-set>";
        }

        const auto* SetPtr = Prop->GetValuePtr<TSet<T, Compare>>(Object);
        if (!SetPtr)
        {
            return "<null-set>";
        }

        MString Result = "{";
        bool bFirst = true;
        for (const T& Value : *SetPtr)
        {
            if (!bFirst)
            {
                Result += ", ";
            }
            Result += ReflectValueToString(Value);
            bFirst = false;
        }
        Result += "}";
        return Result;
    }
};

// 向反射系统注册 TVector 容器属性（元素类型必须已被 MReflectArchive 支持）
template<typename TElement>
class MVectorProperty : public MProperty
{
public:
    MVectorProperty(const MString& InName, size_t InOffset, EPropertyFlags InFlags)
        : MProperty(InName,
                    EPropertyType::None,
                    InOffset,
                    sizeof(TVector<TElement>),
                    std::type_index(typeid(TVector<TElement>)))
    {
        Flags = InFlags;
    }

    virtual void WriteValue(void* Object, MReflectArchive& Ar) const override
    {
        if (!Object)
        {
            return;
        }

        auto* Vec = reinterpret_cast<TVector<TElement>*>(reinterpret_cast<uint8*>(Object) + Offset);
        uint32 Count = 0;
        if (Ar.bWriting)
        {
            Count = static_cast<uint32>(Vec->size());
        }

        Ar << Count;

        if (Ar.bReading)
        {
            Vec->resize(Count);
        }

        for (uint32 Index = 0; Index < Count; ++Index)
        {
            TElement& Element = (*Vec)[Index];
            Ar << Element;
        }
    }

    virtual MString ExportValueToString(const void* Object) const override
    {
        if (!Object)
        {
            return "<null-array>";
        }

        const auto* Vec = reinterpret_cast<const TVector<TElement>*>(reinterpret_cast<const uint8*>(Object) + Offset);
        if (!Vec)
        {
            return "<null-array>";
        }

        MString Result = "[";
        for (size_t Index = 0; Index < Vec->size(); ++Index)
        {
            if (Index > 0)
            {
                Result += ", ";
            }
            Result += ReflectValueToString((*Vec)[Index]);
        }
        Result += "]";
        return Result;
    }
};

template<typename TObject, typename TElement, TVector<TElement> TObject::* MemberPtr>
class TMemberVectorProperty : public MProperty
{
public:
    TMemberVectorProperty(const MString& InName, EPropertyFlags InFlags)
        : MProperty(
            InName,
            EPropertyType::None,
            0,
            sizeof(TVector<TElement>),
            std::type_index(typeid(TVector<TElement>)),
            &TMemberVectorProperty::GetMutableValue,
            &TMemberVectorProperty::GetConstValue)
    {
        Flags = InFlags;
    }

    virtual void WriteValue(void* Object, MReflectArchive& Ar) const override
    {
        auto* Vec = GetValuePtr<TVector<TElement>>(Object);
        if (!Vec)
        {
            return;
        }

        uint32 Count = 0;
        if (Ar.bWriting)
        {
            Count = static_cast<uint32>(Vec->size());
        }

        Ar << Count;

        if (Ar.bReading)
        {
            Vec->resize(Count);
        }

        for (uint32 Index = 0; Index < Count; ++Index)
        {
            TElement& Element = (*Vec)[Index];
            Ar << Element;
        }
    }

    virtual MString ExportValueToString(const void* Object) const override
    {
        const auto* Vec = GetValuePtr<TVector<TElement>>(Object);
        if (!Vec)
        {
            return "<null-array>";
        }

        MString Result = "[";
        for (size_t Index = 0; Index < Vec->size(); ++Index)
        {
            if (Index > 0)
            {
                Result += ", ";
            }
            Result += ReflectValueToString((*Vec)[Index]);
        }
        Result += "]";
        return Result;
    }

private:
    static void* GetMutableValue(void* Object)
    {
        return &(static_cast<TObject*>(Object)->*MemberPtr);
    }

    static const void* GetConstValue(const void* Object)
    {
        return &(static_cast<const TObject*>(Object)->*MemberPtr);
    }
};

#define REGISTER_TVECTOR_PROPERTY(ElementCppType, PropName, PropFlags) \
    do { \
        auto* Prop = new TMemberVectorProperty<ThisClass, ElementCppType, &ThisClass::PropName>(#PropName, PropFlags); \
        InClass->RegisterProperty(Prop); \
    } while(0)
