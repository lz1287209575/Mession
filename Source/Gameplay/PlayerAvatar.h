#pragma once

#include "Common/Runtime/MLib.h"
#include "Gameplay/AvatarMember.h"
#include "Common/Runtime/Object/NetObject.h"

#include <memory>
#include <type_traits>
#include <utility>

class MPlayerSession;

MCLASS()
class MPlayerAvatar : public MActor
{
public:
    MGENERATED_BODY(MPlayerAvatar, MActor, 0)

public:
    MPlayerAvatar();
    ~MPlayerAvatar() override;

    void SetLocation(const SVector& InLocation);
    void SetRotation(const SRotator& InRotation);
    void SetScale(const SVector& InScale);

    void SetOwnerPlayerId(uint64 InOwnerPlayerId);
    uint64 GetOwnerPlayerId() const { return OwnerPlayerId; }

    void SetDisplayName(const MString& InDisplayName);
    const MString& GetDisplayName() const { return DisplayName; }

    void SetAlive(bool bInAlive);
    bool IsAlive() const { return bAlive; }
    void SetPlayerSession(MPlayerSession* InPlayerSession) { PlayerSession = InPlayerSession; }
    MPlayerSession* GetPlayerSession() const { return PlayerSession; }

    template<typename TMember, typename... TArgs>
    TMember* AddMember(TArgs&&... Args)
    {
        static_assert(std::is_base_of_v<MAvatarMember, TMember>, "TMember must derive from MAvatarMember");

        TUniquePtr<TMember> Member = std::make_unique<TMember>(std::forward<TArgs>(Args)...);
        TMember* RawMember = Member.get();
        RawMember->SetClass(TMember::StaticClass());
        RawMember->SetOwnerAvatar(this);
        RawMember->SetOwnerPlayerId(OwnerPlayerId);
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

    const TVector<TUniquePtr<MAvatarMember>>& GetMembers() const
    {
        return Members;
    }

private:
    void InitializeDefaultMembers();

private:
    MPROPERTY(Edit | SaveGame)
    uint64 OwnerPlayerId = 0;

    MPROPERTY(Edit | RepToClient)
    SVector ReplicatedLocation;

    MPROPERTY(Edit | RepToClient)
    SRotator ReplicatedRotation;

    MPROPERTY(Edit | RepToClient)
    SVector ReplicatedScale = SVector(1.0f, 1.0f, 1.0f);

    MPROPERTY(Edit | RepToClient | SaveGame)
    MString DisplayName;

    MPROPERTY(Edit | RepToClient | SaveGame)
    bool bAlive = true;

    MPlayerSession* PlayerSession = nullptr;
    TVector<TUniquePtr<MAvatarMember>> Members;
};
