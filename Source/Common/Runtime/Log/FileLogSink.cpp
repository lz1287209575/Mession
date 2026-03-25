#include "Common/Runtime/Log/FileLogSink.h"

MFileSink::~MFileSink()
{
    Close();
}

bool MFileSink::Open(const MString& FilePath)
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

void MFileSink::Write(ELogLevel Level, const MString& FormattedLine)
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