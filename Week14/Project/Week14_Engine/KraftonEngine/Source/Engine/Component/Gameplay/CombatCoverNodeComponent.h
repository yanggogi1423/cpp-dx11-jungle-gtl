#pragma once

#include "Component/ActorComponent.h"
#include "Math/Vector.h"

#include "Source/Engine/Component/Gameplay/CombatCoverNodeComponent.generated.h"

class FScene;
class UCombatCoverNodeComponent;
class UWorld;

UENUM()
enum class ECombatCoverSlotType : uint8
{
    CombatCover,
    FullCover,
    ExposedDummy,
    StandingCombatCover
};

USTRUCT()
struct FCombatCoverSlot
{
    GENERATED_BODY()

    UPROPERTY(Edit, Save, Category="CombatCover", DisplayName="Slot Id")
    int32 SlotId = 0;

    UPROPERTY(Edit, Save, Category="CombatCover", DisplayName="Slot Type", Enum=ECombatCoverSlotType)
    ECombatCoverSlotType SlotType = ECombatCoverSlotType::CombatCover;

    UPROPERTY(Edit, Save, Category="CombatCover", DisplayName="Local Position", Type=Vec3, Speed=1.0f)
    FVector LocalPosition = FVector::ZeroVector;

    UPROPERTY(Edit, Save, Category="CombatCover", DisplayName="Local Forward", Type=Vec3, Speed=0.05f)
    FVector LocalForward = FVector::ForwardVector;

    UPROPERTY(Edit, Save, Category="CombatCover", DisplayName="Use Approach On Exit")
    bool bUseApproachOnExit = false;

    UPROPERTY(Edit, Save, Category="CombatCover", DisplayName="Use Approach On Entry")
    bool bUseApproachOnEntry = false;

    UPROPERTY(Edit, Save, Category="CombatCover", DisplayName="Local Approach Offset", Type=Vec3, Speed=1.0f)
    FVector LocalApproachOffset = FVector::ZeroVector;

    UPROPERTY(Edit, Save, Category="CombatCover|Movement", DisplayName="Use Exit Tangent")
    bool bUseExitTangent = false;

    UPROPERTY(Edit, Save, Category="CombatCover|Movement", DisplayName="Local Exit Tangent", Type=Vec3, Speed=0.05f)
    FVector LocalExitTangent = FVector::ZeroVector;

    UPROPERTY(Edit, Save, Category="CombatCover|Movement", DisplayName="Use Entry Tangent")
    bool bUseEntryTangent = false;

    UPROPERTY(Edit, Save, Category="CombatCover|Movement", DisplayName="Local Entry Tangent", Type=Vec3, Speed=0.05f)
    FVector LocalEntryTangent = FVector::ZeroVector;

    UPROPERTY(Edit, Save, Category="CombatCover", DisplayName="Radius", Min=1.0f, Max=10000.0f, Speed=1.0f)
    float Radius = 1.0f;

    UPROPERTY(Edit, Save, Category="CombatCover", DisplayName="Tags")
    FString Tags = "Enemy,Cover";

    UPROPERTY(Edit, Save, Category="CombatCover", DisplayName="Weight", Min=0.0f, Max=1000.0f, Speed=0.1f)
    float Weight = 1.0f;

    bool ProvidesCover() const;
    bool CanAttackFrom() const;
    bool CanBeTargetedWhileInCover() const;
    bool RequiresStandingFire() const;
    float GetTargetPriorityMultiplierWhileInCover() const;
    float GetSlotSelectionScore() const;
};

USTRUCT()
struct FCombatCoverLink
{
    GENERATED_BODY()

    UPROPERTY(Edit, Save, Category="CombatCover", DisplayName="Target Node Id")
    FString TargetNodeId;

    UPROPERTY(Edit, Save, Category="CombatCover", DisplayName="Bidirectional")
    bool bBidirectional = false;

    UPROPERTY(Edit, Save, Category="CombatCover", DisplayName="Weight", Min=0.0f, Max=1000.0f, Speed=0.1f)
    float Weight = 1.0f;

    UPROPERTY(Edit, Save, Category="CombatCover", DisplayName="Path Points", Type=Array)
    TArray<FVector> PathPoints;
};

USTRUCT()
struct FCombatCoverSlotHandle
{
    GENERATED_BODY()

    FString NodeId;
    int32 SlotId = -1;

    bool IsValid() const { return !NodeId.empty() && SlotId >= 0; }
    void Reset()
    {
        NodeId.clear();
        SlotId = -1;
    }
};

struct FCombatMovePath
{
    TArray<FVector> Points;
    FCombatCoverSlotHandle FinalSlot;

    bool IsValid() const { return FinalSlot.IsValid() && !Points.empty(); }
    void Reset()
    {
        Points.clear();
        FinalSlot.Reset();
    }
};

UCLASS()
class UCombatCoverNodeComponent : public UActorComponent
{
public:
    GENERATED_BODY()

    UCombatCoverNodeComponent();
    ~UCombatCoverNodeComponent() override = default;

    UFUNCTION(Pure, Category="CombatCover")
    const FString& GetNodeId() const { return NodeId; }

    UFUNCTION(Pure, Category="CombatCover")
    const FString& GetDisplayName() const { return DisplayName; }

    UFUNCTION(Pure, Category="CombatCover")
    int32 GetSlotCount() const { return static_cast<int32>(Slots.size()); }

    UFUNCTION(Pure, Category="CombatCover")
    int32 GetLinkCount() const { return static_cast<int32>(Links.size()); }

    UFUNCTION(Pure, Category="CombatCover")
    int32 GetMaxOccupants() const { return MaxOccupants; }

    UFUNCTION(Callable, Category="CombatCover")
    void SetNodeId(const FString& InNodeId) { NodeId = InNodeId; }

    UFUNCTION(Callable, Category="CombatCover")
    void SetDisplayName(const FString& InDisplayName) { DisplayName = InDisplayName; }

    UFUNCTION(Pure, Category="CombatCover")
    FVector GetSlotWorldPosition(int32 SlotIndex) const;

    UFUNCTION(Pure, Category="CombatCover")
    FVector GetSlotWorldForward(int32 SlotIndex) const;

    UFUNCTION(Pure, Category="CombatCover")
    FVector GetSlotWorldApproachPosition(int32 SlotIndex) const;

    UFUNCTION(Pure, Category="CombatCover")
    FVector GetSlotWorldExitTangent(int32 SlotIndex) const;

    UFUNCTION(Pure, Category="CombatCover")
    FVector GetSlotWorldEntryTangent(int32 SlotIndex) const;

    UFUNCTION(Callable, Category="CombatCover")
    int32 AddSlotAtLocalPosition(const FVector& LocalPosition);

    UFUNCTION(Callable, Category="CombatCover")
    int32 AddSlotInFront(float Distance = 150.0f);

    UFUNCTION(Callable, Category="CombatCover")
    bool RemoveSlotByIndex(int32 SlotIndex);

    UFUNCTION(Callable, Category="CombatCover")
    bool AddLinkToNodeId(const FString& TargetNodeId, bool bBidirectional = false);

    UFUNCTION(Callable, Category="CombatCover")
    bool RemoveLinkToNodeId(const FString& TargetNodeId);

    UFUNCTION(Callable, Category="CombatCover")
    void EnsureNodeId(int32 PreferredIndex);

    const TArray<FCombatCoverSlot>& GetSlots() const { return Slots; }
    TArray<FCombatCoverSlot>& GetMutableSlots() { return Slots; }
    const TArray<FCombatCoverLink>& GetLinks() const { return Links; }
    TArray<FCombatCoverLink>& GetMutableLinks() { return Links; }

    const FCombatCoverSlot* FindSlotById(int32 SlotId) const;
    int32 FindSlotIndexById(int32 SlotId) const;

    void ContributeSelectedVisuals(FScene& Scene) const override;
    void DrawDebugVisuals(FScene& Scene, bool bSelected) const;

    static UCombatCoverNodeComponent* FindNodeById(UWorld* World, const FString& InNodeId);

private:
    int32 MakeNextSlotId() const;

private:
    UPROPERTY(Edit, Save, Category="CombatCover", DisplayName="Node Id")
    FString NodeId;

    UPROPERTY(Edit, Save, Category="CombatCover", DisplayName="Display Name")
    FString DisplayName;

    UPROPERTY(Edit, Save, Category="CombatCover", DisplayName="Slots", Type=Array, Struct=FCombatCoverSlot)
    TArray<FCombatCoverSlot> Slots;

    UPROPERTY(Edit, Save, Category="CombatCover", DisplayName="Links", Type=Array, Struct=FCombatCoverLink)
    TArray<FCombatCoverLink> Links;

    UPROPERTY(Edit, Save, Category="CombatCover", DisplayName="Max Occupants", Min=1, Max=32, Speed=1.0f)
    int32 MaxOccupants = 1;

    UPROPERTY(Edit, Save, Category="CombatCover|Debug", DisplayName="Debug Slot Radius", Min=1.0f, Max=10000.0f, Speed=1.0f)
    float DebugSlotRadius = 1.0f;
};
