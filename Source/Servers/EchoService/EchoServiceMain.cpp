#include "Servers/EchoService/EchoService.h"
#include "Servers/App/ServiceContainer.h"
#include "Common/Runtime/Log/Logger.h"
#include "Common/Runtime/StringUtils.h"
#include "Common/Net/Rpc/RpcRuntimeContext.h"
#include "Common/Net/ServerConnection.h"

#include <cstdlib>

namespace
{
// 解析 --listen=PORT
uint16 ParsePort(int argc, char** argv)
{
    for (int i = 1; i < argc; ++i)
    {
        MString Arg = argv[i] ? argv[i] : "";
        if (Arg.rfind("--listen=", 0) == 0)
        {
            return static_cast<uint16>(atoi(Arg.substr(MString("--listen=").size()).c_str()));
        }
    }
    return 0;
}

// 解析 --service=MEchoService
MString ParseService(int argc, char** argv)
{
    for (int i = 1; i < argc; ++i)
    {
        MString Arg = argv[i] ? argv[i] : "";
        if (Arg.rfind("--service=", 0) == 0)
        {
            return Arg.substr(MString("--service=").size());
        }
    }
    return MString("MEchoService");
}

// 解析 --local-type=Echo 或 --local-type=SampleB（决定本进程的 LocalServerType）
EServerType ParseLocalType(int argc, char** argv)
{
    for (int i = 1; i < argc; ++i)
    {
        MString Arg = argv[i] ? argv[i] : "";
        if (Arg.rfind("--local-type=", 0) == 0)
        {
            MString Value = Arg.substr(MString("--local-type=").size());
            if (Value == "Echo") return EServerType::Echo;
            // 第 2 步扩展: if (Value == "SampleB") return EServerType::SampleB;
            if (Value == "Gateway") return EServerType::Gateway;
        }
    }
    return EServerType::Unknown;
}

// 解析 --actors=1,2,3 — 每个值是 InstId（uint32），最终 ActorId 拼上 LocalServerType
TVector<uint32> ParseActorIds(int argc, char** argv)
{
    TVector<uint32> Result;
    for (int i = 1; i < argc; ++i)
    {
        MString Arg = argv[i] ? argv[i] : "";
        if (Arg.rfind("--actors=", 0) == 0)
        {
            MString Value = Arg.substr(MString("--actors=").size());
            size_t Pos = 0;
            while (Pos < Value.size())
            {
                size_t CommaPos = Value.find(',', Pos);
                MString Token = (CommaPos == MString::npos)
                    ? Value.substr(Pos)
                    : Value.substr(Pos, CommaPos - Pos);
                uint64 Id = 0;
                if (!Token.empty())
                {
                    char* EndPtr = nullptr;
                    Id = std::strtoull(Token.c_str(), &EndPtr, 10);
                    if (EndPtr == Token.c_str() + Token.size() && Id != 0 && Id <= 0xFFFFFFFFu)
                    {
                        Result.push_back(static_cast<uint32>(Id));
                    }
                }
                if (CommaPos == MString::npos) break;
                Pos = CommaPos + 1;
            }
        }
    }
    return Result;
}

// 解析 --peers=Gateway@127.0.0.1:8001,SampleB@127.0.0.1:7002
TVector<SServicePeerConfig> ParsePeers(int argc, char** argv)
{
    TVector<SServicePeerConfig> Result;
    for (int i = 1; i < argc; ++i)
    {
        MString Arg = argv[i] ? argv[i] : "";
        if (Arg.rfind("--peers=", 0) == 0)
        {
            MString Value = Arg.substr(MString("--peers=").size());
            size_t Pos = 0;
            while (Pos < Value.size())
            {
                size_t CommaPos = Value.find(',', Pos);
                MString Token = (CommaPos == MString::npos)
                    ? Value.substr(Pos)
                    : Value.substr(Pos, CommaPos - Pos);

                // 格式：<Type>@<Addr>:<Port>  例如 Gateway@127.0.0.1:8001
                size_t AtPos = Token.find('@');
                size_t ColonPos = Token.rfind(':');
                if (AtPos != MString::npos && ColonPos != MString::npos && ColonPos > AtPos)
                {
                    SServicePeerConfig Peer;
                    MString TypeName = Token.substr(0, AtPos);
                    MString AddrPort = Token.substr(AtPos + 1);

                    if (TypeName == "Gateway") Peer.ServerType = EServerType::Gateway;
                    else if (TypeName == "Echo") Peer.ServerType = EServerType::Echo;
                    // 第 2 步扩展: else if (TypeName == "SampleB") Peer.ServerType = EServerType::SampleB;
                    else continue;

                    size_t InnerColon = AddrPort.find(':');
                    Peer.Address = AddrPort.substr(0, InnerColon);
                    Peer.Port = static_cast<uint16>(atoi(AddrPort.substr(InnerColon + 1).c_str()));
                    Result.push_back(Peer);
                }

                if (CommaPos == MString::npos) break;
                Pos = CommaPos + 1;
            }
        }
    }
    return Result;
}

// 从 EServerType 派生默认 LocalServerId（PoC 阶段硬编码）
uint32 DefaultServerId(EServerType Type)
{
    switch (Type)
    {
        case EServerType::Echo: return 7;
        // 第 2 步扩展: case EServerType::SampleB: return 8;
        case EServerType::Gateway: return 1;
        default: return 0;
    }
}

// 解析 --inst=N（本进程在 LocalServerType 下的实例号）
uint32 ParseInst(int argc, char** argv)
{
    for (int i = 1; i < argc; ++i)
    {
        MString Arg = argv[i] ? argv[i] : "";
        if (Arg.rfind("--inst=", 0) == 0)
        {
            return static_cast<uint32>(atoi(Arg.substr(MString("--inst=").size()).c_str()));
        }
    }
    return 0;
}
}

int main(int argc, char** argv)
{
    // === ServiceInject 平铺：进程入口一次性装配所有依赖 ===

    SEchoServiceConfig Config;
    Config.ListenPort = ParsePort(argc, argv);
    Config.ServiceName = ParseService(argc, argv);
    Config.LocalServerType = ParseLocalType(argc, argv);
    Config.LocalServerId = DefaultServerId(Config.LocalServerType);
    Config.LocalActorIds = ParseActorIds(argc, argv);
    Config.LocalInstId = ParseInst(argc, argv);  // 默认 0；命令行 --inst=N 覆盖
    Config.Peers = ParsePeers(argc, argv);

    MEchoService Service;
    Service.ApplyConfig(Config);

    if (!Service.Init(Config.ListenPort))
    {
        LOG_ERROR("EchoService init failed");
        return 1;
    }

    Service.Run();
    return 0;
}