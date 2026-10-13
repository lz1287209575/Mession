/**
 * @file MessageReader.h
 * @brief MMessageReader —— 从只读字节流按序读出标量/字符串的游标式读取器。
 *
 * @detail 所有权与契约:
 *   - 内部只持有源缓冲的 **const 引用**,不拷贝、不拥有:调用方必须保证该 TByteArray
 *     在整个读取期间存活且不被修改。
 *   - Offset 单调前进;任何一次读取失败都会把 bValid 永久置 false(IsValid 是"曾经失败过"
 *     的锁存标记,而不是当前是否还能读)。
 *   - 失败时返回值可用作流程分支,但不可依赖 OutValue 的内容。
 *   - 非线程安全:单个 reader 只允许单线程使用。
 */
#pragma once

#include "Common/Runtime/MLib.h"
#include "Common/Serialization/MessageUtils.h"


/**
 * @brief 游标式字节流读取器:按写入顺序依次 Read/ReadBE/ReadString。
 *
 * @detail 典型用法:MMessageReader R(Payload);然后连续的
 *         R.Read(A) && R.ReadBE(B) && R.ReadString(S),最后用 R.IsValid() 兜底校验收包完整性。
 */
class MMessageReader {
    public:
    explicit MMessageReader(const TByteArray& InData) : Data(InData) {
    }

    /**
     * @brief 按主机字节序读出一个 T。
     * @param OutValue [out] 读出的值,失败时不修改
     * @return true 剩余字节足够;false 剩余不足(Offset 不推进,并置 bValid=false)
     */
    template <typename T> bool Read(T& OutValue) {
        const bool bSuccess = ReadValue(Data, Offset, OutValue);
        bValid              = bValid && bSuccess;
        return bSuccess;
    }

    /**
     * @brief 按网络字节序(大端)读出 16/32/64 位无符号整数。
     * @param OutValue [out] 主机序结果,失败时不修改
     * @return true 成功;false 剩余字节不足(Offset 不推进,并置 bValid=false)
     */
    bool ReadBE(uint16& OutValue) {
        const bool bSuccess = ReadValueBE(Data, Offset, OutValue);
        bValid              = bValid && bSuccess;
        return bSuccess;
    }

    bool ReadBE(uint32& OutValue) {
        const bool bSuccess = ReadValueBE(Data, Offset, OutValue);
        bValid              = bValid && bSuccess;
        return bSuccess;
    }

    bool ReadBE(uint64& OutValue) {
        const bool bSuccess = ReadValueBE(Data, Offset, OutValue);
        bValid              = bValid && bSuccess;
        return bSuccess;
    }

    /**
     * @brief 读出 [uint16 长度][原始字节] 编码的字符串。
     * @param OutValue [out] 读出的字符串,失败时不修改
     * @return true 成功;false 长度字段或正文不完整(并置 bValid=false)
     *
     * @detail 底层实现见 ::ReadString —— 正文不足时 Offset 可能已被长度字段推进 2,
     *         失败后不要复用本 reader 继续解析。
     */
    bool ReadString(MString& OutValue) {
        const bool bSuccess = ::ReadString(Data, Offset, OutValue);
        bValid              = bValid && bSuccess;
        return bSuccess;
    }

    /**
     * @brief 读出长度字段为大端序的 [uint16 长度][原始字节] 字符串。
     * @param OutValue [out] 读出的字符串,失败时不修改
     * @return true 成功;false 长度字段或正文不完整(并置 bValid=false)
     */
    bool ReadStringBE(MString& OutValue) {
        const bool bSuccess = ::ReadStringBE(Data, Offset, OutValue);
        bValid              = bValid && bSuccess;
        return bSuccess;
    }

    /**
     * @brief 读出 InSize 个原始字节到 OutData(整体替换其旧内容)。
     * @param InSize  期望字节数
     * @param OutData [out] 目标缓冲;失败时不修改
     * @return true 成功(Offset 前进 InSize);false 剩余不足(并置 bValid=false)
     */
    bool ReadBytes(size_t InSize, TByteArray& OutData) {
        if (Offset + InSize > Data.size()) {
            bValid = false;
            return false;
        }

        OutData.assign(Data.begin() + Offset, Data.begin() + Offset + InSize);
        Offset += InSize;
        return true;
    }

    /**
     * @brief 试探剩余字节是否够 InSize。
     * @param InSize 期望字节数
     * @return true 够;false 不够
     *
     * @detail 纯查询:不推进 Offset,不影响 bValid。
     */
    bool CanRead(size_t InSize) const {
        return Offset + InSize <= Data.size();
    }

    /**
     * @brief 当前读取游标位置(已消费字节数)。
     */
    size_t GetOffset() const {
        return Offset;
    }

    /**
     * @brief 剩余可读字节数(Offset 已越界时返回 0,不会出现负值)。
     */
    size_t GetRemainingSize() const {
        return (Offset <= Data.size()) ? (Data.size() - Offset) : 0;
    }

    /**
     * @brief 自构造以来是否从未发生读取失败(锁存标记)。
     * @return true 全部成功;false 至少失败过一次
     */
    bool IsValid() const {
        return bValid;
    }

    private:
    const TByteArray& Data;
    size_t            Offset = 0;
    bool              bValid = true;
};
