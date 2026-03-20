// ============================================
// 序列化系统
// ============================================
#include <cstring>

class MReflectArchive
{
public:
    TByteArray Data;
    size_t ReadPos = 0;
    bool bReading = false;
    bool bWriting = true;
    bool bReadOverflow = false;
    
    MReflectArchive() { bWriting = true; bReading = false; }
    explicit MReflectArchive(const TByteArray& InData) : Data(InData), ReadPos(0), bReading(true), bWriting(false) {}
    
    // 序列化基本类型
    MReflectArchive& operator<<(uint8& Value) { return WritePOD(Value); }
    MReflectArchive& operator<<(uint16& Value) { return WritePOD(Value); }
    MReflectArchive& operator<<(uint32& Value) { return WritePOD(Value); }
    MReflectArchive& operator<<(uint64& Value) { return WritePOD(Value); }
    MReflectArchive& operator<<(int8& Value) { return WritePOD(Value); }
    MReflectArchive& operator<<(int16& Value) { return WritePOD(Value); }
    MReflectArchive& operator<<(int32& Value) { return WritePOD(Value); }
    MReflectArchive& operator<<(int64& Value) { return WritePOD(Value); }
    MReflectArchive& operator<<(float& Value) { return WritePOD(Value); }
    MReflectArchive& operator<<(double& Value) { return WritePOD(Value); }
    MReflectArchive& operator<<(MString& Value) { return WriteString(Value); }
    MReflectArchive& operator<<(SVector& Value) { return WritePOD(Value); }
    MReflectArchive& operator<<(SRotator& Value) { return WritePOD(Value); }
    template<typename T,
             std::enable_if_t<
                 std::is_trivially_copyable_v<T> &&
                 !std::is_arithmetic_v<T> &&
                 !std::is_enum_v<T> &&
                 !std::is_same_v<T, MString> &&
                 !std::is_same_v<T, SVector> &&
                 !std::is_same_v<T, SRotator>, int> = 0>
    MReflectArchive& operator<<(T& Value) { return WritePOD(Value); }
    MReflectArchive& operator<<(bool& Value)
    {
        uint8 Temp = Value ? 1u : 0u;
        *this << Temp;
        if (bReading)
        {
            Value = (Temp != 0u);
        }
        return *this;
    }

    template<typename TEnum, std::enable_if_t<std::is_enum_v<TEnum>, int> = 0>
    MReflectArchive& operator<<(TEnum& Value)
    {
        using TUnderlying = std::underlying_type_t<TEnum>;
        TUnderlying RawValue = static_cast<TUnderlying>(Value);
        *this << RawValue;
        if (bReading)
        {
            Value = static_cast<TEnum>(RawValue);
        }
        return *this;
    }

    // 原始字节序列化，主要用于结构体等复杂类型的按字节拷贝
    MReflectArchive& WriteBytes(void* Buffer, size_t Size)
    {
        if (!Buffer || Size == 0)
        {
            return *this;
        }

        if (bWriting)
        {
            size_t OldSize = Data.size();
            Data.resize(OldSize + Size);
            memcpy(Data.data() + OldSize, Buffer, Size);
        }
        else if (bReading && ReadPos + Size <= Data.size())
        {
            memcpy(Buffer, Data.data() + ReadPos, Size);
            ReadPos += Size;
        }
        else if (bReading)
        {
            bReadOverflow = true;
        }
        return *this;
    }
    
private:
    template<typename T>
    MReflectArchive& WritePOD(T& Value)
    {
        if (bWriting)
        {
            size_t OldSize = Data.size();
            Data.resize(OldSize + sizeof(T));
            memcpy(Data.data() + OldSize, &Value, sizeof(T));
        }
        else if (bReading && ReadPos + sizeof(T) <= Data.size())
        {
            memcpy(&Value, Data.data() + ReadPos, sizeof(T));
            ReadPos += sizeof(T);
        }
        else if (bReading)
        {
            bReadOverflow = true;
        }
        return *this;
    }
    
    MReflectArchive& WriteString(MString& Value)
    {
        if (bWriting)
        {
            uint32 Len = (uint32)Value.size();
            *this << Len;
            if (Len > 0)
            {
                size_t OldSize = Data.size();
                Data.resize(OldSize + Len);
                memcpy(Data.data() + OldSize, Value.data(), Len);
            }
        }
        else if (bReading)
        {
            uint32 Len = 0;
            *this << Len;
            if (Len > 0 && ReadPos + Len <= Data.size())
            {
                Value.assign((char*)Data.data() + ReadPos, Len);
                ReadPos += Len;
            }
            else if (Len > 0)
            {
                bReadOverflow = true;
            }
        }
        return *this;
    }
};

template<typename... TArgs>
inline void SerializeRpcArgs(MReflectArchive& Ar, TArgs&... Args)
{
    (void(Ar << Args), ...);
}

template<typename... TArgs>
inline TByteArray BuildRpcArgsPayload(TArgs&&... Args)
{
    MReflectArchive Ar;
    (void([&Ar](auto&& Value)
    {
        using TValue = std::remove_cv_t<std::remove_reference_t<decltype(Value)>>;
        TValue Copy = static_cast<TValue>(Value);
        Ar << Copy;
    }(std::forward<TArgs>(Args))), ...);
    return std::move(Ar.Data);
}

template<typename TArg>
inline bool SerializeFunctionArgByMeta(MReflectArchive& Ar, const MProperty* Prop, TArg&& Arg)
{
    if (!Prop)
    {
        return false;
    }

    using TStorage = std::remove_cv_t<std::remove_reference_t<TArg>>;
    if (Prop->CppTypeIndex != std::type_index(typeid(TStorage)))
    {
        return false;
    }

    TStorage Copy = static_cast<TStorage>(Arg);
    Ar << Copy;
    return true;
}

inline bool SerializeFunctionArgsByMeta(const MFunction* Func, MReflectArchive&)
{
    return Func && Func->Params.empty();
}

template<typename TArg, typename... TRest>
inline bool SerializeFunctionArgsByMeta(const MFunction* Func, MReflectArchive& Ar, TArg&& Arg, TRest&&... Rest)
{
    if (!Func)
    {
        return false;
    }

    constexpr size_t ArgCount = 1 + sizeof...(TRest);
    if (Func->Params.size() != ArgCount)
    {
        return false;
    }

    size_t Index = 0;
    bool bOk = true;
    auto SerializeOne = [&](auto&& Value)
    {
        if (!bOk || Index >= Func->Params.size())
        {
            bOk = false;
            return;
        }

        bOk = SerializeFunctionArgByMeta(Ar, Func->Params[Index], std::forward<decltype(Value)>(Value));
        ++Index;
    };

    SerializeOne(std::forward<TArg>(Arg));
    (SerializeOne(std::forward<TRest>(Rest)), ...);
    return bOk;
}

inline bool BuildRpcPayloadForFunction(const MFunction* Func, const TByteArray& InPayload, TByteArray& OutData)
{
    if (!Func)
    {
        return false;
    }
    return BuildServerRpcPayload(Func->FunctionId, InPayload, OutData);
}

template<typename... TArgs>
inline bool BuildRpcPayloadForFunctionCall(const MFunction* Func, TByteArray& OutData, TArgs&&... Args)
{
    if (!Func)
    {
        return false;
    }

    MReflectArchive Ar;
    if (!SerializeFunctionArgsByMeta(Func, Ar, std::forward<TArgs>(Args)...))
    {
        return false;
    }

    return BuildRpcPayloadForFunction(Func, Ar.Data, OutData);
}

template<typename TObject, typename... TArgs>
inline bool BuildRpcPayloadForFunctionCall(const char* FunctionName, TByteArray& OutData, TArgs&&... Args)
{
    MClass* Class = TObject::StaticClass();
    if (!Class || !FunctionName)
    {
        return false;
    }

    return BuildRpcPayloadForFunctionCall(Class->FindFunction(FunctionName), OutData, std::forward<TArgs>(Args)...);
}

template<auto MethodPtr>
struct TRpcMethodTraits;

template<typename TObject, typename... TArgs, void (TObject::*MethodPtr)(TArgs...)>
struct TRpcMethodTraits<MethodPtr>
{
    using ObjectType = TObject;
    using ArgsTuple = std::tuple<TRpcArgStorage<TArgs>...>;
};

template<auto MethodPtr, auto ValidatePtr = nullptr>
struct TRpcMethodInvoker;

template<typename TObject, typename... TArgs, void (TObject::*MethodPtr)(TArgs...), bool (TObject::*ValidatePtr)(TArgs...) const>
struct TRpcMethodInvoker<MethodPtr, ValidatePtr>
{
    using Traits = TRpcMethodTraits<MethodPtr>;
    using ObjectType = typename Traits::ObjectType;
    using ArgsTuple = typename Traits::ArgsTuple;

    template<size_t... Indices>
    static bool InvokeImpl(ObjectType* Object, MReflectArchive* InAr, std::index_sequence<Indices...>)
    {
        if (!Object || !InAr)
        {
            return false;
        }

        ArgsTuple Args{};
        SerializeRpcArgs(*InAr, std::get<Indices>(Args)...);

        const bool bValid = std::apply(
            [Object](auto&... UnpackedArgs)
            {
                return (Object->*ValidatePtr)(UnpackedArgs...);
            },
            Args);
        if (!bValid)
        {
            LOG_WARN("RPC validation failed for function");
            return false;
        }

        std::apply(
            [Object](auto&... UnpackedArgs)
            {
                (Object->*MethodPtr)(UnpackedArgs...);
            },
            Args);
        return true;
    }

    static bool Invoke(MObject* Object, MReflectArchive* InAr, MReflectArchive*)
    {
        if (!Object)
        {
            return false;
        }

        auto* TypedObject = static_cast<ObjectType*>(Object);
        return InvokeImpl(TypedObject, InAr, std::index_sequence_for<TArgs...>{});
    }
};

template<typename TObject, typename... TArgs, void (TObject::*MethodPtr)(TArgs...)>
struct TRpcMethodInvoker<MethodPtr, nullptr>
{
    using Traits = TRpcMethodTraits<MethodPtr>;
    using ObjectType = typename Traits::ObjectType;
    using ArgsTuple = typename Traits::ArgsTuple;

    template<size_t... Indices>
    static bool InvokeImpl(ObjectType* Object, MReflectArchive* InAr, std::index_sequence<Indices...>)
    {
        if (!Object || !InAr)
        {
            return false;
        }

        ArgsTuple Args{};
        SerializeRpcArgs(*InAr, std::get<Indices>(Args)...);

        std::apply(
            [Object](auto&... UnpackedArgs)
            {
                (Object->*MethodPtr)(UnpackedArgs...);
            },
            Args);
        return true;
    }

    static bool Invoke(MObject* Object, MReflectArchive* InAr, MReflectArchive*)
    {
        if (!Object)
        {
            return false;
        }

        auto* TypedObject = static_cast<ObjectType*>(Object);
        return InvokeImpl(TypedObject, InAr, std::index_sequence_for<TArgs...>{});
    }
};

template<auto MethodPtr, auto ValidatePtr = nullptr>
inline MFunction* CreateRpcFunction(
    const char* Name,
    EFunctionFlags Flags,
    ERpcType RpcType,
    bool bReliable,
    EServerType EndpointServerType = EServerType::Unknown)
{
    auto* Func = new MFunction();
    Func->Name = Name;
    Func->Flags = Flags;
    Func->RpcType = RpcType;
    Func->bReliable = bReliable;
    Func->EndpointServerType = EndpointServerType;
    Func->NativeInvoke = &TRpcMethodInvoker<MethodPtr, ValidatePtr>::Invoke;
    return Func;
}

template<typename TValue>
inline void SerializeNativeReturnValue(MReflectArchive& Ar, TValue& Value)
{
    Ar << Value;
}

template<auto MethodPtr>
struct TNativeMethodInvoker;

template<typename TObject, typename TReturn, typename... TArgs, TReturn (TObject::*MethodPtr)(TArgs...)>
struct TNativeMethodInvoker<MethodPtr>
{
    using ObjectType = TObject;
    using ArgsTuple = std::tuple<TRpcArgStorage<TArgs>...>;

    template<size_t... Indices>
    static bool InvokeImpl(ObjectType* Object, MReflectArchive* InAr, MReflectArchive* OutAr, std::index_sequence<Indices...>)
    {
        if (!Object)
        {
            return false;
        }

        ArgsTuple Args;
        if (InAr)
        {
            SerializeRpcArgs(*InAr, std::get<Indices>(Args)...);
        }
        if constexpr (std::is_void_v<TReturn>)
        {
            std::apply(
                [Object](auto&... UnpackedArgs)
                {
                    (Object->*MethodPtr)(UnpackedArgs...);
                },
                Args);
        }
        else
        {
            if (!OutAr)
            {
                return false;
            }
            TReturn ReturnValue = std::apply(
                [Object](auto&... UnpackedArgs)
                {
                    return (Object->*MethodPtr)(UnpackedArgs...);
                },
                Args);
            SerializeNativeReturnValue(*OutAr, ReturnValue);
        }
        return true;
    }

    static bool Invoke(MObject* Object, MReflectArchive* InAr, MReflectArchive* OutAr)
    {
        return InvokeImpl(static_cast<ObjectType*>(Object), InAr, OutAr, std::index_sequence_for<TArgs...>{});
    }
};

template<typename TObject, typename TReturn, typename... TArgs, TReturn (TObject::*MethodPtr)(TArgs...) const>
struct TNativeMethodInvoker<MethodPtr>
{
    using ObjectType = TObject;
    using ArgsTuple = std::tuple<TRpcArgStorage<TArgs>...>;

    template<size_t... Indices>
    static bool InvokeImpl(const ObjectType* Object, MReflectArchive* InAr, MReflectArchive* OutAr, std::index_sequence<Indices...>)
    {
        if (!Object)
        {
            return false;
        }

        ArgsTuple Args;
        if (InAr)
        {
            SerializeRpcArgs(*InAr, std::get<Indices>(Args)...);
        }
        if constexpr (std::is_void_v<TReturn>)
        {
            std::apply(
                [Object](auto&... UnpackedArgs)
                {
                    (Object->*MethodPtr)(UnpackedArgs...);
                },
                Args);
        }
        else
        {
            if (!OutAr)
            {
                return false;
            }
            TReturn ReturnValue = std::apply(
                [Object](auto&... UnpackedArgs)
                {
                    return (Object->*MethodPtr)(UnpackedArgs...);
                },
                Args);
            SerializeNativeReturnValue(*OutAr, ReturnValue);
        }
        return true;
    }

    static bool Invoke(MObject* Object, MReflectArchive* InAr, MReflectArchive* OutAr)
    {
        return InvokeImpl(static_cast<const ObjectType*>(Object), InAr, OutAr, std::index_sequence_for<TArgs...>{});
    }
};

template<auto MethodPtr>
inline MFunction* CreateNativeFunction(const char* Name, EFunctionFlags Flags)
{
    auto* Func = new MFunction();
    Func->Name = Name;
    Func->Flags = Flags;
    Func->NativeInvoke = &TNativeMethodInvoker<MethodPtr>::Invoke;
    return Func;
}

inline bool MObject::InvokeFunction(MFunction* Func, MReflectArchive* InAr, MReflectArchive* OutAr)
{
    if (!Func)
    {
        return false;
    }

    if (Func->NativeInvoke)
    {
        return Func->NativeInvoke(this, InAr, OutAr);
    }

    if (Func->NativeFunc && (!InAr || InAr->Data.empty()) && !OutAr)
    {
        Func->NativeFunc(this);
        return true;
    }

    return false;
}

template<typename TReturn, typename... TArgs>
inline bool MObject::InvokeFunction(const MString& InName, TReturn* OutReturn, TArgs&&... Args)
{
    MClass* LocalClass = GetClass();
    if (!LocalClass)
    {
        return false;
    }

    MFunction* Func = LocalClass->FindFunction(InName);
    if (!Func)
    {
        return false;
    }

    MReflectArchive InWriteAr;
    (void([&InWriteAr](auto&& Value)
    {
        using TValue = std::remove_cv_t<std::remove_reference_t<decltype(Value)>>;
        TValue Copy = static_cast<TValue>(Value);
        InWriteAr << Copy;
    }(std::forward<TArgs>(Args))), ...);
    MReflectArchive InReadAr(InWriteAr.Data);

    MReflectArchive OutAr;
    MReflectArchive* OutArPtr = nullptr;
    if constexpr (!std::is_void_v<TReturn>)
    {
        OutArPtr = &OutAr;
    }

    if (!InvokeFunction(Func, &InReadAr, OutArPtr))
    {
        return false;
    }

    if constexpr (!std::is_void_v<TReturn>)
    {
        if (!OutReturn)
        {
            return false;
        }
        MReflectArchive OutReadAr(OutAr.Data);
        TReturn ReturnValue{};
        OutReadAr << ReturnValue;
        *OutReturn = ReturnValue;
    }

    return true;
}

template<typename... TArgs>
inline bool MObject::CallFunctionArgs(const MString& InName, TArgs&&... Args)
{
    return InvokeFunction<void>(InName, nullptr, std::forward<TArgs>(Args)...);
}

template<typename TReturn, typename... TArgs>
inline bool MObject::CallFunctionWithReturn(const MString& InName, TReturn& OutReturn, TArgs&&... Args)
{
    return InvokeFunction<TReturn>(InName, &OutReturn, std::forward<TArgs>(Args)...);
}

inline bool MObject::ProcessEvent(const MString& InName, void* Params)
{
    MClass* LocalClass = GetClass();
    if (!LocalClass)
    {
        return false;
    }

    return ProcessEvent(LocalClass->FindFunction(InName), Params);
}

inline bool MObject::ProcessEvent(MFunction* Func, void* Params)
{
    if (!Func)
    {
        return false;
    }

    MReflectArchive InWriteAr;
    for (MProperty* Param : Func->Params)
    {
        if (!Param)
        {
            continue;
        }
        Param->WriteValue(Params, InWriteAr);
    }

    MReflectArchive InReadAr(InWriteAr.Data);
    MReflectArchive OutAr;
    MReflectArchive* OutArPtr = Func->ReturnProperty ? &OutAr : nullptr;
    if (!InvokeFunction(Func, &InReadAr, OutArPtr))
    {
        return false;
    }

    if (Func->ReturnProperty && Params)
    {
        MReflectArchive OutReadAr(OutAr.Data);
        Func->ReturnProperty->WriteValue(Params, OutReadAr);
    }

    return true;
}

inline bool MObject::InvokeSerializedFunction(MFunction* Func, MReflectArchive& InAr)
{
    return InvokeFunction(Func, &InAr, nullptr);
}

template<auto MethodPtr, typename... TArgs>
inline TByteArray BuildRpcPayloadForCall(TArgs&&... Args)
{
    return BuildRpcArgsPayload(std::forward<TArgs>(Args)...);
}

template<typename TObject>
inline uint16 GetRpcFunctionIdByName(const char* Name)
{
    MClass* Class = TObject::StaticClass();
    if (!Class || !Name)
    {
        return 0;
    }

    MFunction* Func = Class->FindFunction(Name);
    return Func ? Func->FunctionId : 0;
}

inline uint16 GetStableRpcFunctionIdByName(const char* ClassName, const char* FuncName)
{
    return ComputeStableReflectId(ClassName, FuncName);
}

template<typename TObject>
inline uint16 GetCachedRpcFunctionId(uint16& CachedFunctionId, const char* Name)
{
    if (CachedFunctionId != 0)
    {
        return CachedFunctionId;
    }

    CachedFunctionId = GetRpcFunctionIdByName<TObject>(Name);
    return CachedFunctionId;
}

#define MGET_RPC_FUNCTION_ID(ClassType, FuncName) \
    GetRpcFunctionIdByName<ClassType>(#FuncName)

#define MGET_CACHED_RPC_FUNCTION_ID(ClassType, FuncName, CacheVar) \
    GetCachedRpcFunctionId<ClassType>(CacheVar, #FuncName)

#define MGET_STABLE_RPC_FUNCTION_ID(ClassNameLiteral, FuncNameLiteral) \
    GetStableRpcFunctionIdByName(ClassNameLiteral, FuncNameLiteral)
