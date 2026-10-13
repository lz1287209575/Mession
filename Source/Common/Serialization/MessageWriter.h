/**
 * @file MessageWriter.h
 * @brief MMessageWriter —— 把标量/字符串/裸字节按序追加进内部 TByteArray 的写出器。
 *
 * @detail 所有权与契约:
 *   - 拥有内部 TByteArray(Data),GetData 暴露 const 引用,MoveData 交出所有权。
 *   - 各 Write* 返回 *this,支持链式调用。
 *   - 无失败路径:容量不足由 vector 自行扩容(WriteBytes 例外,见其注释)。
 *   - 非线程安全:单个 writer 只允许单线程使用。
 */
#pragma once

#include "Common/Runtime/MLib.h"

#include "Common/Serialization/MessageUtils.h"

/**
 * @brief 字节流写出器:写入顺序即接收端 MMessageReader 的读取顺序。
 *
 * @detail 多字节整数跨平台协议请用 WriteBE;Write 走主机字节序。
 *         MoveData 之后本对象不应再被使用。
 */
class MMessageWriter {
    public:
    MMessageWriter() = default;

    /**
     * @brief 预分配 ReserveSize 字节,避免写入过程中反复扩容。
     * @param ReserveSize 预留容量(不改变已写数据长度)
     */
    explicit MMessageWriter(size_t ReserveSize) {
        Data.reserve(ReserveSize);
    }

    /**
     * @brief 按主机字节序追加一个 T。
     * @param Value 待写入值,须为 trivially copyable
     * @return *this,便于链式调用
     */
    template <typename T> MMessageWriter& Write(const T& Value) {
        AppendValue(Data, Value);
        return *this;
    }

    /**
     * @brief 按网络字节序(大端)追加 16/32/64 位无符号整数。
     * @param Value 主机序数值(内部 HostToNetwork 转换)
     * @return *this,便于链式调用
     *
     * @detail 跨平台协议的多字节整数必须走这组接口。
     */
    MMessageWriter& WriteBE(uint16 Value) {
        AppendValueBE(Data, Value);
        return *this;
    }

    MMessageWriter& WriteBE(uint32 Value) {
        AppendValueBE(Data, Value);
        return *this;
    }

    MMessageWriter& WriteBE(uint64 Value) {
        AppendValueBE(Data, Value);
        return *this;
    }

    /**
     * @brief 追加 [uint16 长度][原始字节] 字符串,长度用主机字节序。
     * @param Value 源字符串
     * @return *this,便于链式调用
     *
     * @detail 长度超 65535 会被静默截断,见 AppendString 的 NOTE。
     */
    MMessageWriter& WriteString(const MString& Value) {
        AppendString(Data, Value);
        return *this;
    }

    /**
     * @brief 追加 [uint16 长度][原始字节] 字符串,长度用网络字节序(大端)。
     * @param Value 源字符串
     * @return *this,便于链式调用
     */
    MMessageWriter& WriteStringBE(const MString& Value) {
        AppendStringBE(Data, Value);
        return *this;
    }

    /**
     * @brief 追加整个缓冲区(裸字节,不做长度前缀)。
     * @param InData 源缓冲
     * @return *this,便于链式调用
     */
    MMessageWriter& WriteBytes(const TByteArray& InData) {
        Data.insert(Data.end(), InData.begin(), InData.end());
        return *this;
    }

    /**
     * @brief 追加 [InData, InData + InSize) 的裸字节。
     * @param InData 源指针;为 nullptr 时整个调用变为 no-op(不会崩溃)
     * @param InSize 字节数
     * @return *this,便于链式调用
     */
    MMessageWriter& WriteBytes(const uint8* InData, size_t InSize) {
        if (InData && InSize > 0) {
            Data.insert(Data.end(), InData, InData + InSize);
        }
        return *this;
    }

    /**
     * @brief 只读访问已写数据。
     * @return 内部缓冲的 const 引用,生命周期随本对象
     */
    const TByteArray& GetData() const {
        return Data;
    }

    /**
     * @brief 移交内部缓冲所有权(移动语义)。
     * @return 内部缓冲的右值引用
     *
     * @detail 陷阱:返回的是**成员**的右值引用,并不延长其生命周期。
     *         调用方应立刻把结果移入目标变量,且此后不得再使用本 writer。
     */
    TByteArray&& MoveData() {
        return std::move(Data);
    }

    private:
    TByteArray Data;
};
