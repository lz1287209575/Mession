/**
 * @file MPlayerItemContainer.cpp
 * @brief MPlayerItemContainer 的实现:演示背包预置、数量读写与 UseItem 扣减。
 */
#include "Servers/Player/Members/MPlayerItemContainer.h"

#include "Common/Runtime/Log/Log.h"
#include "Servers/App/ServerCallAsyncSupport.h"

#include <algorithm>

/** @brief 挂载即预置演示背包,用于验证 GetActorMember / UseItem 链路。 */
void MPlayerItemContainer::OnAttach() {
    // 演示背包:挂载即预置几种物品,验证 GetActorMember / UseItem 链路。
    Items[1001] = 5; // 治疗药水
    Items[1002] = 2; // 魔法药水
    Items[2001] = 1; // 稀有材料
}

void MPlayerItemContainer::SetItemCount(uint64 InItemId, int32 InCount) {
    Items[InItemId] = InCount;
}

int32 MPlayerItemContainer::GetItemCount(uint64 InItemId) const {
    auto It = Items.find(InItemId);
    return (It != Items.end()) ? It->second : 0;
}

// 注意:本函数不做失败兜底 —— 物品缺失/数量不足也返回 Ok(只是 bOk=false),
// 因为这类业务拒绝是正常语义,不应该被上层当成 RPC 错误。
SFutureResult<FPlayerUseItemResponse> MPlayerItemContainer::UseItem(const FPlayerUseItemRequest& InRequest) {
    FPlayerUseItemResponse Response;
    Response.ItemId = InRequest.ItemId;

    auto It = Items.find(InRequest.ItemId);
    if (It == Items.end() || It->second <= 0) {
        Response.bOk       = false;
        Response.Remaining = (It != Items.end()) ? It->second : 0;
        LOG_INFO("MPlayerItemContainer::UseItem player=%llu item=%llu -> fail (not found/empty)", static_cast<unsigned long long>(InRequest.PlayerId), static_cast<unsigned long long>(InRequest.ItemId));
        return MServerCallAsyncSupport::MakeSuccessFuture(std::move(Response));
    }

    const int32 Used = std::min(InRequest.Count, It->second);
    It->second -= Used;

    Response.bOk       = true;
    Response.Remaining = It->second;
    LOG_INFO("MPlayerItemContainer::UseItem player=%llu item=%llu count=%d -> ok remaining=%d", static_cast<unsigned long long>(InRequest.PlayerId), static_cast<unsigned long long>(InRequest.ItemId), static_cast<int>(InRequest.Count),
             static_cast<int>(Response.Remaining));
    return MServerCallAsyncSupport::MakeSuccessFuture(std::move(Response));
}
