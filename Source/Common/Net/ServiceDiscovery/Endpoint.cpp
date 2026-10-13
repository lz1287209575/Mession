/**
 * @file Endpoint.cpp
 * @brief FServiceEndpoint 的固定布局编码/解码实现。
 *
 * @detail 布局规则(Encode 与 Decode 必须成对修改):
 *   [ServerType:4][ServerId:4][Address:2 字节长度 + 内容][Port:2]
 *   [LastHeartbeatMs:8][bHealthy:1][ActorIds 个数:2][ActorId:8 × 个数]
 * 所有整型都按本机字节序整体 memcpy(命名里的 "LE" 隐含假设是**小端主机**,
 * 代码本身没有做字节序转换);字符串长度只有 16 位,超长会被截断。
 * 追加侧不做任何容量校验,读取侧逐字段校验越界。
 */
#include "Common/Net/ServiceDiscovery/Endpoint.h"

#include <cstring>

namespace {
    // 以下 AppendFixedLE* / ReadFixedLE* 都是"整体 memcpy 本机字节序":
    // 在小端主机上等价于 little-endian。追加侧不做边界检查。

    inline void AppendFixedLE(TByteArray& Out, uint32 V) {
        const uint8* P = reinterpret_cast<const uint8*>(&V);
        Out.insert(Out.end(), P, P + sizeof(V));
    }
    inline void AppendFixedLE16(TByteArray& Out, uint16 V) {
        const uint8* P = reinterpret_cast<const uint8*>(&V);
        Out.insert(Out.end(), P, P + sizeof(V));
    }
    inline void AppendFixedLE64(TByteArray& Out, uint64 V) {
        const uint8* P = reinterpret_cast<const uint8*>(&V);
        Out.insert(Out.end(), P, P + sizeof(V));
    }
    // 读取侧统一先判越界再 memcpy,并把游标推进 size;失败时游标保持不动
    inline bool ReadFixedLE(const TByteArray& In, size_t& Off, uint32& Out) {
        if (Off + sizeof(uint32) > In.size())
            return false;
        std::memcpy(&Out, In.data() + Off, sizeof(uint32));
        Off += sizeof(uint32);
        return true;
    }
    inline bool ReadFixedLE16(const TByteArray& In, size_t& Off, uint16& Out) {
        if (Off + sizeof(uint16) > In.size())
            return false;
        std::memcpy(&Out, In.data() + Off, sizeof(uint16));
        Off += sizeof(uint16);
        return true;
    }
    inline bool ReadFixedLE64(const TByteArray& In, size_t& Off, uint64& Out) {
        if (Off + sizeof(uint64) > In.size())
            return false;
        std::memcpy(&Out, In.data() + Off, sizeof(uint64));
        Off += sizeof(uint64);
        return true;
    }
    // 字符串 = 2 字节长度 + 裸字节(无终止符);长度超过 65535 会被截断成 16 位
    inline void AppendString_LE(TByteArray& Out, const MString& S) {
        AppendFixedLE16(Out, static_cast<uint16>(S.size()));
        Out.insert(Out.end(), S.begin(), S.end());
    }
    inline bool ReadString_LE(const TByteArray& In, size_t& Off, MString& Out) {
        uint16 Len = 0;
        if (!ReadFixedLE16(In, Off, Len))
            return false;
        // 先验长度再越界拷贝:防止 Len 被伪造导致读越界
        if (Off + Len > In.size())
            return false;
        Out.assign(In.begin() + Off, In.begin() + Off + Len);
        Off += Len;
        return true;
    }
} // namespace

bool EncodeEndpoint(const FServiceEndpoint& Ep, TByteArray& Out) {
    Out.clear();
    AppendFixedLE(Out, static_cast<uint32>(Ep.ServerType));
    AppendFixedLE(Out, Ep.ServerId);
    AppendString_LE(Out, Ep.Address);
    AppendFixedLE16(Out, Ep.Port);
    AppendFixedLE64(Out, Ep.LastHeartbeatMs);
    Out.push_back(Ep.bHealthy ? 1 : 0);
    // ActorIds 个数用 16 位:单个端点的 actor 数上限 65535
    AppendFixedLE16(Out, static_cast<uint16>(Ep.ActorIds.size()));
    for (uint64 ActorId : Ep.ActorIds) {
        AppendFixedLE64(Out, ActorId);
    }
    return true;
}

bool DecodeEndpoint(const TByteArray& In, size_t& Off, FServiceEndpoint& Ep) {
    uint32 ServerTypeRaw = 0;
    if (!ReadFixedLE(In, Off, ServerTypeRaw))
        return false;
    Ep.ServerType = static_cast<EServerType>(ServerTypeRaw);
    if (!ReadFixedLE(In, Off, Ep.ServerId))
        return false;
    if (!ReadString_LE(In, Off, Ep.Address))
        return false;
    if (!ReadFixedLE16(In, Off, Ep.Port))
        return false;
    if (!ReadFixedLE64(In, Off, Ep.LastHeartbeatMs))
        return false;
    // 布尔按 1 字节读:非 0 即 true
    if (Off + 1 > In.size())
        return false;
    Ep.bHealthy      = (In[Off++] != 0);
    uint16 NumActors = 0;
    if (!ReadFixedLE16(In, Off, NumActors))
        return false;
    Ep.ActorIds.clear();
    Ep.ActorIds.reserve(NumActors);
    for (uint16 i = 0; i < NumActors; ++i) {
        uint64 A = 0;
        if (!ReadFixedLE64(In, Off, A))
            return false;
        Ep.ActorIds.push_back(A);
    }
    return true;
}
