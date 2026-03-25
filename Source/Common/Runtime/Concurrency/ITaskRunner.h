#pragma once

#include "Common/Runtime/MLib.h"

/** 仅表示“可投递任务、下一 tick 执行”的抽象；MAsync::Yield/Post 依赖此接口。 */
class ITaskRunner
{
public:
    using TTask = TFunction<void()>;
    virtual ~ITaskRunner() = default;
    virtual void PostTask(TTask Task) = 0;
};
