/**
 * @file ITaskRunner.h
 * @brief 兼容别名:ITaskRunner = mession::async::IExecutor.
 *
 * 历史上 mession-core 定义 ITaskRunner,被 EventLoop 体系(MTaskEventLoop 等)实现。
 * 抽 mession-async 库后,通用执行器接口搬到 Async/IExecutor.h,这里只留 typedef
 * 别名以保持既有代码兼容。
 *
 * 新代码请直接用 mession::async::IExecutor。
 */
#pragma once

#include "Common/Runtime/Async/IExecutor.h"

// ITaskRunner = IExecutor:旧名字 + 新位置,接口形态不变(都是 Post + IsCurrentThread)。
// 注意:方法名是 Post(不是 PostTask),既有 PostTask 调用点已重命名。
using ITaskRunner = ::mession::async::IExecutor;