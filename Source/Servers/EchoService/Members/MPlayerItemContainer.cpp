#include "Servers/EchoService/Members/MPlayerItemContainer.h"

#include "Common/Runtime/Log/Log.h"
#include "Servers/App/ServerCallAsyncSupport.h"

#include <algorithm>

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

SFutureResult<FPlayerUseItemResponse> MPlayerItemContainer::UseItem(const FPlayerUseItemRequest& InRequest) {
    FPlayerUseItemResponse Response;
    Response.ItemId = InRequest.ItemId;

    auto It = Items.find(InRequest.ItemId);
    if (It == Items.end() || It->second <= 0) {
        Response.bOk       = false;
        Response.Remaining = (It != Items.end()) ? It->second : 0;
        LOG_INFO("MPlayerItemContainer::UseItem player=%llu item=%llu -> fail (not found/empty)",
                 static_cast<unsigned long long>(InRequest.PlayerId),
                 static_cast<unsigned long long>(InRequest.ItemId));
        return MServerCallAsyncSupport::MakeSuccessFuture(std::move(Response));
    }

    const int32 Used = std::min(InRequest.Count, It->second);
    It->second -= Used;

    Response.bOk       = true;
    Response.Remaining = It->second;
    LOG_INFO("MPlayerItemContainer::UseItem player=%llu item=%llu count=%d -> ok remaining=%d",
             static_cast<unsigned long long>(InRequest.PlayerId),
             static_cast<unsigned long long>(InRequest.ItemId),
             static_cast<int>(InRequest.Count),
             static_cast<int>(Response.Remaining));
    return MServerCallAsyncSupport::MakeSuccessFuture(std::move(Response));
}
