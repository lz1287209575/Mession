#pragma once

#include "NetDriver/Reflection.h"

class MPlayerAvatar;

MCLASS()
class MAvatarMember : public MReflectObject
{
public:
    MGENERATED_BODY(MAvatarMember, MReflectObject, 0)

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
