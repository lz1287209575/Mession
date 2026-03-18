#include "Core/Net/NetCore.h"
#include "Core/Net/SocketPlatform.h"
#include "NetDriver/Reflection.h"
#include <atomic>
#include <cstring>

struct SBenchConfig
{
    FString Host = "127.0.0.1";
    uint16 Port = 8001;           // Gateway 默认端口
    int Clients = 32;
    int RequestsPerClient = 50;   // 每个客户端执行多少次登录+移动
};

static bool SendAll(int Fd, const uint8* Data, size_t Size)
{
    size_t SentTotal = 0;
    while (SentTotal < Size)
    {
        int Sent = ::send(Fd, (const char*)Data + SentTotal, static_cast<int>(Size - SentTotal), 0);
        if (Sent <= 0)
        {
            return false;
        }
        SentTotal += static_cast<size_t>(Sent);
    }
    return true;
}

static bool RecvAll(int Fd, uint8* Data, size_t Size)
{
    size_t RecvTotal = 0;
    while (RecvTotal < Size)
    {
        int Recv = ::recv(Fd, (char*)Data + RecvTotal, static_cast<int>(Size - RecvTotal), 0);
        if (Recv <= 0)
        {
            return false;
        }
        RecvTotal += static_cast<size_t>(Recv);
    }
    return true;
}

static bool SendLoginAndWaitResponse(int Fd, uint64 PlayerId)
{
    // 构造统一函数调用登录请求:
    // Length(4) + MsgType(1=MT_FunctionCall) + FunctionId(2) + PayloadSize(4) + PlayerId(8)
    constexpr uint8 MsgType = 13; // EClientMessageType::MT_FunctionCall
    const uint16 FunctionId = MGET_STABLE_RPC_FUNCTION_ID("MGatewayServer", "Client_Login");
    const uint32 PlayerPayloadSize = sizeof(PlayerId);
    const uint32 BodySize = 1 + sizeof(FunctionId) + sizeof(PlayerPayloadSize) + PlayerPayloadSize;

    uint8 Payload[BodySize];
    size_t Offset = 0;
    Payload[Offset++] = MsgType;
    std::memcpy(Payload + Offset, &FunctionId, sizeof(FunctionId));
    Offset += sizeof(FunctionId);
    std::memcpy(Payload + Offset, &PlayerPayloadSize, sizeof(PlayerPayloadSize));
    Offset += sizeof(PlayerPayloadSize);
    std::memcpy(Payload + Offset, &PlayerId, sizeof(PlayerId));

    uint8 Header[4];
    uint32 LengthLE = BodySize;
    std::memcpy(Header, &LengthLE, sizeof(LengthLE));

    if (!SendAll(Fd, Header, sizeof(Header)))
    {
        return false;
    }
    if (!SendAll(Fd, Payload, BodySize))
    {
        return false;
    }

    // 读取响应头
    uint8 RespHeader[4];
    if (!RecvAll(Fd, RespHeader, sizeof(RespHeader)))
    {
        return false;
    }
    uint32 RespLen = 0;
    std::memcpy(&RespLen, RespHeader, sizeof(RespLen));
    if (RespLen < 1 + 4 + 8)
    {
        return false;
    }

    // 读取响应体
    TArray RespBody;
    RespBody.resize(RespLen);
    if (!RecvAll(Fd, RespBody.data(), RespBody.size()))
    {
        return false;
    }
    if (RespBody.empty())
    {
        return false;
    }

    if (RespBody[0] != 13) // EClientMessageType::MT_FunctionCall
    {
        return false;
    }

    if (RespBody.size() < 1 + sizeof(uint16) + sizeof(uint32) + sizeof(uint32) + sizeof(uint64))
    {
        return false;
    }

    uint16 ResponseFunctionId = 0;
    std::memcpy(&ResponseFunctionId, RespBody.data() + 1, sizeof(ResponseFunctionId));
    if (ResponseFunctionId != MGET_STABLE_RPC_FUNCTION_ID("MClientDownlink", "Client_OnLoginResponse"))
    {
        return false;
    }

    uint32 PayloadSize = 0;
    std::memcpy(&PayloadSize, RespBody.data() + 1 + sizeof(ResponseFunctionId), sizeof(PayloadSize));
    if (PayloadSize != sizeof(uint32) + sizeof(uint64))
    {
        return false;
    }

    return RespBody.size() >= 1 + sizeof(uint16) + sizeof(uint32) + PayloadSize;
}

static void RunClientWorker(const SBenchConfig& Config, int Index, std::atomic<int>& SuccessCount)
{
    if (!MSocketPlatform::EnsureInit())
    {
        return;
    }

    int Fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (Fd < 0)
    {
        return;
    }

    sockaddr_in Addr;
    std::memset(&Addr, 0, sizeof(Addr));
    Addr.sin_family = AF_INET;
    Addr.sin_port = htons(Config.Port);
    Addr.sin_addr.s_addr = inet_addr(Config.Host.c_str());

    if (::connect(Fd, (sockaddr*)&Addr, sizeof(Addr)) != 0)
    {
        ::close(Fd);
        return;
    }

    for (int i = 0; i < Config.RequestsPerClient; ++i)
    {
        uint64 PlayerId = static_cast<uint64>(Index * 1000000 + i + 1);
        if (SendLoginAndWaitResponse(Fd, PlayerId))
        {
            ++SuccessCount;
        }
    }

    ::close(Fd);
}

int main(int argc, char** argv)
{
    SBenchConfig Config;
    for (int i = 1; i < argc; ++i)
    {
        FString Arg = argv[i];
        if ((Arg == "--host" || Arg == "-h") && i + 1 < argc)
        {
            Config.Host = argv[++i];
        }
        else if ((Arg == "--port" || Arg == "-p") && i + 1 < argc)
        {
            Config.Port = static_cast<uint16>(std::atoi(argv[++i]));
        }
        else if (Arg == "--clients" && i + 1 < argc)
        {
            Config.Clients = std::atoi(argv[++i]);
        }
        else if (Arg == "--requests" && i + 1 < argc)
        {
            Config.RequestsPerClient = std::atoi(argv[++i]);
        }
    }

    std::printf("NetBench: host=%s port=%u clients=%d requests_per_client=%d\n",
                Config.Host.c_str(), Config.Port, Config.Clients, Config.RequestsPerClient);

    std::atomic<int> SuccessCount{0};
    TVector<std::thread> Threads;
    Threads.reserve(Config.Clients);

    const auto StartTime = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < Config.Clients; ++i)
    {
        Threads.emplace_back(RunClientWorker, std::cref(Config), i, std::ref(SuccessCount));
    }
    for (auto& T : Threads)
    {
        if (T.joinable())
        {
            T.join();
        }
    }

    const auto EndTime = std::chrono::high_resolution_clock::now();
    const double Elapsed = std::chrono::duration<double>(EndTime - StartTime).count();
    const int TotalRequests = Config.Clients * Config.RequestsPerClient;

    std::printf("Total requests: %d, success: %d, time: %.3fs, throughput: %.1f req/s\n",
                TotalRequests, SuccessCount.load(), Elapsed,
                Elapsed > 0.0 ? (TotalRequests / Elapsed) : 0.0);

    return 0;
}
