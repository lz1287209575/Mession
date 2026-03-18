#pragma once

#include "Core/Net/NetCore.h"
#include "Gameplay/AvatarMember.h"
#include "NetDriver/NetObject.h"

#include <memory>
#include <type_traits>
#include <utility>

MCLASS()
class MPlayerAvatar : public MActor, public MReflectObject
{
public:
    MGENERATED_BODY(MPlayerAvatar, MReflectObject, 0)

public:
    MPlayerAvatar();
    ~MPlayerAvatar() override;

    void SetLocation(const SVector& InLocation);
    void SetRotation(const SRotator& InRotation);
    void SetScale(const SVector& InScale);

    void SetOwnerPlayerId(uint64 InOwnerPlayerId) { OwnerPlayerId = InOwnerPlayerId; }
    uint64 GetOwnerPlayerId() const { return OwnerPlayerId; }

    void SetDisplayName(const FString& InDisplayName) { DisplayName = InDisplayName; }
    const FString& GetDisplayName() const { return DisplayName; }

    void SetAlive(bool bInAlive) { bAlive = bInAlive; }
    bool IsAlive() const { return bAlive; }

    template<typename TMember, typename... TArgs>
    TMember* AddMember(TArgs&&... Args)
    {
        static_assert(std::is_base_of_v<MAvatarMember, TMember>, "TMember must derive from MAvatarMember");

        TUniquePtr<TMember> Member = std::make_unique<TMember>(std::forward<TArgs>(Args)...);
        TMember* RawMember = Member.get();
        RawMember->SetOwnerAvatar(this);
        RawMember->Initialize();
        Members.push_back(std::move(Member));
        return RawMember;
    }

    template<typename TMember>
    TMember* FindMember() const
    {
        static_assert(std::is_base_of_v<MAvatarMember, TMember>, "TMember must derive from MAvatarMember");

        for (const TUniquePtr<MAvatarMember>& Member : Members)
        {
            if (auto* TypedMember = dynamic_cast<TMember*>(Member.get()))
            {
                return TypedMember;
            }
        }
        return nullptr;
    }

    template<typename TMember>
    TMember* GetRequiredMember() const
    {
        return FindMember<TMember>();
    }

private:
    void InitializeDefaultMembers();

private:
    MPROPERTY(Edit)
    uint64 OwnerPlayerId = 0;

    MPROPERTY(Edit)
    SVector ReplicatedLocation;

    MPROPERTY(Edit)
    SRotator ReplicatedRotation;

    MPROPERTY(Edit)
    SVector ReplicatedScale = SVector(1.0f, 1.0f, 1.0f);

    MPROPERTY(Edit)
    FString DisplayName;

    MPROPERTY(Edit)
    bool bAlive = true;

    TVector<TUniquePtr<MAvatarMember>> Members;
};
