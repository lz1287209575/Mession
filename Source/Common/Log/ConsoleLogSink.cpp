#include "ConsoleLogSink.h"

#include "Common/StringUtils.h"

#include <iostream>

void MConsoleSink::Write(ELogLevel Level, const MString& FormattedLine)
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
