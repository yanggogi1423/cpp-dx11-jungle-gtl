#include "CombatFlowManagerComponent.h"

#include "Component/Gameplay/CombatCoverAgentComponent.h"
#include "Core/Logging/Log.h"
#include "Debug/DrawDebugHelpers.h"
#include "GameFramework/AActor.h"
#include "GameFramework/World.h"
#include "Render/Scene/FScene.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <random>
#include <sstream>

namespace
{
    FString MakeNodeDebugName(const UCombatCoverNodeComponent* Node)
    {
        if (!Node)
        {
            return "(null)";
        }

        const FString& NodeId = Node->GetNodeId();
        if (!NodeId.empty())
        {
            return NodeId;
        }

        const AActor* Owner = Node->GetOwner();
        return Owner ? Owner->GetName() : FString("(unnamed node)");
    }

    float Dist2D(const FVector& A, const FVector& B)
    {
        const float DX = A.X - B.X;
        const float DY = A.Y - B.Y;
        return sqrtf(DX * DX + DY * DY);
    }

    float DistSquared2D(const FVector& A, const FVector& B)
    {
        const float DX = A.X - B.X;
        const float DY = A.Y - B.Y;
        return DX * DX + DY * DY;
    }

    FVector GetNodeWorldLocation(const UCombatCoverNodeComponent* Node)
    {
        const AActor* Owner = Node ? Node->GetOwner() : nullptr;
        return Owner ? Owner->GetActorLocation() : FVector::ZeroVector;
    }

    FVector MakeFlatOffset(const FVector& Point, const FVector& Origin, float MaxLength)
    {
        FVector Offset(Point.X - Origin.X, Point.Y - Origin.Y, 0.0f);
        const float Length = sqrtf(Offset.X * Offset.X + Offset.Y * Offset.Y);
        if (MaxLength > 0.0f && Length > MaxLength)
        {
            const float Scale = MaxLength / Length;
            Offset.X *= Scale;
            Offset.Y *= Scale;
        }
        return Offset;
    }

    FVector MakeFlatDirection(const FVector& Direction, const FVector& Fallback = FVector::ForwardVector)
    {
        FVector Flat(Direction.X, Direction.Y, 0.0f);
        if (Flat.IsNearlyZero())
        {
            Flat = FVector(Fallback.X, Fallback.Y, 0.0f);
        }
        if (Flat.IsNearlyZero())
        {
            return FVector::ForwardVector;
        }
        return Flat.Normalized();
    }

    float ClampFloat(float Value, float MinValue, float MaxValue)
    {
        return (std::min)((std::max)(Value, MinValue), MaxValue);
    }

    FString MakeLinkRuntimeKey(const FString& A, const FString& B)
    {
        const bool bAB = A <= B;
        const FString& First = bAB ? A : B;
        const FString& Second = bAB ? B : A;

        char Buffer[256] = {};
        std::snprintf(Buffer, sizeof(Buffer), "%s|%s", First.c_str(), Second.c_str());
        return FString(Buffer);
    }

    int32 GetTraversalLaneSign(const FString& FromNodeId, const FString& ToNodeId)
    {
        return FromNodeId <= ToNodeId ? 1 : -1;
    }

    int32 MakeLaneIndexByOrder(int32 Order)
    {
        if (Order <= 0)
        {
            return 0;
        }

        const int32 Magnitude = (Order + 1) / 2;
        return (Order % 2) == 1 ? -Magnitude : Magnitude;
    }

    FVector LerpVector(const FVector& A, const FVector& B, float Alpha)
    {
        return A + (B - A) * Alpha;
    }

    void AppendPathPointIfUseful(TArray<FVector>& Points, const FVector& Point)
    {
        constexpr float DuplicateToleranceSq = 4.0f;
        if (!Points.empty() && DistSquared2D(Points.back(), Point) <= DuplicateToleranceSq)
        {
            return;
        }
        Points.push_back(Point);
    }

    FString FormatSlotHandle(const FCombatCoverSlotHandle& Handle)
    {
        char Buffer[128] = {};
        std::snprintf(Buffer, sizeof(Buffer), "%s:%d", Handle.NodeId.c_str(), Handle.SlotId);
        return FString(Buffer);
    }

    FString MakeGeneratedNodeId(int32 PreferredIndex)
    {
        char Buffer[64] = {};
        std::snprintf(Buffer, sizeof(Buffer), "CoverNode_%03d", (std::max)(1, PreferredIndex));
        return FString(Buffer);
    }

    bool IsSameAgent(const TWeakObjectPtr<UCombatCoverAgentComponent>& Ptr, const UCombatCoverAgentComponent* Agent)
    {
        return Agent && Ptr.Get() == Agent;
    }

    bool IsValidCombatAgent(const UCombatCoverAgentComponent* Agent)
    {
        return IsValid(Agent) && IsValid(Agent->GetOwner()) && Agent->IsAlive();
    }

    void DrawDebugCircleXY(UWorld* World, const FVector& Center, float Radius, const FColor& Color, float Duration)
    {
        if (!World || Radius <= 0.0f)
        {
            return;
        }

        constexpr int32 Segments = 36;
        constexpr float TwoPi = 6.28318530717958647692f;
        FVector Prev = Center + FVector(Radius, 0.0f, 0.0f);
        for (int32 Index = 1; Index <= Segments; ++Index)
        {
            const float Angle = TwoPi * static_cast<float>(Index) / static_cast<float>(Segments);
            const FVector Next = Center + FVector(cosf(Angle) * Radius, sinf(Angle) * Radius, 0.0f);
            DrawDebugLine(World, Prev, Next, Color, Duration);
            Prev = Next;
        }
    }

    FColor MakeFireLineColor(const UCombatCoverAgentComponent* Agent)
    {
        if (Agent && Agent->GetTeamTag().find("Enemy") != FString::npos)
        {
            return FColor(255, 80, 60);
        }
        if (Agent && Agent->GetTeamTag().find("Ally") != FString::npos)
        {
            return FColor(80, 160, 255);
        }
        return FColor(255, 230, 80);
    }

    std::mt19937& GetCombatRandomGenerator()
    {
        static std::mt19937 Generator{ std::random_device{}() };
        return Generator;
    }

    bool RandomChance(float Chance)
    {
        Chance = (std::min)((std::max)(0.0f, Chance), 1.0f);
        return Chance > 0.0f && std::uniform_real_distribution<float>(0.0f, 1.0f)(GetCombatRandomGenerator()) < Chance;
    }

    bool IsAttackSlot(const FCombatCoverSlot& Slot)
    {
        return Slot.ProvidesCover() && Slot.CanAttackFrom();
    }

    float ScoreCombatSlotForAgent(const FCombatCoverSlot& Slot, const UCombatCoverAgentComponent* Agent, ECombatSlotQueryPurpose Purpose)
    {
        float Score = Slot.GetSlotSelectionScore();

        if (Purpose == ECombatSlotQueryPurpose::PreferFullCover)
        {
            switch (Slot.SlotType)
            {
            case ECombatCoverSlotType::FullCover:
                Score += 300.0f;
                break;
            case ECombatCoverSlotType::CombatCover:
                Score += 40.0f;
                break;
            case ECombatCoverSlotType::StandingCombatCover:
                Score += 25.0f;
                break;
            case ECombatCoverSlotType::ExposedDummy:
            default:
                Score -= 100.0f;
                break;
            }
        }

        if (Agent)
        {
            const float HealthRatio = Agent->GetHealthRatio();
            const bool bUnderPressure = Agent->IsSuppressed() || Agent->GetIncomingFireCount() > 0 || HealthRatio <= Agent->GetLowHealthFullCoverRatio();
            const bool bHasTarget = Agent->GetCurrentTarget() != nullptr || Agent->IsEngaging();

            if (bUnderPressure)
            {
                if (Slot.SlotType == ECombatCoverSlotType::FullCover)
                {
                    Score += 120.0f;
                }
                else if (Slot.SlotType == ECombatCoverSlotType::CombatCover)
                {
                    Score += 30.0f;
                }
                else if (Slot.SlotType == ECombatCoverSlotType::StandingCombatCover)
                {
                    Score -= 25.0f;
                }
            }
            else if (bHasTarget)
            {
                if (Slot.SlotType == ECombatCoverSlotType::StandingCombatCover)
                {
                    Score += 70.0f;
                }
                else if (Slot.SlotType == ECombatCoverSlotType::CombatCover)
                {
                    Score += 35.0f;
                }
            }
        }

        return Score;
    }
}

UCombatFlowManagerComponent::UCombatFlowManagerComponent()
{
    PrimaryComponentTick.SetTickGroup(TG_PrePhysics);
    SetComponentTickEnabled(true);
}

void UCombatFlowManagerComponent::BeginPlay()
{
    UActorComponent::BeginPlay();
    RefreshRegistry();
    ResetRuntimeState();
}

void UCombatFlowManagerComponent::EndPlay()
{
    ResetRuntimeState();
    CachedNodes.clear();
    CachedAgents.clear();
    NodeById.clear();
    UActorComponent::EndPlay();
}

void UCombatFlowManagerComponent::RefreshRegistry()
{
    CachedNodes.clear();
    CachedAgents.clear();
    NodeById.clear();

    UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }

    for (AActor* Actor : World->GetActors())
    {
        if (!IsValid(Actor))
        {
            continue;
        }

        if (UCombatCoverNodeComponent* Node = Actor->GetComponentByClass<UCombatCoverNodeComponent>())
        {
            CachedNodes.push_back(Node);
            if (!Node->GetNodeId().empty())
            {
                NodeById[Node->GetNodeId()] = Node;
            }
        }

        if (UCombatCoverAgentComponent* Agent = Actor->GetComponentByClass<UCombatCoverAgentComponent>())
        {
            CachedAgents.push_back(Agent);
        }
    }

    RemoveStaleRuntimeState();
    RemoveStaleAttackState();
    for (UCombatCoverNodeComponent* Node : CachedNodes)
    {
        EnsureRuntimeSlotsForNode(Node);
    }
    RemoveInvalidOrDeadRuntimeClaims();
    RemoveInvalidOrDeadLinkLaneClaims();
}

void UCombatFlowManagerComponent::ResetRuntimeState()
{
    RuntimeStateByNodeId.clear();
    RuntimeStateByLinkKey.clear();
    AttackStateByAgent.clear();
    SuppressionStateByAgent.clear();
    for (UCombatCoverNodeComponent* Node : CachedNodes)
    {
        EnsureRuntimeSlotsForNode(Node);
    }
}

bool UCombatFlowManagerComponent::AssignInitialSlot(UCombatCoverAgentComponent* Agent)
{
    if (!IsValid(Agent) || !Agent->GetOwner())
    {
        return false;
    }

    RefreshRegistry();
    const FCombatCoverSlotHandle Candidate = FindNearestFreeSlot(Agent->GetOwner()->GetActorLocation(), Agent->GetTeamTag(), Agent);
    if (!Candidate.IsValid())
    {
        return false;
    }

    if (!ReserveSlot(Agent, Candidate, InitialSlotNodeOccupancyOverflow))
    {
        return false;
    }

    FCombatMovePath MovePath;
    if (!BuildMovePathToSlot(Candidate, MovePath))
    {
        ReleaseAgent(Agent);
        return false;
    }

    Agent->MoveToReservedSlot(MovePath, true);
    return true;
}

bool UCombatFlowManagerComponent::TryAdvance(UCombatCoverAgentComponent* Agent)
{
    if (!IsValid(Agent))
    {
        return false;
    }

    RefreshRegistry();

    UCombatCoverNodeComponent* CurrentNode = FindNode(Agent->GetCurrentNodeId());
    if (!CurrentNode)
    {
        return AssignInitialSlot(Agent);
    }

    TArray<UCombatCoverNodeComponent*> CandidateNodes;
    GatherAdvanceCandidateNodes(Agent, CurrentNode, CandidateNodes);

    const ECombatSlotQueryPurpose AdvancePurpose = RandomChance(Agent->GetAdvanceFullCoverChance())
        ? ECombatSlotQueryPurpose::PreferFullCover
        : ECombatSlotQueryPurpose::Advance;

    for (UCombatCoverNodeComponent* NextNode : CandidateNodes)
    {
        if (!NextNode)
        {
            continue;
        }

        if (IsNodeAtOrOverCapacity(NextNode, Agent, AdvanceNodeOccupancyOverflow))
        {
            continue;
        }

        const FCombatCoverSlotHandle Candidate = FindFreeSlotInNode(NextNode, Agent->GetTeamTag(), Agent, AdvancePurpose);
        if (!Candidate.IsValid())
        {
            continue;
        }

        if (!ReserveSlot(Agent, Candidate, AdvanceNodeOccupancyOverflow))
        {
            continue;
        }

        FCombatCoverSlotHandle StartSlot;
        StartSlot.NodeId = Agent->GetCurrentNodeId();
        StartSlot.SlotId = Agent->GetCurrentSlotId();

        const int32 ReservedLinkLaneIndex = ReserveLinkLane(Agent, CurrentNode, NextNode);

        FCombatMovePath MovePath;
        if (!BuildMovePathBetweenNodes(CurrentNode, NextNode, StartSlot, Candidate, Agent, ReservedLinkLaneIndex, MovePath))
        {
            ReleaseAgent(Agent);
            continue;
        }

        ReleaseAgentExcept(Agent, Candidate);
        Agent->MoveToReservedSlot(MovePath, false);
        return true;
    }

    return false;
}

bool UCombatFlowManagerComponent::TryRepositionNearby(UCombatCoverAgentComponent* Agent)
{
    if (!IsValid(Agent))
    {
        return false;
    }

    if (Agent->IsMovingForCombatRange() && !Agent->GetTargetNodeId().empty() && Agent->GetTargetSlotId() >= 0)
    {
        return true;
    }

    return TryAdvance(Agent);
}

bool UCombatFlowManagerComponent::TryMoveToFullCoverInCurrentNode(UCombatCoverAgentComponent* Agent)
{
    return TryMoveToSlotTypeInCurrentNode(Agent, ECombatCoverSlotType::FullCover);
}

bool UCombatFlowManagerComponent::TryMoveToCombatSlotInCurrentNode(UCombatCoverAgentComponent* Agent)
{
    return TryMoveToSlotTypeInCurrentNode(Agent, ECombatCoverSlotType::CombatCover);
}

bool UCombatFlowManagerComponent::TryMoveToStandingCombatSlotInCurrentNode(UCombatCoverAgentComponent* Agent)
{
    return TryMoveToSlotTypeInCurrentNode(Agent, ECombatCoverSlotType::StandingCombatCover);
}

void UCombatFlowManagerComponent::ConfirmArrived(UCombatCoverAgentComponent* Agent, const FCombatCoverSlotHandle& SlotHandle)
{
    if (!IsValid(Agent) || !SlotHandle.IsValid())
    {
        return;
    }

    UCombatCoverNodeComponent* Node = FindNode(SlotHandle.NodeId);
    if (!Node)
    {
        return;
    }

    EnsureRuntimeSlotsForNode(Node);
    FCombatNodeRuntimeState& NodeState = RuntimeStateByNodeId[SlotHandle.NodeId];
    FCombatSlotRuntimeState& SlotState = NodeState.Slots[SlotHandle.SlotId];

    if (SlotState.ReservedBy.Get() == Agent || !SlotState.ReservedBy.Get())
    {
        SlotState.ReservedBy.Reset();
        SlotState.OccupiedBy.Reset(Agent);
    }

    ReleaseLinkLane(Agent);
}

void UCombatFlowManagerComponent::ReleaseAgent(UCombatCoverAgentComponent* Agent)
{
    if (!Agent)
    {
        return;
    }

    AttackStateByAgent.erase(Agent);
    ReleaseLinkLane(Agent);
    for (auto& NodePair : RuntimeStateByNodeId)
    {
        for (auto& SlotPair : NodePair.second.Slots)
        {
            FCombatSlotRuntimeState& SlotState = SlotPair.second;
            if (SlotState.ReservedBy.Get() == Agent)
            {
                SlotState.ReservedBy.Reset();
            }
            if (SlotState.OccupiedBy.Get() == Agent)
            {
                SlotState.OccupiedBy.Reset();
            }
        }
    }
}

int32 UCombatFlowManagerComponent::AutoGenerateMissingNodeIds()
{
    RefreshRegistry();

    TSet<FString> UsedNodeIds;
    for (UCombatCoverNodeComponent* Node : CachedNodes)
    {
        if (Node && !Node->GetNodeId().empty())
        {
            UsedNodeIds.insert(Node->GetNodeId());
        }
    }

    int32 GeneratedCount = 0;
    int32 NextIndex = 1;
    for (UCombatCoverNodeComponent* Node : CachedNodes)
    {
        if (!Node || !Node->GetNodeId().empty())
        {
            continue;
        }

        FString Candidate;
        do
        {
            Candidate = MakeGeneratedNodeId(NextIndex++);
        }
        while (UsedNodeIds.find(Candidate) != UsedNodeIds.end());

        Node->SetNodeId(Candidate);
        UsedNodeIds.insert(Candidate);
        ++GeneratedCount;
    }

    RefreshRegistry();
    return GeneratedCount;
}

int32 UCombatFlowManagerComponent::AutoLinkNearby(float MaxDistance, int32 MaxLinksPerNode, bool bDirectedByX)
{
    RefreshRegistry();

    if (MaxDistance <= 0.0f || MaxLinksPerNode <= 0)
    {
        return 0;
    }

    int32 CreatedCount = 0;
    for (UCombatCoverNodeComponent* Source : CachedNodes)
    {
        if (!Source || !Source->GetOwner() || Source->GetNodeId().empty())
        {
            continue;
        }

        TArray<TPair<float, UCombatCoverNodeComponent*>> Candidates;
        const FVector SourceLocation = Source->GetOwner()->GetActorLocation();
        for (UCombatCoverNodeComponent* Target : CachedNodes)
        {
            if (!Target || Target == Source || !Target->GetOwner() || Target->GetNodeId().empty())
            {
                continue;
            }

            const FVector TargetLocation = Target->GetOwner()->GetActorLocation();
            if (bDirectedByX && TargetLocation.X <= SourceLocation.X)
            {
                continue;
            }

            const float Distance = Dist2D(SourceLocation, TargetLocation);
            if (Distance <= MaxDistance)
            {
                Candidates.push_back({ Distance, Target });
            }
        }

        std::sort(Candidates.begin(), Candidates.end(), [](const auto& A, const auto& B)
        {
            return A.first < B.first;
        });

        int32 LinksMadeForNode = 0;
        for (const auto& Candidate : Candidates)
        {
            if (LinksMadeForNode >= MaxLinksPerNode)
            {
                break;
            }

            if (Source->AddLinkToNodeId(Candidate.second->GetNodeId(), false))
            {
                ++LinksMadeForNode;
                ++CreatedCount;
            }
        }
    }

    RefreshRegistry();
    return CreatedCount;
}

FCombatCoverGraphValidationResult UCombatFlowManagerComponent::ValidateGraph(bool bLogToConsole)
{
    RefreshRegistry();

    FCombatCoverGraphValidationResult Result;
    Result.NodeCount = static_cast<int32>(CachedNodes.size());

    TMap<FString, int32> NodeIdCounts;
    for (UCombatCoverNodeComponent* Node : CachedNodes)
    {
        if (!Node)
        {
            continue;
        }

        Result.SlotCount += static_cast<int32>(Node->GetSlots().size());
        Result.LinkCount += static_cast<int32>(Node->GetLinks().size());

        if (Node->GetNodeId().empty())
        {
            AddValidationMessage(Result, true, MakeNodeDebugName(Node) + ": NodeId is empty.");
        }
        else
        {
            NodeIdCounts[Node->GetNodeId()]++;
        }

        if (Node->GetSlots().empty())
        {
            AddValidationMessage(Result, true, MakeNodeDebugName(Node) + ": has no slots.");
        }

        TSet<int32> SlotIds;
        int32 CombatCoverSlotCount = 0;
        int32 FullCoverSlotCount = 0;
        for (const FCombatCoverSlot& Slot : Node->GetSlots())
        {
            if (!SlotIds.insert(Slot.SlotId).second)
            {
                AddValidationMessage(Result, true, MakeNodeDebugName(Node) + ": duplicated SlotId.");
            }

            switch (Slot.SlotType)
            {
            case ECombatCoverSlotType::CombatCover:
            case ECombatCoverSlotType::StandingCombatCover:
                ++CombatCoverSlotCount;
                break;
            case ECombatCoverSlotType::FullCover:
                ++FullCoverSlotCount;
                break;
            case ECombatCoverSlotType::ExposedDummy:
                if (Slot.Weight > 100.0f)
                {
                    AddValidationMessage(Result, false, MakeNodeDebugName(Node) + ": ExposedDummy slot has unusually high Weight.");
                }
                break;
            default:
                AddValidationMessage(Result, false, MakeNodeDebugName(Node) + ": slot has invalid SlotType.");
                break;
            }
        }

        if (FullCoverSlotCount > 0 && CombatCoverSlotCount == 0 && Node->GetLinks().empty())
        {
            AddValidationMessage(Result, false, MakeNodeDebugName(Node) + ": has only FullCover/Exposed slots and no outgoing links.");
        }
    }

    for (const auto& Pair : NodeIdCounts)
    {
        if (Pair.second > 1)
        {
            AddValidationMessage(Result, true, Pair.first + ": duplicated NodeId.");
        }
    }

    for (UCombatCoverNodeComponent* Node : CachedNodes)
    {
        if (!Node)
        {
            continue;
        }

        for (const FCombatCoverLink& Link : Node->GetLinks())
        {
            if (Link.TargetNodeId.empty())
            {
                AddValidationMessage(Result, true, MakeNodeDebugName(Node) + ": link target is empty.");
                continue;
            }

            if (Link.TargetNodeId == Node->GetNodeId())
            {
                AddValidationMessage(Result, true, MakeNodeDebugName(Node) + ": self link is not allowed.");
                continue;
            }

            UCombatCoverNodeComponent* Target = FindNode(Link.TargetNodeId);
            if (!Target)
            {
                AddValidationMessage(Result, true, MakeNodeDebugName(Node) + ": broken link to " + Link.TargetNodeId + ".");
                continue;
            }

            if (Link.bBidirectional)
            {
                bool bReverseFound = false;
                for (const FCombatCoverLink& Reverse : Target->GetLinks())
                {
                    if (Reverse.TargetNodeId == Node->GetNodeId())
                    {
                        bReverseFound = true;
                        break;
                    }
                }
                if (!bReverseFound)
                {
                    AddValidationMessage(Result, false, MakeNodeDebugName(Node) + ": bidirectional flag has no reverse link on " + MakeNodeDebugName(Target) + ".");
                }
            }
        }
    }

    if (bLogToConsole)
    {
        UE_LOG("Combat graph validate: nodes=%d slots=%d links=%d errors=%d warnings=%d",
            Result.NodeCount,
            Result.SlotCount,
            Result.LinkCount,
            Result.ErrorCount,
            Result.WarningCount);
        for (const FString& Message : Result.Messages)
        {
            UE_LOG("  %s", Message.c_str());
        }
    }

    return Result;
}

UCombatCoverNodeComponent* UCombatFlowManagerComponent::FindNode(const FString& NodeId) const
{
    const auto It = NodeById.find(NodeId);
    return It != NodeById.end() ? It->second : nullptr;
}

bool UCombatFlowManagerComponent::IsSlotFree(const FCombatCoverSlotHandle& SlotHandle, const UCombatCoverAgentComponent* RequestingAgent) const
{
    if (!SlotHandle.IsValid())
    {
        return false;
    }

    const auto NodeIt = RuntimeStateByNodeId.find(SlotHandle.NodeId);
    if (NodeIt == RuntimeStateByNodeId.end())
    {
        return true;
    }

    const auto SlotIt = NodeIt->second.Slots.find(SlotHandle.SlotId);
    if (SlotIt == NodeIt->second.Slots.end())
    {
        return true;
    }

    const FCombatSlotRuntimeState& SlotState = SlotIt->second;
    const UCombatCoverAgentComponent* ReservedBy = SlotState.ReservedBy.Get();
    const UCombatCoverAgentComponent* OccupiedBy = SlotState.OccupiedBy.Get();
    const bool bReservedBlocks = ReservedBy && ReservedBy != RequestingAgent && ReservedBy->IsAlive();
    const bool bOccupiedBlocks = OccupiedBy && OccupiedBy != RequestingAgent && OccupiedBy->IsAlive();
    return !bReservedBlocks && !bOccupiedBlocks;
}

bool UCombatFlowManagerComponent::IsNodeOccupiedOrReserved(const UCombatCoverNodeComponent* Node, const UCombatCoverAgentComponent* RequestingAgent) const
{
    return IsNodeAtOrOverCapacity(Node, RequestingAgent, 0);
}

bool UCombatFlowManagerComponent::IsNodeAtOrOverCapacity(
    const UCombatCoverNodeComponent* Node,
    const UCombatCoverAgentComponent* RequestingAgent,
    int32 NodeOccupancyOverflow) const
{
    if (!Node || Node->GetNodeId().empty())
    {
        return true;
    }

    if (!bUseNodeOccupancyLimit)
    {
        return false;
    }

    const int32 EffectiveMaxOccupants = (std::max)(1, Node->GetMaxOccupants() + (std::max)(0, NodeOccupancyOverflow));
    return CountNodeClaims(Node, RequestingAgent) >= EffectiveMaxOccupants;
}

const FCombatCoverSlot* UCombatFlowManagerComponent::FindCurrentSlot(const UCombatCoverAgentComponent* Agent) const
{
    if (!Agent || Agent->GetCurrentNodeId().empty() || Agent->GetCurrentSlotId() < 0)
    {
        return nullptr;
    }

    const UCombatCoverNodeComponent* Node = FindNode(Agent->GetCurrentNodeId());
    return Node ? Node->FindSlotById(Agent->GetCurrentSlotId()) : nullptr;
}

bool UCombatFlowManagerComponent::CanAgentAttackFromCurrentSlot(const UCombatCoverAgentComponent* Agent) const
{
    const FCombatCoverSlot* Slot = FindCurrentSlot(Agent);
    return Slot ? Slot->CanAttackFrom() : true;
}

bool UCombatFlowManagerComponent::CanAgentBeTargetedInCurrentSlot(const UCombatCoverAgentComponent* Agent) const
{
    if (!Agent)
    {
        return true;
    }

    const FCombatCoverSlot* Slot = FindCurrentSlot(Agent);
    return Slot ? Slot->CanBeTargetedWhileInCover() : true;
}

float UCombatFlowManagerComponent::GetTargetPriorityMultiplierForAgent(const UCombatCoverAgentComponent* Agent) const
{
    if (!Agent)
    {
        return 1.0f;
    }

    const FCombatCoverSlot* Slot = FindCurrentSlot(Agent);
    if (!Slot)
    {
        return Agent->IsInCover() ? Agent->GetInCoverTargetPriorityMultiplier() : 1.0f;
    }

    if (Slot->SlotType == ECombatCoverSlotType::CombatCover)
    {
        return Agent->GetInCoverTargetPriorityMultiplier();
    }

    return Slot->GetTargetPriorityMultiplierWhileInCover();
}

bool UCombatFlowManagerComponent::IsAgentInSlotType(const UCombatCoverAgentComponent* Agent, ECombatCoverSlotType SlotType) const
{
    const FCombatCoverSlot* Slot = FindCurrentSlot(Agent);
    return Slot && Slot->SlotType == SlotType;
}

bool UCombatFlowManagerComponent::HasFreeCombatSlotInCurrentNode(const UCombatCoverAgentComponent* Agent) const
{
    if (!Agent || Agent->GetCurrentNodeId().empty())
    {
        return false;
    }

    UCombatCoverNodeComponent* Node = FindNode(Agent->GetCurrentNodeId());
    if (!Node)
    {
        return false;
    }

    FCombatCoverSlotHandle CurrentSlot;
    CurrentSlot.NodeId = Agent->GetCurrentNodeId();
    CurrentSlot.SlotId = Agent->GetCurrentSlotId();
    return FindFreeSlotInNode(Node, Agent->GetTeamTag(), Agent, ECombatSlotQueryPurpose::AttackSlotOnly, &CurrentSlot).IsValid();
}

void UCombatFlowManagerComponent::DrawAllDebugVisuals(bool bIncludeUnselected) const
{
    UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }

    FScene& Scene = World->GetScene();
    for (UCombatCoverNodeComponent* Node : CachedNodes)
    {
        if (!Node)
        {
            continue;
        }
        Node->DrawDebugVisuals(Scene, !bIncludeUnselected);
    }
}

UCombatFlowManagerComponent* UCombatFlowManagerComponent::FindInWorld(UWorld* World)
{
    if (!World)
    {
        return nullptr;
    }

    for (AActor* Actor : World->GetActors())
    {
        if (!IsValid(Actor))
        {
            continue;
        }

        if (UCombatFlowManagerComponent* Manager = Actor->GetComponentByClass<UCombatFlowManagerComponent>())
        {
            return Manager;
        }
    }

    return nullptr;
}

FCombatCoverSlotHandle UCombatFlowManagerComponent::FindNearestFreeSlot(
    const FVector& WorldLocation,
    const FString& TeamTag,
    const UCombatCoverAgentComponent* RequestingAgent) const
{
    FCombatCoverSlotHandle BestHandle;
    float BestDistance = 0.0f;
    bool bHasBest = false;

    float NearestCompatibleDistance = 0.0f;
    bool bHasNearestCompatible = false;

    for (UCombatCoverNodeComponent* Node : CachedNodes)
    {
        if (!Node || Node->GetNodeId().empty())
        {
            continue;
        }

        for (int32 SlotIndex = 0; SlotIndex < static_cast<int32>(Node->GetSlots().size()); ++SlotIndex)
        {
            const FCombatCoverSlot& Slot = Node->GetSlots()[SlotIndex];
            if (!SlotTagsMatchTeam(Slot, TeamTag))
            {
                continue;
            }

            const float Distance = Dist2D(WorldLocation, Node->GetSlotWorldPosition(SlotIndex));
            if (!bHasNearestCompatible || Distance < NearestCompatibleDistance)
            {
                NearestCompatibleDistance = Distance;
                bHasNearestCompatible = true;
            }
        }
    }

    const bool bUseInitialClusterLimit = bLimitInitialSlotSearchToSpawnCluster && bHasNearestCompatible;
    const float InitialClusterMaxDistance = NearestCompatibleDistance + (std::max)(0.0f, InitialSlotSearchDistanceSlack);

    for (UCombatCoverNodeComponent* Node : CachedNodes)
    {
        if (!Node || Node->GetNodeId().empty())
        {
            continue;
        }

        if (IsNodeAtOrOverCapacity(Node, RequestingAgent, InitialSlotNodeOccupancyOverflow))
        {
            continue;
        }

        for (int32 SlotIndex = 0; SlotIndex < static_cast<int32>(Node->GetSlots().size()); ++SlotIndex)
        {
            const FCombatCoverSlot& Slot = Node->GetSlots()[SlotIndex];
            if (!SlotTagsMatchTeam(Slot, TeamTag))
            {
                continue;
            }

            const float Distance = Dist2D(WorldLocation, Node->GetSlotWorldPosition(SlotIndex));
            if (bUseInitialClusterLimit && Distance > InitialClusterMaxDistance)
            {
                continue;
            }

            FCombatCoverSlotHandle Handle;
            Handle.NodeId = Node->GetNodeId();
            Handle.SlotId = Slot.SlotId;
            if (!IsSlotFree(Handle, RequestingAgent))
            {
                continue;
            }

            const float SlotScore = (std::max)(1.0f, ScoreCombatSlotForAgent(Slot, RequestingAgent, ECombatSlotQueryPurpose::Advance));
            const float WeightedDistance = Distance / SlotScore;
            if (!bHasBest || WeightedDistance < BestDistance)
            {
                BestDistance = WeightedDistance;
                BestHandle = Handle;
                bHasBest = true;
            }
        }
    }

    return BestHandle;
}

FCombatCoverSlotHandle UCombatFlowManagerComponent::FindFreeSlotInNode(
    UCombatCoverNodeComponent* Node,
    const FString& TeamTag,
    const UCombatCoverAgentComponent* RequestingAgent,
    ECombatSlotQueryPurpose Purpose,
    const FCombatCoverSlotHandle* SkipSlotHandle) const
{
    FCombatCoverSlotHandle BestHandle;
    float BestWeight = -1000000.0f;

    if (!Node || Node->GetNodeId().empty())
    {
        return BestHandle;
    }

    for (const FCombatCoverSlot& Slot : Node->GetSlots())
    {
        if (SkipSlotHandle && SkipSlotHandle->NodeId == Node->GetNodeId() && SkipSlotHandle->SlotId == Slot.SlotId)
        {
            continue;
        }

        if (!SlotTagsMatchTeam(Slot, TeamTag))
        {
            continue;
        }

        switch (Purpose)
        {
        case ECombatSlotQueryPurpose::FullCoverOnly:
            if (Slot.SlotType != ECombatCoverSlotType::FullCover)
            {
                continue;
            }
            break;
        case ECombatSlotQueryPurpose::CombatCoverOnly:
            if (Slot.SlotType != ECombatCoverSlotType::CombatCover)
            {
                continue;
            }
            break;
        case ECombatSlotQueryPurpose::StandingCombatCoverOnly:
            if (Slot.SlotType != ECombatCoverSlotType::StandingCombatCover)
            {
                continue;
            }
            break;
        case ECombatSlotQueryPurpose::AttackSlotOnly:
            if (!IsAttackSlot(Slot))
            {
                continue;
            }
            break;
        case ECombatSlotQueryPurpose::Advance:
        case ECombatSlotQueryPurpose::PreferFullCover:
        default:
            break;
        }

        FCombatCoverSlotHandle Handle;
        Handle.NodeId = Node->GetNodeId();
        Handle.SlotId = Slot.SlotId;
        if (!IsSlotFree(Handle, RequestingAgent))
        {
            continue;
        }

        const float SlotScore = ScoreCombatSlotForAgent(Slot, RequestingAgent, Purpose);

        if (SlotScore > BestWeight)
        {
            BestWeight = SlotScore;
            BestHandle = Handle;
        }
    }

    return BestHandle;
}

bool UCombatFlowManagerComponent::TryMoveToSlotTypeInCurrentNode(UCombatCoverAgentComponent* Agent, ECombatCoverSlotType DesiredSlotType)
{
    if (!IsValid(Agent) || Agent->GetCurrentNodeId().empty())
    {
        return false;
    }

    RefreshRegistry();

    UCombatCoverNodeComponent* CurrentNode = FindNode(Agent->GetCurrentNodeId());
    if (!CurrentNode)
    {
        return false;
    }

    FCombatCoverSlotHandle StartSlot;
    StartSlot.NodeId = Agent->GetCurrentNodeId();
    StartSlot.SlotId = Agent->GetCurrentSlotId();

    ECombatSlotQueryPurpose Purpose = ECombatSlotQueryPurpose::AttackSlotOnly;
    if (DesiredSlotType == ECombatCoverSlotType::FullCover)
    {
        Purpose = ECombatSlotQueryPurpose::FullCoverOnly;
    }
    else if (DesiredSlotType == ECombatCoverSlotType::CombatCover)
    {
        Purpose = ECombatSlotQueryPurpose::AttackSlotOnly;
    }
    else if (DesiredSlotType == ECombatCoverSlotType::StandingCombatCover)
    {
        Purpose = ECombatSlotQueryPurpose::StandingCombatCoverOnly;
    }
    const FCombatCoverSlotHandle Candidate = FindFreeSlotInNode(CurrentNode, Agent->GetTeamTag(), Agent, Purpose, &StartSlot);
    if (!Candidate.IsValid())
    {
        return false;
    }

    if (!ReserveSlot(Agent, Candidate))
    {
        return false;
    }

    FCombatMovePath MovePath;
    if (!BuildMovePathWithinNode(CurrentNode, StartSlot, Candidate, MovePath))
    {
        ReleaseAgentExcept(Agent, StartSlot);
        return false;
    }

    ReleaseAgentExcept(Agent, Candidate);
    Agent->MoveToReservedSlot(MovePath, false);
    return true;
}

bool UCombatFlowManagerComponent::ReserveSlot(UCombatCoverAgentComponent* Agent, const FCombatCoverSlotHandle& SlotHandle, int32 NodeOccupancyOverflow)
{
    if (!IsValid(Agent) || !SlotHandle.IsValid())
    {
        return false;
    }

    UCombatCoverNodeComponent* Node = FindNode(SlotHandle.NodeId);
    if (!Node || !Node->FindSlotById(SlotHandle.SlotId))
    {
        return false;
    }

    if (IsNodeAtOrOverCapacity(Node, Agent, NodeOccupancyOverflow) || !IsSlotFree(SlotHandle, Agent))
    {
        return false;
    }

    EnsureRuntimeSlotsForNode(Node);
    FCombatSlotRuntimeState& SlotState = RuntimeStateByNodeId[SlotHandle.NodeId].Slots[SlotHandle.SlotId];
    SlotState.ReservedBy.Reset(Agent);
    return true;
}

int32 UCombatFlowManagerComponent::ReserveLinkLane(UCombatCoverAgentComponent* Agent, UCombatCoverNodeComponent* FromNode, UCombatCoverNodeComponent* ToNode)
{
    if (!bEnableLinkLaneReservation || !IsValid(Agent) || !FromNode || !ToNode)
    {
        return 0;
    }

    const FString& FromNodeId = FromNode->GetNodeId();
    const FString& ToNodeId = ToNode->GetNodeId();
    if (FromNodeId.empty() || ToNodeId.empty())
    {
        return 0;
    }

    RemoveInvalidOrDeadLinkLaneClaims();

    const FString LinkKey = MakeLinkRuntimeKey(FromNodeId, ToNodeId);
    FCombatLinkLaneRuntimeState& LinkState = RuntimeStateByLinkKey[LinkKey];

    for (auto It = LinkState.ReservedByLane.begin(); It != LinkState.ReservedByLane.end(); )
    {
        UCombatCoverAgentComponent* ReservedAgent = It->second.Get();
        if (!IsValid(ReservedAgent) || !ReservedAgent->IsAlive() || ReservedAgent == Agent)
        {
            It = LinkState.ReservedByLane.erase(It);
            continue;
        }
        ++It;
    }

    const int32 LaneCount = (std::max)(1, LinkReservedLaneCount);
    int32 BestCanonicalLane = 0;
    bool bFoundFreeLane = false;
    for (int32 Order = 0; Order < LaneCount; ++Order)
    {
        const int32 CandidateLane = MakeLaneIndexByOrder(Order);
        if (LinkState.ReservedByLane.find(CandidateLane) == LinkState.ReservedByLane.end())
        {
            BestCanonicalLane = CandidateLane;
            bFoundFreeLane = true;
            break;
        }
    }

    for (int32 Order = LaneCount; !bFoundFreeLane; ++Order)
    {
        const int32 CandidateLane = MakeLaneIndexByOrder(Order);
        if (LinkState.ReservedByLane.find(CandidateLane) == LinkState.ReservedByLane.end())
        {
            BestCanonicalLane = CandidateLane;
            bFoundFreeLane = true;
            break;
        }
    }

    LinkState.ReservedByLane[BestCanonicalLane].Reset(Agent);
    return BestCanonicalLane * GetTraversalLaneSign(FromNodeId, ToNodeId);
}

void UCombatFlowManagerComponent::ReleaseLinkLane(UCombatCoverAgentComponent* Agent)
{
    if (!Agent)
    {
        return;
    }

    for (auto LinkIt = RuntimeStateByLinkKey.begin(); LinkIt != RuntimeStateByLinkKey.end(); )
    {
        FCombatLinkLaneRuntimeState& LinkState = LinkIt->second;
        for (auto LaneIt = LinkState.ReservedByLane.begin(); LaneIt != LinkState.ReservedByLane.end(); )
        {
            if (LaneIt->second.Get() == Agent)
            {
                LaneIt = LinkState.ReservedByLane.erase(LaneIt);
                continue;
            }
            ++LaneIt;
        }

        if (LinkState.ReservedByLane.empty())
        {
            LinkIt = RuntimeStateByLinkKey.erase(LinkIt);
            continue;
        }
        ++LinkIt;
    }
}

void UCombatFlowManagerComponent::ReleaseAgentExcept(UCombatCoverAgentComponent* Agent, const FCombatCoverSlotHandle& KeepSlotHandle)
{
    if (!Agent)
    {
        return;
    }

    for (auto& NodePair : RuntimeStateByNodeId)
    {
        for (auto& SlotPair : NodePair.second.Slots)
        {
            const bool bKeep = NodePair.first == KeepSlotHandle.NodeId && SlotPair.first == KeepSlotHandle.SlotId;
            if (bKeep)
            {
                continue;
            }

            FCombatSlotRuntimeState& SlotState = SlotPair.second;
            if (SlotState.ReservedBy.Get() == Agent)
            {
                SlotState.ReservedBy.Reset();
            }
            if (SlotState.OccupiedBy.Get() == Agent)
            {
                SlotState.OccupiedBy.Reset();
            }
        }
    }
}

int32 UCombatFlowManagerComponent::CountNodeClaims(const UCombatCoverNodeComponent* Node, const UCombatCoverAgentComponent* IgnoreAgent) const
{
    if (!Node || Node->GetNodeId().empty())
    {
        return 0;
    }

    const auto NodeIt = RuntimeStateByNodeId.find(Node->GetNodeId());
    if (NodeIt == RuntimeStateByNodeId.end())
    {
        return 0;
    }

    int32 Count = 0;
    for (const auto& SlotPair : NodeIt->second.Slots)
    {
        const FCombatSlotRuntimeState& SlotState = SlotPair.second;
        const UCombatCoverAgentComponent* ReservedBy = SlotState.ReservedBy.Get();
        const UCombatCoverAgentComponent* OccupiedBy = SlotState.OccupiedBy.Get();

        if (ReservedBy && ReservedBy != IgnoreAgent && ReservedBy->IsAlive())
        {
            ++Count;
        }
        if (OccupiedBy && OccupiedBy != IgnoreAgent && OccupiedBy != ReservedBy && OccupiedBy->IsAlive())
        {
            ++Count;
        }
    }
    return Count;
}

bool UCombatFlowManagerComponent::SlotTagsMatchTeam(const FCombatCoverSlot& Slot, const FString& TeamTag) const
{
    if (TeamTag.empty())
    {
        return true;
    }

    return Slot.Tags.find(TeamTag) != FString::npos;
}

void UCombatFlowManagerComponent::GatherAdvanceCandidateNodes(
    UCombatCoverAgentComponent* Agent,
    UCombatCoverNodeComponent* CurrentNode,
    TArray<UCombatCoverNodeComponent*>& OutNodes) const
{
    OutNodes.clear();
    if (!IsValid(Agent) || !CurrentNode || CurrentNode->GetNodeId().empty())
    {
        return;
    }

    const ECombatAdvanceLinkMode LinkMode = Agent->GetAdvanceLinkMode();
    auto AddUniqueNode = [&OutNodes](UCombatCoverNodeComponent* Node)
    {
        if (!Node)
        {
            return;
        }
        if (std::find(OutNodes.begin(), OutNodes.end(), Node) == OutNodes.end())
        {
            OutNodes.push_back(Node);
        }
    };

    if (LinkMode == ECombatAdvanceLinkMode::OutgoingLinks || LinkMode == ECombatAdvanceLinkMode::Both)
    {
        for (const FCombatCoverLink& Link : CurrentNode->GetLinks())
        {
            AddUniqueNode(FindNode(Link.TargetNodeId));
        }
    }

    if (LinkMode == ECombatAdvanceLinkMode::IncomingLinks || LinkMode == ECombatAdvanceLinkMode::Both)
    {
        const FString& CurrentNodeId = CurrentNode->GetNodeId();
        for (UCombatCoverNodeComponent* SourceNode : CachedNodes)
        {
            if (!SourceNode || SourceNode == CurrentNode)
            {
                continue;
            }

            for (const FCombatCoverLink& Link : SourceNode->GetLinks())
            {
                if (Link.TargetNodeId == CurrentNodeId)
                {
                    AddUniqueNode(SourceNode);
                    break;
                }
            }
        }
    }
}

FCombatCoverSlotHandle UCombatFlowManagerComponent::FindExitSlotForFullCoverTraversal(
    UCombatCoverNodeComponent* CurrentNode,
    const FCombatCoverSlotHandle& StartSlot,
    const FString& TeamTag,
    const UCombatCoverAgentComponent* RequestingAgent) const
{
    FCombatCoverSlotHandle BestHandle;
    float BestDistance = 0.0f;
    bool bHasBest = false;

    if (!CurrentNode || !StartSlot.IsValid() || StartSlot.NodeId != CurrentNode->GetNodeId())
    {
        return BestHandle;
    }

    const int32 StartSlotIndex = CurrentNode->FindSlotIndexById(StartSlot.SlotId);
    if (StartSlotIndex < 0)
    {
        return BestHandle;
    }

    const FCombatCoverSlot* StartSlotData = CurrentNode->FindSlotById(StartSlot.SlotId);
    if (!StartSlotData || StartSlotData->SlotType != ECombatCoverSlotType::FullCover)
    {
        return BestHandle;
    }

    const FVector StartPosition = CurrentNode->GetSlotWorldPosition(StartSlotIndex);
    for (int32 SlotIndex = 0; SlotIndex < static_cast<int32>(CurrentNode->GetSlots().size()); ++SlotIndex)
    {
        const FCombatCoverSlot& Slot = CurrentNode->GetSlots()[SlotIndex];
        if (Slot.SlotId == StartSlot.SlotId || Slot.SlotType == ECombatCoverSlotType::FullCover)
        {
            continue;
        }

        if (!SlotTagsMatchTeam(Slot, TeamTag))
        {
            continue;
        }

        FCombatCoverSlotHandle Handle;
        Handle.NodeId = CurrentNode->GetNodeId();
        Handle.SlotId = Slot.SlotId;
        if (!IsSlotFree(Handle, RequestingAgent))
        {
            continue;
        }

        const float Distance = Dist2D(StartPosition, CurrentNode->GetSlotWorldPosition(SlotIndex));
        if (!bHasBest || Distance < BestDistance)
        {
            BestDistance = Distance;
            BestHandle = Handle;
            bHasBest = true;
        }
    }

    return BestHandle;
}

bool UCombatFlowManagerComponent::BuildMovePathToSlot(const FCombatCoverSlotHandle& SlotHandle, FCombatMovePath& OutPath) const
{
    OutPath.Reset();
    if (!SlotHandle.IsValid())
    {
        return false;
    }

    UCombatCoverNodeComponent* TargetNode = FindNode(SlotHandle.NodeId);
    if (!TargetNode)
    {
        return false;
    }

    const int32 SlotIndex = TargetNode->FindSlotIndexById(SlotHandle.SlotId);
    if (SlotIndex < 0)
    {
        return false;
    }

    OutPath.FinalSlot = SlotHandle;
    AppendSlotApproachPoint(SlotHandle, false, OutPath.Points);
    OutPath.Points.push_back(TargetNode->GetSlotWorldPosition(SlotIndex));
    return true;
}

bool UCombatFlowManagerComponent::BuildMovePathWithinNode(
    UCombatCoverNodeComponent* Node,
    const FCombatCoverSlotHandle& StartSlot,
    const FCombatCoverSlotHandle& FinalSlot,
    FCombatMovePath& OutPath) const
{
    OutPath.Reset();
    if (!Node || !StartSlot.IsValid() || !FinalSlot.IsValid() || StartSlot.NodeId != FinalSlot.NodeId)
    {
        return false;
    }

    const int32 FinalSlotIndex = Node->FindSlotIndexById(FinalSlot.SlotId);
    if (FinalSlotIndex < 0)
    {
        return false;
    }

    OutPath.FinalSlot = FinalSlot;
    AppendSlotApproachPoint(StartSlot, true, OutPath.Points);
    AppendSlotApproachPoint(FinalSlot, false, OutPath.Points);
    OutPath.Points.push_back(Node->GetSlotWorldPosition(FinalSlotIndex));
    return true;
}

bool UCombatFlowManagerComponent::BuildMovePathBetweenNodes(
    UCombatCoverNodeComponent* FromNode,
    UCombatCoverNodeComponent* ToNode,
    const FCombatCoverSlotHandle& StartSlot,
    const FCombatCoverSlotHandle& FinalSlot,
    UCombatCoverAgentComponent* Agent,
    int32 ReservedLinkLaneIndex,
    FCombatMovePath& OutPath) const
{
    OutPath.Reset();
    if (!FromNode || !ToNode || !StartSlot.IsValid() || !FinalSlot.IsValid())
    {
        return false;
    }

    if (StartSlot.NodeId != FromNode->GetNodeId() || FinalSlot.NodeId != ToNode->GetNodeId())
    {
        return false;
    }

    const int32 FinalSlotIndex = ToNode->FindSlotIndexById(FinalSlot.SlotId);
    if (FinalSlotIndex < 0)
    {
        return false;
    }

    OutPath.FinalSlot = FinalSlot;

    TArray<FVector> EntryPoints;
    AppendSlotApproachPoint(FinalSlot, false, EntryPoints);
    EntryPoints.push_back(ToNode->GetSlotWorldPosition(FinalSlotIndex));

    TArray<FVector> Points;
    FCombatCoverSlotHandle LinkStartSlot = StartSlot;

    const FCombatCoverSlotHandle ExitSlot = FindExitSlotForFullCoverTraversal(
        FromNode,
        StartSlot,
        Agent ? Agent->GetTeamTag() : FString(),
        Agent);
    if (ExitSlot.IsValid())
    {
        FCombatMovePath ExitPath;
        if (!BuildMovePathWithinNode(FromNode, StartSlot, ExitSlot, ExitPath))
        {
            return false;
        }
        Points = ExitPath.Points;
        LinkStartSlot = ExitSlot;
        AppendSlotApproachPoint(LinkStartSlot, true, Points);
    }
    else
    {
        AppendSlotApproachPoint(StartSlot, true, Points);
    }

    FVector LinkStartAnchor = FVector::ZeroVector;
    FVector LinkEndAnchor = FVector::ZeroVector;
    if (!GetSlotPathAnchor(LinkStartSlot, true, LinkStartAnchor) || !GetSlotPathAnchor(FinalSlot, false, LinkEndAnchor))
    {
        return false;
    }

    AppendSlotExitTangentPoint(LinkStartSlot, LinkStartAnchor, Points);

    bool bReverse = false;
    const FCombatCoverLink* Link = FindTraversalLink(FromNode, ToNode, bReverse);
    AppendSlotAwareLinkPoints(FromNode, ToNode, Link, bReverse, LinkStartAnchor, LinkEndAnchor, ReservedLinkLaneIndex, Points);
    AppendSlotEntryTangentPoint(FinalSlot, LinkEndAnchor, Points);

    for (const FVector& Point : EntryPoints)
    {
        AppendPathPointIfUseful(Points, Point);
    }

    OutPath.Points = Points;
    return !OutPath.Points.empty();
}

bool UCombatFlowManagerComponent::AppendSlotApproachPoint(const FCombatCoverSlotHandle& SlotHandle, bool bForExit, TArray<FVector>& OutPoints) const
{
    FVector ApproachPoint;
    if (!GetSlotPathAnchor(SlotHandle, bForExit, ApproachPoint))
    {
        return false;
    }

    UCombatCoverNodeComponent* Node = FindNode(SlotHandle.NodeId);
    const FCombatCoverSlot* Slot = Node ? Node->FindSlotById(SlotHandle.SlotId) : nullptr;
    const bool bUseApproach = Slot && (bForExit ? Slot->bUseApproachOnExit : Slot->bUseApproachOnEntry);
    if (!bUseApproach || Slot->LocalApproachOffset.IsNearlyZero())
    {
        return false;
    }

    AppendPathPointIfUseful(OutPoints, ApproachPoint);
    return true;
}

bool UCombatFlowManagerComponent::GetSlotPathAnchor(const FCombatCoverSlotHandle& SlotHandle, bool bForExit, FVector& OutAnchor) const
{
    if (!SlotHandle.IsValid())
    {
        return false;
    }

    UCombatCoverNodeComponent* Node = FindNode(SlotHandle.NodeId);
    if (!Node)
    {
        return false;
    }

    const int32 SlotIndex = Node->FindSlotIndexById(SlotHandle.SlotId);
    if (SlotIndex < 0)
    {
        return false;
    }

    const FCombatCoverSlot* Slot = Node->FindSlotById(SlotHandle.SlotId);
    if (!Slot)
    {
        return false;
    }

    const bool bUseApproach = bForExit ? Slot->bUseApproachOnExit : Slot->bUseApproachOnEntry;
    OutAnchor = (bUseApproach && !Slot->LocalApproachOffset.IsNearlyZero())
        ? Node->GetSlotWorldApproachPosition(SlotIndex)
        : Node->GetSlotWorldPosition(SlotIndex);
    return true;
}

bool UCombatFlowManagerComponent::GetSlotPathTangent(const FCombatCoverSlotHandle& SlotHandle, bool bForExit, FVector& OutTangent) const
{
    if (!SlotHandle.IsValid())
    {
        return false;
    }

    UCombatCoverNodeComponent* Node = FindNode(SlotHandle.NodeId);
    if (!Node)
    {
        return false;
    }

    const int32 SlotIndex = Node->FindSlotIndexById(SlotHandle.SlotId);
    const FCombatCoverSlot* Slot = Node->FindSlotById(SlotHandle.SlotId);
    if (SlotIndex < 0 || !Slot)
    {
        return false;
    }

    const bool bUseExplicitTangent = bForExit ? Slot->bUseExitTangent : Slot->bUseEntryTangent;
    const FVector& LocalExplicitTangent = bForExit ? Slot->LocalExitTangent : Slot->LocalEntryTangent;
    if (bUseExplicitTangent && !LocalExplicitTangent.IsNearlyZero())
    {
        OutTangent = bForExit ? Node->GetSlotWorldExitTangent(SlotIndex) : Node->GetSlotWorldEntryTangent(SlotIndex);
        OutTangent = MakeFlatDirection(OutTangent);
        return true;
    }

    const bool bUseApproach = bForExit ? Slot->bUseApproachOnExit : Slot->bUseApproachOnEntry;
    if (bUseApproachOffsetAsImplicitTangent && bUseApproach && !Slot->LocalApproachOffset.IsNearlyZero())
    {
        const FVector SlotWorld = Node->GetSlotWorldPosition(SlotIndex);
        const FVector ApproachWorld = Node->GetSlotWorldApproachPosition(SlotIndex);
        OutTangent = bForExit
            ? MakeFlatDirection(ApproachWorld - SlotWorld)
            : MakeFlatDirection(SlotWorld - ApproachWorld);
        return true;
    }

    return false;
}

void UCombatFlowManagerComponent::AppendSlotExitTangentPoint(const FCombatCoverSlotHandle& SlotHandle, const FVector& StartAnchor, TArray<FVector>& OutPoints) const
{
    const float GuideDistance = (std::max)(0.0f, SlotTangentGuideDistance);
    if (GuideDistance <= 0.0f)
    {
        return;
    }

    FVector Tangent = FVector::ZeroVector;
    if (!GetSlotPathTangent(SlotHandle, true, Tangent))
    {
        return;
    }

    AppendPathPointIfUseful(OutPoints, StartAnchor + Tangent * GuideDistance);
}

void UCombatFlowManagerComponent::AppendSlotEntryTangentPoint(const FCombatCoverSlotHandle& SlotHandle, const FVector& EndAnchor, TArray<FVector>& OutPoints) const
{
    const float GuideDistance = (std::max)(0.0f, SlotTangentGuideDistance);
    if (GuideDistance <= 0.0f)
    {
        return;
    }

    FVector Tangent = FVector::ZeroVector;
    if (!GetSlotPathTangent(SlotHandle, false, Tangent))
    {
        return;
    }

    AppendPathPointIfUseful(OutPoints, EndAnchor - Tangent * GuideDistance);
}

void UCombatFlowManagerComponent::AppendSlotAwareLinkPoints(
    UCombatCoverNodeComponent* FromNode,
    UCombatCoverNodeComponent* ToNode,
    const FCombatCoverLink* Link,
    bool bReverse,
    const FVector& StartAnchor,
    const FVector& EndAnchor,
    int32 ReservedLinkLaneIndex,
    TArray<FVector>& OutPoints) const
{
    if (!FromNode || !ToNode)
    {
        return;
    }

    if (!Link || Link->PathPoints.empty())
    {
        return;
    }

    const float OffsetBlend = ClampFloat(SlotAwareLinkOffsetBlend, 0.0f, 1.0f);
    const float ReservedLaneBlend = ClampFloat(LinkReservedLaneOffsetBlend, 0.0f, 1.0f);
    const FVector StartCenter = GetNodeWorldLocation(FromNode);
    const FVector EndCenter = GetNodeWorldLocation(ToNode);

    TArray<FVector> CenterPoints;
    if (bReverse)
    {
        for (int32 Index = static_cast<int32>(Link->PathPoints.size()) - 1; Index >= 0; --Index)
        {
            CenterPoints.push_back(Link->PathPoints[Index]);
        }
    }
    else
    {
        for (const FVector& Point : Link->PathPoints)
        {
            CenterPoints.push_back(Point);
        }
    }

    FVector LinkDirection = EndCenter - StartCenter;
    if (CenterPoints.size() >= 2)
    {
        LinkDirection = CenterPoints.back() - CenterPoints.front();
    }
    else if (CenterPoints.size() == 1)
    {
        LinkDirection = CenterPoints.front() - StartCenter;
    }

    const FVector LinkForward = MakeFlatDirection(LinkDirection, EndCenter - StartCenter);
    FVector LinkRight = FVector::Cross(FVector::UpVector, LinkForward);
    LinkRight = MakeFlatDirection(LinkRight, FVector::RightVector);

    const float MaxSlotOffset = (std::max)(0.0f, MaxSlotAwareLinkOffset);
    const float StartSide = ClampFloat(MakeFlatOffset(StartAnchor, StartCenter, MaxSlotOffset).Dot(LinkRight), -MaxSlotOffset, MaxSlotOffset);
    const float EndSide = ClampFloat(MakeFlatOffset(EndAnchor, EndCenter, MaxSlotOffset).Dot(LinkRight), -MaxSlotOffset, MaxSlotOffset);
    const float ReservedLaneOffset = static_cast<float>(ReservedLinkLaneIndex) * (std::max)(0.0f, LinkReservedLaneSpacing) * ReservedLaneBlend;

    if (OffsetBlend <= 0.0f && std::fabs(ReservedLaneOffset) <= 1e-3f)
    {
        for (const FVector& CenterPoint : CenterPoints)
        {
            AppendPathPointIfUseful(OutPoints, CenterPoint);
        }
        return;
    }

    float TotalLength = 0.0f;
    FVector Previous = StartCenter;
    for (const FVector& CenterPoint : CenterPoints)
    {
        TotalLength += Dist2D(Previous, CenterPoint);
        Previous = CenterPoint;
    }
    TotalLength += Dist2D(Previous, EndCenter);

    if (TotalLength <= 1e-3f)
    {
        for (const FVector& CenterPoint : CenterPoints)
        {
            AppendPathPointIfUseful(OutPoints, CenterPoint);
        }
        return;
    }

    float AccumulatedLength = 0.0f;
    Previous = StartCenter;
    for (const FVector& CenterPoint : CenterPoints)
    {
        AccumulatedLength += Dist2D(Previous, CenterPoint);
        const float Alpha = (std::min)((std::max)(AccumulatedLength / TotalLength, 0.0f), 1.0f);
        const float SlotSide = (StartSide + (EndSide - StartSide) * Alpha) * OffsetBlend;
        const FVector Offset = LinkRight * (SlotSide + ReservedLaneOffset);
        AppendPathPointIfUseful(OutPoints, CenterPoint + Offset);
        Previous = CenterPoint;
    }
}

const FCombatCoverLink* UCombatFlowManagerComponent::FindTraversalLink(
    UCombatCoverNodeComponent* FromNode,
    UCombatCoverNodeComponent* ToNode,
    bool& bOutReverse) const
{
    bOutReverse = false;
    if (!FromNode || !ToNode || FromNode->GetNodeId().empty() || ToNode->GetNodeId().empty())
    {
        return nullptr;
    }

    const FString& ToNodeId = ToNode->GetNodeId();
    for (const FCombatCoverLink& Link : FromNode->GetLinks())
    {
        if (Link.TargetNodeId == ToNodeId)
        {
            return &Link;
        }
    }

    const FString& FromNodeId = FromNode->GetNodeId();
    for (const FCombatCoverLink& Link : ToNode->GetLinks())
    {
        if (Link.TargetNodeId == FromNodeId)
        {
            bOutReverse = true;
            return &Link;
        }
    }

    return nullptr;
}

UCombatCoverAgentComponent* UCombatFlowManagerComponent::FindBestTargetFor(UCombatCoverAgentComponent* Agent) const
{
    if (!IsValidCombatAgent(Agent))
    {
        return nullptr;
    }

    UCombatCoverAgentComponent* BestTarget = nullptr;
    float BestScore = 0.0f;
    bool bHasBest = false;
    const FVector AgentLocation = Agent->GetOwner()->GetActorLocation();

    for (UCombatCoverAgentComponent* Candidate : CachedAgents)
    {
        if (!IsValidCombatAgent(Candidate) || Candidate == Agent)
        {
            continue;
        }

        if (Candidate->GetTeamTag() == Agent->GetTeamTag())
        {
            continue;
        }

        if (!CanEngage(Agent, Candidate))
        {
            continue;
        }

        if (!CanAgentBeTargetedInCurrentSlot(Candidate))
        {
            continue;
        }

        const float Distance = Dist2D(AgentLocation, Candidate->GetOwner()->GetActorLocation());
        const float TargetPriorityMultiplier = (std::min)((std::max)(0.05f, GetTargetPriorityMultiplierForAgent(Candidate)), 1.0f);
        const float HealthPressure = 0.75f + (std::min)((std::max)(0.0f, Candidate->GetHealthRatio()), 1.0f) * 0.25f;
        const float ThreatPressure = Candidate->IsEngaging() ? 0.85f : 1.0f;
        const float IncomingFirePressure = Candidate->GetIncomingFireCount() > 0 ? 0.9f : 1.0f;
        const float Score = (Distance * HealthPressure * ThreatPressure * IncomingFirePressure) / TargetPriorityMultiplier;
        if (!bHasBest || Score < BestScore)
        {
            BestScore = Score;
            BestTarget = Candidate;
            bHasBest = true;
        }
    }

    return BestTarget;
}

bool UCombatFlowManagerComponent::CanEngage(const UCombatCoverAgentComponent* Shooter, const UCombatCoverAgentComponent* Target) const
{
    if (!IsValidCombatAgent(Shooter) || !IsValidCombatAgent(Target))
    {
        return false;
    }

    if (!CanAgentAttackFromCurrentSlot(Shooter))
    {
        return false;
    }

    const float Range = Shooter->GetEffectiveFireRange();
    if (Range <= 0.0f)
    {
        return false;
    }

    return Dist2D(Shooter->GetOwner()->GetActorLocation(), Target->GetOwner()->GetActorLocation()) <= Range;
}

void UCombatFlowManagerComponent::UpdateCombatSimulation(float DeltaTime)
{
    if (DeltaTime <= 0.0f)
    {
        return;
    }

    RefreshRegistry();

    TMap<UCombatCoverAgentComponent*, int32> IncomingFireCounts;
    TMap<UCombatCoverAgentComponent*, float> IncomingAttackDamage;

    for (auto It = SuppressionStateByAgent.begin(); It != SuppressionStateByAgent.end(); )
    {
        UCombatCoverAgentComponent* Agent = It->first;
        FCombatSuppressionRuntimeState& SuppressionState = It->second;
        SuppressionState.TimeRemaining -= DeltaTime;
        if (!IsValidCombatAgent(Agent) || SuppressionState.TimeRemaining <= 0.0f)
        {
            It = SuppressionStateByAgent.erase(It);
            continue;
        }
        ++It;
    }

    for (UCombatCoverAgentComponent* Agent : CachedAgents)
    {
        if (!IsValid(Agent))
        {
            continue;
        }
        Agent->SetIncomingFireStats(0, 0.0f);
    }

    for (UCombatCoverAgentComponent* Agent : CachedAgents)
    {
        if (!IsValidCombatAgent(Agent))
        {
            continue;
        }

        if (Agent->IsHoldingCoverForCombat())
        {
            Agent->ClearEngagementTarget();
            AttackStateByAgent.erase(Agent);
            continue;
        }

        if (!Agent->IsSuppressed() && !Agent->IsMovingForCombatRange() && Agent->CanMakeCombatDecision() && !Agent->GetCurrentNodeId().empty() &&
            Agent->GetHealthRatio() <= Agent->GetLowHealthFullCoverRatio() &&
            !IsAgentInSlotType(Agent, ECombatCoverSlotType::FullCover))
        {
            Agent->MarkCombatDecisionMade();
            if (RandomChance(Agent->GetLowHealthFullCoverChance()))
            {
                Agent->ClearEngagementTarget();
                if (TryMoveToFullCoverInCurrentNode(Agent))
                {
                    AttackStateByAgent.erase(Agent);
                    continue;
                }
            }
        }

        UCombatCoverAgentComponent* Target = FindBestTargetFor(Agent);
        if (!Target)
        {
            Agent->ClearEngagementTarget();
            AttackStateByAgent.erase(Agent);
            continue;
        }

        if (!Agent->CanFireWhileMoving() && Agent->IsMovingForCombatRange() &&
            !Agent->GetTargetNodeId().empty() && Agent->GetTargetSlotId() >= 0)
        {
            Agent->ClearEngagementTarget();
            AttackStateByAgent.erase(Agent);
            continue;
        }

        if (!Agent->IsSuppressed() && Agent->CanMakeCombatDecision())
        {
            const float TakeCoverChance = (std::min)((std::max)(0.0f, Agent->GetTakeCoverChanceWhenInRange()), 1.0f);
            const float RepositionChance = (std::min)((std::max)(0.0f, Agent->GetRepositionChanceWhenInRange()), 1.0f);
            const float TotalDecisionChance = (std::min)(1.0f, TakeCoverChance + RepositionChance);

            if (TotalDecisionChance > 0.0f)
            {
                Agent->MarkCombatDecisionMade();

                const float Roll = std::uniform_real_distribution<float>(0.0f, 1.0f)(GetCombatRandomGenerator());
                if (Roll < TakeCoverChance)
                {
                    Agent->ClearEngagementTarget();
                    Agent->EnterCombatCoverHold(PickCoverHoldDuration(Agent));
                    AttackStateByAgent.erase(Agent);
                    continue;
                }

                if (Roll < TotalDecisionChance)
                {
                    Agent->ClearEngagementTarget();
                    if (TryRepositionNearby(Agent))
                    {
                        AttackStateByAgent.erase(Agent);
                        continue;
                    }
                }
            }
        }

        Agent->SetEngagementTarget(Target);

        FCombatAttackRuntimeState& AttackState = AttackStateByAgent[Agent];
        if (AttackState.Target.Get() != Target)
        {
            AttackState.Target.Reset(Target);
            AttackState.TimeUntilNextAttack = PickAttackInterval(Agent);
        }

        AttackState.TimeUntilNextAttack -= DeltaTime;
        if (AttackState.TimeUntilNextAttack > 0.0f)
        {
            continue;
        }

        const float Damage = Agent->GetAttackDamage();
        Target->ApplyDamage(Damage);
        IncomingFireCounts[Target] += 1;
        IncomingAttackDamage[Target] += Damage;

        if (Target->IsAlive())
        {
            FCombatSuppressionRuntimeState& SuppressionState = SuppressionStateByAgent[Target];
            SuppressionState.IncomingHitCount += 1;
            SuppressionState.TimeRemaining = (std::max)(0.01f, SuppressionAccumulationWindow);
        }

        if (bDrawFireDebugLines)
        {
            DrawFireDebugLine(Agent, Target, 0.12f);
        }

        AttackState.TimeUntilNextAttack = PickAttackInterval(Agent);
    }

    for (const auto& Pair : IncomingAttackDamage)
    {
        UCombatCoverAgentComponent* Target = Pair.first;
        if (!IsValid(Target))
        {
            continue;
        }

        const int32 IncomingCount = IncomingFireCounts[Target];
        Target->SetIncomingFireStats(IncomingCount, Pair.second);
    }

    {
        for (auto It = SuppressionStateByAgent.begin(); It != SuppressionStateByAgent.end(); )
        {
            UCombatCoverAgentComponent* Target = It->first;
            FCombatSuppressionRuntimeState& SuppressionState = It->second;
            if (!IsValidCombatAgent(Target))
            {
                It = SuppressionStateByAgent.erase(It);
                continue;
            }

            if (SuppressionState.IncomingHitCount >= (std::max)(1, SuppressionIncomingFireThreshold))
            {
                Target->ApplySuppression(SuppressionDuration);
                It = SuppressionStateByAgent.erase(It);
                continue;
            }

            ++It;
        }
    }

    DrawCombatDebugVisuals();
}

void UCombatFlowManagerComponent::DrawCombatDebugVisuals(float Duration) const
{
    if (bDrawFireRanges)
    {
        DrawFireRanges(Duration);
    }

    if (bDrawAllNodeDebugVisuals)
    {
        UWorld* World = GetWorld();
        if (World)
        {
            FScene& Scene = World->GetScene();
            for (UCombatCoverNodeComponent* Node : CachedNodes)
            {
                if (Node && Node->GetOwner())
                {
                    Node->DrawDebugVisuals(Scene, false);
                }
            }
        }
    }
}

void UCombatFlowManagerComponent::DrawFireDebugLine(UCombatCoverAgentComponent* Shooter, UCombatCoverAgentComponent* Target, float Duration) const
{
    UWorld* World = GetWorld();
    if (!World || !IsValidCombatAgent(Shooter) || !IsValidCombatAgent(Target))
    {
        return;
    }

    const FVector Start = Shooter->GetOwner()->GetActorLocation();
    std::uniform_real_distribution<float> HorizontalOffset(-0.2f, 0.2f);
    std::uniform_real_distribution<float> VerticalOffset(-0.1f, 0.1f);
    const FVector TargetOffset(
        HorizontalOffset(GetCombatRandomGenerator()),
        HorizontalOffset(GetCombatRandomGenerator()),
        VerticalOffset(GetCombatRandomGenerator()));
    const FVector End = Target->GetOwner()->GetActorLocation() + TargetOffset;
    DrawDebugLine(World, Start, End, MakeFireLineColor(Shooter), Duration);
}

void UCombatFlowManagerComponent::DrawFireRanges(float Duration) const
{
    UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }

    for (UCombatCoverAgentComponent* Agent : CachedAgents)
    {
        if (!IsValidCombatAgent(Agent))
        {
            continue;
        }

        const FVector Center = Agent->GetOwner()->GetActorLocation() + FVector(0.0f, 0.0f, 8.0f);
        DrawDebugCircleXY(World, Center, Agent->GetEffectiveFireRange(), MakeFireLineColor(Agent), Duration);
    }
}

float UCombatFlowManagerComponent::PickAttackInterval(const UCombatCoverAgentComponent* Agent) const
{
    if (!Agent)
    {
        return 1.0f;
    }

    const float MinInterval = (std::max)(0.0f, Agent->GetAttackIntervalMin());
    const float MaxInterval = (std::max)(MinInterval, Agent->GetAttackIntervalMax());
    float Interval = MinInterval;
    if (MaxInterval > MinInterval)
    {
        Interval = std::uniform_real_distribution<float>(MinInterval, MaxInterval)(GetCombatRandomGenerator());
    }

    if (Agent->IsSuppressed())
    {
        const float SpeedMultiplier = (std::max)(0.01f, Agent->GetSuppressedAttackSpeedMultiplier());
        Interval /= SpeedMultiplier;
    }

    return Interval;
}

float UCombatFlowManagerComponent::PickCoverHoldDuration(const UCombatCoverAgentComponent* Agent) const
{
    if (!Agent)
    {
        return 1.5f;
    }

    const float MinDuration = (std::max)(0.0f, Agent->GetTakeCoverDurationMin());
    const float MaxDuration = (std::max)(MinDuration, Agent->GetTakeCoverDurationMax());
    if (MaxDuration <= MinDuration)
    {
        return MinDuration;
    }

    return std::uniform_real_distribution<float>(MinDuration, MaxDuration)(GetCombatRandomGenerator());
}

void UCombatFlowManagerComponent::EnsureRuntimeSlotsForNode(UCombatCoverNodeComponent* Node)
{
    if (!Node || Node->GetNodeId().empty())
    {
        return;
    }

    FCombatNodeRuntimeState& NodeState = RuntimeStateByNodeId[Node->GetNodeId()];
    TSet<int32> ValidSlotIds;
    for (const FCombatCoverSlot& Slot : Node->GetSlots())
    {
        ValidSlotIds.insert(Slot.SlotId);
        NodeState.Slots[Slot.SlotId];
    }

    for (auto It = NodeState.Slots.begin(); It != NodeState.Slots.end(); )
    {
        if (ValidSlotIds.find(It->first) == ValidSlotIds.end())
        {
            It = NodeState.Slots.erase(It);
            continue;
        }
        ++It;
    }
}

void UCombatFlowManagerComponent::RemoveStaleRuntimeState()
{
    TSet<FString> ValidNodeIds;
    for (UCombatCoverNodeComponent* Node : CachedNodes)
    {
        if (Node && !Node->GetNodeId().empty())
        {
            ValidNodeIds.insert(Node->GetNodeId());
        }
    }

    for (auto It = RuntimeStateByNodeId.begin(); It != RuntimeStateByNodeId.end(); )
    {
        if (ValidNodeIds.find(It->first) == ValidNodeIds.end())
        {
            It = RuntimeStateByNodeId.erase(It);
            continue;
        }
        ++It;
    }
}

void UCombatFlowManagerComponent::RemoveStaleAttackState()
{
    TSet<UCombatCoverAgentComponent*> ValidAgents;
    for (UCombatCoverAgentComponent* Agent : CachedAgents)
    {
        if (IsValidCombatAgent(Agent))
        {
            ValidAgents.insert(Agent);
        }
    }

    for (auto It = AttackStateByAgent.begin(); It != AttackStateByAgent.end(); )
    {
        if (ValidAgents.find(It->first) == ValidAgents.end())
        {
            It = AttackStateByAgent.erase(It);
            continue;
        }
        ++It;
    }

    for (auto It = SuppressionStateByAgent.begin(); It != SuppressionStateByAgent.end(); )
    {
        if (ValidAgents.find(It->first) == ValidAgents.end())
        {
            It = SuppressionStateByAgent.erase(It);
            continue;
        }
        ++It;
    }
}

void UCombatFlowManagerComponent::RemoveInvalidOrDeadRuntimeClaims()
{
    for (auto& NodePair : RuntimeStateByNodeId)
    {
        for (auto& SlotPair : NodePair.second.Slots)
        {
            FCombatSlotRuntimeState& SlotState = SlotPair.second;

            UCombatCoverAgentComponent* ReservedBy = SlotState.ReservedBy.Get();
            if (!IsValid(ReservedBy) || !ReservedBy->IsAlive())
            {
                SlotState.ReservedBy.Reset();
            }

            UCombatCoverAgentComponent* OccupiedBy = SlotState.OccupiedBy.Get();
            if (!IsValid(OccupiedBy) || !OccupiedBy->IsAlive())
            {
                SlotState.OccupiedBy.Reset();
            }
        }
    }
}

void UCombatFlowManagerComponent::RemoveInvalidOrDeadLinkLaneClaims()
{
    for (auto LinkIt = RuntimeStateByLinkKey.begin(); LinkIt != RuntimeStateByLinkKey.end(); )
    {
        FCombatLinkLaneRuntimeState& LinkState = LinkIt->second;
        for (auto LaneIt = LinkState.ReservedByLane.begin(); LaneIt != LinkState.ReservedByLane.end(); )
        {
            UCombatCoverAgentComponent* Agent = LaneIt->second.Get();
            if (!IsValid(Agent) || !Agent->IsAlive())
            {
                LaneIt = LinkState.ReservedByLane.erase(LaneIt);
                continue;
            }
            ++LaneIt;
        }

        if (LinkState.ReservedByLane.empty())
        {
            LinkIt = RuntimeStateByLinkKey.erase(LinkIt);
            continue;
        }
        ++LinkIt;
    }
}

void UCombatFlowManagerComponent::AddValidationMessage(FCombatCoverGraphValidationResult& Result, bool bError, const FString& Message) const
{
    if (bError)
    {
        ++Result.ErrorCount;
        Result.Messages.push_back(FString("ERROR: ") + Message);
    }
    else
    {
        ++Result.WarningCount;
        Result.Messages.push_back(FString("WARN: ") + Message);
    }
}

void UCombatFlowManagerComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction)
{
    UActorComponent::TickComponent(DeltaTime, TickType, ThisTickFunction);

    UpdateCombatSimulation(DeltaTime);

    if (!bDrawDebugDuringTick)
    {
        return;
    }

    DebugDrawTimer += DeltaTime;
    if (DebugDrawTimer < DebugDrawInterval)
    {
        return;
    }

    DebugDrawTimer = 0.0f;
    RefreshRegistry();
    DrawAllDebugVisuals(true);
}
