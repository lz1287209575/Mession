#pragma once

#include "Core/NetCore.h"
#include <cstring>
#include <cstdlib>

namespace MParseArgs
{
inline void Parse(int argc, char* argv[], FString& OutConfigPath, int& OutPort, int DefaultPort)
{
    OutConfigPath.clear();
    OutPort = DefaultPort;
    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "--config") == 0 && i + 1 < argc)
        {
            OutConfigPath = argv[++i];
        }
        else if (std::strcmp(argv[i], "--port") == 0 && i + 1 < argc)
        {
            OutPort = std::atoi(argv[++i]);
        }
    }
}
}
