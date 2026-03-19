#pragma once

#include "Common/MLib.h"
#include "Common/Logger.h"
#include "NetDriver/Reflection.h"

// MonoServer：单机测试服务器，用于跑反射等功能的示例/自测
MCLASS()
class MMonoServer
{
public:
    MMonoServer() = default;
    
    // 初始化（可后续扩展配置解析等）
    bool Init();
    
    // 执行测试逻辑，返回是否全部通过
    bool Run();
};
