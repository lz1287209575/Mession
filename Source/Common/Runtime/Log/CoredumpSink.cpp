#include "Common/Runtime/Log/CoredumpSink.h"
#include "Common/Runtime/Log/LogStringTable.h"
#include "Common/Runtime/Log/LogRegistry.h"
#include "Common/Runtime/Json.h"

#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iostream>
#include <sys/stat.h>
#include <sys/types.h>
#include <thread>
#include <unistd.h>

namespace
{
    MString IsoTimestampNow()
    {
        using namespace std::chrono;
        const auto Now = system_clock::now();
        const std::time_t TimeT = system_clock::to_time_t(Now);
        std::tm Tm{};
#if defined(_MSC_VER)
        localtime_s(&Tm, &TimeT);
#else
        localtime_r(&TimeT, &Tm);
#endif
        char Buf[32];
        std::strftime(Buf, sizeof(Buf), "%Y%m%dT%H%M%S", &Tm);
        return MString(Buf);
    }

    bool MkdirP(const MString& Path)
    {
        if (Path.empty()) return false;
        MString Acc;
        for (size_t i = 0; i < Path.size(); ++i)
        {
            Acc.push_back(Path[i]);
            if (Path[i] == '/' || i + 1 == Path.size())
            {
                if (::mkdir(Acc.c_str(), 0755) != 0)
                {
                    if (errno != EEXIST) return false;
                }
            }
        }
        return true;
    }

    void CopyMessage(const SLogRecord& R, char* Out, size_t OutSize)
    {
        const size_t Max = sizeof(R.Payload.Inline.Data);
        size_t Len = 0;
        while (Len < Max && R.Payload.Inline.Data[Len] != '\0') ++Len;
        if (Len >= OutSize) Len = OutSize - 1;
        std::memcpy(Out, R.Payload.Inline.Data, Len);
        Out[Len] = '\0';
    }

    const char* ResolveCategoryName(uint16 Id)
    {
        const SLogCategory* Cat = MLogRegistry::Get().GetById(Id);
        return (Cat && Cat->Name) ? Cat->Name : "?";
    }

    const char* ResolveInterned(uint32 Id)
    {
        const char* S = MLogStringTable::Get(Id);
        return (S && S[0]) ? S : "";
    }

    // Read /proc/self/status safely; returns "" on missing file.
    MString ReadFileOrEmpty(const MString& Path, size_t MaxBytes = 64 * 1024)
    {
        std::ifstream F(Path);
        if (!F.is_open()) return MString();
        MString Out;
        Out.reserve(512);
        char C;
        size_t N = 0;
        while (F.get(C) && N < MaxBytes)
        {
            Out.push_back(C);
            ++N;
        }
        return Out;
    }

    MString JsonEscape(const MString& S)
    {
        MString Out;
        Out.reserve(S.size() + 8);
        for (char C : S)
        {
            switch (C)
            {
                case '"':  Out += "\\\""; break;
                case '\\': Out += "\\\\"; break;
                case '\n': Out += "\\n";  break;
                case '\r': Out += "\\r";  break;
                case '\t': Out += "\\t";  break;
                default:
                    if (static_cast<unsigned char>(C) < 0x20)
                    {
                        const MString Esc = MFormat::Format("\\u{:04x}", static_cast<unsigned char>(C));
                        Out += Esc;
                    }
                    else
                    {
                        Out.push_back(C);
                    }
            }
        }
        return Out;
    }

    MString RenderRecord(const SLogRecord& R)
    {
        char Message[64];
        CopyMessage(R, Message, sizeof(Message));
        const ELogLevel Lvl = static_cast<ELogLevel>(R.Level);
        const char* CatName = ResolveCategoryName(R.CategoryId);
        const char* FileStr = ResolveInterned(R.FileStringId);
        const char* FuncStr = ResolveInterned(R.FuncStringId);

        MJsonWriter W = MJsonWriter::Object();
        W.Key("ts");       W.Value(R.TimestampNs);
        W.Key("level");    W.Value(MString(LogLevelToString(Lvl)));
        W.Key("category"); W.Value(MString(CatName));
        W.Key("thread");   W.Value(static_cast<uint64>(R.ThreadId));
        W.Key("file");     W.Value(MString(FileStr));
        W.Key("line");     W.Value(static_cast<uint64>(R.Line));
        W.Key("func");     W.Value(MString(FuncStr));
        W.Key("msg");      W.Value(MString(Message));
        W.EndObject();
        return W.ToString();
    }
}

void MCoredumpSink::HandleFatal(const SConfig& Config,
                                const SLogRecord& TriggeringRecord,
                                const TVector<SLogRecord>& TailRecords,
                                const MString& TriggeringMessage)
{
    if (!MkdirP(Config.DumpDir))
    {
        std::fprintf(stderr, "[MLog] FATAL: cannot create dump dir %s\n",
            Config.DumpDir.c_str());
    }

    const MString Ts = IsoTimestampNow();
    const MString Path = Config.DumpDir + "/coredump-" + Ts + ".jsonl";

    std::ofstream Out(Path, std::ios::out | std::ios::trunc);
    if (!Out.is_open())
    {
        std::fprintf(stderr, "[MLog] FATAL: cannot open dump file %s\n", Path.c_str());
        if (Config.bForceCoreDump)
        {
            std::abort();
        }
        return;
    }

    // 1) Triggering record (carries the formatted message in the file so the
    // dump is self-contained even if MLogStringTable is not flushed).
    Out << "{\"event\":\"trigger\",\"msg\":\""
        << JsonEscape(TriggeringMessage).c_str() << "\",\"record\":"
        << RenderRecord(TriggeringRecord).c_str() << "}\n";

    // 2) Tail of the ring buffer (recent N records).
    Out << "{\"event\":\"tail\",\"records\":[";
    for (size_t i = 0; i < TailRecords.size(); ++i)
    {
        if (i) Out << ",";
        Out << RenderRecord(TailRecords[i]).c_str();
    }
    Out << "]}\n";

    // 3) Process/system context.
    const MString Pid = MFormat::Format("{}", static_cast<int>(::getpid()));
    MString Status = ReadFileOrEmpty("/proc/self/status");
    MString Maps   = ReadFileOrEmpty("/proc/self/maps");
    char Host[256] = {};
    if (::gethostname(Host, sizeof(Host) - 1) != 0) Host[0] = '\0';

    MJsonWriter Ctx = MJsonWriter::Object();
    Ctx.Key("pid");        Ctx.Value(Pid);
    Ctx.Key("hostname");   Ctx.Value(MString(Host));
    Ctx.Key("status");     Ctx.Value(Status);
    Ctx.Key("maps_excerpt"); Ctx.Value(Maps.substr(0, std::min<size_t>(Maps.size(), 8192)));
    Ctx.EndObject();
    Out << "{\"event\":\"context\"," << Ctx.ToString().c_str() << "}\n";

    Out.flush();
    Out.close();

    // Fire upload hook asynchronously so the abort below is not blocked.
    if (Config.UploadHook)
    {
        std::thread([Hook = Config.UploadHook, Path]() {
            try { Hook(Path); }
            catch (...) {
                std::fprintf(stderr, "[MLog] UploadHook threw\n");
            }
        }).detach();
    }

    if (Config.bForceCoreDump)
    {
        // raise(SIGABRT) — OS writes core file (or terminates the process
        // according to system policy). Spec §6.1 step 6.
        std::raise(SIGABRT);
    }
}