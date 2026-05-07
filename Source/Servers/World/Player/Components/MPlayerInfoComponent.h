#pragma once

#include "Common/Runtime/Component/IComponent.h"
#include "Common/Runtime/Service/IService.h"
#include "Common/Runtime/Async/MAsync.h"
#include "Protocol/Messages/Player/PlayerInfoMessages.h"

MCLASS(InjectionClass=MPlayer)
class MPlayerInfoComponent : public IComponent
{
public:
    void OnAttach(MObject* Owner) override;

    MFUNCTION(Async, Injection)
    MFUTURE(SGetPlayerInfoResponse) GetPlayerInfo(const SGetPlayerInfoRequest& Request);

    void SetDbService(IService* InDb) { DbService = InDb; }

private:
    IService* DbService = nullptr;
};
