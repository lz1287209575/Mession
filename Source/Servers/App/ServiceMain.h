#pragma once
#include "Servers/App/ServiceId.h"
#include "Common/Runtime/Log/Logger.h"
#include "Common/Runtime/Concurrency/SignalHandler.h"
#include <cstdlib>
#include <cstring>

namespace MServiceMain
{
/**
 * SServicePeerConfig — 对端 Service 连接配置（Gateway 和 EchoService 共用）
 */
struct SServicePeerConfig
{
    EServerType ServerType = EServerType::Unknown;
    MString Address = "127.0.0.1";
    uint16 Port = 0;
};

/**
 * SCommonConfig — 所有 Service main 共用的命令行解析结果
 */
struct SCommonConfig
{
    int PortOverride = 0;
    TVector<SServicePeerConfig> Peers;
};

/**
 * ParsePeers — 解析 --peers=Type1@addr:port,Type2@addr:port,...
 */
inline TVector<SServicePeerConfig> ParsePeers(const MString& Value)
{
    TVector<SServicePeerConfig> Result;
    size_t Pos = 0;
    while (Pos < Value.size())
    {
        size_t CommaPos = Value.find(',', Pos);
        MString Token = (CommaPos == MString::npos)
            ? Value.substr(Pos)
            : Value.substr(Pos, CommaPos - Pos);

        size_t AtPos = Token.find('@');
        size_t ColonPos = Token.rfind(':');
        if (AtPos != MString::npos && ColonPos != MString::npos && ColonPos > AtPos)
        {
            SServicePeerConfig Peer;
            MString TypeName = Token.substr(0, AtPos);
            MString AddrPort = Token.substr(AtPos + 1);

            if (TypeName == "Gateway") Peer.ServerType = EServerType::Gateway;
            else if (TypeName == "Echo") Peer.ServerType = EServerType::Echo;
            else
            {
                if (CommaPos == MString::npos) break;
                Pos = CommaPos + 1;
                continue;
            }

            size_t InnerColon = AddrPort.find(':');
            Peer.Address = AddrPort.substr(0, InnerColon);
            Peer.Port = static_cast<uint16>(std::atoi(AddrPort.substr(InnerColon + 1).c_str()));
            Result.push_back(Peer);
        }

        if (CommaPos == MString::npos) break;
        Pos = CommaPos + 1;
    }
    return Result;
}

/**
 * ParseCommonArgs — 解析所有 Service main 共用的参数
 *
 * 支持:
 *   -p N / --port N       → PortOverride
 *   --peers=...           → Peers
 *   --listen=N            → PortOverride（同 -p）
 */
inline SCommonConfig ParseCommonArgs(int argc, char** argv)
{
    SCommonConfig Config;
    for (int i = 1; i < argc; ++i)
    {
        MString Arg = argv[i] ? argv[i] : "";
        if (Arg == "-p" || Arg == "--port")
        {
            if (i + 1 < argc)
            {
                Config.PortOverride = std::atoi(argv[++i]);
            }
        }
        else if (Arg == "--listen")
        {
            if (i + 1 < argc)
            {
                Config.PortOverride = std::atoi(argv[++i]);
            }
        }
        else if (Arg.rfind("--listen=", 0) == 0)
        {
            Config.PortOverride = std::atoi(Arg.substr(MString("--listen=").size()).c_str());
        }
        else if (Arg.rfind("--peers=", 0) == 0)
        {
            Config.Peers = ParsePeers(Arg.substr(MString("--peers=").size()));
        }
    }
    return Config;
}

/**
 * Run — Service 统一入口
 *
 * Service 必须提供：
 *   static SConfig BuildConfig(int argc, char** argv)
 *
 * 流程：BuildConfig → ApplyConfig → Init(port) → Run
 */
template<typename TService, typename TConfig>
int Run(int argc, char** argv)
{
    // 注册 SIGINT/SIGTERM → 优雅退出；SIGPIPE → 忽略。
    MSignalHandler::Install();

    TConfig Config = TService::BuildConfig(argc, argv);
    TService Service;
    Service.ApplyConfig(Config);
    if (!Service.Init(Config.ListenPort))
    {
        return 1;
    }
    Service.Run();
    return 0;
}

} // namespace MServiceMain
