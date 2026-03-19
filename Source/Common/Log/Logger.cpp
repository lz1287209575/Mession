#include "Logger.h"
#include "ConsoleLogSink.h"
#include "FileLogSink.h"

#include <iostream>
#include <sstream>
#include <chrono>
#include <iomanip>
#include <ctime>
#include <cstdarg>

// --- MLogger ---

namespace
{
const size_t kMessageBufferSize = 4096;
const char* kDefaultPattern = "[%t] [%l] [%n] %v";
}

static MLogger* GDefaultLogger = nullptr;
static MConsoleSink GDefaultConsoleSink(ELogLevel::Trace);
static MFileSink GDefaultFileSink;
static std::mutex GRegistryMutex;
static TMap<MString, TSharedPtr<MLogger>> GRegistry;
static bool GConsoleOutputEnabled = true;


MLogger::MLogger(MString InName)
    : Name(std::move(InName))
    , Pattern(kDefaultPattern)
{
}

void MLogger::AddSink(ILogSink* Sink)
{
    if (Sink)
    {
        std::lock_guard<std::mutex> Lock(Mutex);
        Sinks.push_back(Sink);
    }
}

void MLogger::ClearSinks()
{
    std::lock_guard<std::mutex> Lock(Mutex);
    Sinks.clear();
}

MString MLogger::FormatLine(ELogLevel Level, const char* File, int Line, const char* Func, const char* Message) const
{
    MString Out;
    Out.reserve(Pattern.size() + 512);
    const char* P = Pattern.c_str();
    while (*P)
    {
        if (P[0] == '%' && P[1] != '\0')
        {
            switch (P[1])
            {
                case 't':
                {
                    auto Now = std::chrono::system_clock::now();
                    auto TimeT = std::chrono::system_clock::to_time_t(Now);
                    char Buf[32];
#if defined(_MSC_VER) || (defined(__MINGW32__) && !defined(__USE_MINGW_ANSI_STDIO))
                    std::tm Tm = {};
                    if (localtime_s(&Tm, &TimeT) == 0)
                    {
                        std::strftime(Buf, sizeof(Buf), "%Y-%m-%d %H:%M:%S", &Tm);
                    }
                    else
                    {
                        std::snprintf(Buf, sizeof(Buf), "(time?)");
                    }
#else
                    std::tm* Tm = std::localtime(&TimeT);
                    if (Tm)
                    {
                        std::strftime(Buf, sizeof(Buf), "%Y-%m-%d %H:%M:%S", Tm);
                    }
                    else
                    {
                        std::snprintf(Buf, sizeof(Buf), "(time?)");
                    }
#endif
                    Out += Buf;
                    break;
                }
                case 'l':
                    Out += LogLevelToString(Level);
                    break;
                case 'n':
                    Out += Name;
                    break;
                case 's':
                    if (File && File[0])
                    {
                        Out += File;
                    }
                    break;
                case '#':
                    if (File && File[0])
                    {
                        char Buf[24];
                        std::snprintf(Buf, sizeof(Buf), "%d", Line);
                        Out += Buf;
                    }
                    break;
                case 'f':
                    Out += (Func && Func[0]) ? Func : "";
                    break;
                case 'v':
                    Out += (Message && Message[0]) ? Message : "";
                    break;
                default:
                    Out += P[0];
                    Out += P[1];
                    break;
            }
            P += 2;
            continue;
        }
        Out += *P++;
    }
    return Out;
}

void MLogger::VLog(ELogLevel InLevel, const char* File, int Line, const char* Func, const char* Fmt, va_list Args)
{
    if (InLevel < Level)
    {
        return;
    }
    char MsgBuf[kMessageBufferSize];
    va_list ArgsCopy;
    va_copy(ArgsCopy, Args);
    int N = std::vsnprintf(MsgBuf, sizeof(MsgBuf), Fmt, ArgsCopy);
    va_end(ArgsCopy);
    if (N < 0)
    {
        return;
    }
    if (static_cast<size_t>(N) >= sizeof(MsgBuf))
    {
        N = static_cast<int>(sizeof(MsgBuf) - 1);
    }
    MsgBuf[N] = '\0';

    MString FormattedLine = FormatLine(InLevel, File, Line, Func, MsgBuf);

    std::lock_guard<std::mutex> Lock(Mutex);
    for (ILogSink* Sink : Sinks)
    {
        if (Sink && InLevel >= Sink->GetMinLevel())
        {
            Sink->Write(InLevel, FormattedLine);
        }
    }
}

void MLogger::Log(ELogLevel Level, const char* File, int Line, const char* Func, const char* Fmt, ...)
{
    va_list Args;
    va_start(Args, Fmt);
    VLog(Level, File, Line, Func, Fmt, Args);
    va_end(Args);
}

MLogger* MLogger::DefaultLogger()
{
    if (GDefaultLogger)
    {
        return GDefaultLogger;
    }
    std::lock_guard<std::mutex> Lock(GRegistryMutex);
    if (!GDefaultLogger)
    {
        GDefaultLogger = new MLogger("default");
        GDefaultLogger->AddSink(&GDefaultConsoleSink);
    }
    return GDefaultLogger;
}

void MLogger::Init(const MString& LogFileName, int InMinLevel)
{
    MLogger* L = DefaultLogger();
    L->SetLevel(LegacyIntToLevel(InMinLevel));
    L->ClearSinks();
    if (GConsoleOutputEnabled)
    {
        GDefaultConsoleSink.SetMinLevel(LegacyIntToLevel(InMinLevel));
        L->AddSink(&GDefaultConsoleSink);
    }
    if (!LogFileName.empty())
    {
        if (GDefaultFileSink.Open(LogFileName))
        {
            GDefaultFileSink.SetMinLevel(LegacyIntToLevel(InMinLevel));
            L->AddSink(&GDefaultFileSink);
        }
    }
}

void MLogger::SetMinLevel(int InMinLevel)
{
    DefaultLogger()->SetLevel(LegacyIntToLevel(InMinLevel));
}

void MLogger::SetConsoleOutput(bool bEnable)
{
    GConsoleOutputEnabled = bEnable;
    MLogger* L = DefaultLogger();
    L->ClearSinks();
    if (GConsoleOutputEnabled)
    {
        L->AddSink(&GDefaultConsoleSink);
    }
    if (GDefaultFileSink.IsOpen())
    {
        L->AddSink(&GDefaultFileSink);
    }
}

MLogger* MLogger::GetLogger(const MString& Name)
{
    std::lock_guard<std::mutex> Lock(GRegistryMutex);
    auto It = GRegistry.find(Name);
    return (It != GRegistry.end() && It->second) ? It->second.get() : nullptr;
}

MLogger* MLogger::GetOrCreateLogger(const MString& Name)
{
    {
        std::lock_guard<std::mutex> Lock(GRegistryMutex);
        auto It = GRegistry.find(Name);
        if (It != GRegistry.end() && It->second)
        {
            return It->second.get();
        }
        TSharedPtr<MLogger> Logger = MakeShared<MLogger>(Name);
        MLogger* Raw = Logger.get();
        GRegistry[Name] = std::move(Logger);
        Raw->SetLevel(DefaultLogger()->GetLevel());
        Raw->SetPattern(DefaultLogger()->GetPattern());
        Raw->AddSink(&GDefaultConsoleSink);
        if (GDefaultFileSink.IsOpen())
        {
            Raw->AddSink(&GDefaultFileSink);
        }
        return Raw;
    }
}

void MLogger::LogStartupBanner(const char* ServiceName, uint16 Port, intptr_t Fd)
{
    static const char* const AsciiArt[] = {
      " ███╗   ███╗███████╗███████╗███████╗██╗ ██████╗ ███╗   ██╗ ",
      " ████╗ ████║██╔════╝██╔════╝██╔════╝██║██╔═══██╗████╗  ██║ ",
      " ██╔████╔██║█████╗  ███████╗███████╗██║██║   ██║██╔██╗ ██║ ",
      " ██║╚██╔╝██║██╔══╝  ╚════██║╚════██║██║██║   ██║██║╚██╗██║ ",
      " ██║ ╚═╝ ██║███████╗███████║███████║██║╚██████╔╝██║ ╚████║ ",
      " ╚═╝     ╚═╝╚══════╝╚══════╝╚══════╝╚═╝ ╚═════╝ ╚═╝  ╚═══╝ "
    };
    static const char* Version = "1.0.0";

    LOG_INFO(""); // 输出空行分隔
    for (const char* Line : AsciiArt)
    {
        LOG_INFO("%s", Line);
    }
    LOG_INFO("  :: Mession Game Server ::  (v%s)", Version);
    LOG_INFO("");
    LOG_INFO("  .   Starting %s...", ServiceName);
    if (Fd >= 0)
    {
        LOG_INFO("  .   Listening on port %u (fd=%zd)", static_cast<unsigned>(Port), Fd);
    }
    else
    {
        LOG_INFO("  .   Listening on port %u", static_cast<unsigned>(Port));
    }
}

void MLogger::LogStarted(const char* ServiceName, double ElapsedSeconds)
{
    LOG_INFO("  .   Started %s in %.3f seconds", ServiceName, ElapsedSeconds);
    LOG_INFO("");
}
