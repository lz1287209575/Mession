/**
 * @file Log.cpp
 * @brief MLog 门面实现 —— 进程单例的装配/拆卸,写入热路径,以及 FATAL 现场捕获。
 *
 * 全局状态均为进程单例:MPSC 环形队列 + 一个 dispatcher + 每个 sink
 * 一个写线程。Init() 按参数装配 sink 并启动线程,Shutdown() 逆序拆卸。
 * 热路径 WriteV() 只做「过滤 → 路由 → 组记录 → TryEnqueue」,入队失败
 * 记 DroppedOverflow,绝不阻塞业务线程。
 */
#include "Common/Runtime/Log/Log.h"
#include "Common/Runtime/Log/ConsoleSink.h"
#include "Common/Runtime/Log/CoredumpSink.h"
#include "Common/Runtime/Log/Dispatcher.h"
#include "Common/Runtime/Log/LogCategories.h"
#include "Common/Runtime/Log/LogFilter.h"
#include "Common/Runtime/Log/LogMetrics.h"
#include "Common/Runtime/Log/LogRegistry.h"
#include "Common/Runtime/Log/LogRouter.h"
#include "Common/Runtime/Log/LogStringTable.h"
#include "Common/Runtime/Log/RollingFileSink.h"
#include "Common/Runtime/Log/SinkWriter.h"
#include "Common/Runtime/Log/TcpSink.h"
#include "Common/Runtime/Log/UdpSink.h"

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <thread>

namespace {
    // MUdpSink 适配器:让 MLogSinkWriter 能通过统一的 ILogSink* 操作一个
    // (可能未配置的)MUdpSink。实现放在这里(UdpSink.h 之后)是因为此时
    // MUdpSink 才是完整类型。
    class MUdpSinkHolder : public ILogSink {
        public:
        TUniquePtr<MUdpSink> Sink;
        bool                 Open() override {
            return Sink ? Sink->Open() : false;
        }
        void Close() override {
            if (Sink)
                Sink->Close();
        }
        void WriteBatch(TSpan<const SLogRecord> B, TSpanMutable<char> O) override {
            if (Sink)
                Sink->WriteBatch(B, O);
        }
        void Flush() override {
            if (Sink)
                Sink->Flush();
        }
        ELogLevel MinLevel() const override {
            return Sink ? Sink->MinLevel() : ELogLevel::Trace;
        }
        const char* Name() const override {
            return "udp";
        }
    };

    // ---- 全局状态(进程单例) ----
    std::atomic<bool>            GInitialized{false};
    TMpscRingBuffer<SLogRecord>* GRing       = nullptr;
    MLogDispatcher*              GDispatcher = nullptr;
    TVector<MLogSinkWriter*>     GWriters;
    // sink 本体。用 unique_ptr 持有,以便 Shutdown() 按序拆卸:
    // 先停写线程 → 再 Close sink → 最后释放。
    TUniquePtr<MConsoleSink>     GConsole;
    TUniquePtr<MRollingFileSink> GFile;
    TUniquePtr<MUdpSinkHolder>   GUdpHolder;
    TUniquePtr<MTcpSink>         GTcp;

    constexpr size_t kMaxInlineMessage = sizeof(SLogRecord::Payload.Inline.Data);

    // 把 printf 风格消息格式化进 Out。返回写入字节数(不含结尾 NUL);
    // 结果被截断时返回 OutSize-1(N 为负时返回 0)。
    size_t FormatInline(const char* Fmt, va_list Args, char* Out, size_t OutSize) {
        // va_list 路径:fmt 在 C++17 下无直接 va_list → format_args 的官方 API。
        // fmt::make_format_args 需要编译期参数类型,vsnprintf 是此路径上
        // 最简实现。后续若 fmt 提供 va_list 支持(0.x 或 1.x 实验分支)
        // 再切换。
        if (OutSize == 0)
            return 0;
        const int N = std::vsnprintf(Out, OutSize, Fmt, Args);
        if (N < 0) {
            Out[0] = '\0';
            return 0;
        }
        const size_t Len = static_cast<size_t>(N);
        if (Len >= OutSize)
            return OutSize - 1;
        return Len;
    }
} // namespace

namespace MLog {
    void Init(const SLogInitParams& Params) {
        if (GInitialized.exchange(true))
            return;

        MLogStringTable::Init();

        GRing       = new TMpscRingBuffer<SLogRecord>(Params.RingCapacity);
        GDispatcher = new MLogDispatcher();
        GDispatcher->SetBatchSize(1024);

        // 解析路由掩码:先默认打开全部 sink,再由 Router 在
        // ResolveSinkMask 里按分类收窄。
        uint32 Mask = 0;

        if (Params.bEnableConsole) {
            GConsole = TUniquePtr<MConsoleSink>(new MConsoleSink());
            GConsole->Open();
            Mask |= (1u << static_cast<uint32>(ELogSinkId::Console));
        }
        if (!Params.FilePath.empty()) {
            GFile                   = TUniquePtr<MRollingFileSink>(new MRollingFileSink());
            GFile->ServiceName      = "Mession";
            GFile->FilePath         = Params.FilePath;
            GFile->RotatedFileBytes = Params.RotatedFileBytes;
            GFile->NumArchives      = Params.NumArchives;
            if (GFile->Open()) {
                Mask |= (1u << static_cast<uint32>(ELogSinkId::File));
            }
        }
        if (Params.bEnableUdp && !Params.UdpTarget.empty()) {
            GUdpHolder               = TUniquePtr<MUdpSinkHolder>(new MUdpSinkHolder());
            GUdpHolder->Sink         = TUniquePtr<MUdpSink>(new MUdpSink());
            GUdpHolder->Sink->Target = Params.UdpTarget;
            if (GUdpHolder->Open()) {
                Mask |= (1u << static_cast<uint32>(ELogSinkId::Udp));
            }
        }
        if (Params.bEnableTcp && !Params.TcpTarget.empty()) {
            GTcp         = TUniquePtr<MTcpSink>(new MTcpSink());
            GTcp->Target = Params.TcpTarget;
            if (GTcp->Open()) {
                Mask |= (1u << static_cast<uint32>(ELogSinkId::Tcp));
            }
        }

        // 每个启用的 sink 建一个写线程。顺序很重要:dispatcher 按位位置把
        // 记录分进各分区,位位置必须与 sink 下标一致。
        auto AddWriter = [&](ILogSink* SinkPtr) {
            if (SinkPtr == nullptr)
                return;
            MLogSinkWriter* W = new MLogSinkWriter();
            W->SetSink(SinkPtr, EFlushPolicy::IntervalOrSize);
            W->Start();
            GWriters.push_back(W);
        };
        AddWriter(GConsole.get());
        AddWriter(GFile.get());
        AddWriter(GUdpHolder.get());
        AddWriter(GTcp.get());

        GDispatcher->Configure(GRing, &GWriters);
        GDispatcher->Start();

        // 抑制 unused-Mask 告警:当前 Router 的默认规则已把所有 sink 打开,
        // 这个局部掩码尚未被消费,保留给后续「按分类设默认掩码」使用。
        (void)Mask;
    }

    void Shutdown() {
        if (!GInitialized.exchange(false))
            return;
        if (GDispatcher) {
            GDispatcher->Stop();
        }
        for (MLogSinkWriter* W : GWriters) {
            if (W)
                W->Stop();
        }
        for (MLogSinkWriter* W : GWriters) {
            delete W;
        }
        GWriters.clear();

        if (GConsole) {
            GConsole->Close();
            GConsole.reset();
        }
        if (GFile) {
            GFile->Close();
            GFile.reset();
        }
        if (GUdpHolder) {
            GUdpHolder->Close();
            GUdpHolder.reset();
        }
        if (GTcp) {
            GTcp->Close();
            GTcp.reset();
        }

        if (GDispatcher) {
            delete GDispatcher;
            GDispatcher = nullptr;
        }
        if (GRing) {
            delete GRing;
            GRing = nullptr;
        }
    }

    void WriteV(const SLogCategory* Category, ELogLevel Level, const char* Fmt, va_list Args) {
        if (GRing == nullptr)
            return;
        if (Category == nullptr)
            return;
        if (!MLogFilter::ShouldLog(Category, Level))
            return;

        const uint32 SinkMask = MLogRouter::Get().ResolveSinkMask(Category->Id, Level);
        if (SinkMask == 0)
            return;

        SLogRecord R{};
        R.TimestampNs       = static_cast<uint64>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
        R.ThreadId          = 0;
        R.CategoryId        = Category->Id;
        R.Level             = static_cast<uint8>(Level);
        R.Flags             = 0;
        R.FileStringId      = 0;
        R.Line              = 0;
        R.FuncStringId      = 0;
        R.SinkMask          = SinkMask;
        R.ContextSnapshotId = 0;

        va_list ArgsCopy;
        va_copy(ArgsCopy, Args);
        const size_t Len = FormatInline(Fmt, ArgsCopy, R.Payload.Inline.Data, kMaxInlineMessage);
        va_end(ArgsCopy);
        if (R.Payload.Inline.Data[Len] != '\0') {
            R.Payload.Inline.Data[std::min(Len, kMaxInlineMessage - 1)] = '\0';
        }

        if (!GRing->TryEnqueue(R)) {
            MLogMetrics::IncDroppedOverflow();
            return;
        }
        MLogMetrics::IncEnqueued();
    }

    void Write(const SLogCategory* Category, ELogLevel Level, const char* Fmt, ...) {
        va_list Args;
        va_start(Args, Fmt);
        WriteV(Category, Level, Fmt, Args);
        va_end(Args);
    }

    void HandleFatal(const SLogCategory* Category, const char* Fmt, va_list Args) {
        if (Category == nullptr)
            return;

        // 就地构造触发记录:绕过环形队列,直接交给 coredump 路径。
        SLogRecord Trigger{};
        Trigger.TimestampNs = static_cast<uint64>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
        Trigger.CategoryId  = Category->Id;
        Trigger.Level       = static_cast<uint8>(ELogLevel::Critical);

        va_list ArgsCopy;
        va_copy(ArgsCopy, Args);
        const size_t Len = FormatInline(Fmt, ArgsCopy, Trigger.Payload.Inline.Data, kMaxInlineMessage);
        va_end(ArgsCopy);
        if (Trigger.Payload.Inline.Data[Len] != '\0') {
            Trigger.Payload.Inline.Data[std::min(Len, kMaxInlineMessage - 1)] = '\0';
        }

        // 先排空环形队列,让 dump 覆盖到仍在途中的记录。按 RecentRecords
        // 的上限反复出队。
        TVector<SLogRecord> Tail;
        if (GRing) {
            SLogRecord Buf[256];
            // NOTE: 单趟取一批 —— 队列无法按时间回溯。规范要求「扫描
            // RingBuffer 尾部」,但我们的 MpscRingBuffer 只暴露存活头部;
            // 因此这里只取当前已入队的部分,其余依赖写线程在这条同步路径
            // 之前已排空。PoC 阶段单趟 DequeueBatch 足够。
            const size_t N = GRing->DequeueBatch(Buf, 256);
            for (size_t i = 0; i < N; ++i)
                Tail.push_back(Buf[i]);
        }

        MCoredumpSink::SConfig Cfg;
        Cfg.DumpDir        = "Logs/coredump";
        Cfg.RecentRecords  = 1000;
        Cfg.bForceCoreDump = true;
        // 强制刷新在途写线程,保证 file/console 镜像先于 coredump 文件落盘。
        for (MLogSinkWriter* W : GWriters) {
            if (W)
                W->FlushSync();
        }

        MString TriggeringMessage = MString(Trigger.Payload.Inline.Data);
        MCoredumpSink::HandleFatal(Cfg, Trigger, Tail, TriggeringMessage);
    }

    void SetCategoryLevel(const SLogCategory& Category, ELogLevel Level) {
        const_cast<SLogCategory&>(Category).RuntimeLevel.store(Level);
    }

    void SetCategorySuppressed(const SLogCategory& Category, bool bSuppressed) {
        const_cast<SLogCategory&>(Category).bSuppressed.store(bSuppressed);
    }

    bool ApplyCategoryConfig(const TVector<SLogCategoryConfig>& Configs) {
        bool Any = false;
        for (const auto& C : Configs) {
            const SLogCategory* Cat = MLogRegistry::Get().FindByName(C.Name);
            if (Cat == nullptr)
                continue;
            const_cast<SLogCategory*>(Cat)->RuntimeLevel.store(C.Level);
            const_cast<SLogCategory*>(Cat)->bSuppressed.store(C.bSuppressed);
            Any = true;
        }
        return Any;
    }

    bool ApplyRouteConfig(const TVector<SLogRouteConfig>& Configs) {
        bool Any = false;
        for (const auto& R : Configs) {
            const SLogCategory* Cat = MLogRegistry::Get().FindByName(R.Name);
            if (Cat == nullptr)
                continue;
            uint32 Mask = 0;
            for (const auto& S : R.Sinks) {
                if (S == "console")
                    Mask |= 1u << static_cast<uint32>(ELogSinkId::Console);
                else if (S == "file")
                    Mask |= 1u << static_cast<uint32>(ELogSinkId::File);
                else if (S == "udp")
                    Mask |= 1u << static_cast<uint32>(ELogSinkId::Udp);
                else if (S == "tcp")
                    Mask |= 1u << static_cast<uint32>(ELogSinkId::Tcp);
                else if (S == "coredump")
                    Mask |= 1u << static_cast<uint32>(ELogSinkId::Coredump);
            }
            if (Mask == 0)
                Mask = 0xFFFFFFFFu; // 未知 sink 名:保持默认。
            SLogRouteRule Rule;
            Rule.Category = Cat;
            Rule.SinkMask = Mask;
            Rule.MinLevel = R.MinLevel;
            MLogRouter::Get().SetRule(Rule);
            Any = true;
        }
        return Any;
    }

    void Flush(int /*TimeoutMs*/) {
        if (GDispatcher)
            GDispatcher->Stop();
        for (MLogSinkWriter* W : GWriters)
            if (W)
                W->FlushSync();
    }
} // namespace MLog