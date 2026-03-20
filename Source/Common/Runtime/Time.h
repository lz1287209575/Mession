#pragma once

#include "Common/Runtime/MLib.h"

#include <chrono>
#include <thread>


// 时间抽象
class MTime
{
public:
    static double GetTimeSeconds()
    {
        auto Now = std::chrono::steady_clock::now();
        return std::chrono::duration<double>(Now.time_since_epoch()).count();
    }

    static void SleepSeconds(double Seconds)
    {
        if (Seconds > 0.0)
        {
            std::this_thread::sleep_for(std::chrono::duration<double>(Seconds));
        }
    }

    static void SleepMilliseconds(uint32 Ms)
    {
        if (Ms > 0)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(Ms));
        }
    }
};