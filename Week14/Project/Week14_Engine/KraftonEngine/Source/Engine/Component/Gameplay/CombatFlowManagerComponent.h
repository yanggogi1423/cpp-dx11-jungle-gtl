#pragma once

#include "Component/ActorComponent.h"
#include "Component/Gameplay/CombatCoverNodeComponent.h"
#include "Object/Ptr/WeakObjectPtr.h"

#include "Source/Engine/Component/Gameplay/CombatFlowManagerComponent.generated.h"

class UCombatCoverAgentComponent;
class UCombatCoverNodeComponent;
class UWorld;

struct FCombatSlotRuntimeState
{
    TWeakObjectPtr<UCombatCoverAgentComponent> ReservedBy;
    TWeakObjectPtr<UCombatCoverAgentComponent> OccupiedBy;
};

struct FCombatNodeRuntimeState
{
    TMap<int32, FCombatSlotRuntimeState> Slots;
};

struct FCombatAttackRuntimeState
{
    TWeakObjectPtr<UCombatCoverAgentComponent> Target;
    float TimeUntilNextAttack = 0.0f;
};

struct FCombatSuppressionRuntimeState
{
    int32 IncomingHitCount = 0;
    float TimeRemaining = 0.0f;
};

struct FCombatLinkLaneRuntimeState
{
    TMap<int32, TWeakObjectPtr<UCombatCoverAgentComponent>> ReservedByLane;
};

struct FCombatCoverGraphValidationResult
{
    int32 NodeCount = 0;
    int32 SlotCount = 0;
    int32 LinkCount = 0;
    int32 ErrorCount = 0;
    int32 WarningCount = 0;
    TArray<FString> Messages;
};

enum class ECombatSlotQueryPurpose : uint8
{
    Advance,
    PreferFullCover,
    FullCoverOnly,
    CombatCoverOnly,
    StandingCombatCoverOnly,
    AttackSlotOnly
};

UCLASS()
class UCombatFlowManagerComponent : public UActorComponent
{
public:
    GENERATED_BODY()

    UCombatFlowManagerComponent();
    ~UCombatFlowManagerComponent() override = default;

    void BeginPlay() override;
    void EndPlay() override;

    UFUNCTION(Callable, Category="CombatFlow")
    void RefreshRegistry();

    UFUNCTION(Callable, Category="CombatFlow")
    void ResetRuntimeState();

    UFUNCTION(Callable, Category="CombatFlow")
    bool AssignInitialSlot(UCombatCoverAgentComponent* Agent);

    UFUNCTION(Callable, Category="CombatFlow")
    bool TryAdvance(UCombatCoverAgentComponent* Agent);

    UFUNCTION(Callable, Category="CombatFlow")
    bool TryRepositionNearby(UCombatCoverAgentComponent* Agent);

    UFUNCTION(Callable, Category="CombatFlow")
    bool TryMoveToFullCoverInCurrentNode(UCombatCoverAgentComponent* Agent);

    UFUNCTION(Callable, Category="CombatFlow")
    bool TryMoveToCombatSlotInCurrentNode(UCombatCoverAgentComponent* Agent);

    UFUNCTION(Callable, Category="CombatFlow")
    bool TryMoveToStandingCombatSlotInCurrentNode(UCombatCoverAgentComponent* Agent);

    void ConfirmArrived(UCombatCoverAgentComponent* Agent, const FCombatCoverSlotHandle& SlotHandle);

    UFUNCTION(Callable, Category="CombatFlow")
    void ReleaseAgent(UCombatCoverAgentComponent* Agent);

    UFUNCTION(Callable, Category="CombatFlow")
    int32 AutoGenerateMissingNodeIds();

    UFUNCTION(Callable, Category="CombatFlow")
    int32 AutoLinkNearby(float MaxDistance, int32 MaxLinksPerNode, bool bDirectedByX = true);

    FCombatCoverGraphValidationResult ValidateGraph(bool bLogToConsole = true);

    UCombatCoverNodeComponent* FindNode(const FString& NodeId) const;
    const TArray<UCombatCoverNodeComponent*>& GetNodes() const { return CachedNodes; }
    const TArray<UCombatCoverAgentComponent*>& GetAgents() const { return CachedAgents; }

    bool IsSlotFree(const FCombatCoverSlotHandle& SlotHandle, const UCombatCoverAgentComponent* RequestingAgent = nullptr) const;
    bool IsNodeOccupiedOrReserved(const UCombatCoverNodeComponent* Node, const UCombatCoverAgentComponent* RequestingAgent = nullptr) const;
    const FCombatCoverSlot* FindCurrentSlot(const UCombatCoverAgentComponent* Agent) const;
    bool CanAgentAttackFromCurrentSlot(const UCombatCoverAgentComponent* Agent) const;
    bool CanAgentBeTargetedInCurrentSlot(const UCombatCoverAgentComponent* Agent) const;
    float GetTargetPriorityMultiplierForAgent(const UCombatCoverAgentComponent* Agent) const;
    bool IsAgentInSlotType(const UCombatCoverAgentComponent* Agent, ECombatCoverSlotType SlotType) const;
    bool HasFreeCombatSlotInCurrentNode(const UCombatCoverAgentComponent* Agent) const;

    void DrawAllDebugVisuals(bool bIncludeUnselected = true) const;
    void DrawCombatDebugVisuals(float Duration = 0.0f) const;

    UFUNCTION(Pure, Category="CombatFlow|Debug")
    bool GetDrawAllNodeDebugVisuals() const { return bDrawAllNodeDebugVisuals; }

    UFUNCTION(Callable, Category="CombatFlow|Debug")
    void SetDrawAllNodeDebugVisuals(bool bEnabled) { bDrawAllNodeDebugVisuals = bEnabled; }

    UFUNCTION(Callable, Category="CombatFlow|Combat")
    void UpdateCombatSimulation(float DeltaTime);

    UFUNCTION(Pure, Category="CombatFlow")
    bool GetRequireSlotTagMatch() const { return true; }

    UFUNCTION(Callable, Category="CombatFlow")
    void SetRequireSlotTagMatch(bool /*bRequired*/) { bRequireSlotTagMatch = true; }

    UFUNCTION(Pure, Category="CombatFlow|Debug")
    bool GetDrawFireDebugLines() const { return bDrawFireDebugLines; }

    UFUNCTION(Callable, Category="CombatFlow|Debug")
    void SetDrawFireDebugLines(bool bEnabled) { bDrawFireDebugLines = bEnabled; }

    UFUNCTION(Pure, Category="CombatFlow|Debug")
    bool GetDrawFireRanges() const { return bDrawFireRanges; }

    UFUNCTION(Callable, Category="CombatFlow|Debug")
    void SetDrawFireRanges(bool bEnabled) { bDrawFireRanges = bEnabled; }

    UFUNCTION(Pure, Category="CombatFlow|Combat")
    bool GetEnableSuppression() const { return true; }

    UFUNCTION(Callable, Category="CombatFlow|Combat")
    void SetEnableSuppression(bool /*bEnabled*/) { bEnableSuppression = true; }

    UFUNCTION(Pure, Category="CombatFlow|Combat")
    int32 GetSuppressionIncomingFireThreshold() const { return SuppressionIncomingFireThreshold; }

    UFUNCTION(Pure, Category="CombatFlow|Combat")
    float GetSuppressionDuration() const { return SuppressionDuration; }

    UFUNCTION(Pure, Category="CombatFlow|Combat")
    float GetSuppressionAccumulationWindow() const { return SuppressionAccumulationWindow; }

    static UCombatFlowManagerComponent* FindInWorld(UWorld* World);

private:
    FCombatCoverSlotHandle FindNearestFreeSlot(const FVector& WorldLocation, const FString& TeamTag, const UCombatCoverAgentComponent* RequestingAgent) const;
    FCombatCoverSlotHandle FindFreeSlotInNode(UCombatCoverNodeComponent* Node, const FString& TeamTag, const UCombatCoverAgentComponent* RequestingAgent, ECombatSlotQueryPurpose Purpose = ECombatSlotQueryPurpose::Advance, const FCombatCoverSlotHandle* SkipSlotHandle = nullptr) const;
    bool TryMoveToSlotTypeInCurrentNode(UCombatCoverAgentComponent* Agent, ECombatCoverSlotType DesiredSlotType);
    bool ReserveSlot(UCombatCoverAgentComponent* Agent, const FCombatCoverSlotHandle& SlotHandle, int32 NodeOccupancyOverflow = 0);
    bool IsNodeAtOrOverCapacity(const UCombatCoverNodeComponent* Node, const UCombatCoverAgentComponent* RequestingAgent, int32 NodeOccupancyOverflow = 0) const;
    int32 ReserveLinkLane(UCombatCoverAgentComponent* Agent, UCombatCoverNodeComponent* FromNode, UCombatCoverNodeComponent* ToNode);
    void ReleaseLinkLane(UCombatCoverAgentComponent* Agent);
    void ReleaseAgentExcept(UCombatCoverAgentComponent* Agent, const FCombatCoverSlotHandle& KeepSlotHandle);
    int32 CountNodeClaims(const UCombatCoverNodeComponent* Node, const UCombatCoverAgentComponent* IgnoreAgent) const;
    bool SlotTagsMatchTeam(const FCombatCoverSlot& Slot, const FString& TeamTag) const;
    void GatherAdvanceCandidateNodes(UCombatCoverAgentComponent* Agent, UCombatCoverNodeComponent* CurrentNode, TArray<UCombatCoverNodeComponent*>& OutNodes) const;
    FCombatCoverSlotHandle FindExitSlotForFullCoverTraversal(UCombatCoverNodeComponent* CurrentNode, const FCombatCoverSlotHandle& StartSlot, const FString& TeamTag, const UCombatCoverAgentComponent* RequestingAgent) const;
    bool BuildMovePathToSlot(const FCombatCoverSlotHandle& SlotHandle, FCombatMovePath& OutPath) const;
    bool BuildMovePathWithinNode(UCombatCoverNodeComponent* Node, const FCombatCoverSlotHandle& StartSlot, const FCombatCoverSlotHandle& FinalSlot, FCombatMovePath& OutPath) const;
    bool BuildMovePathBetweenNodes(UCombatCoverNodeComponent* FromNode, UCombatCoverNodeComponent* ToNode, const FCombatCoverSlotHandle& StartSlot, const FCombatCoverSlotHandle& FinalSlot, UCombatCoverAgentComponent* Agent, int32 ReservedLinkLaneIndex, FCombatMovePath& OutPath) const;
    bool AppendSlotApproachPoint(const FCombatCoverSlotHandle& SlotHandle, bool bForExit, TArray<FVector>& OutPoints) const;
    bool GetSlotPathAnchor(const FCombatCoverSlotHandle& SlotHandle, bool bForExit, FVector& OutAnchor) const;
    bool GetSlotPathTangent(const FCombatCoverSlotHandle& SlotHandle, bool bForExit, FVector& OutTangent) const;
    void AppendSlotExitTangentPoint(const FCombatCoverSlotHandle& SlotHandle, const FVector& StartAnchor, TArray<FVector>& OutPoints) const;
    void AppendSlotEntryTangentPoint(const FCombatCoverSlotHandle& SlotHandle, const FVector& EndAnchor, TArray<FVector>& OutPoints) const;
    void AppendSlotAwareLinkPoints(UCombatCoverNodeComponent* FromNode, UCombatCoverNodeComponent* ToNode, const FCombatCoverLink* Link, bool bReverse, const FVector& StartAnchor, const FVector& EndAnchor, int32 ReservedLinkLaneIndex, TArray<FVector>& OutPoints) const;
    const FCombatCoverLink* FindTraversalLink(UCombatCoverNodeComponent* FromNode, UCombatCoverNodeComponent* ToNode, bool& bOutReverse) const;
    UCombatCoverAgentComponent* FindBestTargetFor(UCombatCoverAgentComponent* Agent) const;
    bool CanEngage(const UCombatCoverAgentComponent* Shooter, const UCombatCoverAgentComponent* Target) const;
    void DrawFireDebugLine(UCombatCoverAgentComponent* Shooter, UCombatCoverAgentComponent* Target, float Duration) const;
    void DrawFireRanges(float Duration) const;
    float PickAttackInterval(const UCombatCoverAgentComponent* Agent) const;
    float PickCoverHoldDuration(const UCombatCoverAgentComponent* Agent) const;
    void RemoveStaleAttackState();
    void EnsureRuntimeSlotsForNode(UCombatCoverNodeComponent* Node);
    void RemoveStaleRuntimeState();
    void RemoveInvalidOrDeadRuntimeClaims();
    void RemoveInvalidOrDeadLinkLaneClaims();
    void AddValidationMessage(FCombatCoverGraphValidationResult& Result, bool bError, const FString& Message) const;

private:
    UPROPERTY(Edit, Save, Category="CombatFlow", DisplayName="Require Slot Tag Match")
    bool bRequireSlotTagMatch = true;

    UPROPERTY(Edit, Save, Category="CombatFlow", DisplayName="Use Node Occupancy Limit")
    bool bUseNodeOccupancyLimit = true;

    UPROPERTY(Edit, Save, Category="CombatFlow|Initial Placement", DisplayName="Limit Initial Slot Search To Spawn Cluster")
    bool bLimitInitialSlotSearchToSpawnCluster = true;

    UPROPERTY(Edit, Save, Category="CombatFlow|Initial Placement", DisplayName="Initial Slot Search Distance Slack", Min=0.0f, Max=10000.0f, Speed=0.1f)
    float InitialSlotSearchDistanceSlack = 25.0f;

    UPROPERTY(Edit, Save, Category="CombatFlow|Initial Placement", DisplayName="Initial Slot Node Occupancy Overflow", Min=0, Max=32, Speed=1)
    int32 InitialSlotNodeOccupancyOverflow = 0;

    UPROPERTY(Edit, Save, Category="CombatFlow|Movement", DisplayName="Advance Node Occupancy Overflow", Min=0, Max=32, Speed=1)
    int32 AdvanceNodeOccupancyOverflow = 2;

    UPROPERTY(Edit, Save, Category="CombatFlow|Movement", DisplayName="Slot Aware Link Offset Blend", Min=0.0f, Max=1.0f, Speed=0.05f)
    float SlotAwareLinkOffsetBlend = 1.0f;

    UPROPERTY(Edit, Save, Category="CombatFlow|Movement", DisplayName="Max Slot Aware Link Offset", Min=0.0f, Max=10000.0f, Speed=1.0f)
    float MaxSlotAwareLinkOffset = 350.0f;

    UPROPERTY(Edit, Save, Category="CombatFlow|Movement", DisplayName="Use Approach Offset As Implicit Tangent")
    bool bUseApproachOffsetAsImplicitTangent = true;

    UPROPERTY(Edit, Save, Category="CombatFlow|Movement", DisplayName="Slot Tangent Guide Distance", Min=0.0f, Max=10000.0f, Speed=1.0f)
    float SlotTangentGuideDistance = 1.0f;

    UPROPERTY(Edit, Save, Category="CombatFlow|Movement", DisplayName="Enable Link Lane Reservation")
    bool bEnableLinkLaneReservation = true;

    UPROPERTY(Edit, Save, Category="CombatFlow|Movement", DisplayName="Link Reserved Lane Count", Min=1, Max=9, Speed=1)
    int32 LinkReservedLaneCount = 3;

    UPROPERTY(Edit, Save, Category="CombatFlow|Movement", DisplayName="Link Reserved Lane Spacing", Min=0.0f, Max=10000.0f, Speed=1.0f)
    float LinkReservedLaneSpacing = 0.3f;

    UPROPERTY(Edit, Save, Category="CombatFlow|Movement", DisplayName="Link Reserved Lane Offset Blend", Min=0.0f, Max=1.0f, Speed=0.05f)
    float LinkReservedLaneOffsetBlend = 1.0f;

    UPROPERTY(Edit, Save, Category="CombatFlow|Combat", DisplayName="Enable Suppression")
    bool bEnableSuppression = true;

    UPROPERTY(Edit, Save, Category="CombatFlow|Combat", DisplayName="Suppression Incoming Fire Threshold", Min=1, Max=16, Speed=1)
    int32 SuppressionIncomingFireThreshold = 2;

    UPROPERTY(Edit, Save, Category="CombatFlow|Combat", DisplayName="Suppression Duration", Min=0.0f, Max=30.0f, Speed=0.1f)
    float SuppressionDuration = 1.5f;

    UPROPERTY(Edit, Save, Category="CombatFlow|Combat", DisplayName="Suppression Accumulation Window", Min=0.0f, Max=10.0f, Speed=0.1f)
    float SuppressionAccumulationWindow = 1.0f;

    UPROPERTY(Edit, Save, Category="CombatFlow|Debug", DisplayName="Draw Fire Debug Lines")
    bool bDrawFireDebugLines = true;

    UPROPERTY(Edit, Save, Category="CombatFlow|Debug", DisplayName="Draw Fire Ranges")
    bool bDrawFireRanges = false;

    UPROPERTY(Edit, Save, Category="CombatFlow|Debug", DisplayName="Draw All Node Debug Visuals")
    bool bDrawAllNodeDebugVisuals = false;

    UPROPERTY(Edit, Save, Category="CombatFlow|Debug", DisplayName="Draw Debug During Tick")
    bool bDrawDebugDuringTick = false;

    UPROPERTY(Edit, Save, Category="CombatFlow|Debug", DisplayName="Debug Draw Interval", Min=0.01f, Max=10.0f, Speed=0.01f)
    float DebugDrawInterval = 0.1f;

    TArray<UCombatCoverNodeComponent*> CachedNodes;
    TArray<UCombatCoverAgentComponent*> CachedAgents;
    TMap<FString, UCombatCoverNodeComponent*> NodeById;
    TMap<FString, FCombatNodeRuntimeState> RuntimeStateByNodeId;
    TMap<FString, FCombatLinkLaneRuntimeState> RuntimeStateByLinkKey;
    TMap<UCombatCoverAgentComponent*, FCombatAttackRuntimeState> AttackStateByAgent;
    TMap<UCombatCoverAgentComponent*, FCombatSuppressionRuntimeState> SuppressionStateByAgent;
    float DebugDrawTimer = 0.0f;

protected:
    void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction) override;
};
