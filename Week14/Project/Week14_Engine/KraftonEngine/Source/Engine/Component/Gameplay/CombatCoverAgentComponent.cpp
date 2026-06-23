#include "CombatCoverAgentComponent.h"

#include "Component/Gameplay/CombatFlowManagerComponent.h"
#include "Component/Gameplay/SniperTypes.h"
#include "Core/Logging/Log.h"
#include "GameFramework/AActor.h"
#include "GameFramework/Pawn/Character.h"
#include "Component/Movement/CharacterMovementComponent.h"

#include <algorithm>
#include <cmath>
#include <random>

namespace
{
	//여기입니다! 일어섰다가 움직일 때 잠깐 멈추는 시간이 여기입니다!
    constexpr float LinkedMoveStartDelaySeconds = 1.2f;

    float Distance2D(const FVector& A, const FVector& B)
    {
        const float DX = A.X - B.X;
        const float DY = A.Y - B.Y;
        return sqrtf(DX * DX + DY * DY);
    }

    float NormalizeYawDelta(float DeltaYaw)
    {
        while (DeltaYaw > 180.0f)
        {
            DeltaYaw -= 360.0f;
        }
        while (DeltaYaw < -180.0f)
        {
            DeltaYaw += 360.0f;
        }
        return DeltaYaw;
    }

    std::mt19937& GetAgentRandomGenerator()
    {
        static std::mt19937 Generator{ std::random_device{}() };
        return Generator;
    }

    float RandomFloatInRange(float MinValue, float MaxValue)
    {
        MinValue = (std::max)(0.0f, MinValue);
        MaxValue = (std::max)(MinValue, MaxValue);
        if (MaxValue <= MinValue)
        {
            return MinValue;
        }
        return std::uniform_real_distribution<float>(MinValue, MaxValue)(GetAgentRandomGenerator());
    }
}

UCombatCoverAgentComponent::UCombatCoverAgentComponent()
{
    PrimaryComponentTick.SetTickGroup(TG_PrePhysics);
    SetComponentTickEnabled(true);
}

void UCombatCoverAgentComponent::BeginPlay()
{
    UActorComponent::BeginPlay();
    State = ECombatCoverAgentState::Idle;
    CurrentNodeId.clear();
    CurrentSlotId = -1;
    TargetNodeId.clear();
    TargetSlotId = -1;
    CurrentMovePath.clear();
    CurrentMovePathIndex = 0;
    FinalReservedSlot.Reset();
    ApplyCombatRoleDefaults();
    ClampRuntimeEditableValues();
    PickRandomAdvanceInterval();
    AdvanceTimer = 0.0f;
    RetryTimer = 0.0f;
    TargetScanTimer = 0.0f;
    IncomingFireCount = 0;
    IncomingAttackDamage = 0.0f;
    SuppressionTimer = 0.0f;
    HitReactionTimer = 0.0f;
    bHitReactionPending = false;
    CombatDecisionCooldownRemaining = 0.0f;
    CoverHoldTimer = 0.0f;
    StateBeforeEngage = ECombatCoverAgentState::Idle;
    StateBeforeSuppressed = ECombatCoverAgentState::Idle;
    CurrentTarget.Reset();
    ResolveManager();
}

void UCombatCoverAgentComponent::EndPlay()
{
    if (UCombatFlowManagerComponent* Manager = ResolveManager())
    {
        Manager->ReleaseAgent(this);
    }
    CachedManager.Reset();
    UActorComponent::EndPlay();
}

void UCombatCoverAgentComponent::PostEditProperty(const char* PropertyName)
{
    UActorComponent::PostEditProperty(PropertyName);

    (void)PropertyName;
    ApplyCombatRoleDefaults();
    ClampRuntimeEditableValues();
}

void UCombatCoverAgentComponent::SetTeamTag(const FString& InTeamTag)
{
    TeamTag = InTeamTag.empty() ? FString("Enemy") : InTeamTag;
}

FString UCombatCoverAgentComponent::GetDisplayName() const
{
    if (!AgentDisplayName.empty())
    {
        return AgentDisplayName;
    }

    if (const AActor* Owner = GetOwner())
    {
        return Owner->GetFName().ToString();
    }

    return "Combat Agent";
}

void UCombatCoverAgentComponent::SetDisplayName(const FString& InDisplayName)
{
    AgentDisplayName = InDisplayName;
}

void UCombatCoverAgentComponent::RecordSniperHit(const FSniperHitInfo& HitInfo)
{
    bHasLastSniperHit = true;
    LastHitBoneName = HitInfo.HitBoneName;
    LastHitBodyName = !HitInfo.HitBodyName.empty()
        ? HitInfo.HitBodyName
        : (HitInfo.HitBoneName.IsValid() && HitInfo.HitBoneName != FName::None ? HitInfo.HitBoneName.ToString() : FString());
    LastHitRegionName = !HitInfo.HitRegionName.empty()
        ? HitInfo.HitRegionName
        : FString(GetSniperHitRegionName(HitInfo.HitRegion));
    LastHitRegionDisplayName = !HitInfo.HitRegionDisplayName.empty()
        ? HitInfo.HitRegionDisplayName
        : FString(GetSniperHitRegionDisplayName(HitInfo.HitRegion));
    LastHitDamage = HitInfo.Damage;
    LastHitScoreMultiplier = HitInfo.HitScoreMultiplier;
    LastHitScoreValue = HitInfo.HitScoreValue;
    bLastHitKilled = HitInfo.bKilled;

    if (!HitInfo.bKilled && IsAlive())
    {
        QueueHitReaction();
    }
}

void UCombatCoverAgentComponent::RequestInitialSlot()
{
    if (State == ECombatCoverAgentState::Dead || State == ECombatCoverAgentState::Suppressed || (State == ECombatCoverAgentState::Engaging && !bCanFireWhileMoving))
    {
        return;
    }

    UCombatFlowManagerComponent* Manager = ResolveManager();
    if (!Manager || !Manager->AssignInitialSlot(this))
    {
        SetBlocked();
    }
}

void UCombatCoverAgentComponent::RequestAdvance()
{
    if (State == ECombatCoverAgentState::Dead || State == ECombatCoverAgentState::Suppressed || (State == ECombatCoverAgentState::Engaging && !bCanFireWhileMoving))
    {
        return;
    }

    UCombatFlowManagerComponent* Manager = ResolveManager();
    if (!Manager || !Manager->TryAdvance(this))
    {
        if (!CurrentNodeId.empty())
        {
            CurrentTarget.Reset();
            TargetNodeId.clear();
            TargetSlotId = -1;
            CurrentMovePath.clear();
            CurrentMovePathIndex = 0;
            FinalReservedSlot.Reset();
            LinkedMoveStartDelayRemaining = 0.0f;
            AdvanceTimer = (std::max)(0.0f, AdvanceInterval - RetryInterval);
            RetryTimer = 0.0f;
            State = ECombatCoverAgentState::InCover;
            return;
        }

        SetBlocked();
    }
}

void UCombatCoverAgentComponent::MoveToReservedSlot(const FCombatMovePath& MovePath, bool bInitialMove)
{
    if (!MovePath.IsValid())
    {
        SetBlocked();
        return;
    }

    FinalReservedSlot = MovePath.FinalSlot;
    TargetNodeId = MovePath.FinalSlot.NodeId;
    TargetSlotId = MovePath.FinalSlot.SlotId;
    CurrentMovePath = MovePath.Points;
    CurrentMovePathIndex = 0;
    LinkedMoveStartDelayRemaining = bInitialMove ? 0.0f : LinkedMoveStartDelaySeconds;
    CoverHoldTimer = 0.0f;
    bMoveToCombatSlotAfterCoverHold = false;
    AdvanceTimer = 0.0f;
    RetryTimer = 0.0f;
    State = bInitialMove ? ECombatCoverAgentState::MovingToInitialSlot : ECombatCoverAgentState::MovingToLinkedNode;
}

void UCombatCoverAgentComponent::MarkDead()
{
    if (State == ECombatCoverAgentState::Dead)
    {
        if (UCombatFlowManagerComponent* Manager = ResolveManager())
        {
            Manager->ReleaseAgent(this);
        }
        return;
    }

    CurrentTarget.Reset();
    IncomingFireCount = 0;
    IncomingAttackDamage = 0.0f;
    SuppressionTimer = 0.0f;
    HitReactionTimer = 0.0f;
    bHitReactionPending = false;
    CoverHoldTimer = 0.0f;
    bMoveToCombatSlotAfterCoverHold = false;
    Health = 0.0f;

    if (UCombatFlowManagerComponent* Manager = ResolveManager())
    {
        Manager->ReleaseAgent(this);
    }

    State = ECombatCoverAgentState::Dead;
    StateBeforeEngage = ECombatCoverAgentState::Dead;
    StateBeforeSuppressed = ECombatCoverAgentState::Dead;
    CurrentNodeId.clear();
    CurrentSlotId = -1;
    TargetNodeId.clear();
    TargetSlotId = -1;
    CurrentMovePath.clear();
    CurrentMovePathIndex = 0;
    LinkedMoveStartDelayRemaining = 0.0f;
    FinalReservedSlot.Reset();
}


ECombatAgentRole UCombatCoverAgentComponent::GetResolvedCombatRole() const
{
    if (CombatRole != ECombatAgentRole::AutoFromTeam)
    {
        return CombatRole;
    }

    if (TeamTag.find("Ally") != FString::npos)
    {
        return ECombatAgentRole::Ally;
    }

    return ECombatAgentRole::EnemyShortRange;
}

void UCombatCoverAgentComponent::ApplyCombatRoleDefaults()
{
    if (!bUseRoleCombatDefaults)
    {
        return;
    }

    switch (GetResolvedCombatRole())
    {
    case ECombatAgentRole::Ally:
        TeamTag = "Ally";
        AdvanceLinkMode = ECombatAdvanceLinkMode::OutgoingLinks;
        FireRange = 50.0f;
        MovingFireRange = 30.0f;
        AttackDamage = 5.0f;
        AttackIntervalMin = 1.0f;
        AttackIntervalMax = 2.0f;
        AdvanceIntervalMin = 80.0f;
        AdvanceIntervalMax = 120.0f;
        RepositionChanceWhenInRange = 0.0f;
        CombatDecisionCooldown = 3.0f;
        TakeCoverChanceWhenInRange = 0.0f;
        TakeCoverDurationMin = 1.0f;
        TakeCoverDurationMax = 2.0f;
        InCoverTargetPriorityMultiplier = 0.45f;
        AdvanceFullCoverChance = 0.45f;
        FullCoverToCombatDelayMin = 1.0f;
        FullCoverToCombatDelayMax = 2.0f;
        LowHealthFullCoverRatio = 0.35f;
        LowHealthFullCoverChance = 0.80f;
        break;

    case ECombatAgentRole::EnemyLongRangeSlow:
        TeamTag = "Enemy";
        AdvanceLinkMode = ECombatAdvanceLinkMode::IncomingLinks;
        FireRange = 80.0f;
        MovingFireRange = 30.0f;
        AttackDamage = 7.0f;
        AttackIntervalMin = 4.5f;
        AttackIntervalMax = 6.5f;
        AdvanceIntervalMin = 12.0f;
        AdvanceIntervalMax = 16.0f;
        RepositionChanceWhenInRange = 0.01f;
        CombatDecisionCooldown = 6.0f;
        TakeCoverChanceWhenInRange = 0.50f;
        TakeCoverDurationMin = 2.0f;
        TakeCoverDurationMax = 4.0f;
        InCoverTargetPriorityMultiplier = 0.30f;
        AdvanceFullCoverChance = 0.85f;
        FullCoverToCombatDelayMin = 1.0f;
        FullCoverToCombatDelayMax = 2.0f;
        LowHealthFullCoverRatio = 0.35f;
        LowHealthFullCoverChance = 0.75f;
        break;

    case ECombatAgentRole::EnemyShortRange:
    case ECombatAgentRole::AutoFromTeam:
    default:
        TeamTag = "Enemy";
        AdvanceLinkMode = ECombatAdvanceLinkMode::IncomingLinks;
        FireRange = 35.0f;
        MovingFireRange = 25.0f;
        AttackDamage = 5.0f;
        AttackIntervalMin = 0.8f;
        AttackIntervalMax = 1.4f;
        AdvanceIntervalMin = 6.0f;
        AdvanceIntervalMax = 10.0f;
        RepositionChanceWhenInRange = 0.02f;
        CombatDecisionCooldown = 5.0f;
        TakeCoverChanceWhenInRange = 0.35f;
        TakeCoverDurationMin = 1.5f;
        TakeCoverDurationMax = 3.0f;
        InCoverTargetPriorityMultiplier = 0.35f;
        AdvanceFullCoverChance = 0.85f;
        FullCoverToCombatDelayMin = 0.8f;
        FullCoverToCombatDelayMax = 1.6f;
        LowHealthFullCoverRatio = 0.35f;
        LowHealthFullCoverChance = 0.75f;
        break;

    case ECombatAgentRole::EnemyAssault:
        TeamTag = "Enemy";
        AdvanceLinkMode = ECombatAdvanceLinkMode::IncomingLinks;
        FireRange = 18.0f;
        MovingFireRange = 12.0f;
        AttackDamage = 4.0f;
        AttackIntervalMin = 0.7f;
        AttackIntervalMax = 1.1f;
        AdvanceIntervalMin = 3.5f;
        AdvanceIntervalMax = 4.5f;
        RepositionChanceWhenInRange = 0.08f;
        CombatDecisionCooldown = 2.5f;
        TakeCoverChanceWhenInRange = 0.10f;
        TakeCoverDurationMin = 0.5f;
        TakeCoverDurationMax = 1.0f;
        InCoverTargetPriorityMultiplier = 0.55f;
        AdvanceFullCoverChance = 0.20f;
        FullCoverToCombatDelayMin = 0.2f;
        FullCoverToCombatDelayMax = 0.5f;
        LowHealthFullCoverRatio = 0.25f;
        LowHealthFullCoverChance = 0.35f;
        break;
    }

    bUseMovingFireRange = true;
    bCanFireWhileMoving = false;
}

float UCombatCoverAgentComponent::GetHealthRatio() const
{
    if (MaxHealth <= 0.0f)
    {
        return 0.0f;
    }

    return (std::min)((std::max)(Health / MaxHealth, 0.0f), 1.0f);
}

void UCombatCoverAgentComponent::SetMaxHealth(float InMaxHealth)
{
    MaxHealth = (std::max)(1.0f, InMaxHealth);
    Health = (std::min)((std::max)(0.0f, Health), MaxHealth);
    if (Health <= 0.0f)
    {
        MarkDead();
    }
}

void UCombatCoverAgentComponent::SetHealth(float InHealth)
{
    Health = (std::min)((std::max)(0.0f, InHealth), MaxHealth);
    if (Health <= 0.0f)
    {
        MarkDead();
    }
}

void UCombatCoverAgentComponent::MarkCombatDecisionMade()
{
    CombatDecisionCooldownRemaining = (std::max)(0.0f, CombatDecisionCooldown);
}

void UCombatCoverAgentComponent::PickRandomAdvanceInterval()
{
    AdvanceInterval = RandomFloatInRange(AdvanceIntervalMin, AdvanceIntervalMax);
}

float UCombatCoverAgentComponent::PickFullCoverToCombatDelay() const
{
    return RandomFloatInRange(FullCoverToCombatDelayMin, FullCoverToCombatDelayMax);
}

void UCombatCoverAgentComponent::HandleArrivedAtCoverSlot(UCombatFlowManagerComponent* Manager)
{
    PickRandomAdvanceInterval();
    bMoveToCombatSlotAfterCoverHold = false;

    if (!Manager || CurrentNodeId.empty())
    {
        return;
    }

    if (Manager->IsAgentInSlotType(this, ECombatCoverSlotType::FullCover) && Manager->HasFreeCombatSlotInCurrentNode(this))
    {
        CoverHoldTimer = PickFullCoverToCombatDelay();
        bMoveToCombatSlotAfterCoverHold = true;
    }
}

void UCombatCoverAgentComponent::EnterCombatCoverHold(float Duration)
{
    if (State == ECombatCoverAgentState::Dead || State == ECombatCoverAgentState::Suppressed)
    {
        return;
    }

    CurrentTarget.Reset();
    bMoveToCombatSlotAfterCoverHold = false;
    AdvanceTimer = 0.0f;
    RetryTimer = 0.0f;
    LinkedMoveStartDelayRemaining = 0.0f;
    CoverHoldTimer = (std::max)(0.0f, Duration);

    if (!CurrentNodeId.empty())
    {
        TargetNodeId.clear();
        TargetSlotId = -1;
        CurrentMovePath.clear();
        CurrentMovePathIndex = 0;
        FinalReservedSlot.Reset();
        State = ECombatCoverAgentState::InCover;
    }
    else
    {
        CoverHoldTimer = 0.0f;
        State = ECombatCoverAgentState::Idle;
    }
}

void UCombatCoverAgentComponent::TickCombatDecisionCooldown(float DeltaTime)
{
    if (CombatDecisionCooldownRemaining <= 0.0f || DeltaTime <= 0.0f)
    {
        return;
    }

    CombatDecisionCooldownRemaining = (std::max)(0.0f, CombatDecisionCooldownRemaining - DeltaTime);
}

void UCombatCoverAgentComponent::ClampRuntimeEditableValues()
{
    MoveSpeed = (std::max)(0.0f, MoveSpeed);
    CrouchMoveSpeed = (std::max)(0.0f, CrouchMoveSpeed);
    RunMoveSpeed = (std::max)(0.0f, RunMoveSpeed);
    AssaultWalkBelowHealthRatio = (std::min)((std::max)(0.0f, AssaultWalkBelowHealthRatio), 1.0f);
    AcceptanceRadius = (std::max)(1.0f, AcceptanceRadius);
    AdvanceIntervalMin = (std::max)(0.0f, AdvanceIntervalMin);
    AdvanceIntervalMax = (std::max)(AdvanceIntervalMin, AdvanceIntervalMax);
    AdvanceInterval = (std::min)((std::max)(0.0f, AdvanceInterval), AdvanceIntervalMax);
    RetryInterval = (std::max)(0.1f, RetryInterval);
    MaxHealth = (std::max)(1.0f, MaxHealth);
    Health = (std::min)((std::max)(0.0f, Health), MaxHealth);
    FireRange = (std::max)(0.0f, FireRange);
    MovingFireRange = (std::max)(0.0f, MovingFireRange);
    FacingYawRate = (std::max)(0.0f, FacingYawRate);
    FacingYawOffset = NormalizeYawDelta(FacingYawOffset);
    AttackDamage = (std::max)(0.0f, AttackDamage);
    AttackIntervalMin = (std::max)(0.0f, AttackIntervalMin);
    AttackIntervalMax = (std::max)(AttackIntervalMin, AttackIntervalMax);
    TargetScanInterval = (std::max)(0.01f, TargetScanInterval);
    SuppressedAttackSpeedMultiplier = (std::min)((std::max)(0.01f, SuppressedAttackSpeedMultiplier), 1.0f);
    SuppressedMoveSpeedMultiplier = (std::min)((std::max)(0.01f, SuppressedMoveSpeedMultiplier), 1.0f);
    HitReactionDuration = (std::min)((std::max)(0.0f, HitReactionDuration), 10.0f);
    RepositionChanceWhenInRange = (std::min)((std::max)(0.0f, RepositionChanceWhenInRange), 1.0f);
    CombatDecisionCooldown = (std::max)(0.0f, CombatDecisionCooldown);
    TakeCoverChanceWhenInRange = (std::min)((std::max)(0.0f, TakeCoverChanceWhenInRange), 1.0f);
    TakeCoverDurationMin = (std::max)(0.0f, TakeCoverDurationMin);
    TakeCoverDurationMax = (std::max)(TakeCoverDurationMin, TakeCoverDurationMax);
    InCoverTargetPriorityMultiplier = (std::min)((std::max)(0.05f, InCoverTargetPriorityMultiplier), 1.0f);
    AdvanceFullCoverChance = (std::min)((std::max)(0.0f, AdvanceFullCoverChance), 1.0f);
    FullCoverToCombatDelayMin = (std::max)(0.0f, FullCoverToCombatDelayMin);
    FullCoverToCombatDelayMax = (std::max)(FullCoverToCombatDelayMin, FullCoverToCombatDelayMax);
    LowHealthFullCoverRatio = (std::min)((std::max)(0.0f, LowHealthFullCoverRatio), 1.0f);
    LowHealthFullCoverChance = (std::min)((std::max)(0.0f, LowHealthFullCoverChance), 1.0f);
    DeathDebugScaleMultiplier = (std::min)((std::max)(0.01f, DeathDebugScaleMultiplier), 1.0f);
}

bool UCombatCoverAgentComponent::IsMovingForCombatRange() const
{
    return State == ECombatCoverAgentState::MovingToInitialSlot ||
        State == ECombatCoverAgentState::MovingToLinkedNode;
}

float UCombatCoverAgentComponent::GetEffectiveFireRange() const
{
    if (bUseMovingFireRange && IsMovingForCombatRange())
    {
        return (std::max)(0.0f, MovingFireRange);
    }

    return (std::max)(0.0f, FireRange);
}

ECombatCoverSlotType UCombatCoverAgentComponent::GetCurrentSlotType() const
{
    UCombatFlowManagerComponent* Manager = const_cast<UCombatCoverAgentComponent*>(this)->ResolveManager();
    const FCombatCoverSlot* Slot = Manager ? Manager->FindCurrentSlot(this) : nullptr;
    return Slot ? Slot->SlotType : ECombatCoverSlotType::ExposedDummy;
}

bool UCombatCoverAgentComponent::IsInStandingCombatSlot() const
{
    return GetCurrentSlotType() == ECombatCoverSlotType::StandingCombatCover;
}

bool UCombatCoverAgentComponent::ShouldUseStandingFire() const
{
    return IsInStandingCombatSlot();
}

float UCombatCoverAgentComponent::GetCombatAnimationMoveState() const
{
    if (IsMovingForCombatRange())
    {
        return ShouldRunDuringCombatMovement() ? 2.0f : 1.5f;
    }

    if (ShouldUseStandingFire())
    {
        return 0.0f;
    }

    if (IsInCover() || IsEngaging())
    {
        return 1.0f;
    }

    return 0.0f;
}

bool UCombatCoverAgentComponent::ShouldRunDuringCombatMovement() const
{
    if (GetResolvedCombatRole() != ECombatAgentRole::EnemyAssault)
    {
        return false;
    }

    return GetHealthRatio() > AssaultWalkBelowHealthRatio;
}

float UCombatCoverAgentComponent::GetCurrentCombatMoveSpeed() const
{
    const float MoveState = GetCombatAnimationMoveState();
    const float BaseSpeed = MoveState >= 2.0f
        ? (std::max)(0.0f, RunMoveSpeed)
        : (MoveState >= 1.5f
            ? (std::max)(0.0f, MoveSpeed)
            : (std::max)(0.0f, CrouchMoveSpeed));

    if (IsSuppressed())
    {
        return BaseSpeed * (std::min)((std::max)(0.01f, SuppressedMoveSpeedMultiplier), 1.0f);
    }

    return BaseSpeed;
}

bool UCombatCoverAgentComponent::ConsumeHitReaction()
{
    if (State == ECombatCoverAgentState::Dead || !bHitReactionPending || HitReactionTimer <= 0.0f)
    {
        return false;
    }

    bHitReactionPending = false;
    return true;
}

const char* UCombatCoverAgentComponent::GetStateName() const
{
    switch (State)
    {
    case ECombatCoverAgentState::Idle: return "Idle";
    case ECombatCoverAgentState::MovingToInitialSlot: return "MovingToInitialSlot";
    case ECombatCoverAgentState::InCover: return "InCover";
    case ECombatCoverAgentState::MovingToLinkedNode: return "MovingToLinkedNode";
    case ECombatCoverAgentState::Engaging: return "Engaging";
    case ECombatCoverAgentState::Suppressed: return "Suppressed";
    case ECombatCoverAgentState::Blocked: return "Blocked";
    case ECombatCoverAgentState::Dead: return "Dead";
    default: return "Unknown";
    }
}

const char* UCombatCoverAgentComponent::GetAdvanceLinkModeName() const
{
    switch (AdvanceLinkMode)
    {
    case ECombatAdvanceLinkMode::OutgoingLinks: return "OutgoingLinks";
    case ECombatAdvanceLinkMode::IncomingLinks: return "IncomingLinks";
    case ECombatAdvanceLinkMode::Both: return "Both";
    default: return "Unknown";
    }
}


const char* UCombatCoverAgentComponent::GetCombatRoleName() const
{
    switch (CombatRole)
    {
    case ECombatAgentRole::AutoFromTeam: return "AutoFromTeam";
    case ECombatAgentRole::Ally: return "Ally";
    case ECombatAgentRole::EnemyShortRange: return "EnemyShortRange";
    case ECombatAgentRole::EnemyLongRangeSlow: return "EnemyLongRangeSlow";
    case ECombatAgentRole::EnemyAssault: return "EnemyAssault";
    default: return "Unknown";
    }
}

const char* UCombatCoverAgentComponent::GetResolvedCombatRoleName() const
{
    switch (GetResolvedCombatRole())
    {
    case ECombatAgentRole::Ally: return "Ally";
    case ECombatAgentRole::EnemyShortRange: return "EnemyShortRange";
    case ECombatAgentRole::EnemyLongRangeSlow: return "EnemyLongRangeSlow";
    case ECombatAgentRole::EnemyAssault: return "EnemyAssault";
    case ECombatAgentRole::AutoFromTeam: return "AutoFromTeam";
    default: return "Unknown";
    }
}

void UCombatCoverAgentComponent::SetEngagementTarget(UCombatCoverAgentComponent* Target)
{
    if (State == ECombatCoverAgentState::Dead)
    {
        return;
    }

    if (!IsValid(Target) || Target == this || !Target->IsAlive())
    {
        ClearEngagementTarget();
        return;
    }

    CurrentTarget.Reset(Target);

    if (State != ECombatCoverAgentState::Suppressed)
    {
        CoverHoldTimer = 0.0f;
    }

    if (State != ECombatCoverAgentState::Suppressed && !bCanFireWhileMoving && State != ECombatCoverAgentState::Engaging)
    {
        StateBeforeEngage = State;
        State = ECombatCoverAgentState::Engaging;
    }
}

void UCombatCoverAgentComponent::ClearEngagementTarget()
{
    const bool bHadTarget = CurrentTarget.Get() != nullptr;
    CurrentTarget.Reset();

    if (State != ECombatCoverAgentState::Engaging)
    {
        return;
    }

    switch (StateBeforeEngage)
    {
    case ECombatCoverAgentState::MovingToInitialSlot:
    case ECombatCoverAgentState::MovingToLinkedNode:
        if (!TargetNodeId.empty() && TargetSlotId >= 0)
        {
            State = StateBeforeEngage;
        }
        else
        {
            State = CurrentNodeId.empty() ? ECombatCoverAgentState::Idle : ECombatCoverAgentState::InCover;
        }
        break;

    case ECombatCoverAgentState::Suppressed:
        State = ECombatCoverAgentState::Suppressed;
        break;

    case ECombatCoverAgentState::Blocked:
        State = ECombatCoverAgentState::Blocked;
        break;

    case ECombatCoverAgentState::Dead:
        State = ECombatCoverAgentState::Dead;
        break;

    case ECombatCoverAgentState::Idle:
        State = CurrentNodeId.empty() ? ECombatCoverAgentState::Idle : ECombatCoverAgentState::InCover;
        break;

    case ECombatCoverAgentState::InCover:
    case ECombatCoverAgentState::Engaging:
    default:
        State = ECombatCoverAgentState::InCover;
        break;
    }

    if (bHadTarget)
    {
        AdvanceTimer = 0.0f;
        RetryTimer = 0.0f;
    }
    StateBeforeEngage = ECombatCoverAgentState::Idle;
}

void UCombatCoverAgentComponent::ApplyDamage(float Damage)
{
    if (State == ECombatCoverAgentState::Dead || Damage <= 0.0f)
    {
        return;
    }

    Health = (std::max)(0.0f, Health - Damage);
    if (Health <= 0.0f)
    {
        MarkDead();
        return;
    }
}


void UCombatCoverAgentComponent::ApplySuppression(float Duration)
{
    if (State == ECombatCoverAgentState::Dead || Duration <= 0.0f)
    {
        return;
    }

    SuppressionTimer = (std::max)(SuppressionTimer, Duration);
}

void UCombatCoverAgentComponent::FinishSuppression()
{
    const bool bWasSuppressedState = State == ECombatCoverAgentState::Suppressed;
    SuppressionTimer = 0.0f;

    if (!bWasSuppressedState)
    {
        StateBeforeSuppressed = ECombatCoverAgentState::Idle;
        return;
    }

    switch (StateBeforeSuppressed)
    {
    case ECombatCoverAgentState::MovingToInitialSlot:
    case ECombatCoverAgentState::MovingToLinkedNode:
        if (!TargetNodeId.empty() && TargetSlotId >= 0)
        {
            State = StateBeforeSuppressed;
        }
        else
        {
            State = CurrentNodeId.empty() ? ECombatCoverAgentState::Idle : ECombatCoverAgentState::InCover;
        }
        break;

    case ECombatCoverAgentState::Blocked:
        State = ECombatCoverAgentState::Blocked;
        break;

    case ECombatCoverAgentState::Dead:
        State = ECombatCoverAgentState::Dead;
        break;

    case ECombatCoverAgentState::Idle:
        State = CurrentNodeId.empty() ? ECombatCoverAgentState::Idle : ECombatCoverAgentState::InCover;
        break;

    case ECombatCoverAgentState::Engaging:
        State = CurrentTarget.Get() && CurrentTarget.Get()->IsAlive()
            ? ECombatCoverAgentState::Engaging
            : (CurrentNodeId.empty() ? ECombatCoverAgentState::Idle : ECombatCoverAgentState::InCover);
        break;

    case ECombatCoverAgentState::Suppressed:
    case ECombatCoverAgentState::InCover:
    default:
        State = CurrentNodeId.empty() ? ECombatCoverAgentState::Idle : ECombatCoverAgentState::InCover;
        break;
    }

    StateBeforeSuppressed = ECombatCoverAgentState::Idle;
}

void UCombatCoverAgentComponent::SetIncomingFireStats(int32 Count, float AttackDamage)
{
    IncomingFireCount = (std::max)(0, Count);
    IncomingAttackDamage = (std::max)(0.0f, AttackDamage);
}


UCombatFlowManagerComponent* UCombatCoverAgentComponent::ResolveManager()
{
    if (UCombatFlowManagerComponent* Manager = CachedManager.Get())
    {
        return Manager;
    }

    UCombatFlowManagerComponent* FoundManager = UCombatFlowManagerComponent::FindInWorld(GetWorld());
    if (FoundManager)
    {
        CachedManager.Reset(FoundManager);
    }
    return FoundManager;
}

void UCombatCoverAgentComponent::QueueHitReaction()
{
    if (State == ECombatCoverAgentState::Dead || HitReactionDuration <= 0.0f)
    {
        return;
    }

    HitReactionTimer = (std::max)(HitReactionTimer, HitReactionDuration);
    bHitReactionPending = true;
}

bool UCombatCoverAgentComponent::IsHitReactionMoveLocked() const
{
    return State != ECombatCoverAgentState::Dead && HitReactionTimer > 0.0f;
}

void UCombatCoverAgentComponent::TickMoveToTarget(float DeltaTime)
{
    if (IsHitReactionMoveLocked())
    {
        return;
    }

    AActor* Owner = GetOwner();
    UCombatFlowManagerComponent* Manager = ResolveManager();
    if (!Owner || !Manager)
    {
        SetBlocked();
        return;
    }

    UCombatCoverNodeComponent* TargetNode = Manager->FindNode(TargetNodeId);
    if (!TargetNode)
    {
        SetBlocked();
        return;
    }

    const int32 SlotIndex = TargetNode->FindSlotIndexById(TargetSlotId);
    if (SlotIndex < 0)
    {
        SetBlocked();
        return;
    }

    if (CurrentMovePath.empty())
    {
        CurrentMovePath.push_back(TargetNode->GetSlotWorldPosition(SlotIndex));
        CurrentMovePathIndex = 0;
    }

    if (CurrentMovePathIndex < 0)
    {
        CurrentMovePathIndex = 0;
    }

    if (State == ECombatCoverAgentState::MovingToLinkedNode && LinkedMoveStartDelayRemaining > 0.0f)
    {
        LinkedMoveStartDelayRemaining = (std::max)(0.0f, LinkedMoveStartDelayRemaining - DeltaTime);
        return;
    }

    auto FinishMove = [this, Manager]()
    {
        FCombatCoverSlotHandle ArrivedSlot = FinalReservedSlot;
        if (!ArrivedSlot.IsValid())
        {
            ArrivedSlot.NodeId = TargetNodeId;
            ArrivedSlot.SlotId = TargetSlotId;
        }

        Manager->ConfirmArrived(this, ArrivedSlot);

        CurrentNodeId = ArrivedSlot.NodeId;
        CurrentSlotId = ArrivedSlot.SlotId;
        TargetNodeId.clear();
        TargetSlotId = -1;
        CurrentMovePath.clear();
        CurrentMovePathIndex = 0;
        LinkedMoveStartDelayRemaining = 0.0f;
        FinalReservedSlot.Reset();
        AdvanceTimer = 0.0f;
        RetryTimer = 0.0f;
        State = ECombatCoverAgentState::InCover;
        HandleArrivedAtCoverSlot(Manager);
    };

    if (CurrentMovePathIndex >= static_cast<int32>(CurrentMovePath.size()))
    {
        FinishMove();
        return;
    }

    const FVector TargetLocation = CurrentMovePath[CurrentMovePathIndex];
    FVector CurrentLocation = Owner->GetActorLocation();
    FVector Delta = TargetLocation - CurrentLocation;
    Delta.Z = 0.0f;

    const float Distance = Delta.Length();
    if (Distance <= AcceptanceRadius)
    {
        ++CurrentMovePathIndex;
        if (CurrentMovePathIndex >= static_cast<int32>(CurrentMovePath.size()))
        {
            FinishMove();
        }
        return;
    }

    if (Delta.IsNearlyZero())
    {
        return;
    }

    const FVector Direction = Delta.Normalized();
    if (!CurrentTarget.Get())
    {
        FaceDirection2D(Direction, DeltaTime);
    }

    if (bUseCharacterMovement)
    {
        if (ACharacter* Character = Cast<ACharacter>(Owner))
        {
            if (UCharacterMovementComponent* CharacterMovement = Character->GetCharacterMovement())
            {
                if (CharacterMovement->HasValidUpdatedComponent())
                {
                    CharacterMovement->MaxWalkSpeed = GetCurrentCombatMoveSpeed();
                    Character->AddMovementInput(Direction, 1.0f);
                    return;
                }
            }
        }
    }

    const float Step = GetCurrentCombatMoveSpeed() * DeltaTime;
    if (Step <= 0.0f)
    {
        return;
    }

    if (Step >= Distance)
    {
        Owner->SetActorLocation(FVector(TargetLocation.X, TargetLocation.Y, CurrentLocation.Z));
    }
    else
    {
        Owner->SetActorLocation(CurrentLocation + Direction * Step);
    }
}

void UCombatCoverAgentComponent::FaceDirection2D(const FVector& Direction, float DeltaTime)
{
    if (!bOrientToCombatDirection)
    {
        return;
    }

    AActor* Owner = GetOwner();
    if (!Owner)
    {
        return;
    }

    FVector FlatDirection = Direction;
    FlatDirection.Z = 0.0f;

    const float LengthSq = FlatDirection.X * FlatDirection.X + FlatDirection.Y * FlatDirection.Y;
    if (LengthSq <= 1e-6f)
    {
        return;
    }

    const float TargetYaw = std::atan2(FlatDirection.Y, FlatDirection.X) * (180.0f / 3.14159265358979323846f) + FacingYawOffset;

    FRotator Rotation = Owner->GetActorRotation();
    const float DeltaYaw = NormalizeYawDelta(TargetYaw - Rotation.Yaw);
    const float YawRate = (std::max)(0.0f, FacingYawRate);

    if (YawRate <= 0.0f || DeltaTime <= 0.0f)
    {
        Rotation.Yaw = TargetYaw;
    }
    else
    {
        const float Step = YawRate * DeltaTime;
        if (std::fabs(DeltaYaw) <= Step)
        {
            Rotation.Yaw = TargetYaw;
        }
        else
        {
            Rotation.Yaw += (DeltaYaw > 0.0f ? Step : -Step);
        }
    }

    Rotation.Yaw = NormalizeYawDelta(Rotation.Yaw);
    Owner->SetActorRotation(Rotation);
}

void UCombatCoverAgentComponent::FaceLocation2D(const FVector& WorldLocation, float DeltaTime)
{
    AActor* Owner = GetOwner();
    if (!Owner)
    {
        return;
    }

    FaceDirection2D(WorldLocation - Owner->GetActorLocation(), DeltaTime);
}

void UCombatCoverAgentComponent::TickFaceCombatTarget(float DeltaTime)
{
    UCombatCoverAgentComponent* Target = CurrentTarget.Get();
    if (!Target || !Target->IsAlive() || !Target->GetOwner())
    {
        return;
    }

    FaceLocation2D(Target->GetOwner()->GetActorLocation(), DeltaTime);
}

void UCombatCoverAgentComponent::SetBlocked()
{
    State = ECombatCoverAgentState::Blocked;
    LinkedMoveStartDelayRemaining = 0.0f;
    RetryTimer = 0.0f;
}

void UCombatCoverAgentComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction)
{
    UActorComponent::TickComponent(DeltaTime, TickType, ThisTickFunction);

    if (State != ECombatCoverAgentState::Dead && Health <= 0.0f)
    {
        MarkDead();
        return;
    }

    TickCombatDecisionCooldown(DeltaTime);

    if (SuppressionTimer > 0.0f)
    {
        SuppressionTimer = (std::max)(0.0f, SuppressionTimer - DeltaTime);
        if (SuppressionTimer <= 0.0f)
        {
            FinishSuppression();
        }
    }

    if (HitReactionTimer > 0.0f)
    {
        HitReactionTimer = (std::max)(0.0f, HitReactionTimer - DeltaTime);
        if (HitReactionTimer <= 0.0f)
        {
            bHitReactionPending = false;
        }
    }

    if (State != ECombatCoverAgentState::Dead)
    {
        UCombatCoverAgentComponent* Target = CurrentTarget.Get();
        if (Target && !Target->IsAlive())
        {
            ClearEngagementTarget();
        }
        else
        {
            TickFaceCombatTarget(DeltaTime);
        }
    }

    switch (State)
    {
    case ECombatCoverAgentState::Idle:
        if (bAutoStart)
        {
            RequestInitialSlot();
        }
        break;

    case ECombatCoverAgentState::MovingToInitialSlot:
    case ECombatCoverAgentState::MovingToLinkedNode:
        TickMoveToTarget(DeltaTime);
        break;

    case ECombatCoverAgentState::InCover:
        if (CoverHoldTimer > 0.0f)
        {
            CoverHoldTimer = (std::max)(0.0f, CoverHoldTimer - DeltaTime);
            AdvanceTimer = 0.0f;
            if (CoverHoldTimer <= 0.0f && bMoveToCombatSlotAfterCoverHold)
            {
                bMoveToCombatSlotAfterCoverHold = false;
                if (UCombatFlowManagerComponent* Manager = ResolveManager())
                {
                    if (Manager->TryMoveToCombatSlotInCurrentNode(this))
                    {
                        break;
                    }
                }
                PickRandomAdvanceInterval();
            }
            break;
        }

        if (bMoveToCombatSlotAfterCoverHold)
        {
            bMoveToCombatSlotAfterCoverHold = false;
            if (UCombatFlowManagerComponent* Manager = ResolveManager())
            {
                if (Manager->TryMoveToCombatSlotInCurrentNode(this))
                {
                    break;
                }
            }
            PickRandomAdvanceInterval();
        }

        AdvanceTimer += DeltaTime;
        if (AdvanceTimer >= AdvanceInterval)
        {
            AdvanceTimer = 0.0f;
            RequestAdvance();
        }
        break;

    case ECombatCoverAgentState::Engaging:
        break;

    case ECombatCoverAgentState::Suppressed:
        if (SuppressionTimer <= 0.0f)
        {
            FinishSuppression();
        }
        break;

    case ECombatCoverAgentState::Blocked:
        RetryTimer += DeltaTime;
        if (RetryTimer >= RetryInterval)
        {
            RetryTimer = 0.0f;
            if (CurrentNodeId.empty())
            {
                RequestInitialSlot();
            }
            else
            {
                RequestAdvance();
            }
        }
        break;

    case ECombatCoverAgentState::Dead:
    default:
        break;
    }
}
