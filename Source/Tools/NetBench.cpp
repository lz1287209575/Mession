/**
 * @file NetBench.cpp
 * @brief NetBench —— 网关登录链路的并发压测小工具(非生产代码)。
 *
 * @detail 每个客户端一条线程、一条长连接,循环发送登录请求并等待 Client_OnLoginResponse
 *         下行,跑完打印总耗时与吞吐(req/s)。
 *         外层信封与 MLengthPrefixedPacketCodec 一致:4 字节长度前缀 + 消息体,长度字段按
 *         **主机字节序**原样 memcpy(不调用 htonl),因此只能与同字节序的对端互测。
 *         只做验收打点,不重试、不重连、无超时:任一步 send/recv 失败即把该次请求判为失败。
 *         命令行:--host/-h、--port/-p、--clients、--requests;未知参数被静默忽略。
 *
 *   NOTE: 本工具实现的是**旧版** Client↔Gateway 报文(消息体首字节是 1 字节 MessageType),
 *   而 2026-07 step-1/2 协议族重构后 Gateway 已改收无 MessageType 的反射 envelope
 *   `[RequestId:8B][FunctionId:2B][PayloadSize:4B][Payload:N]`(见 RpcTransport.h 与
 *   GatewayServer.cpp 的 ParseClientEnvelopePacket)。两者不兼容:当前 Gateway 会把
 *   MessageType 字节当成 RequestId 的一部分解析失败并 LOG_WARN,本工具因此收不到下行、
 *   成功计数恒为 0。此外原注释引用的 EClientMessageType 枚举在本次重构中已整体删除。
 *   此处只按代码现状写准注释,未改动协议字节(改协议属功能修改,不在本阶段范围)。
 */
#include "Common/IO/Socket/SocketPlatform.h"
#include "Common/Runtime/MLib.h"
#include "Common/Runtime/Reflect/Reflection.h"
#include <atomic>
#include <cstring>

/**
 * @brief NetBench 的运行参数,可由命令行覆盖。
 */
struct SBenchConfig {
    MString Host              = "127.0.0.1";
    uint16  Port              = 8001; // Gateway 默认端口
    int     Clients           = 32;
    int     RequestsPerClient = 50; // 每个客户端发起多少次登录请求
};

/**
 * @brief 循环 send,直到 Size 字节全部写出。
 * @param Fd   已连接的 socket
 * @param Data 待发送缓冲(只读)
 * @param Size 字节数
 * @return true 全部写出;false 对端关闭或出错
 *
 * @detail 处理短写:单次 send 可能只写出部分字节,靠 SentTotal 累加推进。阻塞式、无超时;
 *         失败时已写出的部分不回滚(调用方直接判本次请求失败)。
 */
static bool SendAll(TSocketFd Fd, const uint8* Data, size_t Size) {
    size_t SentTotal = 0;
    while (SentTotal < Size) {
        int Sent = ::send(Fd, (const char*)Data + SentTotal, static_cast<int>(Size - SentTotal), 0);
        if (Sent <= 0) {
            return false;
        }
        SentTotal += static_cast<size_t>(Sent);
    }
    return true;
}

/**
 * @brief 循环 recv,直到收满 Size 字节。
 * @param Fd   已连接的 socket
 * @param Data [out] 接收缓冲
 * @param Size 期望字节数
 * @return true 收满;false 对端关闭或出错
 *
 * @detail 阻塞式、无超时。返回 false 时 Data 里可能已有部分收到的字节,调用方不得使用。
 */
static bool RecvAll(TSocketFd Fd, uint8* Data, size_t Size) {
    size_t RecvTotal = 0;
    while (RecvTotal < Size) {
        int Recv = ::recv(Fd, (char*)Data + RecvTotal, static_cast<int>(Size - RecvTotal), 0);
        if (Recv <= 0) {
            return false;
        }
        RecvTotal += static_cast<size_t>(Recv);
    }
    return true;
}

/**
 * @brief 发一次"统一函数调用"登录请求,并校验 Client_OnLoginResponse 下行。
 * @param Fd       已连接的 socket
 * @param PlayerId 请求载荷里携带的玩家 ID
 * @return true 收到响应且逐级校验通过;false 任一步失败(含对端提前关闭)
 *
 * @detail 报文 = 4 字节长度前缀 + 消息体;消息体 =
 *   MsgType(1) + FunctionId(2) + PayloadSize(4) + PlayerId(8),共 15 字节。
 *   多字节整数一律按主机字节序原样 memcpy,不调用 htonl。
 *   响应按 MsgType → FunctionId → PayloadSize → 载荷长度 逐级校验,任一级不符即失败。
 *   本函数不重试、不打印诊断,只回布尔值,方便压测侧直接累加成功计数。
 *
 * @detail NOTE: 这里的 1 字节 MessageType 是旧协议残留(见文件头 NOTE);当前 Gateway 收的是
 *   无 MessageType 的 envelope,因此本函数对现行 Gateway 会解析失败、拿不到响应。
 */
static bool SendLoginAndWaitResponse(TSocketFd Fd, uint64 PlayerId) {
    // 构造统一函数调用登录请求(旧版带 MessageType 字节的线格式):
    // 报文布局 = Length(4) + MsgType(1) + FunctionId(2) + PayloadSize(4) + PlayerId(8)
    constexpr uint8 MsgType           = 13; // 13 = 旧协议 client MessageType(注释原写 EClientMessageType::MT_FunctionCall,该枚举已删除)
    const uint16    FunctionId        = MGET_STABLE_CLIENT_FUNCTION_ID("Client_Login");
    const uint32    PlayerPayloadSize = sizeof(PlayerId);
    const uint32    BodySize          = 1 + sizeof(FunctionId) + sizeof(PlayerPayloadSize) + PlayerPayloadSize;

    uint8  Payload[BodySize];
    size_t Offset     = 0;
    Payload[Offset++] = MsgType;
    std::memcpy(Payload + Offset, &FunctionId, sizeof(FunctionId));
    Offset += sizeof(FunctionId);
    std::memcpy(Payload + Offset, &PlayerPayloadSize, sizeof(PlayerPayloadSize));
    Offset += sizeof(PlayerPayloadSize);
    std::memcpy(Payload + Offset, &PlayerId, sizeof(PlayerId));

    uint8  Header[4];
    uint32 LengthLE = BodySize;
    std::memcpy(Header, &LengthLE, sizeof(LengthLE));

    if (!SendAll(Fd, Header, sizeof(Header))) {
        return false;
    }
    if (!SendAll(Fd, Payload, BodySize)) {
        return false;
    }

    // 读取响应头
    uint8 RespHeader[4];
    if (!RecvAll(Fd, RespHeader, sizeof(RespHeader))) {
        return false;
    }
    uint32 RespLen = 0;
    std::memcpy(&RespLen, RespHeader, sizeof(RespLen));
    // 粗筛:这里的常量与其后的逐字段校验写法不一致 —— 真实最小头长是 1+2+4=7,最小合法
    // 整包是 19;13 只是随手取的宽松下限,不会误杀合法包,精确校验见下面按字段做的 return。
    if (RespLen < 1 + 4 + 8) {
        return false;
    }

    // 读取响应体
    TByteArray RespBody;
    RespBody.resize(RespLen);
    if (!RecvAll(Fd, RespBody.data(), RespBody.size())) {
        return false;
    }
    if (RespBody.empty()) {
        return false;
    }

    if (RespBody[0] != 13) // 13 = 旧协议 client MessageType(EClientMessageType::MT_FunctionCall 已删除)
    {
        return false;
    }

    if (RespBody.size() < 1 + sizeof(uint16) + sizeof(uint32) + sizeof(uint32) + sizeof(uint64)) {
        return false;
    }

    uint16 ResponseFunctionId = 0;
    std::memcpy(&ResponseFunctionId, RespBody.data() + 1, sizeof(ResponseFunctionId));
    if (ResponseFunctionId != MGET_STABLE_RPC_FUNCTION_ID("MClientDownlink", "Client_OnLoginResponse")) {
        return false;
    }

    uint32 PayloadSize = 0;
    std::memcpy(&PayloadSize, RespBody.data() + 1 + sizeof(ResponseFunctionId), sizeof(PayloadSize));
    if (PayloadSize != sizeof(uint32) + sizeof(uint64)) {
        return false;
    }

    return RespBody.size() >= 1 + sizeof(uint16) + sizeof(uint32) + PayloadSize;
}

/**
 * @brief 一个压测客户端线程体:建连后循环发起 RequestsPerClient 次登录。
 * @param Config       运行参数,只读共享
 * @param Index        客户端序号,用于派生互不相同的 PlayerId
 * @param SuccessCount 跨线程成功计数,原子累加
 *
 * @detail 共享可变状态只有 SuccessCount(atomic);Config 只读。socket 初始化失败或建连
 *         失败时静默返回 —— 该客户端贡献 0 次成功。连接在整个循环里复用,退出时关闭。
 */
static void RunClientWorker(const SBenchConfig& Config, int Index, std::atomic<int>& SuccessCount) {
    if (!MSocketPlatform::EnsureInit()) {
        return;
    }

    TSocketFd Fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (Fd == INVALID_SOCKET_FD) {
        return;
    }

    sockaddr_in Addr;
    std::memset(&Addr, 0, sizeof(Addr));
    Addr.sin_family      = AF_INET;
    Addr.sin_port        = htons(Config.Port);
    Addr.sin_addr.s_addr = inet_addr(Config.Host.c_str());

    if (::connect(Fd, (sockaddr*)&Addr, sizeof(Addr)) != 0) {
        MSocketPlatform::CloseSocket(Fd);
        return;
    }

    for (int i = 0; i < Config.RequestsPerClient; ++i) {
        uint64 PlayerId = static_cast<uint64>(Index * 1000000 + i + 1);
        if (SendLoginAndWaitResponse(Fd, PlayerId)) {
            ++SuccessCount;
        }
    }

    MSocketPlatform::CloseSocket(Fd);
}

/**
 * @brief 解析命令行参数,起 Clients 条线程跑压测,打印吞吐后退出。
 * @return 恒为 0(压测失败次数不影响进程退出码)
 *
 * @detail 选项值缺失或选项名不认识时静默跳过并使用默认值,不做用法报错。计时覆盖
 *         "建连 + 全部请求"的总墙钟时间,所以吞吐会被建连开销摊薄。
 */
int main(int argc, char** argv) {
    SBenchConfig Config;
    for (int i = 1; i < argc; ++i) {
        MString Arg = argv[i];
        if ((Arg == "--host" || Arg == "-h") && i + 1 < argc) {
            Config.Host = argv[++i];
        } else if ((Arg == "--port" || Arg == "-p") && i + 1 < argc) {
            Config.Port = static_cast<uint16>(std::atoi(argv[++i]));
        } else if (Arg == "--clients" && i + 1 < argc) {
            Config.Clients = std::atoi(argv[++i]);
        } else if (Arg == "--requests" && i + 1 < argc) {
            Config.RequestsPerClient = std::atoi(argv[++i]);
        }
    }

    std::printf("NetBench: host=%s port=%u clients=%d requests_per_client=%d\n", Config.Host.c_str(), Config.Port, Config.Clients, Config.RequestsPerClient);

    std::atomic<int>     SuccessCount{0};
    TVector<std::thread> Threads;
    Threads.reserve(Config.Clients);

    const auto StartTime = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < Config.Clients; ++i) {
        Threads.emplace_back(RunClientWorker, std::cref(Config), i, std::ref(SuccessCount));
    }
    for (auto& T : Threads) {
        if (T.joinable()) {
            T.join();
        }
    }

    const auto   EndTime       = std::chrono::high_resolution_clock::now();
    const double Elapsed       = std::chrono::duration<double>(EndTime - StartTime).count();
    const int    TotalRequests = Config.Clients * Config.RequestsPerClient;

    std::printf("Total requests: %d, success: %d, time: %.3fs, throughput: %.1f req/s\n", TotalRequests, SuccessCount.load(), Elapsed, Elapsed > 0.0 ? (TotalRequests / Elapsed) : 0.0);

    return 0;
}
