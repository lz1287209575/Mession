#include "Logger.h"
#include <iostream>
#include <sstream>
#include <chrono>
#include <iomanip>
#include <ctime>
#if defined(_WIN32) || defined(_WIN64)
#include <windows.h>
#endif

namespace
{
const size_t kMessageBufferSize = 4096;
const char* kDefaultPattern = "[%t] [%l] [%n] %v";

ELogLevel LegacyIntToLevel(int Value)
{
    if (Value <= 0)
    {
        return ELogLevel::Debug;
    }
    if (Value >= 5)
    {
        return ELogLevel::Critical;
    }
    return static_cast<ELogLevel>(Value);
}

#if defined(_WIN32) || defined(_WIN64)
bool WriteUtf8LineToWindowsConsole(const FString& Line)
{
    HANDLE StdoutHandle = GetStdHandle(STD_OUTPUT_HANDLE);
    if (StdoutHandle == nullptr || StdoutHandle == INVALID_HANDLE_VALUE)
    {
        return false;
    }

    DWORD ConsoleMode = 0;
    if (!GetConsoleMode(StdoutHandle, &ConsoleMode))
    {
        return false;
    }

    const int WideLength = MultiByteToWideChar(
        CP_UTF8,
        0,
        Line.c_str(),
        static_cast<int>(Line.size()),
        nullptr,
        0);
    if (WideLength <= 0)
    {
        return false;
    }

    std::wstring WideLine;
    WideLine.resize(static_cast<size_t>(WideLength));
    if (MultiByteToWideChar(
            CP_UTF8,
            0,
            Line.c_str(),
            static_cast<int>(Line.size()),
            WideLine.data(),
            WideLength) <= 0)
    {
        return false;
    }

    WideLine += L"\n";
    DWORD CharsWritten = 0;
    return WriteConsoleW(
        StdoutHandle,
        WideLine.c_str(),
        static_cast<DWORD>(WideLine.size()),
        &CharsWritten,
        nullptr) != 0;
}
#endif
}

void MConsoleSink::Write(ELogLevel Level, const FString& FormattedLine)
{
    if (Level < MinLevel)
    {
        return;
    }
    std::lock_guard<std::mutex> Lock(WriteMutex);
#if defined(_WIN32) || defined(_WIN64)
    if (WriteUtf8LineToWindowsConsole(FormattedLine))
    {
        return;
    }
#endif
    std::cout << FormattedLine << std::endl;
}

MFileSink::~MFileSink()
{
    Close();
}

bool MFileSink::Open(const FString& FilePath)
{
    std::lock_guard<std::mutex> Lock(WriteMutex);
    if (Stream.is_open())
    {
        Stream.close();
    }
    Stream.open(FilePath, std::ios::out | std::ios::app);
    return Stream.is_open();
}

void MFileSink::Close()
{
    std::lock_guard<std::mutex> Lock(WriteMutex);
    if (Stream.is_open())
    {
        Stream.close();
    }
}

void MFileSink::Write(ELogLevel Level, const FString& FormattedLine)
{
    if (Level < MinLevel)
    {
        return;
    }
    std::lock_guard<std::mutex> Lock(WriteMutex);
    if (Stream.is_open())
    {
        Stream << FormattedLine << std::endl;
    }
}

void MFileSink::Flush()
{
    std::lock_guard<std::mutex> Lock(WriteMutex);
    if (Stream.is_open())
    {
        Stream.flush();
    }
}

// --- MLogger ---

static MLogger* GDefaultLogger = nullptr;
static MConsoleSink GDefaultConsoleSink(ELogLevel::Trace);
static MFileSink GDefaultFileSink;
static std::mutex GRegistryMutex;
static TMap<FString, TSharedPtr<MLogger>> GRegistry;
static bool GConsoleOutputEnabled = true;

MLogger::MLogger(FString InName)
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

FString MLogger::FormatLine(ELogLevel Level, const char* File, int Line, const char* Func, const char* Message) const
{
    FString Out;
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

    FString FormattedLine = FormatLine(InLevel, File, Line, Func, MsgBuf);

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

void MLogger::Debug(const char* Fmt, ...)
{
    va_list Args;
    va_start(Args, Fmt);
    VLog(ELogLevel::Debug, "", 0, "", Fmt, Args);
    va_end(Args);
}

void MLogger::Info(const char* Fmt, ...)
{
    va_list Args;
    va_start(Args, Fmt);
    VLog(ELogLevel::Info, "", 0, "", Fmt, Args);
    va_end(Args);
}

void MLogger::Warning(const char* Fmt, ...)
{
    va_list Args;
    va_start(Args, Fmt);
    VLog(ELogLevel::Warn, "", 0, "", Fmt, Args);
    va_end(Args);
}

void MLogger::Error(const char* Fmt, ...)
{
    va_list Args;
    va_start(Args, Fmt);
    VLog(ELogLevel::Error, "", 0, "", Fmt, Args);
    va_end(Args);
}

void MLogger::Fatal(const char* Fmt, ...)
{
    va_list Args;
    va_start(Args, Fmt);
    VLog(ELogLevel::Critical, "", 0, "", Fmt, Args);
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

void MLogger::Init(const FString& LogFileName, int InMinLevel)
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

MLogger* MLogger::GetLogger(const FString& Name)
{
    std::lock_guard<std::mutex> Lock(GRegistryMutex);
    auto It = GRegistry.find(Name);
    return (It != GRegistry.end() && It->second) ? It->second.get() : nullptr;
}

MLogger* MLogger::GetOrCreateLogger(const FString& Name)
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

    MLogger* L = DefaultLogger();
    L->Info("");
    for (const char* Line : AsciiArt)
    {
        L->Info("%s", Line);
    }
    L->Info("  :: Mession Game Server ::  (v%s)", Version);
    L->Info("");
    L->Info("  .   Starting %s...", ServiceName);
    if (Fd >= 0)
    {
        L->Info("  .   Listening on port %u (fd=%zd)", static_cast<unsigned>(Port), Fd);
    }
    else
    {
        L->Info("  .   Listening on port %u", static_cast<unsigned>(Port));
    }
}

void MLogger::LogStarted(const char* ServiceName, double ElapsedSeconds)
{
    DefaultLogger()->Info("  .   Started %s in %.3f seconds", ServiceName, ElapsedSeconds);
    DefaultLogger()->Info("");
}
