/**
 * @file Time.h
 * @brief MTime —— 基于 std::chrono::steady_clock 的计时与休眠工具。
 *
 * 统一使用单调时钟,不受系统时间调整影响,适合计时与超时判断。需要墙上
 * 时间(例如给日志打时间戳)时要显式转换,不要把这些值当 Unix 时间用。
 */
#pragma once

#include "Common/Runtime/MLib.h"

#include <chrono>
#include <thread>


// 时间抽象
class MTime {
    public:
    static double GetTimeSeconds() {
        auto Now = std::chrono::steady_clock::now();
        return std::chrono::duration<double>(Now.time_since_epoch()).count();
    }

    static void SleepSeconds(double Seconds) {
        if (Seconds > 0.0) {
            std::this_thread::sleep_for(std::chrono::duration<double>(Seconds));
        }
    }

    static void SleepMilliseconds(uint32 Ms) {
        if (Ms > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(Ms));
        }
    }
};