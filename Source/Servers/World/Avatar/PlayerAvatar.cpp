#include "Servers/World/Avatar/PlayerAvatar.h"

#include "Servers/World/Avatar/AttributeMember.h"
#include "MPlayerAvatar.mgenerated.h"
#include "Servers/World/Avatar/InventoryMember.h"
#include "Servers/World/Avatar/InteractionMember.h"
#include "Servers/World/Avatar/MovementMember.h"
#include "Servers/World/PlayerSession.h"

MPlayerAvatar::MPlayerAvatar()
{
    SetClass(StaticClass());
    SetReplicated(true);
    SetActorReplicates(true);
    SetActorActive(true);
    ReplicatedLocation = MActor::GetLocation();
    ReplicatedRotation = MActor::GetRotation();
    ReplicatedScale = MActor::GetScale();
    PlayerSession = CreateDefaultSubObject<MPlayerSession>(this, "PlayerSession");
    InitializeDefaultMembers();
}

MPlayerAvatar::~MPlayerAvatar()
{
    for (MAvatarMember* Member : Members)
    {
        if (Member)
        {
            Member->Shutdown();
        }
    }
}

void MPlayerAvatar::SetLocation(const SVector& InLocation)
{
    MActor::SetLocation(InLocation);
    ReplicatedLocation = MActor::GetLocation();
    SetPropertyDirty(Prop_MPlayerAvatar_ReplicatedLocation());
}

void MPlayerAvatar::SetRotation(const SRotator& InRotation)
{
    MActor::SetRotation(InRotation);
    ReplicatedRotation = MActor::GetRotation();
    SetPropertyDirty(Prop_MPlayerAvatar_ReplicatedRotation());
}

void MPlayerAvatar::SetScale(const SVector& InScale)
{
    MActor::SetScale(InScale);
    ReplicatedScale = MActor::GetScale();
    SetPropertyDirty(Prop_MPlayerAvatar_ReplicatedScale());
}

void MPlayerAvatar::SetOwnerPlayerId(uint64 InOwnerPlayerId)
{
    if (OwnerPlayerId == InOwnerPlayerId)
    {
        return;
    }

    OwnerPlayerId = InOwnerPlayerId;
    SetPropertyDirty(Prop_MPlayerAvatar_OwnerPlayerId());
    for (MAvatarMember* Member : Members)
    {
        if (Member)
        {
            Member->SetOwnerPlayerId(InOwnerPlayerId);
        }
    }
}

void MPlayerAvatar::SetDisplayName(const MString& InDisplayName)
{
    if (DisplayName == InDisplayName)
    {
        return;
    }

    DisplayName = InDisplayName;
    SetPropertyDirty(Prop_MPlayerAvatar_DisplayName());
}

void MPlayerAvatar::SetAlive(bool bInAlive)
{
    if (bAlive == bInAlive)
    {
        return;
    }

    bAlive = bInAlive;
    SetPropertyDirty(Prop_MPlayerAvatar_bAlive());
}

void MPlayerAvatar::InitializeDefaultMembers()
{
    AddMember<MMovementMember>("MovementMember");
    AddMember<MAttributeMember>("AttributeMember");
    AddMember<MInventoryMember>("InventoryMember");
    AddMember<MInteractionMember>("InteractionMember");
}
