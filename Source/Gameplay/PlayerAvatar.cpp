#include "Gameplay/PlayerAvatar.h"

#include "Gameplay/AttributeMember.h"
#include "Gameplay/InteractionMember.h"
#include "Gameplay/MovementMember.h"

MPlayerAvatar::MPlayerAvatar()
{
    SetClass(StaticClass());
    SetReplicated(true);
    SetActorReplicates(true);
    SetActorActive(true);
    ReplicatedLocation = MActor::GetLocation();
    ReplicatedRotation = MActor::GetRotation();
    ReplicatedScale = MActor::GetScale();
    InitializeDefaultMembers();
}

MPlayerAvatar::~MPlayerAvatar()
{
    for (const TUniquePtr<MAvatarMember>& Member : Members)
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
}

void MPlayerAvatar::SetRotation(const SRotator& InRotation)
{
    MActor::SetRotation(InRotation);
    ReplicatedRotation = MActor::GetRotation();
}

void MPlayerAvatar::SetScale(const SVector& InScale)
{
    MActor::SetScale(InScale);
    ReplicatedScale = MActor::GetScale();
}

void MPlayerAvatar::InitializeDefaultMembers()
{
    AddMember<MMovementMember>();
    AddMember<MAttributeMember>();
    AddMember<MInteractionMember>();
}
