#include "CombatCoverNodeComponent.h"

#include "Core/Logging/Log.h"
#include "Core/Types/EngineTypes.h"
#include "GameFramework/AActor.h"
#include "Component/SceneComponent.h"
#include "GameFramework/World.h"
#include "Math/MathUtils.h"
#include "Render/Scene/FScene.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace
{
    FVector GetOwnerLocationSafe(const UCombatCoverNodeComponent* Node)
    {
        const AActor* Owner = Node ? Node->GetOwner() : nullptr;
        return Owner ? Owner->GetActorLocation() : FVector::ZeroVector;
    }

    FVector TransformLocalPosition(const AActor* Owner, const FVector& LocalPosition)
    {
        if (!Owner)
        {
            return LocalPosition;
        }

        if (const USceneComponent* Root = Owner->GetRootComponent())
        {
            return Root->GetWorldMatrix().TransformPositionWithW(LocalPosition);
        }

        return Owner->GetActorLocation() + LocalPosition;
    }

    FVector TransformLocalVector(const AActor* Owner, const FVector& LocalVector)
    {
        FVector Result = LocalVector;
        if (Owner)
        {
            if (const USceneComponent* Root = Owner->GetRootComponent())
            {
                Result = Root->GetWorldMatrix().TransformVector(LocalVector);
            }
        }

        if (Result.IsNearlyZero())
        {
            return FVector::ForwardVector;
        }

        return Result.Normalized();
    }

    void AddDebugCross(FScene& Scene, const FVector& Center, float Radius, const FColor& Color)
    {
        Scene.AddDebugLine(Center - FVector(Radius, 0.0f, 0.0f), Center + FVector(Radius, 0.0f, 0.0f), Color);
        Scene.AddDebugLine(Center - FVector(0.0f, Radius, 0.0f), Center + FVector(0.0f, Radius, 0.0f), Color);
        Scene.AddDebugLine(Center - FVector(0.0f, 0.0f, Radius), Center + FVector(0.0f, 0.0f, Radius), Color);
    }

    void AddDebugArrow(FScene& Scene, const FVector& Start, const FVector& Direction, float Length, const FColor& Color)
    {
        if (Direction.IsNearlyZero() || Length <= 0.0f)
        {
            return;
        }

        const FVector Dir = Direction.Normalized();
        const FVector End = Start + Dir * Length;
        Scene.AddDebugLine(Start, End, Color);

        FVector Side = FVector::Cross(Dir, FVector(0.0f, 0.0f, 1.0f));
        if (Side.IsNearlyZero())
        {
            Side = FVector::Cross(Dir, FVector(0.0f, 1.0f, 0.0f));
        }
        Side.Normalize();

        const FVector Back = Dir * (Length * 0.22f);
        const FVector Wing = Side * (Length * 0.12f);
        Scene.AddDebugLine(End, End - Back + Wing, Color);
        Scene.AddDebugLine(End, End - Back - Wing, Color);
    }

    void AddDebugCircleXY(FScene& Scene, const FVector& Center, float Radius, const FColor& Color)
    {
        if (Radius <= 0.0f)
        {
            return;
        }

        constexpr int32 Segments = 24;
        constexpr float TwoPi = 6.28318530717958647692f;
        FVector Prev = Center + FVector(Radius, 0.0f, 0.0f);
        for (int32 Index = 1; Index <= Segments; ++Index)
        {
            const float Angle = TwoPi * static_cast<float>(Index) / static_cast<float>(Segments);
            const FVector Next = Center + FVector(cosf(Angle) * Radius, sinf(Angle) * Radius, 0.0f);
            Scene.AddDebugLine(Prev, Next, Color);
            Prev = Next;
        }
    }

    FString MakeGeneratedNodeId(int32 PreferredIndex)
    {
        char Buffer[64] = {};
        std::snprintf(Buffer, sizeof(Buffer), "CoverNode_%03d", (std::max)(1, PreferredIndex));
        return FString(Buffer);
    }

    FColor GetSlotDebugColor(const FCombatCoverSlot& Slot, bool bSelected)
    {
        switch (Slot.SlotType)
        {
        case ECombatCoverSlotType::FullCover:
            return bSelected ? FColor(80, 180, 255) : FColor(40, 100, 255);
        case ECombatCoverSlotType::ExposedDummy:
            return bSelected ? FColor(255, 220, 60) : FColor(220, 150, 40);
        case ECombatCoverSlotType::StandingCombatCover:
            return bSelected ? FColor(120, 255, 180) : FColor(80, 220, 120);
        case ECombatCoverSlotType::CombatCover:
        default:
            return bSelected ? FColor(0, 255, 120) : FColor(0, 160, 255);
        }
    }
}

UCombatCoverNodeComponent::UCombatCoverNodeComponent()
{
    SetComponentTickEnabled(false);
}

bool FCombatCoverSlot::ProvidesCover() const
{
    return SlotType == ECombatCoverSlotType::CombatCover
        || SlotType == ECombatCoverSlotType::FullCover
        || SlotType == ECombatCoverSlotType::StandingCombatCover;
}

bool FCombatCoverSlot::CanAttackFrom() const
{
    return SlotType != ECombatCoverSlotType::FullCover;
}

bool FCombatCoverSlot::CanBeTargetedWhileInCover() const
{
    return SlotType != ECombatCoverSlotType::FullCover;
}

bool FCombatCoverSlot::RequiresStandingFire() const
{
    return SlotType == ECombatCoverSlotType::StandingCombatCover;
}

float FCombatCoverSlot::GetTargetPriorityMultiplierWhileInCover() const
{
    switch (SlotType)
    {
    case ECombatCoverSlotType::FullCover:
        return 0.0f;
    case ECombatCoverSlotType::ExposedDummy:
        return 1.0f;
    case ECombatCoverSlotType::StandingCombatCover:
        return 0.55f;
    case ECombatCoverSlotType::CombatCover:
    default:
        return 0.35f;
    }
}

float FCombatCoverSlot::GetSlotSelectionScore() const
{
    switch (SlotType)
    {
    case ECombatCoverSlotType::FullCover:
        return Weight + 40.0f;
    case ECombatCoverSlotType::ExposedDummy:
        return Weight - 100.0f;
    case ECombatCoverSlotType::StandingCombatCover:
        return Weight + 85.0f;
    case ECombatCoverSlotType::CombatCover:
    default:
        return Weight + 100.0f;
    }
}

FVector UCombatCoverNodeComponent::GetSlotWorldPosition(int32 SlotIndex) const
{
    if (SlotIndex < 0 || SlotIndex >= static_cast<int32>(Slots.size()))
    {
        return GetOwnerLocationSafe(this);
    }

    return TransformLocalPosition(GetOwner(), Slots[SlotIndex].LocalPosition);
}

FVector UCombatCoverNodeComponent::GetSlotWorldForward(int32 SlotIndex) const
{
    if (SlotIndex < 0 || SlotIndex >= static_cast<int32>(Slots.size()))
    {
        return GetOwner() ? GetOwner()->GetActorForward() : FVector::ForwardVector;
    }

    return TransformLocalVector(GetOwner(), Slots[SlotIndex].LocalForward);
}

FVector UCombatCoverNodeComponent::GetSlotWorldApproachPosition(int32 SlotIndex) const
{
    if (SlotIndex < 0 || SlotIndex >= static_cast<int32>(Slots.size()))
    {
        return GetOwnerLocationSafe(this);
    }

    const FCombatCoverSlot& Slot = Slots[SlotIndex];
    return TransformLocalPosition(GetOwner(), Slot.LocalPosition + Slot.LocalApproachOffset);
}

FVector UCombatCoverNodeComponent::GetSlotWorldExitTangent(int32 SlotIndex) const
{
    if (SlotIndex < 0 || SlotIndex >= static_cast<int32>(Slots.size()))
    {
        return GetOwner() ? GetOwner()->GetActorForward() : FVector::ForwardVector;
    }

    const FCombatCoverSlot& Slot = Slots[SlotIndex];
    return TransformLocalVector(GetOwner(), Slot.LocalExitTangent);
}

FVector UCombatCoverNodeComponent::GetSlotWorldEntryTangent(int32 SlotIndex) const
{
    if (SlotIndex < 0 || SlotIndex >= static_cast<int32>(Slots.size()))
    {
        return GetOwner() ? GetOwner()->GetActorForward() : FVector::ForwardVector;
    }

    const FCombatCoverSlot& Slot = Slots[SlotIndex];
    return TransformLocalVector(GetOwner(), Slot.LocalEntryTangent);
}

int32 UCombatCoverNodeComponent::AddSlotAtLocalPosition(const FVector& LocalPosition)
{
    FCombatCoverSlot Slot;
    Slot.SlotId = MakeNextSlotId();
    Slot.LocalPosition = LocalPosition;
    Slot.LocalForward = FVector::ForwardVector;
    Slots.push_back(Slot);
    return static_cast<int32>(Slots.size()) - 1;
}

int32 UCombatCoverNodeComponent::AddSlotInFront(float Distance)
{
    return AddSlotAtLocalPosition(FVector((std::max)(0.0f, Distance), 0.0f, 0.0f));
}

bool UCombatCoverNodeComponent::RemoveSlotByIndex(int32 SlotIndex)
{
    if (SlotIndex < 0 || SlotIndex >= static_cast<int32>(Slots.size()))
    {
        return false;
    }

    Slots.erase(Slots.begin() + SlotIndex);
    return true;
}

bool UCombatCoverNodeComponent::AddLinkToNodeId(const FString& InTargetNodeId, bool bBidirectional)
{
    if (InTargetNodeId.empty() || InTargetNodeId == NodeId)
    {
        return false;
    }

    for (FCombatCoverLink& Link : Links)
    {
        if (Link.TargetNodeId == InTargetNodeId)
        {
            Link.bBidirectional = Link.bBidirectional || bBidirectional;
            return false;
        }
    }

    FCombatCoverLink NewLink;
    NewLink.TargetNodeId = InTargetNodeId;
    NewLink.bBidirectional = bBidirectional;
    Links.push_back(NewLink);
    return true;
}

bool UCombatCoverNodeComponent::RemoveLinkToNodeId(const FString& InTargetNodeId)
{
    const size_t OldSize = Links.size();
    Links.erase(std::remove_if(Links.begin(), Links.end(), [&InTargetNodeId](const FCombatCoverLink& Link)
    {
        return Link.TargetNodeId == InTargetNodeId;
    }), Links.end());
    return Links.size() != OldSize;
}

void UCombatCoverNodeComponent::EnsureNodeId(int32 PreferredIndex)
{
    if (!NodeId.empty())
    {
        return;
    }

    NodeId = MakeGeneratedNodeId(PreferredIndex);
}

const FCombatCoverSlot* UCombatCoverNodeComponent::FindSlotById(int32 SlotId) const
{
    const int32 Index = FindSlotIndexById(SlotId);
    return Index >= 0 ? &Slots[Index] : nullptr;
}

int32 UCombatCoverNodeComponent::FindSlotIndexById(int32 SlotId) const
{
    for (int32 Index = 0; Index < static_cast<int32>(Slots.size()); ++Index)
    {
        if (Slots[Index].SlotId == SlotId)
        {
            return Index;
        }
    }
    return -1;
}

void UCombatCoverNodeComponent::ContributeSelectedVisuals(FScene& Scene) const
{
    DrawDebugVisuals(Scene, true);
}

void UCombatCoverNodeComponent::DrawDebugVisuals(FScene& Scene, bool bSelected) const
{
    const AActor* Owner = GetOwner();
    if (!Owner)
    {
        return;
    }

    const FVector NodeLocation = Owner->GetActorLocation();
    const FColor ApproachColor = bSelected ? FColor(255, 130, 30) : FColor(255, 95, 20);
    const FColor ExitTangentColor = bSelected ? FColor(255, 80, 30) : FColor(220, 60, 25);
    const FColor EntryTangentColor = bSelected ? FColor(60, 220, 255) : FColor(40, 160, 220);
    const FColor LinkColor = bSelected ? FColor(255, 180, 0) : FColor(120, 180, 255);

    for (int32 Index = 0; Index < static_cast<int32>(Slots.size()); ++Index)
    {
        const FVector SlotWorld = GetSlotWorldPosition(Index);
        const FVector SlotForward = GetSlotWorldForward(Index);
        const float Radius = (std::max)(0.1f, Slots[Index].Radius);
        const FColor SlotColor = GetSlotDebugColor(Slots[Index], bSelected);
        AddDebugCross(Scene, SlotWorld, (std::min)(Radius, DebugSlotRadius), SlotColor);
        AddDebugCircleXY(Scene, SlotWorld, Radius, SlotColor);
        AddDebugArrow(Scene, SlotWorld, SlotForward, Radius * 1.4f, SlotColor);
        Scene.AddDebugLine(NodeLocation, SlotWorld, FColor(80, 80, 80));

        if (Slots[Index].bUseApproachOnExit || Slots[Index].bUseApproachOnEntry)
        {
            const FVector ApproachWorld = GetSlotWorldApproachPosition(Index);
            const float ApproachRadius = (std::max)(1.0f, Radius * 0.5f);
            AddDebugCross(Scene, ApproachWorld, ApproachRadius, ApproachColor);
            AddDebugCircleXY(Scene, ApproachWorld, ApproachRadius, ApproachColor);
            Scene.AddDebugLine(SlotWorld, ApproachWorld, ApproachColor);
        }

        if (Slots[Index].bUseExitTangent && !Slots[Index].LocalExitTangent.IsNearlyZero())
        {
            const FVector ExitAnchor = Slots[Index].bUseApproachOnExit ? GetSlotWorldApproachPosition(Index) : SlotWorld;
            AddDebugArrow(Scene, ExitAnchor, GetSlotWorldExitTangent(Index), Radius * 2.0f, ExitTangentColor);
        }

        if (Slots[Index].bUseEntryTangent && !Slots[Index].LocalEntryTangent.IsNearlyZero())
        {
            const FVector EntryAnchor = Slots[Index].bUseApproachOnEntry ? GetSlotWorldApproachPosition(Index) : SlotWorld;
            AddDebugArrow(Scene, EntryAnchor, GetSlotWorldEntryTangent(Index), Radius * 2.0f, EntryTangentColor);
        }
    }

    UWorld* World = GetWorld();
    for (const FCombatCoverLink& Link : Links)
    {
        UCombatCoverNodeComponent* Target = FindNodeById(World, Link.TargetNodeId);
        if (!Target || !Target->GetOwner())
        {
            continue;
        }

        const FVector TargetLocation = Target->GetOwner()->GetActorLocation();
        FVector PreviousPoint = NodeLocation;
        for (const FVector& PathPoint : Link.PathPoints)
        {
            Scene.AddDebugLine(PreviousPoint, PathPoint, LinkColor);
            AddDebugCross(Scene, PathPoint, 1.0f, FColor(255, 120, 40));
            PreviousPoint = PathPoint;
        }
        Scene.AddDebugLine(PreviousPoint, TargetLocation, LinkColor);

        const FVector ToTarget = TargetLocation - PreviousPoint;
        if (!ToTarget.IsNearlyZero())
        {
            const FVector TargetDirection = ToTarget.Normalized();
            const float ArrowLength = (std::min)(12.0f, ToTarget.Length());
            AddDebugArrow(Scene, TargetLocation - TargetDirection * ArrowLength, TargetDirection, ArrowLength, LinkColor);
        }
    }
}

UCombatCoverNodeComponent* UCombatCoverNodeComponent::FindNodeById(UWorld* World, const FString& InNodeId)
{
    if (!World || InNodeId.empty())
    {
        return nullptr;
    }

    for (AActor* Actor : World->GetActors())
    {
        if (!IsValid(Actor))
        {
            continue;
        }

        UCombatCoverNodeComponent* Node = Actor->GetComponentByClass<UCombatCoverNodeComponent>();
        if (Node && Node->GetNodeId() == InNodeId)
        {
            return Node;
        }
    }

    return nullptr;
}

int32 UCombatCoverNodeComponent::MakeNextSlotId() const
{
    int32 MaxSlotId = -1;
    for (const FCombatCoverSlot& Slot : Slots)
    {
        MaxSlotId = (std::max)(MaxSlotId, Slot.SlotId);
    }
    return MaxSlotId + 1;
}
