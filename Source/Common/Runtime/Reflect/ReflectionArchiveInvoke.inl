/**
 * @file ReflectionArchiveInvoke.inl
 * @brief 反射归档 MReflectArchive,以及基于它的参数序列化与函数调用适配。
 *
 * @detail 由 Reflection.h 在文件末尾包含,内容处于全局作用域:
 *   - MReflectArchive 是一个"双向"归档:方向由 bWriting/bReading 决定,
 *     同一个 operator<< 既负责写也负责读,因此调用方不需要两套代码;
 *   - 读越界不抛异常,只置 bReadOverflow,由上层检查 —— 这样协议解析不会因为
 *     一个坏包直接终止进程;
 *   - 网络/RPC 参数使用固定网络字节序（大端）；不是资产文件格式。
 *   - 后半部分用成员函数指针 + 索引序列做编译期展开,把 RPC/原生方法包装成
 *     MFunction::NativeInvoke,使反射能真正调用到 C++ 方法。
 */
#include <cstring>

/**
 * @brief MReflectArchive - 可读可写的反射归档缓冲区。
 *
 * @detail 两种用法:
 *   - 写:默认构造(bWriting=true),多次 `Ar << Value` 追加到 Data;
 *   - 读:用 `MReflectArchive Ar(Buffer)` 构造(bReading=true),从 Data 顺序消费。
 *   读模式下若剩余字节不足或结构体元数据缺失,只置 bReadOverflow 并返回,
 *   不会抛异常;调用方必须显式检查该标志(SerializeArchiveValue 就是这么做的)。
 *   注意本类没有"同时读写"的用法,构造函数也保证了二者互斥。
 */
class MReflectArchive {
    public:
    TByteArray Data;              // 写方向:累积输出;读方向:待消费的输入
    size_t     ReadPos       = 0; // 读游标(仅 bReading 时有意义)
    bool       bReading      = false;
    bool       bWriting      = true;  // 默认构造即写模式
    bool       bReadOverflow = false; // 读越界或类型不受支持时置位,不抛异常

    /** @brief 构造写归档:bWriting=true、bReading=false。 */
    MReflectArchive() {
        bWriting = true;
        bReading = false;
    }
    /** @brief 构造读归档:以 InData 为输入,bReading=true、bWriting=false。 */
    explicit MReflectArchive(const TByteArray& InData) : Data(InData), ReadPos(0), bReading(true), bWriting(false) {
    }

    // 基本数值统一使用固定网络字节序，方向由本对象状态决定。
    // 注意:这里特意按具体类型重载而不是模板,是为了给 MString / SVector / SRotator
    // 这些"非平凡或有专用格式"的类型留出优先匹配的普通函数。
    MReflectArchive& operator<<(uint8& Value) {
        return WriteScalar(Value);
    }
    MReflectArchive& operator<<(uint16& Value) {
        return WriteScalar(Value);
    }
    MReflectArchive& operator<<(uint32& Value) {
        return WriteScalar(Value);
    }
    MReflectArchive& operator<<(uint64& Value) {
        return WriteScalar(Value);
    }
    MReflectArchive& operator<<(int8& Value) {
        return WriteScalar(Value);
    }
    MReflectArchive& operator<<(int16& Value) {
        return WriteScalar(Value);
    }
    MReflectArchive& operator<<(int32& Value) {
        return WriteScalar(Value);
    }
    MReflectArchive& operator<<(int64& Value) {
        return WriteScalar(Value);
    }
    MReflectArchive& operator<<(float& Value) {
        return WriteScalar(Value);
    }
    MReflectArchive& operator<<(double& Value) {
        return WriteScalar(Value);
    }
    MReflectArchive& operator<<(MString& Value) {
        return WriteString(Value);
    }
    MReflectArchive& operator<<(SVector& Value) { *this << Value.X << Value.Y << Value.Z; return *this; }
    MReflectArchive& operator<<(SRotator& Value) { *this << Value.Pitch << Value.Yaw << Value.Roll; return *this; }
    /**
     * @brief 平凡可拷贝类型的通配重载:直接按字节 memcpy。
     *
     * @detail 排除算术/枚举/MString/SVector/SRotator,因为它们各自有更专门的语义
     *   (枚举要按底层类型、字符串要带长度前缀)。
     */
    template <typename T, std::enable_if_t<std::is_trivially_copyable_v<T> && !std::is_arithmetic_v<T> && !std::is_enum_v<T> && !std::is_same_v<T, MString> && !std::is_same_v<T, SVector> && !std::is_same_v<T, SRotator>, int> = 0>
    MReflectArchive& operator<<(T& Value) {
        return WritePOD(Value);
    }

    /**
     * @brief 非平凡且非容器的通配重载:查已注册结构体元数据,按字段递归序列化。
     *
     * @detail 这是"嵌套 MSTRUCT / 带 vector、MString 成员的结构体"能正确读回的关键 ——
     *   它不做浅拷贝,而是一个字段一个字段地递归。
     *   类型未注册结构体时,读方向置 bReadOverflow 报错,但写方向只是静默地
     *   什么都不写(bReadOverflow 被赋回 false),生成的数据会短一截且没有报错信号。
     *   因此新增嵌套类型时必须记得注册结构体元数据。
     */
    template <typename T, std::enable_if_t<!std::is_arithmetic_v<T> && !std::is_enum_v<T> && !std::is_same_v<T, MString> && !std::is_same_v<T, SVector> && !std::is_same_v<T, SRotator> && !std::is_trivially_copyable_v<T>, int> = 0>
    MReflectArchive& operator<<(T& Value) {
        if (MClass* StructMeta = MObject::FindStruct(std::type_index(typeid(T)))) {
            StructMeta->WriteSnapshot(&Value, *this);
            return *this;
        }

        bReadOverflow = bReading; // 读模式=报错;写模式=静默丢弃(无 1 字节输出)
        return *this;
    }

    /** @brief bool 走 1 字节编码(0/1),读回时非 0 即为 true;避免 bool 的实现相关宽度。 */
    MReflectArchive& operator<<(bool& Value) {
        uint8 Temp = Value ? 1u : 0u;
        *this << Temp;
        if (bReading) {
            Value = (Temp != 0u);
        }
        return *this;
    }

    /** @brief 枚举按底层整数类型序列化,不写枚举宽度;读回时强转回 TEnum。 */
    template <typename TEnum, std::enable_if_t<std::is_enum_v<TEnum>, int> = 0> MReflectArchive& operator<<(TEnum& Value) {
        using TUnderlying    = std::underlying_type_t<TEnum>;
        TUnderlying RawValue = static_cast<TUnderlying>(Value);
        *this << RawValue;
        if (bReading) {
            Value = static_cast<TEnum>(RawValue);
        }
        return *this;
    }

    /**
     * @brief WriteBytes - 按 Size 字节整体拷贝;主要用于结构体等复杂类型的原始转储。
     * @param Buffer 读写缓冲区;为 nullptr 或 Size 为 0 时直接返回
     * @param Size   字节数
     *
     * @detail 读方向越界时只置 bReadOverflow 并保持 Buffer 不变,不做部分写入。
     */
    MReflectArchive& WriteBytes(void* Buffer, size_t Size) {
        if (!Buffer || Size == 0) {
            return *this;
        }

        if (bWriting) {
            size_t OldSize = Data.size();
            Data.resize(OldSize + Size);
            memcpy(Data.data() + OldSize, Buffer, Size);
        } else if (bReading && ReadPos + Size <= Data.size()) {
            memcpy(Buffer, Data.data() + ReadPos, Size);
            ReadPos += Size;
        } else if (bReading) {
            bReadOverflow = true;
        }
        return *this;
    }

    private:
    /**
     * @brief WritePOD - 以 sizeof(T) 为单位做一次 memcpy 式的读或写。
     * @param Value 读写目标
     *
     * @detail 不做端序转换(本机序),不做对齐处理(按字节流追加)。
     *   读方向越界时只置 bReadOverflow,Value 保持原值。
     */
    template <typename T> MReflectArchive& WritePOD(T& Value) {
        if (bWriting) {
            size_t OldSize = Data.size();
            Data.resize(OldSize + sizeof(T));
            memcpy(Data.data() + OldSize, &Value, sizeof(T));
        } else if (bReading && ReadPos + sizeof(T) <= Data.size()) {
            memcpy(&Value, Data.data() + ReadPos, sizeof(T));
            ReadPos += sizeof(T);
        } else if (bReading) {
            bReadOverflow = true;
        }
        return *this;
    }

    /** @brief 按网络字节序读写整数/浮点的位表示，保持 IEEE 754 的既有约定。 */
    template <typename T> MReflectArchive& WriteScalar(T& Value) {
        static_assert(std::is_arithmetic_v<T> && !std::is_same_v<T, bool>, "WriteScalar only supports numeric types");
        if constexpr (sizeof(T) == 1) {
            return WritePOD(Value);
        } else {
            using TBits = std::conditional_t<sizeof(T) == 2, uint16, std::conditional_t<sizeof(T) == 4, uint32, uint64>>;
            TBits Bits = 0;
            if (bWriting) {
                std::memcpy(&Bits, &Value, sizeof(Value));
                Bits = HostToNetwork(Bits);
                return WritePOD(Bits);
            }
            if (bReading && ReadPos + sizeof(TBits) <= Data.size()) {
                std::memcpy(&Bits, Data.data() + ReadPos, sizeof(Bits));
                ReadPos += sizeof(Bits);
                Bits = NetworkToHost(Bits);
                std::memcpy(&Value, &Bits, sizeof(Value));
            } else if (bReading) {
                bReadOverflow = true;
            }
            return *this;
        }
    }

    /**
     * @brief WriteString - 以"uint32 长度前缀 + 原始字符(不含结尾 NUL)"编码字符串。
     * @param Value 读写目标字符串
     *
     * @detail 写方向先写长度再写内容(长度为 0 时只写长度,不分配);
     *   读方向按长度一次性 assign,越界则置 bReadOverflow 并保持目标不变。
     *   注意长度是字节数而非字符数,多字节编码下不代表字符个数。
     */
    MReflectArchive& WriteString(MString& Value) {
        if (bWriting) {
            uint32 Len = (uint32)Value.size();
            *this << Len;
            if (Len > 0) {
                size_t OldSize = Data.size();
                Data.resize(OldSize + Len);
                memcpy(Data.data() + OldSize, Value.data(), Len);
            }
        } else if (bReading) {
            uint32 Len = 0;
            *this << Len;
            if (Len > 0 && ReadPos + Len <= Data.size()) {
                Value.assign((char*)Data.data() + ReadPos, Len);
                ReadPos += Len;
            } else if (Len > 0) {
                bReadOverflow = true;
            }
        }
        return *this;
    }
};

/** @brief SerializeRpcArgs - 把一组实参依次写进归档(逗号折叠表达式,顺序即形参顺序)。 */
template <typename... TArgs> inline void SerializeRpcArgs(MReflectArchive& Ar, TArgs&... Args) {
    (void(Ar << Args), ...);
}

/**
 * @brief BuildRpcArgsPayload - 把一组实参序列化成字节载荷。
 * @param Args 待序列化的实参(完美转发)
 * @return 序列化后的字节数组(由归档 Data 移出)
 *
 * @detail 每个实参先拷一份再写:因为 operator<< 取非 const 引用,
 *   而这里可能是右值/const 实参,拷贝是让它能绑定的代价最低的做法。
 *   返回的是本机序、无对齐填充的字节流,只适用于同构进程间的 RPC。
 */
template <typename... TArgs> inline TByteArray BuildRpcArgsPayload(TArgs&&... Args) {
    MReflectArchive Ar;
    (void([&Ar](auto&& Value) {
         using TValue = std::remove_cv_t<std::remove_reference_t<decltype(Value)>>;
         TValue Copy  = static_cast<TValue>(Value);
         Ar << Copy;
     }(std::forward<TArgs>(Args))),
     ...);
    return std::move(Ar.Data);
}

// 下面一族 SerializeArchiveValue 是"带成功标志"的序列化入口:
// 返回 false 表示本次读/写不完整(越界或类型不受支持),调用方必须检查。
// 分成多个重载而不是一个 if constexpr 链,是为了让容器元素类型也能参与重载决策。

inline bool SerializeArchiveValue(MReflectArchive& Ar, MString& Value) {
    Ar << Value;
    return !Ar.bReadOverflow;
}

inline bool SerializeArchiveValue(MReflectArchive& Ar, SVector& Value) {
    Ar << Value;
    return !Ar.bReadOverflow;
}

inline bool SerializeArchiveValue(MReflectArchive& Ar, SRotator& Value) {
    Ar << Value;
    return !Ar.bReadOverflow;
}

/** @brief 算术类型与枚举:最常用的一条路径,直接按底层宽度写。 */
template <typename TValue, std::enable_if_t<std::is_arithmetic_v<TValue> || std::is_enum_v<TValue>, int> = 0> inline bool SerializeArchiveValue(MReflectArchive& Ar, TValue& Value) {
    Ar << Value;
    return !Ar.bReadOverflow;
}

/** @brief 其他平凡可拷贝类型:按 sizeof 整体 memcpy(不查结构体元数据)。 */
template <
    typename TValue,
    std::enable_if_t<std::is_trivially_copyable_v<TValue> && !std::is_arithmetic_v<TValue> && !std::is_enum_v<TValue> && !std::is_same_v<TValue, MString> && !std::is_same_v<TValue, SVector> && !std::is_same_v<TValue, SRotator>, int> = 0>
inline bool SerializeArchiveValue(MReflectArchive& Ar, TValue& Value) {
    Ar << Value;
    return !Ar.bReadOverflow;
}

/**
 * @brief TVector 容器:先写 uint32 元素个数,再逐个元素序列化。
 *
 * @detail 读方向按"先清空、再 reserve、逐元素读入并 push_back"处理,
 *   元素是默认构造后覆盖,因此元素类型必须有默认构造函数。
 *   计数与元素内容分开写,所以坏数据下可能出现"计数很大但元素读不完",
 *   这种情况由元素级的返回 false 兜住。
 */
template <typename TElement> inline bool SerializeArchiveValue(MReflectArchive& Ar, TVector<TElement>& Values) {
    uint32 Count = static_cast<uint32>(Values.size());
    Ar << Count;
    if (Ar.bReadOverflow) {
        return false;
    }

    if (Ar.bWriting) {
        for (TElement& Value : Values) {
            if (!SerializeArchiveValue(Ar, Value)) {
                return false;
            }
        }
        return true;
    }

    Values.clear();
    Values.reserve(Count);
    for (uint32 Index = 0; Index < Count; ++Index) {
        TElement Value{};
        if (!SerializeArchiveValue(Ar, Value)) {
            return false;
        }
        Values.push_back(std::move(Value));
    }
    return true;
}

/**
 * @brief 非平凡、非容器的类型:交给已注册的结构体元数据按字段递归序列化。
 *
 * @detail 与 operator<< 的通配重载是同一套策略,只是一个返回 bool。
 *   类型未注册时直接返回 false(读写两个方向都会失败,不会像 operator<< 那样静默)。
 */
template <
    typename TValue,
    std::enable_if_t<!std::is_arithmetic_v<TValue> && !std::is_enum_v<TValue> && !std::is_same_v<TValue, MString> && !std::is_same_v<TValue, SVector> && !std::is_same_v<TValue, SRotator> && !std::is_trivially_copyable_v<TValue>, int> = 0>
inline bool SerializeArchiveValue(MReflectArchive& Ar, TValue& Value) {
    if (MClass* StructMeta = MObject::FindStruct(std::type_index(typeid(TValue)))) {
        StructMeta->WriteSnapshot(&Value, Ar);
        return !Ar.bReadOverflow;
    }

    return false;
}

/**
 * @brief SerializeFunctionArgByMeta - 按形参元数据校验类型后再序列化一个实参。
 * @param Ar  目标归档
 * @param Prop 形参的属性描述符(提供 CppTypeIndex 做类型校验)
 * @param Arg  实参
 * @return true 类型匹配且序列化成功,false 未提供元数据或类型不符
 *
 * @detail 这里做的是"编译期实参类型 == 运行期登记类型"的强校验:
 *   两边 type_index 不等就直接失败,而不是尝试隐式转换。
 *   因此形参登记类型必须与调用点实参类型完全一致(int32 与 int 在部分平台也不同)。
 *   写之前先按值拷一份,理由与 BuildRpcArgsPayload 相同。
 */
template <typename TArg> inline bool SerializeFunctionArgByMeta(MReflectArchive& Ar, const MProperty* Prop, TArg&& Arg) {
    if (!Prop) {
        return false;
    }

    using TStorage = std::remove_cv_t<std::remove_reference_t<TArg>>;
    if (Prop->CppTypeIndex != std::type_index(typeid(TStorage))) {
        return false;
    }

    TStorage Copy = static_cast<TStorage>(Arg);
    return SerializeArchiveValue(Ar, Copy);
}

/** @brief 无参重载:仅要求函数存在且确实没有形参。 */
inline bool SerializeFunctionArgsByMeta(const MFunction* Func, MReflectArchive&) {
    return Func && Func->Params.empty();
}

/**
 * @brief SerializeFunctionArgsByMeta - 按元数据逐个序列化全部实参。
 * @param Func 目标函数(提供形参表)
 * @param Ar   目标归档
 * @param Arg  第一个实参,Rest 为其余实参
 * @return true 实参个数、类型全部匹配且序列化成功
 *
 * @detail 个数必须与 Func->Params.size() 完全相等,少了多了都直接失败(不做默认值填充)。
 *   逐项写入,一旦某项失败立即停止并把 bOk 置 false(用 until-fold 保证不短路求值,
 *   避免展开顺序不确定带来的问题);Index 越界也会被置 false 兜住。
 */
template <typename TArg, typename... TRest> inline bool SerializeFunctionArgsByMeta(const MFunction* Func, MReflectArchive& Ar, TArg&& Arg, TRest&&... Rest) {
    if (!Func) {
        return false;
    }

    constexpr size_t ArgCount = 1 + sizeof...(TRest);
    if (Func->Params.size() != ArgCount) {
        return false;
    }

    size_t Index        = 0;
    bool   bOk          = true;
    auto   SerializeOne = [&](auto&& Value) {
        if (!bOk || Index >= Func->Params.size()) {
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

/**
 * @brief BuildRpcPayloadForFunction - 把已序列化的实参载荷封成可发包的 RPC 消息。
 * @param Func       目标函数(提供 FunctionId)
 * @param InPayload  已序列化的实参字节
 * @param OutData    输出消息字节
 * @return true 封装成功
 *
 * @detail 只是加上函数 id 与消息头(细节在 BuildServerRpcPayload),不做参数校验。
 */
inline bool BuildRpcPayloadForFunction(const MFunction* Func, const TByteArray& InPayload, TByteArray& OutData) {
    if (!Func) {
        return false;
    }
    return BuildServerRpcPayload(Func->FunctionId, InPayload, OutData);
}

/**
 * @brief BuildRpcPayloadForFunctionCall - 按实参现场序列化并封包(已拿到 MFunction 的版本)。
 * @param Func    目标函数
 * @param OutData 输出消息字节
 * @param Args    调用的实参
 * @return true 序列化与封包都成功
 */
template <typename... TArgs> inline bool BuildRpcPayloadForFunctionCall(const MFunction* Func, TByteArray& OutData, TArgs&&... Args) {
    if (!Func) {
        return false;
    }

    MReflectArchive Ar;
    if (!SerializeFunctionArgsByMeta(Func, Ar, std::forward<TArgs>(Args)...)) {
        return false;
    }

    return BuildRpcPayloadForFunction(Func, Ar.Data, OutData);
}

/**
 * @brief BuildRpcPayloadForFunctionCall - 按"类 + 函数名"现场序列化并封包。
 * @param FunctionName 函数名;为空则失败
 * @param OutData      输出消息字节
 * @param Args         调用的实参
 * @return true 函数存在、实参匹配且封包成功
 *
 * @detail 用 TObject::StaticClass() 拿类元数据(会触发该类的首次注册),再按名字查函数。
 */
template <typename TObject, typename... TArgs> inline bool BuildRpcPayloadForFunctionCall(const char* FunctionName, TByteArray& OutData, TArgs&&... Args) {
    MClass* Class = TObject::StaticClass();
    if (!Class || !FunctionName) {
        return false;
    }

    return BuildRpcPayloadForFunctionCall(Class->FindFunction(FunctionName), OutData, std::forward<TArgs>(Args)...);
}

/** @brief TRpcMethodTraits - 由成员函数指针反推宿主类型与"去引用后"的形参元组。 */
template <auto MethodPtr> struct TRpcMethodTraits;

template <typename TObject, typename... TArgs, void (TObject::*MethodPtr)(TArgs...)> struct TRpcMethodTraits<MethodPtr> {
    using ObjectType = TObject;
    using ArgsTuple  = std::tuple<TRpcArgStorage<TArgs>...>;
};

/** @brief TRpcMethodInvoker - 把"成员函数指针(+可选校验函数)"包成 MFunction::NativeInvoke 回调。 */
template <auto MethodPtr, auto ValidatePtr = nullptr> struct TRpcMethodInvoker;

/**
 * @brief 带校验函数的 RPC 方法调用器。
 *
 * @detail 调用顺序:先从归档反序列化出全部实参,再调用 ValidatePtr 校验,
 *   校验不通过则丢弃本次调用(只打 LOG_WARN,不执行方法体)。
 *   校验函数签名必须是 `bool (TObject::*)(Args...) const`,形参表与目标方法一致。
 *   注意 RPC 方法没有返回值通道:OutAr 形参被忽略。
 */
template <typename TObject, typename... TArgs, void (TObject::*MethodPtr)(TArgs...), bool (TObject::*ValidatePtr)(TArgs...) const> struct TRpcMethodInvoker<MethodPtr, ValidatePtr> {
    using Traits     = TRpcMethodTraits<MethodPtr>;
    using ObjectType = typename Traits::ObjectType;
    using ArgsTuple  = typename Traits::ArgsTuple;

    template <size_t... Indices> static bool InvokeImpl(ObjectType* Object, MReflectArchive* InAr, std::index_sequence<Indices...>) {
        if (!Object || !InAr) {
            return false;
        }

        ArgsTuple Args{}; // 全部实参先默认构造,再按序覆盖
        SerializeRpcArgs(*InAr, std::get<Indices>(Args)...);

        const bool bValid = std::apply([Object](auto&... UnpackedArgs) { return (Object->*ValidatePtr)(UnpackedArgs...); }, Args);
        if (!bValid) {
            LOG_WARN("RPC validation failed for function");
            return false;
        }

        std::apply([Object](auto&... UnpackedArgs) { (Object->*MethodPtr)(UnpackedArgs...); }, Args);
        return true;
    }

    /**
     * @brief Invoke - NativeInvoke 入口。
     *
     * @detail 这里用的是 static_cast 而非 dynamic_cast:调用方保证 MObject 实际类型正确,
     *   类型不符属于编程错误,不做运行期兜底。
     */
    static bool Invoke(MObject* Object, MReflectArchive* InAr, MReflectArchive*) {
        if (!Object) {
            return false;
        }

        auto* TypedObject = static_cast<ObjectType*>(Object);
        return InvokeImpl(TypedObject, InAr, std::index_sequence_for<TArgs...>{});
    }
};

/** @brief 不带校验函数的版本:反序列化实参后直接调用。 */
template <typename TObject, typename... TArgs, void (TObject::*MethodPtr)(TArgs...)> struct TRpcMethodInvoker<MethodPtr, nullptr> {
    using Traits     = TRpcMethodTraits<MethodPtr>;
    using ObjectType = typename Traits::ObjectType;
    using ArgsTuple  = typename Traits::ArgsTuple;

    template <size_t... Indices> static bool InvokeImpl(ObjectType* Object, MReflectArchive* InAr, std::index_sequence<Indices...>) {
        if (!Object || !InAr) {
            return false;
        }

        ArgsTuple Args{};
        SerializeRpcArgs(*InAr, std::get<Indices>(Args)...);

        std::apply([Object](auto&... UnpackedArgs) { (Object->*MethodPtr)(UnpackedArgs...); }, Args);
        return true;
    }

    static bool Invoke(MObject* Object, MReflectArchive* InAr, MReflectArchive*) {
        if (!Object) {
            return false;
        }

        auto* TypedObject = static_cast<ObjectType*>(Object);
        return InvokeImpl(TypedObject, InAr, std::index_sequence_for<TArgs...>{});
    }
};

/**
 * @brief CreateRpcFunction - 构造一个 RPC 函数描述符并绑定调用器。
 * @param Name               函数名(会字符串化进 MFunction::Name)
 * @param Flags              EFunctionFlags 位组合
 * @param RpcType            ERpcType(RPC 种类)
 * @param bReliable          是否可靠投递
 * @param EndpointServerType 承载该 RPC 的服务器类型,默认 Unknown(不限)
 * @return 新建的 MFunction,所有权交给调用方(由 MClass::RegisterFunction 接管)
 *
 * @detail 只登记"怎么调"和"走什么通道",不登记形参表 —— 形参由生成器另外调用
 *   MFunction::Params 填充,反射调用前会用它做类型校验。
 */
template <auto MethodPtr, auto ValidatePtr = nullptr> inline MFunction* CreateRpcFunction(const char* Name, EFunctionFlags Flags, ERpcType RpcType, bool bReliable, EServerType EndpointServerType = EServerType::Unknown) {
    auto* Func               = new MFunction();
    Func->Name               = Name;
    Func->Flags              = Flags;
    Func->RpcType            = RpcType;
    Func->bReliable          = bReliable;
    Func->EndpointServerType = EndpointServerType;
    Func->NativeInvoke       = &TRpcMethodInvoker<MethodPtr, ValidatePtr>::Invoke;
    return Func;
}

/** @brief SerializeNativeReturnValue - 把原生方法返回值写进归档(非 void 返回类型专用)。 */
template <typename TValue> inline void SerializeNativeReturnValue(MReflectArchive& Ar, TValue& Value) {
    Ar << Value;
}

/** @brief TNativeMethodInvoker - 把普通成员函数指针(带返回值、可为 const)包成 NativeInvoke。 */
template <auto MethodPtr> struct TNativeMethodInvoker;

/**
 * @brief 非 const 成员函数的调用器。
 *
 * @detail 实参约定:InAr 非空时从中反序列化全部实参;为空表示无参调用。
 *   返回值为 void 时忽略 OutAr;否则 OutAr 必须非空,返回值序列化后写进它。
 *   注意 Args 是默认初始化而非值初始化(`ArgsTuple Args;`),只有当 InAr 提供了
 *   全部实参时才安全 —— 传空 InAr 又带 non-POD 形参会导致实参处于不确定状态。
 */
template <typename TObject, typename TReturn, typename... TArgs, TReturn (TObject::*MethodPtr)(TArgs...)> struct TNativeMethodInvoker<MethodPtr> {
    using ObjectType = TObject;
    using ArgsTuple  = std::tuple<TRpcArgStorage<TArgs>...>;

    template <size_t... Indices> static bool InvokeImpl(ObjectType* Object, MReflectArchive* InAr, MReflectArchive* OutAr, std::index_sequence<Indices...>) {
        if (!Object) {
            return false;
        }

        ArgsTuple Args;
        if (InAr) {
            SerializeRpcArgs(*InAr, std::get<Indices>(Args)...);
        }
        if constexpr (std::is_void_v<TReturn>) {
            std::apply([Object](auto&... UnpackedArgs) { (Object->*MethodPtr)(UnpackedArgs...); }, Args);
        } else {
            if (!OutAr) {
                return false; // 有返回值却没有输出归档:直接失败,不调用方法
            }
            TReturn ReturnValue = std::apply([Object](auto&... UnpackedArgs) { return (Object->*MethodPtr)(UnpackedArgs...); }, Args);
            SerializeNativeReturnValue(*OutAr, ReturnValue);
        }
        return true;
    }

    static bool Invoke(MObject* Object, MReflectArchive* InAr, MReflectArchive* OutAr) {
        return InvokeImpl(static_cast<ObjectType*>(Object), InAr, OutAr, std::index_sequence_for<TArgs...>{});
    }
};

/** @brief const 成员函数版本:实现与上面相同,区别是接收对象为 const 指针。 */
template <typename TObject, typename TReturn, typename... TArgs, TReturn (TObject::*MethodPtr)(TArgs...) const> struct TNativeMethodInvoker<MethodPtr> {
    using ObjectType = TObject;
    using ArgsTuple  = std::tuple<TRpcArgStorage<TArgs>...>;

    template <size_t... Indices> static bool InvokeImpl(const ObjectType* Object, MReflectArchive* InAr, MReflectArchive* OutAr, std::index_sequence<Indices...>) {
        if (!Object) {
            return false;
        }

        ArgsTuple Args;
        if (InAr) {
            SerializeRpcArgs(*InAr, std::get<Indices>(Args)...);
        }
        if constexpr (std::is_void_v<TReturn>) {
            std::apply([Object](auto&... UnpackedArgs) { (Object->*MethodPtr)(UnpackedArgs...); }, Args);
        } else {
            if (!OutAr) {
                return false;
            }
            TReturn ReturnValue = std::apply([Object](auto&... UnpackedArgs) { return (Object->*MethodPtr)(UnpackedArgs...); }, Args);
            SerializeNativeReturnValue(*OutAr, ReturnValue);
        }
        return true;
    }

    static bool Invoke(MObject* Object, MReflectArchive* InAr, MReflectArchive* OutAr) {
        return InvokeImpl(static_cast<const ObjectType*>(Object), InAr, OutAr, std::index_sequence_for<TArgs...>{});
    }
};

/**
 * @brief CreateNativeFunction - 构造一个原生(非 RPC)函数描述符并绑定调用器。
 * @param Name  函数名
 * @param Flags EFunctionFlags 位组合
 * @return 新建的 MFunction,所有权交给调用方
 *
 * @detail 与 CreateRpcFunction 的区别是不设置 RpcType/bReliable/EndpointServerType,
 *   即该函数不参与网络投递,只能在本进程内被反射调用。
 */
template <auto MethodPtr> inline MFunction* CreateNativeFunction(const char* Name, EFunctionFlags Flags) {
    auto* Func         = new MFunction();
    Func->Name         = Name;
    Func->Flags        = Flags;
    Func->NativeInvoke = &TNativeMethodInvoker<MethodPtr>::Invoke;
    return Func;
}

/**
 * @brief MObject::InvokeFunction - 用已序列化的入参归档调用函数(核心分发)。
 * @param Func   目标函数
 * @param InAr   入参归档;可为空表示"无入参"
 * @param OutAr  出参归档;可为空表示不接收返回值
 * @return true 表示确实执行了某个调用体
 *
 * @detail 两条互斥的调用路径:
 *   1. NativeInvoke —— 成员函数指针包装器,负责反序列化实参与返回值,最常用;
 *   2. NativeFunc —— 无参、无返回、无入参字节的极简回调,只有 InAr 为空或
 *      其 Data 为空、且没有 OutAr 时才允许走它(这是它对调用前提的硬要求)。
 *   两者都不满足则返回 false —— 注意"函数存在但没有可调用体"和"调用失败"
 *   在这里无法区分。
 */
inline bool MObject::InvokeFunction(MFunction* Func, MReflectArchive* InAr, MReflectArchive* OutAr) {
    if (!Func) {
        return false;
    }

    if (Func->NativeInvoke) {
        return Func->NativeInvoke(this, InAr, OutAr);
    }

    if (Func->NativeFunc && (!InAr || InAr->Data.empty()) && !OutAr) {
        Func->NativeFunc(this);
        return true;
    }

    return false;
}

/**
 * @brief MObject::InvokeFunction - 按名字调用并带回返回值。
 * @param InName    函数名
 * @param OutReturn 返回值输出指针;TReturn 非 void 时必须非空
 * @param Args      实参
 * @return true 表示函数存在、调用成功且(如有)返回值已取回
 *
 * @detail 实参的处理方式:先逐个拷贝并写进临时归档(写→读一趟),
 *   再交给无参版 InvokeFunction 分发 —— 这样调用器只需面对统一的字节流,
 *   不必区分"直接传参"和"从网络包传参"两种来源。
 *   返回同样要过一趟归档:调用器写出的字节被读成 ReturnValue 后拷贝给调用方;
 *   因此 TReturn 必须是可序列化类型。
 */
template <typename TReturn, typename... TArgs> inline bool MObject::InvokeFunction(const MString& InName, TReturn* OutReturn, TArgs&&... Args) {
    MClass* LocalClass = GetClass();
    if (!LocalClass) {
        return false;
    }

    MFunction* Func = LocalClass->FindFunction(InName);
    if (!Func) {
        return false;
    }

    MReflectArchive InWriteAr;
    (void([&InWriteAr](auto&& Value) {
         using TValue = std::remove_cv_t<std::remove_reference_t<decltype(Value)>>;
         TValue Copy  = static_cast<TValue>(Value);
         InWriteAr << Copy;
     }(std::forward<TArgs>(Args))),
     ...);
    MReflectArchive InReadAr(InWriteAr.Data);

    MReflectArchive  OutAr;
    MReflectArchive* OutArPtr = nullptr;
    if constexpr (!std::is_void_v<TReturn>) { // void 返回时不提供出参归档,调用器也不会写
        OutArPtr = &OutAr;
    }

    if (!InvokeFunction(Func, &InReadAr, OutArPtr)) {
        return false;
    }

    if constexpr (!std::is_void_v<TReturn>) {
        if (!OutReturn) {
            return false;
        }
        MReflectArchive OutReadAr(OutAr.Data);
        TReturn         ReturnValue{};
        OutReadAr << ReturnValue;
        *OutReturn = ReturnValue;
    }

    return true;
}

/** @brief CallFunctionArgs - 按名字调用无返回值函数并传参;等价于 InvokeFunction<void>。 */
template <typename... TArgs> inline bool MObject::CallFunctionArgs(const MString& InName, TArgs&&... Args) {
    return InvokeFunction<void>(InName, nullptr, std::forward<TArgs>(Args)...);
}

/** @brief CallFunctionWithReturn - 按名字调用并取回返回值;等价于 InvokeFunction<TReturn>。 */
template <typename TReturn, typename... TArgs> inline bool MObject::CallFunctionWithReturn(const MString& InName, TReturn& OutReturn, TArgs&&... Args) {
    return InvokeFunction<TReturn>(InName, &OutReturn, std::forward<TArgs>(Args)...);
}

/** @brief ProcessEvent - 按名字做一次事件分发;名字查不到则失败(见下面按 MFunction* 的版本)。 */
inline bool MObject::ProcessEvent(const MString& InName, void* Params) {
    MClass* LocalClass = GetClass();
    if (!LocalClass) {
        return false;
    }

    return ProcessEvent(LocalClass->FindFunction(InName), Params);
}

/**
 * @brief ProcessEvent - 用"参数结构体"做一次事件分发。
 * @param Func   目标函数
 * @param Params 参数结构体基址:入参从它读取,返回值也写回它
 * @return true 表示调用成功(返回值已写回 Params)
 *
 * @detail 与 CallFunctionArgs 的区别是这里不传散装实参,而是把一个参数结构体
 *   按 Func->Params 逐字段抠出来序列化(正是 Param->WriteValue(Params, ...) 的用途:
 *   每个形参描述符只知道自己在结构体里的偏移)。
 *   有返回值时,返回值会被写回 Params 的同一块内存 —— 所以 Params 通常是一个
 *   "入参 + 出参"复用的结构体,调用方要保证它足够大。
 *   Params 为 nullptr 时,无参函数仍可调用(入参循环没有可读对象)。
 */
inline bool MObject::ProcessEvent(MFunction* Func, void* Params) {
    if (!Func) {
        return false;
    }

    MReflectArchive InWriteAr;
    for (MProperty* Param : Func->Params) {
        if (!Param) {
            continue; // 形参表里出现空描述符就跳过,不中断整次分发
        }
        Param->WriteValue(Params, InWriteAr);
    }

    MReflectArchive  InReadAr(InWriteAr.Data);
    MReflectArchive  OutAr;
    MReflectArchive* OutArPtr = Func->ReturnProperty ? &OutAr : nullptr;
    if (!InvokeFunction(Func, &InReadAr, OutArPtr)) {
        return false;
    }

    if (Func->ReturnProperty && Params) {
        MReflectArchive OutReadAr(OutAr.Data);
        Func->ReturnProperty->WriteValue(Params, OutReadAr); // 写回同一块 Params 内存
    }

    return true;
}

/** @brief InvokeSerializedFunction - 直接把已有归档当入参调用,不接收返回值。 */
inline bool MObject::InvokeSerializedFunction(MFunction* Func, MReflectArchive& InAr) {
    return InvokeFunction(Func, &InAr, nullptr);
}

/**
 * @brief BuildRpcPayloadForCall - 按成员函数指针类型推断并构造调用载荷。
 * @param Args 实参
 * @return 序列化后的载荷字节
 *
 * @detail MethodPtr 模板参数当前只用于让调用点显式指定函数,函数体内并未使用它 ——
 *   也就是说这里不做任何函数校验,只负责序列化实参。
 */
template <auto MethodPtr, typename... TArgs> inline TByteArray BuildRpcPayloadForCall(TArgs&&... Args) {
    return BuildRpcArgsPayload(std::forward<TArgs>(Args)...);
}

/**
 * @brief GetRpcFunctionIdByName - 查某类里函数的注册 id。
 * @param Name 函数名
 * @return 函数 id;类或名字无效、函数不存在时返回 0(0 是"无效 id"哨兵)
 *
 * @detail 取到的是运行期分配的 id,依赖该类已被注册(StaticClass 会触发注册)。
 *   跨进程/跨版本要稳定标识请用 GetStableRpcFunctionIdByName。
 */
template <typename TObject> inline uint16 GetRpcFunctionIdByName(const char* Name) {
    MClass* Class = TObject::StaticClass();
    if (!Class || !Name) {
        return 0;
    }

    MFunction* Func = Class->FindFunction(Name);
    return Func ? Func->FunctionId : 0;
}

/** @brief GetStableRpcFunctionIdByName - 由"类名 + 函数名"算出与注册顺序无关的稳定 id。 */
inline uint16 GetStableRpcFunctionIdByName(const char* ClassName, const char* FuncName) {
    return ComputeStableReflectId(ClassName, FuncName);
}

/** @brief GetStableClientFunctionIdByName - 由客户端 API 名算出稳定 id(命名空间 "MClientApi")。 */
inline uint16 GetStableClientFunctionIdByName(const char* ClientApiName) {
    return ComputeStableClientFunctionId(ClientApiName);
}

/**
 * @brief GetCachedRpcFunctionId - 带缓存的函数 id 查询。
 * @param CachedFunctionId 缓存变量(调用方持有),非 0 时直接返回
 * @param Name             函数名
 * @return 函数 id(查不到为 0)
 *
 * @detail 目的只是省一次查表,不是线程安全缓存:多个线程同时写同一个变量会数据竞争,
 *   所以缓存变量通常做成某对象的成员并由单线程使用。
 */
template <typename TObject> inline uint16 GetCachedRpcFunctionId(uint16& CachedFunctionId, const char* Name) {
    if (CachedFunctionId != 0) {
        return CachedFunctionId;
    }

    CachedFunctionId = GetRpcFunctionIdByName<TObject>(Name);
    return CachedFunctionId;
}

// 便捷宏:把"取函数 id"的三种方式(运行期查表 / 带缓存 / 稳定哈希)暴露给业务代码。
// 注意 MGET_RPC_FUNCTION_ID 与 MGET_CACHED_RPC_FUNCTION_ID 目前在仓库内没有调用点。
#define MGET_RPC_FUNCTION_ID(ClassType, FuncName) GetRpcFunctionIdByName<ClassType>(#FuncName)

#define MGET_CACHED_RPC_FUNCTION_ID(ClassType, FuncName, CacheVar) GetCachedRpcFunctionId<ClassType>(CacheVar, #FuncName)

#define MGET_STABLE_RPC_FUNCTION_ID(ClassNameLiteral, FuncNameLiteral) GetStableRpcFunctionIdByName(ClassNameLiteral, FuncNameLiteral)

#define MGET_STABLE_CLIENT_FUNCTION_ID(ClientApiLiteral) GetStableClientFunctionIdByName(ClientApiLiteral)
