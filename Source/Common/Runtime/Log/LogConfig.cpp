#include "Common/Runtime/Log/LogConfig.h"
#include "Common/Runtime/Json.h"

#include <fstream>

namespace
{
    // Light-weight JSON tokenizer sufficient for the LogConfig schema.
    struct SLex
    {
        const char* P;
        size_t Len;
    };

    void SkipWs(SLex& L)
    {
        while (L.P && *L.P && (*L.P == ' ' || *L.P == '\t' || *L.P == '\n' || *L.P == '\r'))
        {
            ++L.P;
        }
    }

    bool Consume(SLex& L, char C)
    {
        SkipWs(L);
        if (L.P && *L.P == C) { ++L.P; return true; }
        return false;
    }

    // Accept either a quoted string ("...") or a bare token (true/false/numbers).
    // The schema only uses these forms; both compare equal against strings on the
    // call sites, so accepting both avoids forcing every caller to wrap values
    // in quotes.
    bool ReadString(SLex& L, MString& Out)
    {
        SkipWs(L);
        if (!L.P || !*L.P) return false;
        if (*L.P == '"')
        {
            ++L.P;
            Out.clear();
            while (L.P && *L.P && *L.P != '"')
            {
                if (*L.P == '\\' && L.P[1])
                {
                    ++L.P;
                    Out.push_back(*L.P);
                    ++L.P;
                    continue;
                }
                Out.push_back(*L.P++);
            }
            if (!L.P || *L.P != '"') return false;
            ++L.P;
            return true;
        }
        Out.clear();
        while (L.P && *L.P
            && *L.P != ',' && *L.P != '}' && *L.P != ']'
            && *L.P != ' ' && *L.P != '\t' && *L.P != '\n' && *L.P != '\r')
        {
            Out.push_back(*L.P++);
        }
        return !Out.empty();
    }
}

bool MLogApplyConfigFile(const MString& Path,
                         SLogInitParams& InOutParams,
                         TVector<SLogCategoryConfig>& OutCategories,
                         TVector<SLogRouteConfig>& OutRoutes)
{
    std::ifstream F(Path);
    if (!F.is_open()) return false;

    MString Buf;
    Buf.reserve(8192);
    char C;
    while (F.get(C)) Buf.push_back(C);

    SLex L{Buf.c_str(), Buf.size()};
    if (!Consume(L, '{')) return false;

    while (true)
    {
        SkipWs(L);
        if (!L.P || *L.P == '}') break;
        if (!L.P || *L.P != '"') return false;
        MString Key;
        if (!ReadString(L, Key)) return false;
        if (!Consume(L, ':')) return false;

        if (Key == "defaultLevel")
        {
            MString V; if (!ReadString(L, V)) return false;
            if      (V == "Trace")    InOutParams.GlobalDefaultLevel = ELogLevel::Trace;
            else if (V == "Debug")    InOutParams.GlobalDefaultLevel = ELogLevel::Debug;
            else if (V == "Info")     InOutParams.GlobalDefaultLevel = ELogLevel::Info;
            else if (V == "Warn")     InOutParams.GlobalDefaultLevel = ELogLevel::Warn;
            else if (V == "Error")    InOutParams.GlobalDefaultLevel = ELogLevel::Error;
            else if (V == "Critical") InOutParams.GlobalDefaultLevel = ELogLevel::Critical;
        }
        else if (Key == "filePath")
        {
            MString V; if (!ReadString(L, V)) return false;
            InOutParams.FilePath = V;
        }
        else if (Key == "enableConsole")
        {
            MString V; if (!ReadString(L, V)) return false;
            InOutParams.bEnableConsole = (V == "true");
        }
        else if (Key == "logDir")
        {
            MString V; if (!ReadString(L, V)) return false;
            InOutParams.LogDir = V;
        }
        else if (Key == "rotatedFileBytes")
        {
            MString V; if (!ReadString(L, V)) return false;
            InOutParams.RotatedFileBytes = static_cast<size_t>(std::strtoull(V.c_str(), nullptr, 10));
        }
        else if (Key == "numArchives")
        {
            MString V; if (!ReadString(L, V)) return false;
            InOutParams.NumArchives = static_cast<size_t>(std::strtoul(V.c_str(), nullptr, 10));
        }
        else if (Key == "enableUdp")
        {
            MString V; if (!ReadString(L, V)) return false;
            InOutParams.bEnableUdp = (V == "true");
        }
        else if (Key == "udpTarget")
        {
            MString V; if (!ReadString(L, V)) return false;
            InOutParams.UdpTarget = V;
        }
        else if (Key == "enableTcp")
        {
            MString V; if (!ReadString(L, V)) return false;
            InOutParams.bEnableTcp = (V == "true");
        }
        else if (Key == "tcpTarget")
        {
            MString V; if (!ReadString(L, V)) return false;
            InOutParams.TcpTarget = V;
        }
        else if (Key == "ringCapacity")
        {
            MString V; if (!ReadString(L, V)) return false;
            InOutParams.RingCapacity = static_cast<size_t>(std::strtoul(V.c_str(), nullptr, 10));
        }
        else if (Key == "categories")
        {
            if (!Consume(L, '[')) return false;
            while (!Consume(L, ']'))
            {
                if (!Consume(L, '{')) return false;
                SLogCategoryConfig Cat;
                while (!Consume(L, '}'))
                {
                    MString K; if (!ReadString(L, K)) return false;
                    if (!Consume(L, ':')) return false;
                    MString V; if (!ReadString(L, V)) return false;
                    if (K == "name") Cat.Name = V;
                    else if (K == "level")
                    {
                        if      (V == "Trace")    Cat.Level = ELogLevel::Trace;
                        else if (V == "Debug")    Cat.Level = ELogLevel::Debug;
                        else if (V == "Info")     Cat.Level = ELogLevel::Info;
                        else if (V == "Warn")     Cat.Level = ELogLevel::Warn;
                        else if (V == "Error")    Cat.Level = ELogLevel::Error;
                        else if (V == "Critical") Cat.Level = ELogLevel::Critical;
                    }
                    else if (K == "suppressed") Cat.bSuppressed = (V == "true");
                    if (!Consume(L, ',')) break;
                }
                if (!Cat.Name.empty()) OutCategories.push_back(Cat);
                if (!Consume(L, ',')) break;
            }
        }
        else if (Key == "routes")
        {
            if (!Consume(L, '[')) return false;
            while (!Consume(L, ']'))
            {
                if (!Consume(L, '{')) return false;
                SLogRouteConfig Rt;
                while (!Consume(L, '}'))
                {
                    MString K; if (!ReadString(L, K)) return false;
                    if (!Consume(L, ':')) return false;
                    if (K == "name") { MString V; if (!ReadString(L, V)) return false; Rt.Name = V; }
                    else if (K == "minLevel")
                    {
                        MString V; if (!ReadString(L, V)) return false;
                        if      (V == "Trace")    Rt.MinLevel = ELogLevel::Trace;
                        else if (V == "Debug")    Rt.MinLevel = ELogLevel::Debug;
                        else if (V == "Info")     Rt.MinLevel = ELogLevel::Info;
                        else if (V == "Warn")     Rt.MinLevel = ELogLevel::Warn;
                        else if (V == "Error")    Rt.MinLevel = ELogLevel::Error;
                        else if (V == "Critical") Rt.MinLevel = ELogLevel::Critical;
                    }
                    else if (K == "sinks")
                    {
                        if (!Consume(L, '[')) return false;
                        while (!Consume(L, ']'))
                        {
                            MString V; if (!ReadString(L, V)) return false;
                            Rt.Sinks.push_back(V);
                            if (!Consume(L, ',')) break;
                        }
                    }
                    if (!Consume(L, ',')) break;
                }
                if (!Rt.Name.empty()) OutRoutes.push_back(Rt);
                if (!Consume(L, ',')) break;
            }
        }
        else
        {
            // Skip unknown value: string only.
            MString V; if (!ReadString(L, V)) return false;
        }

        if (!Consume(L, ',')) break;
        SkipWs(L);
        if (L.P && *L.P == '}') break;
    }
    return true;
}