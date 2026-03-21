#pragma once

#include "Common/Runtime/Object/Object.h"

class MPlayerAvatar;

MCLASS()
class MAvatarMember : public MObject
{
public:
    MGENERATED_BODY(MAvatarMember, MObject, 0)

public:
    virtual ~MAvatarMember() = default;

    void SetOwnerAvatar(MPlayerAvatar* InOwnerAvatar) { OwnerAvatar = InOwnerAvatar; }
    MPlayerAvatar* GetOwnerAvatar() const { return OwnerAvatar; }
    virtual void SetOwnerPlayerId(uint64 InOwnerPlayerId);
    uint64 GetOwnerPlayerId() const { return OwnerPlayerId; }

    virtual void Initialize() {}
    virtual void Shutdown() {}

private:
    MPROPERTY(SaveGame)
    uint64 OwnerPlayerId = 0;

    MPlayerAvatar* OwnerAvatar = nullptr;
};
