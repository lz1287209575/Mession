/**
 * @file Endpoint.cpp
 * @brief FServiceEndpoint 的固定布局编码/解码实现。
 *
 * @detail 布局规则(Encode 与 Decode 必须成对修改):
 *   [ServerType:4][ServerId:4][Address:2 字节长度 + 内容][Port:2]
 *   [LastHeartbeatMs:8][bHealthy:1][ActorIds 个数:2][ActorId:8 × 个数]
 * 所有整型均使用网络字节序（大端）；字符串长度只有 16 位,超长会被截断。
 * 追加侧不做任何容量校验,读取侧逐字段校验越界。
 */
#include "Common/Net/ServiceDiscovery/Endpoint.h"

#include "Common/Serialization/MessageUtils.h"

namespace {
    // 字符串 = 2 字节长度 + 裸字节(无终止符);长度超过 65535 会被截断成 16 位
    inline void AppendString_LE(TByteArray& Out, const MString& S) {
        AppendValueBE(Out, static_cast<uint16>(S.size()));
        Out.insert(Out.end(), S.begin(), S.end());
    }
    inline bool ReadString_LE(const TByteArray& In, size_t& Off, MString& Out) {
        uint16 Len = 0;
        if (!ReadValueBE(In, Off, Len))
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
    AppendValueBE(Out, static_cast<uint32>(Ep.ServerType));
    AppendValueBE(Out, Ep.ServerId);
    AppendString_LE(Out, Ep.Address);
    AppendValueBE(Out, Ep.Port);
    AppendValueBE(Out, Ep.LastHeartbeatMs);
    Out.push_back(Ep.bHealthy ? 1 : 0);
    // ActorIds 个数用 16 位:单个端点的 actor 数上限 65535
    AppendValueBE(Out, static_cast<uint16>(Ep.ActorIds.size()));
    for (uint64 ActorId : Ep.ActorIds) {
        AppendValueBE(Out, ActorId);
    }
    return true;
}

bool DecodeEndpoint(const TByteArray& In, size_t& Off, FServiceEndpoint& Ep) {
    uint32 ServerTypeRaw = 0;
    if (!ReadValueBE(In, Off, ServerTypeRaw))
        return false;
    Ep.ServerType = static_cast<EServerType>(ServerTypeRaw);
    if (!ReadValueBE(In, Off, Ep.ServerId))
        return false;
    if (!ReadString_LE(In, Off, Ep.Address))
        return false;
    if (!ReadValueBE(In, Off, Ep.Port))
        return false;
    if (!ReadValueBE(In, Off, Ep.LastHeartbeatMs))
        return false;
    // 布尔按 1 字节读:非 0 即 true
    if (Off + 1 > In.size())
        return false;
    Ep.bHealthy      = (In[Off++] != 0);
    uint16 NumActors = 0;
    if (!ReadValueBE(In, Off, NumActors))
        return false;
    Ep.ActorIds.clear();
    Ep.ActorIds.reserve(NumActors);
    for (uint16 i = 0; i < NumActors; ++i) {
        uint64 A = 0;
        if (!ReadValueBE(In, Off, A))
            return false;
        Ep.ActorIds.push_back(A);
    }
    return true;
}
