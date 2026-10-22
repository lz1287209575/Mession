/**
 * @file PacketCodec.h
 * @brief MLengthPrefixedPacketCodec —— 4 字节长度前缀的拆包/封包编解码。
 *
 * @detail 线格式:[uint32 PacketSize][PacketSize 字节 Payload]。
 *   - 长度字段走网络字节序（大端），可跨端序传输。
 *   - 长度上下界由 MAX_PACKET_SIZE(65535)约束;0 长度视为非法包。
 *   - 本类是纯静态工具,不持有状态;线程安全性取决于调用方对收发缓冲的独占访问。
 */
#pragma once

#include "Common/Runtime/MLib.h"
#include <cstring>

/**
 * @brief 拆包结果。
 *
 * @detail NoPacket 既表示"连长度字段都没收齐",也表示"包体还没收全"——两者都要求
 *         调用方继续等更多字节;InvalidPacket 表示流已无法恢复(长度字段非法),应断开连接。
 */
enum class EPacketDecodeResult : uint8 { NoPacket = 0, PacketReady = 1, InvalidPacket = 2 };

/**
 * @brief [uint32 长度][Payload] 线格式的编解码器(全静态方法,无实例状态)。
 */
class MLengthPrefixedPacketCodec {
    public:
    /**
     * @brief 把 Payload 封成 [uint32 长度][Payload] 写入 OutBytes(整体覆盖旧内容)。
     * @param Payload  待发送载荷
     * @param OutBytes [out] 封包结果
     * @return true 封包成功;false 载荷为空或超过 MAX_PACKET_SIZE(OutBytes 内容未定义/被 resize)
     */
    static bool EncodePacket(const TByteArray& Payload, TByteArray& OutBytes) {
        if (Payload.empty() || Payload.size() > MAX_PACKET_SIZE) {
            return false;
        }

        const uint32 PacketSize = static_cast<uint32>(Payload.size());
        const uint32 NetworkSize = HostToNetwork(PacketSize);
        OutBytes.resize(sizeof(PacketSize) + PacketSize);
        memcpy(OutBytes.data(), &NetworkSize, sizeof(NetworkSize));
        memcpy(OutBytes.data() + sizeof(PacketSize), Payload.data(), PacketSize);
        return true;
    }

    /**
     * @brief 从接收缓冲头部尝试取出一个完整包。
     * @param InOutRecvBuffer [in,out] 累积的接收缓冲;仅在返回 PacketReady 时删除已消费的前缀
     * @param OutPayload      [out] 取出的载荷;函数入口先清空
     * @return NoPacket 数据还不够(缓冲保持不变);PacketReady 已取出一个包;
     *         InvalidPacket 长度字段为 0 或超过 MAX_PACKET_SIZE
     *
     * @detail 复杂度:成功路径会从 vector 头部 erase,O(剩余字节数),不适合超大缓冲;
     *         返回 NoPacket 时缓冲**未被修改**,可安全等待下次收包,但错误分支
     *         (InvalidPacket)不消费任何字节,调用方必须自行断连,否则会死循环。
     */
    static EPacketDecodeResult TryDecodePacket(TByteArray& InOutRecvBuffer, TByteArray& OutPayload) {
        OutPayload.clear();

        if (InOutRecvBuffer.size() < sizeof(uint32)) {
            return EPacketDecodeResult::NoPacket;
        }

        uint32 PacketSize = 0;
        memcpy(&PacketSize, InOutRecvBuffer.data(), sizeof(PacketSize));
        PacketSize = NetworkToHost(PacketSize);

        if (PacketSize == 0 || PacketSize > MAX_PACKET_SIZE) {
            return EPacketDecodeResult::InvalidPacket;
        }

        const size_t TotalSize = sizeof(PacketSize) + PacketSize;
        if (InOutRecvBuffer.size() < TotalSize) {
            return EPacketDecodeResult::NoPacket;
        }

        OutPayload.assign(InOutRecvBuffer.begin() + sizeof(PacketSize), InOutRecvBuffer.begin() + TotalSize);
        InOutRecvBuffer.erase(InOutRecvBuffer.begin(), InOutRecvBuffer.begin() + TotalSize);
        return EPacketDecodeResult::PacketReady;
    }
};
