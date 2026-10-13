/**
 * @file MObjectAssetBinary.h
 * @brief MObjectAssetBinary —— MObject/MSTRUCT 资产(.mobj)二进制编解码。
 *
 * @detail 文件布局(全部小端):
 *   magic "MOBJ"(4B) + Version(2B) + Flags(2B) + RootAssetTypeId(4B)
 *   + PayloadEncoding(4B) + PayloadSize(4B) + Payload
 *   共 20 字节定长头,负载按 PayloadEncoding 二选一:
 *   - JsonBridge:负载即 JSON 文本(由 MObjectAssetJson 解析)
 *   - TaggedFields:字段记录数组(字段 id + 类型 + 变长长度 + 值),与 JSON 路径
 *     的最终结果等价,但不依赖文本解析
 *
 * 字段记录只收录带 Asset 域的属性(HasAnyDomains(Asset)),因此本格式与
 * "资产视图"而非"完整对象快照"对应。Append 系列与 Read 系列都在成功时才推进
 * InOutOffset,失败时不改动调用方状态。
 */
#pragma once

#include "Common/Runtime/MLib.h"
#include <cstring>
#include <type_traits>

class MObject;
class MClass;

namespace MObjectAssetBinary {
    /** @brief 当前文件格式版本;ReadHeader 只接受等于该值的文件。 */
    constexpr uint16 FileVersion = 1;

    /**
     * @brief TBinaryRawType - 取用于落盘的原始整数类型。
     *
     * @detail enum 取 underlying_type(避免按 enum 的内存布局落盘),
     *   其余类型保持原样。AppendFixedLE/ReadFixedLE 依赖它。
     */
    template <typename TValue, bool = std::is_enum_v<TValue>> struct TBinaryRawType {
        using Type = TValue;
    };

    template <typename TValue> struct TBinaryRawType<TValue, true> {
        using Type = std::underlying_type_t<TValue>;
    };

    /** @brief 负载编码方式,写入文件头供读取方分派。 */
    enum class EPayloadEncoding : uint32 {
        None         = 0, // 无效值,仅作默认初始化
        JsonBridge   = 1, // 负载是 JSON 文本
        TaggedFields = 2, // 负载是字段记录数组
    };

    /** @brief .mobj 定长文件头(20 字节,不含前置 magic)。 */
    struct SFileHeader {
        uint16           Version         = FileVersion;
        uint16           Flags           = 0;
        uint32           RootAssetTypeId = 0; // 根对象类的 ComputeStableAssetTypeId
        EPayloadEncoding PayloadEncoding = EPayloadEncoding::None;
        uint32           PayloadSize     = 0; // 负载字节数(Payload 之后不得有数据)
    };

    /** @brief 追加单个字节。 */
    inline void AppendByte(TByteArray& OutBytes, uint8 Value) {
        OutBytes.push_back(Value);
    }

    /**
     * @brief AppendFixedLE - 按指定类型宽度以小端追加一个定长整数/enum。
     * @param OutBytes 输出缓冲(追加)
     * @param Value 值;enum 按底层类型宽度写入
     */
    template <typename TValue> inline void AppendFixedLE(TByteArray& OutBytes, TValue Value) {
        static_assert(std::is_integral_v<TValue> || std::is_enum_v<TValue>, "AppendFixedLE expects integral or enum");
        using TRaw          = typename TBinaryRawType<TValue>::Type;
        const TRaw RawValue = static_cast<TRaw>(Value);
        for (size_t Index = 0; Index < sizeof(TRaw); ++Index) {
            AppendByte(OutBytes, static_cast<uint8>((static_cast<uint64>(RawValue) >> (Index * 8)) & 0xFFu));
        }
    }

    /** @brief 以小端追加 float(按位模式 reinterpret,不做数值格式化)。 */
    inline void AppendFloat32LE(TByteArray& OutBytes, float Value) {
        uint32 Raw = 0;
        static_assert(sizeof(float) == sizeof(uint32));
        std::memcpy(&Raw, &Value, sizeof(float));
        AppendFixedLE(OutBytes, Raw);
    }

    /** @brief 以小端追加 double(按位模式 reinterpret)。 */
    inline void AppendFloat64LE(TByteArray& OutBytes, double Value) {
        uint64 Raw = 0;
        static_assert(sizeof(double) == sizeof(uint64));
        std::memcpy(&Raw, &Value, sizeof(double));
        AppendFixedLE(OutBytes, Raw);
    }

    /**
     * @brief ReadByte - 读一字节。
     * @param InBytes 输入
     * @param InOutOffset 读游标;仅在成功时 +1
     * @param OutValue 读出的值
     * @return true 成功,false 已到输入末尾
     */
    inline bool ReadByte(const TByteArray& InBytes, size_t& InOutOffset, uint8& OutValue) {
        if (InOutOffset >= InBytes.size()) {
            return false;
        }
        OutValue = InBytes[InOutOffset++];
        return true;
    }

    /**
     * @brief ReadFixedLE - 读一个定长小端整数/enum。
     * @param InBytes 输入
     * @param InOutOffset 读游标;仅在成功时推进 sizeof(TRaw)
     * @param OutValue 读出的值(enum 经底层类型回转)
     * @return true 成功,false 剩余字节不足
     */
    template <typename TValue> inline bool ReadFixedLE(const TByteArray& InBytes, size_t& InOutOffset, TValue& OutValue) {
        static_assert(std::is_integral_v<TValue> || std::is_enum_v<TValue>, "ReadFixedLE expects integral or enum");
        using TRaw = typename TBinaryRawType<TValue>::Type;
        if (InOutOffset + sizeof(TRaw) > InBytes.size()) {
            return false;
        }

        std::make_unsigned_t<TRaw> RawValue = 0;
        for (size_t Index = 0; Index < sizeof(TRaw); ++Index) {
            RawValue |= static_cast<std::make_unsigned_t<TRaw>>(InBytes[InOutOffset + Index]) << (Index * 8);
        }
        InOutOffset += sizeof(TRaw);
        OutValue = static_cast<TValue>(static_cast<TRaw>(RawValue));
        return true;
    }

    /** @brief 读小端 float;字节不足返回 false 且不推进游标。 */
    inline bool ReadFloat32LE(const TByteArray& InBytes, size_t& InOutOffset, float& OutValue) {
        uint32 Raw = 0;
        if (!ReadFixedLE(InBytes, InOutOffset, Raw)) {
            return false;
        }
        std::memcpy(&OutValue, &Raw, sizeof(float));
        return true;
    }

    /** @brief 读小端 double;字节不足返回 false 且不推进游标。 */
    inline bool ReadFloat64LE(const TByteArray& InBytes, size_t& InOutOffset, double& OutValue) {
        uint64 Raw = 0;
        if (!ReadFixedLE(InBytes, InOutOffset, Raw)) {
            return false;
        }
        std::memcpy(&OutValue, &Raw, sizeof(double));
        return true;
    }

    /**
     * @brief AppendVarUInt - 以 LEB128 变长编码追加无符号整数(小端 7 位分组)。
     * @param OutBytes 输出缓冲(追加)
     * @param Value 值;最大 64 位时占 10 字节
     */
    inline void AppendVarUInt(TByteArray& OutBytes, uint64 Value) {
        while (Value >= 0x80u) {
            AppendByte(OutBytes, static_cast<uint8>((Value & 0x7Fu) | 0x80u));
            Value >>= 7;
        }
        AppendByte(OutBytes, static_cast<uint8>(Value));
    }

    /**
     * @brief ReadVarUInt - 读取 LEB128 变长无符号整数。
     * @param InBytes 输入
     * @param InOutOffset 读游标;成功时推进实际消费的字节数
     * @param OutValue 读出的值
     * @return true 成功;false 表示字节耗尽,或分组超过 64 位仍未终止(畸形输入)
     */
    inline bool ReadVarUInt(const TByteArray& InBytes, size_t& InOutOffset, uint64& OutValue) {
        OutValue     = 0;
        uint32 Shift = 0;
        while (Shift < 64) {
            uint8 Byte = 0;
            if (!ReadByte(InBytes, InOutOffset, Byte)) {
                return false;
            }

            OutValue |= static_cast<uint64>(Byte & 0x7Fu) << Shift;
            if ((Byte & 0x80u) == 0) {
                return true;
            }
            Shift += 7;
        }
        return false;
    }

    /**
     * @brief AppendString - 追加字符串:变长长度前缀 + 原始字节(不含终止符)。
     * @param OutBytes 输出缓冲(追加)
     * @param Value 字符串
     */
    inline void AppendString(TByteArray& OutBytes, const MString& Value) {
        AppendVarUInt(OutBytes, static_cast<uint64>(Value.size()));
        OutBytes.insert(OutBytes.end(), Value.begin(), Value.end());
    }

    /**
     * @brief ReadString - 读取 AppendString 写出的字符串。
     * @param InBytes 输入
     * @param InOutOffset 读游标;成功时推进长度前缀 + 内容长度
     * @param OutValue 读出的字符串
     * @return true 成功;false 表示长度前缀损坏,或声明长度超过剩余字节
     */
    inline bool ReadString(const TByteArray& InBytes, size_t& InOutOffset, MString& OutValue) {
        uint64 Length = 0;
        if (!ReadVarUInt(InBytes, InOutOffset, Length)) {
            return false;
        }
        if (Length > static_cast<uint64>(InBytes.size() - InOutOffset)) {
            return false;
        }

        OutValue.assign(reinterpret_cast<const char*>(InBytes.data() + InOutOffset), static_cast<size_t>(Length));
        InOutOffset += static_cast<size_t>(Length);
        return true;
    }

    /**
     * @brief EncodeStructFields - 序列化 MSTRUCT 的全部属性(不限 Asset 域)。
     * @param StructMeta 结构体反射信息;空则失败
     * @param StructData 结构体实例
     * @param OutData 输出(会先 clear)
     * @param OutError 失败原因,可空
     * @return true 成功
     */
    bool EncodeStructFields(const MClass* StructMeta, const void* StructData, TByteArray& OutData, MString* OutError = nullptr);
    /**
     * @brief DecodeStructFields - EncodeStructFields 的逆操作。
     * @param StructMeta 结构体反射信息;空则失败
     * @param StructData 目标实例(已构造)
     * @param InData 负载;必须被恰好消费完,否则报 trailing 错误
     * @param OutError 失败原因,可空
     * @return true 成功
     */
    bool DecodeStructFields(const MClass* StructMeta, void* StructData, const TByteArray& InData, MString* OutError = nullptr);

    /**
     * @brief BuildFromObject - 把一个对象序列化成完整 .mobj 文件字节。
     * @param RootObject 根对象(只收录 Asset 域属性);空则失败
     * @param OutBytes 输出(含文件头)
     * @param OutError 失败原因,可空
     * @return true 成功
     */
    bool BuildFromObject(const MObject* RootObject, TByteArray& OutBytes, MString* OutError = nullptr);
    /**
     * @brief BuildFromJson - 先按 JSON 造出临时对象,再走 BuildFromObject。
     * @param JsonText 资产 JSON(含 $class 等保留键)
     * @param OutBytes 输出(含文件头)
     * @param OutError 失败原因,可空
     * @return true 成功
     *
     * @detail 临时对象由本函数负责释放。
     */
    bool BuildFromJson(const MString& JsonText, TByteArray& OutBytes, MString* OutError = nullptr);

    /**
     * @brief LoadFromBytes - 从 .mobj 字节还原对象。
     * @param Bytes 完整文件字节
     * @param Outer 若不为空则挂为其子对象,否则加入 RootSet
     * @param OutError 失败原因,可空
     * @return 新对象(调用方持有);失败返回 nullptr
     *
     * @detail 两种负载编码都支持,并且都会校验对象实际类与头里的
     *   RootAssetTypeId 一致,不一致则销毁已建对象并失败。
     */
    MObject* LoadFromBytes(const TByteArray& Bytes, MObject* Outer = nullptr, MString* OutError = nullptr);
    /**
     * @brief ReadHeader - 校验并解析文件头。
     * @param Bytes 完整文件字节
     * @param OutHeader 解析结果
     * @param OutPayloadOffset 负载起始偏移(成功时有效)
     * @param OutError 失败原因,可空
     * @return true 成功
     *
     * @detail 校验 magic、版本号、负载编码合法性以及 PayloadSize 与剩余字节是否自洽。
     */
    bool ReadHeader(const TByteArray& Bytes, SFileHeader& OutHeader, size_t& OutPayloadOffset, MString* OutError = nullptr);
    /**
     * @brief ExtractPayloadJson - 只取负载中的 JSON 文本(JsonBridge 专用)。
     * @param Bytes 完整文件字节
     * @param OutJson 负载文本
     * @param OutHeader 可选回传文件头
     * @param OutError 失败原因,可空
     * @return true 成功;TaggedFields 负载会以 payload_not_json_bridge 失败
     */
    bool ExtractPayloadJson(const TByteArray& Bytes, MString& OutJson, SFileHeader* OutHeader = nullptr, MString* OutError = nullptr);
} // namespace MObjectAssetBinary
