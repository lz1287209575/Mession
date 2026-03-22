#pragma once

#include "Common/Runtime/MLib.h"
#include <cstdlib>

namespace MServerMain
{
inline int FindPortOverride(int argc, char** argv)
{
    for (int Index = 1; Index + 1 < argc; ++Index)
    {
        const MString Arg = argv[Index] ? argv[Index] : "";
        if (Arg == "--port" || Arg == "-p")
        {
            return std::atoi(argv[Index + 1]);
        }
    }

    return 0;
}
}

template<typename TServer>
int RunMessionServerMain(int argc, char** argv, const char* ConfigPath = "")
{
    TServer Server;
    Server.LoadConfig(ConfigPath ? ConfigPath : "");
    if (!Server.Init(MServerMain::FindPortOverride(argc, argv)))
    {
        return 1;
    }

    Server.Run();
    return 0;
}
