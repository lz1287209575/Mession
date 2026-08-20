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

template<typename TElement>
inline bool ExportReflectValueToJson(const TVector<TElement>& Value, MJsonValue& OutValue, MString* OutError)
{
    OutValue.Type = EJsonType::Array;
    OutValue.ArrayValue.clear();
    OutValue.ArrayValue.reserve(Value.size());
    for (const TElement& Element : Value)
    {
        MJsonValue ElementValue;
        if (!ExportReflectValueToJson(Element, ElementValue, OutError))
        {
            return false;
        }
        OutValue.ArrayValue.push_back(std::move(ElementValue));
    }
    return true;
}

template<typename TElement>
inline bool ImportReflectValueFromJson(
    const MJsonValue& InValue,
    TVector<TElement>& OutValue,
    const MProperty* Prop,
    void* OwnerObject,
    MString* OutError)
{
    if (!InValue.IsArray())
    {
        if (OutError)
        {
            *OutError = "json_import_expected_array";
        }
        return false;
    }

    OutValue.clear();
    OutValue.reserve(InValue.ArrayValue.size());
    for (const MJsonValue& ElementValue : InValue.ArrayValue)
    {
        TElement Element{};
        if (!ImportReflectValueFromJson(ElementValue, Element, Prop, OwnerObject, OutError))
        {
            return false;
        }
        OutValue.push_back(std::move(Element));
    }
    return true;
}

template<typename TElement>
inline bool ExportReflectValueToBinary(const TVector<TElement>& Value, TByteArray& OutData, MString* OutError)
{
    OutData.clear();
    MObjectAssetBinary::AppendVarUInt(OutData, static_cast<uint64>(Value.size()));

    for (const TElement& Element : Value)
    {
        TByteArray ElementPayload;
        if (!ExportReflectValueToBinary(Element, ElementPayload, OutError))
        {
            return false;
        }

        MObjectAssetBinary::AppendVarUInt(OutData, static_cast<uint64>(ElementPayload.size()));
        OutData.insert(OutData.end(), ElementPayload.begin(), ElementPayload.end());
    }

    return true;
}

template<typename TElement>
inline bool ImportReflectValueFromBinary(
    const TByteArray& InData,
    TVector<TElement>& OutValue,
    const MProperty* Prop,
    void* OwnerObject,
    MString* OutError)
{
    size_t Offset = 0;
    uint64 Count = 0;
    if (!MObjectAssetBinary::ReadVarUInt(InData, Offset, Count))
    {
        if (OutError)
        {
            *OutError = "binary_import_expected_array";
        }
        return false;
    }

    OutValue.clear();
    OutValue.reserve(static_cast<size_t>(Count));
    for (uint64 Index = 0; Index < Count; ++Index)
    {
        uint64 ElementSize = 0;
        if (!MObjectAssetBinary::ReadVarUInt(InData, Offset, ElementSize) ||
            ElementSize > static_cast<uint64>(InData.size() - Offset))
        {
            if (OutError)
            {
                *OutError = "binary_import_truncated_array_element";
            }
            return false;
        }

        TByteArray ElementPayload(
            InData.begin() + static_cast<std::ptrdiff_t>(Offset),
            InData.begin() + static_cast<std::ptrdiff_t>(Offset + static_cast<size_t>(ElementSize)));
        Offset += static_cast<size_t>(ElementSize);

        TElement Element{};
        if (!ImportReflectValueFromBinary(ElementPayload, Element, Prop, OwnerObject, OutError))
        {
            return false;
        }
        OutValue.push_back(std::move(Element));
    }

    return Detail::EnsureFullyConsumed(InData, Offset, OutError);
}

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
