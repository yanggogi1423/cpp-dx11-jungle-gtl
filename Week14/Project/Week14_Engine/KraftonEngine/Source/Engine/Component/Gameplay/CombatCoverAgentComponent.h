#pragma once

#include "Component/ActorComponent.h"
#include "Component/Gameplay/CombatCoverNodeComponent.h"
#include "Object/FName.h"
#include "Object/Ptr/WeakObjectPtr.h"

#include "Source/Engine/Component/Gameplay/CombatCoverAgentComponent.generated.h"

class UCombatFlowManagerComponent;
struct FSniperHitInfo;

UENUM()
enum class ECombatCoverAgentState : uint8
{
    Idle,
    MovingToInitialSlot,
    InCover,
    MovingToLinkedNode,
    Engaging,
    Suppressed,
    Blocked,
    Dead
};

UENUM()
enum class ECombatAdvanceLinkMode : uint8
{
    OutgoingLinks,
    IncomingLinks,
    Both
};

UENUM()
enum class ECombatAgentRole : uint8
{
    AutoFromTeam,
    Ally,
    EnemyShortRange,
    EnemyLongRangeSlow,
    EnemyAssault
};

UCLASS()
class UCombatCoverAgentComponent : public UActorComponent
{
public:
    GENERATED_BODY()

    UCombatCoverAgentComponent();
    ~UCombatCoverAgentComponent() override = default;

    void BeginPlay() override;
    void EndPlay() override;
    void PostEditProperty(const char* PropertyName) override;

    UFUNCTION(Callable, Category="CombatAgent")
    void RequestInitialSlot();

    UFUNCTION(Callable, Category="CombatAgent")
    void RequestAdvance();

    void MoveToReservedSlot(const FCombatMovePath& MovePath, bool bInitialMove);

    UFUNCTION(Callable, Category="CombatAgent")
    void MarkDead();

    UFUNCTION(Pure, Category="CombatAgent")
    const FString& GetTeamTag() const { return TeamTag; }

    UFUNCTION(Callable, Category="CombatAgent")
    void SetTeamTag(const FString& InTeamTag);

    UFUNCTION(Pure, Category="CombatAgent")
    FString GetDisplayName() const;

    UFUNCTION(Callable, Category="CombatAgent")
    void SetDisplayName(const FString& InDisplayName);

    UFUNCTION(Pure, Category="CombatAgent")
    const FString& GetCurrentNodeId() const { return CurrentNodeId; }

    UFUNCTION(Pure, Category="CombatAgent")
    int32 GetCurrentSlotId() const { return CurrentSlotId; }

    UFUNCTION(Pure, Category="CombatAgent")
    const FString& GetTargetNodeId() const { return TargetNodeId; }

    UFUNCTION(Pure, Category="CombatAgent")
    int32 GetTargetSlotId() const { return TargetSlotId; }

    UFUNCTION(Pure, Category="CombatAgent")
    ECombatCoverAgentState GetState() const { return State; }

    UFUNCTION(Pure, Category="CombatAgent")
    ECombatAdvanceLinkMode GetAdvanceLinkMode() const { return AdvanceLinkMode; }

    UFUNCTION(Pure, Category="CombatAgent")
    ECombatAgentRole GetCombatRole() const { return CombatRole; }

    UFUNCTION(Pure, Category="CombatAgent")
    ECombatAgentRole GetResolvedCombatRole() const;

    UFUNCTION(Pure, Category="CombatAgent")
    bool UsesRoleCombatDefaults() const { return bUseRoleCombatDefaults; }

    UFUNCTION(Pure, Category="CombatAgent|Combat")
    float GetMaxHealth() const { return MaxHealth; }

    UFUNCTION(Callable, Category="CombatAgent|Combat")
    void SetMaxHealth(float InMaxHealth);

    UFUNCTION(Pure, Category="CombatAgent|Combat")
    float GetHealth() const { return Health; }

    UFUNCTION(Callable, Category="CombatAgent|Combat")
    void SetHealth(float InHealth);

    UFUNCTION(Pure, Category="CombatAgent|Combat")
    float GetHealthRatio() const;

    UFUNCTION(Pure, Category="CombatAgent|Combat")
    float GetFireRange() const { return FireRange; }

    UFUNCTION(Pure, Category="CombatAgent|Combat")
    float GetMovingFireRange() const { return MovingFireRange; }

    UFUNCTION(Pure, Category="CombatAgent|Combat")
    bool UsesMovingFireRange() const { return bUseMovingFireRange; }

    UFUNCTION(Pure, Category="CombatAgent|Combat")
    bool IsMovingForCombatRange() const;

    UFUNCTION(Pure, Category="CombatAgent|Combat")
    float GetEffectiveFireRange() const;

    UFUNCTION(Pure, Category="CombatAgent|Combat")
    float GetAttackDamage() const { return AttackDamage; }

    UFUNCTION(Pure, Category="CombatAgent|Combat")
    float GetAttackIntervalMin() const { return AttackIntervalMin; }

    UFUNCTION(Pure, Category="CombatAgent|Combat")
    float GetAttackIntervalMax() const { return AttackIntervalMax; }

    UFUNCTION(Pure, Category="CombatAgent|Combat")
    float GetSuppressedAttackSpeedMultiplier() const { return SuppressedAttackSpeedMultiplier; }

    UFUNCTION(Pure, Category="CombatAgent|Combat")
    float GetSuppressedMoveSpeedMultiplier() const { return SuppressedMoveSpeedMultiplier; }

    UFUNCTION(Pure, Category="CombatAgent|Combat")
    int32 GetIncomingFireCount() const { return IncomingFireCount; }

    UFUNCTION(Pure, Category="CombatAgent|Combat")
    float GetIncomingAttackDamage() const { return IncomingAttackDamage; }

    UFUNCTION(Pure, Category="CombatAgent|Combat")
    UCombatCoverAgentComponent* GetCurrentTarget() const { return CurrentTarget.Get(); }

    UFUNCTION(Pure, Category="CombatAgent|Combat")
    bool CanFireWhileMoving() const { return bCanFireWhileMoving; }

    UFUNCTION(Pure, Category="CombatAgent|Behavior")
    float GetRepositionChanceWhenInRange() const { return RepositionChanceWhenInRange; }

    UFUNCTION(Pure, Category="CombatAgent|Behavior")
    float GetCombatDecisionCooldown() const { return CombatDecisionCooldown; }

    UFUNCTION(Pure, Category="CombatAgent|Behavior")
    float GetTakeCoverChanceWhenInRange() const { return TakeCoverChanceWhenInRange; }

    UFUNCTION(Pure, Category="CombatAgent|Behavior")
    float GetTakeCoverDurationMin() const { return TakeCoverDurationMin; }

    UFUNCTION(Pure, Category="CombatAgent|Behavior")
    float GetTakeCoverDurationMax() const { return TakeCoverDurationMax; }

    UFUNCTION(Pure, Category="CombatAgent|Behavior")
    float GetInCoverTargetPriorityMultiplier() const { return InCoverTargetPriorityMultiplier; }

    UFUNCTION(Pure, Category="CombatAgent|Behavior")
    float GetAdvanceFullCoverChance() const { return AdvanceFullCoverChance; }

    UFUNCTION(Pure, Category="CombatAgent|Behavior")
    float GetLowHealthFullCoverRatio() const { return LowHealthFullCoverRatio; }

    UFUNCTION(Pure, Category="CombatAgent|Behavior")
    float GetLowHealthFullCoverChance() const { return LowHealthFullCoverChance; }

    UFUNCTION(Pure, Category="CombatAgent|Behavior")
    bool CanMakeCombatDecision() const { return CombatDecisionCooldownRemaining <= 0.0f; }

    UFUNCTION(Pure, Category="CombatAgent|Behavior")
    bool IsHoldingCoverForCombat() const { return State == ECombatCoverAgentState::InCover && CoverHoldTimer > 0.0f; }

    UFUNCTION(Callable, Category="CombatAgent|Behavior")
    void MarkCombatDecisionMade();

    UFUNCTION(Callable, Category="CombatAgent|Behavior")
    void EnterCombatCoverHold(float Duration);

    UFUNCTION(Pure, Category="CombatAgent|Combat")
    bool IsInCover() const { return State == ECombatCoverAgentState::InCover; }

    UFUNCTION(Pure, Category="CombatAgent|Combat")
    bool IsSuppressed() const { return State != ECombatCoverAgentState::Dead && SuppressionTimer > 0.0f; }

    UFUNCTION(Pure, Category="CombatAgent|Combat")
    float GetSuppressionTimeRemaining() const { return SuppressionTimer; }

    UFUNCTION(Pure, Category="CombatAgent|Combat")
    float GetDeathDebugScaleMultiplier() const { return DeathDebugScaleMultiplier; }

    UFUNCTION(Pure, Category="CombatAgent|Combat")
    bool IsAlive() const { return State != ECombatCoverAgentState::Dead && Health > 0.0f; }

    UFUNCTION(Pure, Category="CombatAgent|Combat")
    bool IsEngaging() const { return State == ECombatCoverAgentState::Engaging || CurrentTarget.Get() != nullptr; }

    UFUNCTION(Pure, Category="CombatAgent|Animation")
    ECombatCoverSlotType GetCurrentSlotType() const;

    UFUNCTION(Pure, Category="CombatAgent|Animation")
    bool IsInStandingCombatSlot() const;

    UFUNCTION(Pure, Category="CombatAgent|Animation")
    bool ShouldUseStandingFire() const;

    UFUNCTION(Pure, Category="CombatAgent|Animation")
    float GetCombatAnimationMoveState() const;

    UFUNCTION(Pure, Category="CombatAgent|Animation")
    bool ShouldRunDuringCombatMovement() const;

    UFUNCTION(Pure, Category="CombatAgent|Movement")
    float GetCurrentCombatMoveSpeed() const;

    UFUNCTION(Pure, Category="CombatAgent|Animation")
    bool WantsHitReaction() const { return bHitReactionPending && HitReactionTimer > 0.0f; }

    UFUNCTION(Pure, Category="CombatAgent|Animation")
    float GetHitReactionTimeRemaining() const { return HitReactionTimer; }

    UFUNCTION(Callable, Category="CombatAgent|Animation")
    bool ConsumeHitReaction();

    void RecordSniperHit(const FSniperHitInfo& HitInfo);

    UFUNCTION(Pure, Category="CombatAgent|Sniper")
    bool HasLastSniperHit() const { return bHasLastSniperHit; }

    UFUNCTION(Pure, Category="CombatAgent|Sniper")
    FName GetLastHitBoneName() const { return LastHitBoneName; }

    UFUNCTION(Pure, Category="CombatAgent|Sniper")
    const FString& GetLastHitBodyName() const { return LastHitBodyName; }

    UFUNCTION(Pure, Category="CombatAgent|Sniper")
    const FString& GetLastHitRegionName() const { return LastHitRegionName; }

    UFUNCTION(Pure, Category="CombatAgent|Sniper")
    const FString& GetLastHitRegionDisplayName() const { return LastHitRegionDisplayName; }

    UFUNCTION(Pure, Category="CombatAgent|Sniper")
    float GetLastHitDamage() const { return LastHitDamage; }

    UFUNCTION(Pure, Category="CombatAgent|Sniper")
    float GetLastHitScoreMultiplier() const { return LastHitScoreMultiplier; }

    UFUNCTION(Pure, Category="CombatAgent|Sniper")
    int32 GetLastHitScoreValue() const { return LastHitScoreValue; }

    UFUNCTION(Pure, Category="CombatAgent|Sniper")
    bool WasLastHitKilled() const { return bLastHitKilled; }

    const char* GetStateName() const;
    const char* GetAdvanceLinkModeName() const;
    const char* GetCombatRoleName() const;
    const char* GetResolvedCombatRoleName() const;

    void SetEngagementTarget(UCombatCoverAgentComponent* Target);
    void ClearEngagementTarget();
    void ApplyDamage(float Damage);
    void ApplySuppression(float Duration);
    void SetIncomingFireStats(int32 Count, float AttackDamage);

private:
    UCombatFlowManagerComponent* ResolveManager();
    void ApplyCombatRoleDefaults();
    void ClampRuntimeEditableValues();
    void PickRandomAdvanceInterval();
    float PickFullCoverToCombatDelay() const;
    void HandleArrivedAtCoverSlot(UCombatFlowManagerComponent* Manager);
    void TickCombatDecisionCooldown(float DeltaTime);
    void FinishSuppression();
    void QueueHitReaction();
    bool IsHitReactionMoveLocked() const;
    void TickMoveToTarget(float DeltaTime);
    void FaceDirection2D(const FVector& Direction, float DeltaTime);
    void FaceLocation2D(const FVector& WorldLocation, float DeltaTime);
    void TickFaceCombatTarget(float DeltaTime);
    void SetBlocked();

private:
    UPROPERTY(Edit, Save, Category="CombatAgent", DisplayName="Team Tag")
    FString TeamTag = "Enemy";

    UPROPERTY(Edit, Save, Category="CombatAgent", DisplayName="Agent Display Name")
    FString AgentDisplayName;

    UPROPERTY(Edit, Save, Category="CombatAgent", DisplayName="Advance Link Mode", Enum=ECombatAdvanceLinkMode)
    ECombatAdvanceLinkMode AdvanceLinkMode = ECombatAdvanceLinkMode::OutgoingLinks;

    UPROPERTY(Edit, Save, Category="CombatAgent", DisplayName="Combat Role", Enum=ECombatAgentRole)
    ECombatAgentRole CombatRole = ECombatAgentRole::AutoFromTeam;

    UPROPERTY(Edit, Save, Category="CombatAgent", DisplayName="Use Role Combat Defaults")
    bool bUseRoleCombatDefaults = true;

    UPROPERTY(Edit, Save, Category="CombatAgent", DisplayName="Legacy Move Speed", Min=0.0f, Max=10000.0f, Speed=1.0f)
    float MoveSpeed = 10.0f;

    UPROPERTY(Edit, Save, Category="CombatAgent|Movement", DisplayName="Crouch Move Speed", Min=0.0f, Max=10000.0f, Speed=1.0f)
    float CrouchMoveSpeed = 4.0f;

    UPROPERTY(Edit, Save, Category="CombatAgent|Movement", DisplayName="Run Move Speed", Min=0.0f, Max=10000.0f, Speed=1.0f)
    float RunMoveSpeed = 6.0f;

    UPROPERTY(Edit, Save, Category="CombatAgent|Movement", DisplayName="Assault Walk Below Health Ratio", Min=0.0f, Max=1.0f, Speed=0.01f)
    float AssaultWalkBelowHealthRatio = 0.5f;

    UPROPERTY(Edit, Save, Category="CombatAgent", DisplayName="Acceptance Radius", Min=1.0f, Max=10000.0f, Speed=1.0f)
    float AcceptanceRadius = 0.0f;

    UPROPERTY(Edit, Save, Category="CombatAgent", DisplayName="Advance Interval", Min=0.0f, Max=120.0f, Speed=0.1f)
    float AdvanceInterval = 3.0f;

    UPROPERTY(Edit, Save, Category="CombatAgent", DisplayName="Advance Interval Min", Min=0.0f, Max=120.0f, Speed=0.1f)
    float AdvanceIntervalMin = 3.0f;

    UPROPERTY(Edit, Save, Category="CombatAgent", DisplayName="Advance Interval Max", Min=0.0f, Max=120.0f, Speed=0.1f)
    float AdvanceIntervalMax = 3.0f;

    UPROPERTY(Edit, Save, Category="CombatAgent", DisplayName="Retry Interval", Min=0.1f, Max=120.0f, Speed=0.1f)
    float RetryInterval = 1.0f;

    UPROPERTY(Edit, Save, Category="CombatAgent", DisplayName="Auto Start")
    bool bAutoStart = true;

    UPROPERTY(Edit, Save, Category="CombatAgent", DisplayName="Use Character Movement")
    bool bUseCharacterMovement = true;

    UPROPERTY(Edit, Save, Category="CombatAgent|Facing", DisplayName="Orient To Combat Direction")
    bool bOrientToCombatDirection = true;

    UPROPERTY(Edit, Save, Category="CombatAgent|Facing", DisplayName="Facing Yaw Rate", Min=0.0f, Max=3600.0f, Speed=5.0f)
    float FacingYawRate = 720.0f;

    UPROPERTY(Edit, Save, Category="CombatAgent|Facing", DisplayName="Facing Yaw Offset", Min=-180.0f, Max=180.0f, Speed=1.0f)
    float FacingYawOffset = 0.0f;

    UPROPERTY(Edit, Save, Category="CombatAgent|Combat", DisplayName="Max Health", Min=1.0f, Max=100000.0f, Speed=1.0f)
    float MaxHealth = 100.0f;

    UPROPERTY(Edit, Save, Category="CombatAgent|Combat", DisplayName="Health", Min=0.0f, Max=100000.0f, Speed=1.0f)
    float Health = 100.0f;

    UPROPERTY(Edit, Save, Category="CombatAgent|Combat", DisplayName="Fire Range", Min=0.0f, Max=100000.0f, Speed=1.0f)
    float FireRange = 50.0f;

    UPROPERTY(Edit, Save, Category="CombatAgent|Combat", DisplayName="Moving Fire Range", Min=0.0f, Max=100000.0f, Speed=1.0f)
    float MovingFireRange = 30.0f;

    UPROPERTY(Edit, Save, Category="CombatAgent|Combat", DisplayName="Use Moving Fire Range")
    bool bUseMovingFireRange = true;

    UPROPERTY(Edit, Save, Category="CombatAgent|Debug", DisplayName="Shrink Actor On Death")
    bool bShrinkActorOnDeath = false;

    UPROPERTY(Edit, Save, Category="CombatAgent|Debug", DisplayName="Death Debug Scale Multiplier", Min=0.01f, Max=1.0f, Speed=0.01f)
    float DeathDebugScaleMultiplier = 0.1f;

    UPROPERTY(Edit, Save, Category="CombatAgent|Combat", DisplayName="Attack Damage", Min=0.0f, Max=100000.0f, Speed=1.0f)
    float AttackDamage = 5.0f;

    UPROPERTY(Edit, Save, Category="CombatAgent|Combat", DisplayName="Attack Interval Min", Min=0.0f, Max=120.0f, Speed=0.1f)
    float AttackIntervalMin = 1.0f;

    UPROPERTY(Edit, Save, Category="CombatAgent|Combat", DisplayName="Attack Interval Max", Min=0.0f, Max=120.0f, Speed=0.1f)
    float AttackIntervalMax = 2.0f;

    UPROPERTY(Edit, Save, Category="CombatAgent|Combat", DisplayName="Target Scan Interval", Min=0.01f, Max=10.0f, Speed=0.01f)
    float TargetScanInterval = 0.2f;

    UPROPERTY(Edit, Save, Category="CombatAgent|Combat", DisplayName="Can Fire While Moving")
    bool bCanFireWhileMoving = false;

    UPROPERTY(Edit, Save, Category="CombatAgent|Combat", DisplayName="Suppressed Attack Speed Multiplier", Min=0.01f, Max=1.0f, Speed=0.01f)
    float SuppressedAttackSpeedMultiplier = 0.25f;

    UPROPERTY(Edit, Save, Category="CombatAgent|Combat", DisplayName="Suppressed Move Speed Multiplier", Min=0.01f, Max=1.0f, Speed=0.01f)
    float SuppressedMoveSpeedMultiplier = 0.25f;

    UPROPERTY(Edit, Save, Category="CombatAgent|Animation", DisplayName="Hit Reaction Duration", Min=0.0f, Max=10.0f, Speed=0.01f)
    float HitReactionDuration = 0.45f;

    UPROPERTY(Edit, Save, Category="CombatAgent|Animation", DisplayName="Trigger Hit Reaction On Suppression")
    bool bTriggerHitReactionOnSuppression = false;

    UPROPERTY(Edit, Save, Category="CombatAgent|Behavior", DisplayName="Reposition Chance When In Range", Min=0.0f, Max=1.0f, Speed=0.01f)
    float RepositionChanceWhenInRange = 0.25f;

    UPROPERTY(Edit, Save, Category="CombatAgent|Behavior", DisplayName="Combat Decision Cooldown", Min=0.0f, Max=120.0f, Speed=0.1f)
    float CombatDecisionCooldown = 2.0f;

    UPROPERTY(Edit, Save, Category="CombatAgent|Behavior", DisplayName="Take Cover Chance When In Range", Min=0.0f, Max=1.0f, Speed=0.01f)
    float TakeCoverChanceWhenInRange = 0.35f;

    UPROPERTY(Edit, Save, Category="CombatAgent|Behavior", DisplayName="Take Cover Duration Min", Min=0.0f, Max=120.0f, Speed=0.1f)
    float TakeCoverDurationMin = 1.5f;

    UPROPERTY(Edit, Save, Category="CombatAgent|Behavior", DisplayName="Take Cover Duration Max", Min=0.0f, Max=120.0f, Speed=0.1f)
    float TakeCoverDurationMax = 3.0f;

    UPROPERTY(Edit, Save, Category="CombatAgent|Behavior", DisplayName="In Cover Target Priority Multiplier", Min=0.05f, Max=1.0f, Speed=0.01f)
    float InCoverTargetPriorityMultiplier = 0.35f;

    UPROPERTY(Edit, Save, Category="CombatAgent|Behavior", DisplayName="Advance Full Cover Chance", Min=0.0f, Max=1.0f, Speed=0.01f)
    float AdvanceFullCoverChance = 0.85f;

    UPROPERTY(Edit, Save, Category="CombatAgent|Behavior", DisplayName="Full Cover To Combat Delay Min", Min=0.0f, Max=120.0f, Speed=0.1f)
    float FullCoverToCombatDelayMin = 0.8f;

    UPROPERTY(Edit, Save, Category="CombatAgent|Behavior", DisplayName="Full Cover To Combat Delay Max", Min=0.0f, Max=120.0f, Speed=0.1f)
    float FullCoverToCombatDelayMax = 1.6f;

    UPROPERTY(Edit, Save, Category="CombatAgent|Behavior", DisplayName="Low Health Full Cover Ratio", Min=0.0f, Max=1.0f, Speed=0.01f)
    float LowHealthFullCoverRatio = 0.35f;

    UPROPERTY(Edit, Save, Category="CombatAgent|Behavior", DisplayName="Low Health Full Cover Chance", Min=0.0f, Max=1.0f, Speed=0.01f)
    float LowHealthFullCoverChance = 0.75f;

    ECombatCoverAgentState State = ECombatCoverAgentState::Idle;
    FString CurrentNodeId;
    int32 CurrentSlotId = -1;
    FString TargetNodeId;
    int32 TargetSlotId = -1;
    TArray<FVector> CurrentMovePath;
    int32 CurrentMovePathIndex = 0;
    float LinkedMoveStartDelayRemaining = 0.0f;
    FCombatCoverSlotHandle FinalReservedSlot;
    float AdvanceTimer = 0.0f;
    float RetryTimer = 0.0f;
    float TargetScanTimer = 0.0f;
    int32 IncomingFireCount = 0;
    float IncomingAttackDamage = 0.0f;
    float SuppressionTimer = 0.0f;
    float HitReactionTimer = 0.0f;
    float CombatDecisionCooldownRemaining = 0.0f;
    float CoverHoldTimer = 0.0f;
    bool bMoveToCombatSlotAfterCoverHold = false;
    bool bHitReactionPending = false;
    ECombatCoverAgentState StateBeforeEngage = ECombatCoverAgentState::Idle;
    ECombatCoverAgentState StateBeforeSuppressed = ECombatCoverAgentState::Idle;
    TWeakObjectPtr<UCombatCoverAgentComponent> CurrentTarget;
    TWeakObjectPtr<UCombatFlowManagerComponent> CachedManager;
    bool bHasLastSniperHit = false;
    FName LastHitBoneName = FName::None;
    FString LastHitBodyName;
    FString LastHitRegionName = "Unknown";
    FString LastHitRegionDisplayName = "UNKNOWN";
    float LastHitDamage = 0.0f;
    float LastHitScoreMultiplier = 1.0f;
    int32 LastHitScoreValue = 0;
    bool bLastHitKilled = false;

protected:
    void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction) override;
};
