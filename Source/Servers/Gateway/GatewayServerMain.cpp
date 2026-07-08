#include "Servers/Gateway/GatewayServer.h"
#include "Servers/App/ServerMain.h"

#include <cstdlib>

namespace
{
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

                size_t AtPos = Token.find('@');
                size_t ColonPos = Token.rfind(':');
                if (AtPos != MString::npos && ColonPos != MString::npos && ColonPos > AtPos)
                {
                    SServicePeerConfig Peer;
                    MString TypeName = Token.substr(0, AtPos);
                    MString AddrPort = Token.substr(AtPos + 1);

                    if (TypeName == "Gateway") Peer.ServerType = EServerType::Gateway;
                    else if (TypeName == "Echo") Peer.ServerType = EServerType::Echo;
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
}

int main(int argc, char** argv)
{
    MString ConfigPath;
    int PortOverride = 0;
    TVector<SServicePeerConfig> Peers;

    for (int i = 1; i < argc; ++i)
    {
        const MString Arg = argv[i] ? argv[i] : "";
        if (Arg == "--port" || Arg == "-p")
        {
            if (i + 1 < argc)
            {
                PortOverride = std::atoi(argv[++i]);
            }
        }
        else if (Arg.rfind("--peers=", 0) == 0)
        {
            Peers = ParsePeers(argc, argv);
        }
    }

    MGatewayServer Gateway;

    SGatewayConfig Config;
    Config.ListenPort = (PortOverride > 0) ? static_cast<uint16>(PortOverride) : Config.ListenPort;
    Config.Peers = std::move(Peers);
    Gateway.ApplyConfig(Config);

    Gateway.LoadConfig(ConfigPath);
    if (!Gateway.Init(Config.ListenPort))
    {
        return 1;
    }

    Gateway.Run();
    return 0;
}
