#pragma once

#include "Common/Runtime/MLib.h"
#include "Common/Runtime/Reflect/Reflection.h"

// F0 ActorMember 框架验证消息(2026-08-14-actor-member-framework.md §5):
// 请求继承 FPlayerRequestBase,显式携带 PlayerId——框架成员分发器
// (DispatchFrameworkMemberCall)反序列化后 FindProperty("PlayerId") 路由到
// actor 宿主(继承序列化先父后子,基类字段落盘,见 Class.cpp)。

// 请求基类:PlayerId 显式寻址(不做实例编码,实例经 Registry ActorIds 定位)。
MSTRUCT()
struct FPlayerRequestBase {
    MPROPERTY()
    uint64 PlayerId = 0;
};

// 背包成员(MPlayerItemContainer::UseItem)请求——继承基类字段。
MSTRUCT()
struct FPlayerUseItemRequest : FPlayerRequestBase {
    MPROPERTY()
    uint64 ItemId = 0;

    MPROPERTY()
    int32 Count = 0;
};

// 使用物品响应。
MSTRUCT()
struct FPlayerUseItemResponse {
    MPROPERTY()
    bool bOk = false;

    MPROPERTY()
    uint64 ItemId = 0;

    // 使用后剩余数量(-1 表示物品不存在)。
    MPROPERTY()
    int32 Remaining = 0;
};
