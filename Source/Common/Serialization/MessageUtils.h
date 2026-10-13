/**
 * @file MessageUtils.h
 * @brief 消息字节流读写原语:主机字节序与网络字节序(大端)两套 Append/Read。
 *
 * @detail
 *   - Append* 把数据追加到 TByteArray 尾部,无失败路径(内部先 resize 再 memcpy)。
 *   - Read* 从 Offset 处就地读取;剩余字节不足时返回 false,**不推进 Offset**,
 *     也不写 OutValue(字符串读取是唯一例外,见 ReadString)。
 *   - 无后缀 = 主机字节序(直接 memcpy):仅限同机/同架构场景,历史兼容保留。
 *   - BE 后缀 = 网络字节序(大端):多字节整数跨平台传输必须走这组接口。
 *   - 字符串编码 = [uint16 长度][UTF-8 原始字节],长度不含长度字段自身。
 */
#pragma once

#include "Common/Runtime/MLib.h"
#include <cstring>
#include <type_traits>
#include <utility>

/**
 * @brief 按主机字节序把 Value 的原始字节追加到 OutData 尾部(直接 memcpy)。
 * @param OutData 目标缓冲,追加后 size 增加 sizeof(T)
 * @param Value   待写入值,须为 trivially copyable(编译期 static_assert)
 *
 * @detail 无失败路径。多字节整数走此接口不保证跨平台一致,新代码优先用 AppendValueBE。
 */
template <typename T> inline void AppendValue(TByteArray& OutData, const T& Value) {
    static_assert(std::is_trivially_copyable_v<T>, "AppendValue requires trivially copyable type");

    const size_t WriteOffset = OutData.size();
    OutData.resize(WriteOffset + sizeof(T));
    memcpy(OutData.data() + WriteOffset, &Value, sizeof(T));
}

/**
 * @brief 从 Data 的 Offset 处按主机字节序读出一个 T。
 * @param Data     源缓冲
 * @param Offset   [in,out] 读取起点;成功时前进 sizeof(T)
 * @param OutValue [out] 读出的值;失败时不修改
 * @return true 剩余字节足够;false 剩余不足(Offset 保持不变)
 */
template <typename T> inline bool ReadValue(const TByteArray& Data, size_t& Offset, T& OutValue) {
    static_assert(std::is_trivially_copyable_v<T>, "ReadValue requires trivially copyable type");

    if (Offset + sizeof(T) > Data.size()) {
        return false;
    }

    memcpy(&OutValue, Data.data() + Offset, sizeof(T));
    Offset += sizeof(T);
    return true;
}

/**
 * @brief 按网络字节序(大端)追加 16/32/64 位无符号整数。
 * @param OutData 目标缓冲
 * @param Value   主机序数值,写入前经 HostToNetwork 转换
 *
 * @detail 多字节整数跨平台传输必须用这组接口;主机序 AppendValue 仅限同架构场景。
 */
inline void AppendValueBE(TByteArray& OutData, uint16 Value) {
    uint16 Net = HostToNetwork(Value);
    AppendValue(OutData, Net);
}
inline void AppendValueBE(TByteArray& OutData, uint32 Value) {
    uint32 Net = HostToNetwork(Value);
    AppendValue(OutData, Net);
}
inline void AppendValueBE(TByteArray& OutData, uint64 Value) {
    uint64 Net = HostToNetwork(Value);
    AppendValue(OutData, Net);
}

/**
 * @brief 按网络字节序(大端)读取 16/32/64 位无符号整数。
 * @param Data     源缓冲
 * @param Offset   [in,out] 读取起点;成功时前进对应字节数
 * @param OutValue [out] 主机序结果,内部经 NetworkToHost 转换
 * @return true 成功;false 剩余字节不足(此时 Offset 不推进)
 */
inline bool ReadValueBE(const TByteArray& Data, size_t& Offset, uint16& OutValue) {
    if (!ReadValue(Data, Offset, OutValue)) {
        return false;
    }
    OutValue = NetworkToHost(OutValue);
    return true;
}
inline bool ReadValueBE(const TByteArray& Data, size_t& Offset, uint32& OutValue) {
    if (!ReadValue(Data, Offset, OutValue)) {
        return false;
    }
    OutValue = NetworkToHost(OutValue);
    return true;
}
inline bool ReadValueBE(const TByteArray& Data, size_t& Offset, uint64& OutValue) {
    if (!ReadValue(Data, Offset, OutValue)) {
        return false;
    }
    OutValue = NetworkToHost(OutValue);
    return true;
}

/**
 * @brief 把字符串按 [uint16 长度][原始字节] 追加到 OutData(长度用主机字节序)。
 * @param OutData 目标缓冲
 * @param Value   源字符串(不校验编码,按原样拷贝)
 *
 * @detail 空串只写 2 字节长度。
 * NOTE: 长度强转为 uint16,Value.size() > 65535 时长度字段会静默截断(正文也只写截断后的字节数),
 *       调用方需自行保证字符串长度不超过 65535。
 */
inline void AppendString(TByteArray& OutData, const MString& Value) {
    const uint16 Length = static_cast<uint16>(Value.size());
    AppendValue(OutData, Length);
    if (Length == 0) {
        return;
    }

    const size_t WriteOffset = OutData.size();
    OutData.resize(WriteOffset + Length);
    memcpy(OutData.data() + WriteOffset, Value.data(), Length);
}

/**
 * @brief 把字符串按 [uint16 长度][原始字节] 追加到 OutData(长度用网络字节序/大端)。
 * @param OutData 目标缓冲
 * @param Value   源字符串
 *
 * @detail 与 AppendString 唯一区别是长度字段走大端。正文编码本身无字节序问题。
 */
inline void AppendStringBE(TByteArray& OutData, const MString& Value) {
    const uint16 Length = static_cast<uint16>(Value.size());
    AppendValueBE(OutData, Length);
    if (Length == 0) {
        return;
    }

    const size_t WriteOffset = OutData.size();
    OutData.resize(WriteOffset + Length);
    memcpy(OutData.data() + WriteOffset, Value.data(), Length);
}

/**
 * @brief 读取 [uint16 长度][原始字节] 编码的字符串(长度用主机字节序)。
 * @param Data     源缓冲
 * @param Offset   [in,out] 读取起点;成功时前进 2 + 长度
 * @param OutValue [out] 读出的字符串;失败时不修改
 * @return true 成功;false 长度字段读不出或正文不完整
 *
 * @detail 陷阱:长度字段读成功后若正文不足,Offset 已前进 2 才返回 false,
 *         调用方不可在失败路径继续复用该 Offset。
 */
inline bool ReadString(const TByteArray& Data, size_t& Offset, MString& OutValue) {
    uint16 Length = 0;
    if (!ReadValue(Data, Offset, Length) || Offset + Length > Data.size()) {
        return false;
    }

    OutValue.assign(reinterpret_cast<const char*>(Data.data() + Offset), Length);
    Offset += Length;
    return true;
}

/**
 * @brief 读取 [uint16 长度][原始字节] 编码的字符串(长度用网络字节序/大端)。
 * @param Data     源缓冲
 * @param Offset   [in,out] 读取起点;成功时前进 2 + 长度
 * @param OutValue [out] 读出的字符串;失败时不修改
 * @return true 成功;false 长度字段读不出或正文不完整
 *
 * @detail 与 ReadString 同样的 Offset 前进陷阱,见 ReadString。
 */
inline bool ReadStringBE(const TByteArray& Data, size_t& Offset, MString& OutValue) {
    uint16 Length = 0;
    if (!ReadValueBE(Data, Offset, Length) || Offset + Length > Data.size()) {
        return false;
    }

    OutValue.assign(reinterpret_cast<const char*>(Data.data() + Offset), Length);
    Offset += Length;
    return true;
}
