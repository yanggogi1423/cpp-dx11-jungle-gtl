#pragma once

#include "GameFramework/Pawn/Pawn.h"
#include "Component/Gameplay/SniperTypes.h"
#include "Object/Ptr/SoftObjectPtr.h"
#include "Object/Ptr/WeakObjectPtr.h"

#include "Source/Engine/GameFramework/Pawn/SniperPawn.generated.h"

class UBallisticBulletManagerComponent;
class UCameraComponent;
class UBoxComponent;
class UCapsuleComponent;
class USceneComponent;
class USniperWeaponComponent;
class UActionComponent;
class USkeletalMeshComponent;
class UStaticMeshComponent;
class FArchive;
struct FBoundingBox;
struct FHitResult;

UCLASS()
class ASniperPawn : public APawn
{
public:
	GENERATED_BODY()
	ASniperPawn();
	~ASniperPawn() override = default;

	void BeginPlay() override;
	void EndPlay() override;
	void PostDuplicate() override;
	void OnPostLoad(FArchive& Ar) override;
	void PreGetEditableProperties() override;
	void SetupInputComponent() override;
	void ProcessPlayerInput(const FInputSystemSnapshot& Snapshot, float DeltaTime) override;
	void Tick(float DeltaTime) override;

	void InitDefaultComponents();

	UFUNCTION(Pure, Category="Sniper|Components")
	USceneComponent* GetSniperRoot() const { return SniperRoot.Get(); }
	UFUNCTION(Pure, Category="Sniper|Components")
	UCameraComponent* GetCamera() const { return Camera.Get(); }
	UFUNCTION(Pure, Category="Sniper|Components")
	USkeletalMeshComponent* GetWeaponHandsMeshComponent() const { return WeaponHandsMeshComponent.Get(); }
	UFUNCTION(Pure, Category="Sniper|Components")
	UStaticMeshComponent* GetWeaponVisualComponent() const { return WeaponVisualComponent.Get(); }
	UFUNCTION(Pure, Category="Sniper|Components")
	USniperWeaponComponent* GetSniperWeaponComponent() const { return WeaponComponent.Get(); }
	UFUNCTION(Pure, Category="Sniper|Components")
	UBallisticBulletManagerComponent* GetBallisticBulletManagerComponent() const { return BulletManagerComponent.Get(); }
	UFUNCTION(Pure, Category="Sniper|Components")
	UActionComponent* GetSniperActionComponent() const { return ActionComponent.Get(); }
	UFUNCTION(Pure, Category="Sniper|State")
	bool IsScoped() const { return ScopeState.bIsScoped; }
	UFUNCTION(Pure, Category="Sniper|State")
	bool IsReloading() const;
	UFUNCTION(Pure, Category="Sniper|State")
	float GetReloadRemaining() const;
	UFUNCTION(Pure, Category="Sniper|State")
	float GetReloadProgress() const;
	UFUNCTION(Callable, Category="Sniper|State")
	void ForceScopeReleased();
	UFUNCTION(Pure, Category="Sniper|State")
	float GetScopeBlendAlpha() const;
	UFUNCTION(Pure, Category="Sniper|State")
	float GetCurrentScopeFOV() const { return ScopeState.CurrentFOV; }
	UFUNCTION(Pure, Category="Sniper|State")
	float GetCurrentScopeZoomMagnification() const { return ScopeState.CurrentZoomMagnification; }
	UFUNCTION(Pure, Category="Sniper|State")
	float GetMinScopeZoomMagnification() const { return ScopeState.MinZoomMagnification; }
	UFUNCTION(Pure, Category="Sniper|State")
	float GetMaxScopeZoomMagnification() const { return ScopeState.MaxZoomMagnification; }
	UFUNCTION(Pure, Category="Sniper|State")
	float GetCurrentScopeSensitivity() const { return ScopeState.CurrentSensitivity; }
	UFUNCTION(Pure, Category="Sniper|Input")
	float GetMouseSensitivityMultiplier() const;
	UFUNCTION(Callable, Category="Sniper|Input")
	void SetMouseSensitivityMultiplier(float Multiplier);
	UFUNCTION(Pure, Category="Sniper|Input")
	float GetGamepadLookSensitivityMultiplier() const;
	UFUNCTION(Callable, Category="Sniper|Input")
	void SetGamepadLookSensitivityMultiplier(float Multiplier);
	UFUNCTION(Pure, Category="Sniper|Input")
	bool IsRightClickZoomToggleMode() const { return bRightClickZoomToggleMode; }
	UFUNCTION(Callable, Category="Sniper|Input")
	void SetRightClickZoomToggleMode(bool bToggleMode);
	UFUNCTION(Pure, Category="Sniper|State")
	bool IsHoldBreathActive() const;
	UFUNCTION(Pure, Category="Sniper|State")
	float GetHoldBreathGauge() const { return AimSwayState.HoldBreathGauge; }
	UFUNCTION(Pure, Category="Sniper|State")
	float GetMaxHoldBreathGauge() const { return AimSwayState.MaxHoldBreathGauge; }

	UFUNCTION(Pure, Category="Sniper|State")
	bool IsHoldBreathInputHeld() const { return InputState.bHoldBreathHeld; }
	UFUNCTION(Pure, Category="Sniper|State")
	bool IsHoldBreathRecovering() const { return AimSwayState.bForcedRecovery; }
	UFUNCTION(Pure, Category="Sniper|State")
	bool IsHoldBreathReleaseRequired() const { return AimSwayState.bRequireHoldBreathRelease; }
	UFUNCTION(Pure, Category="Sniper|State")
	bool IsHoldBreathOnCooldown() const { return AimSwayState.HoldBreathCooldownRemaining > 0.0f; }
	UFUNCTION(Pure, Category="Sniper|State")
	float GetHoldBreathCooldownRemaining() const { return AimSwayState.HoldBreathCooldownRemaining; }
	UFUNCTION(Pure, Category="Sniper|State")
	float GetHoldBreathDuration() const
	{
		return AimSwayState.HoldBreathConsumeSpeed > 0.0f
			? AimSwayState.MaxHoldBreathGauge / AimSwayState.HoldBreathConsumeSpeed
			: 0.0f;
	}
	UFUNCTION(Pure, Category="Sniper|State")
	float GetHoldBreathGaugeRatio() const
	{
		if (AimSwayState.MaxHoldBreathGauge <= 0.0f)
		{
			return 0.0f;
		}

		const float Ratio = AimSwayState.HoldBreathGauge / AimSwayState.MaxHoldBreathGauge;
		return Ratio < 0.0f ? 0.0f : (Ratio > 1.0f ? 1.0f : Ratio);
	}

	UPROPERTY(Edit, Save, Category="Sniper|Input", DisplayName="Mouse Sensitivity", Min=0.0f, Max=10.0f, Speed=0.01f)
	float MouseSensitivity = 0.2f;
	UPROPERTY(Edit, Save, Category="Sniper|Input", DisplayName="Gamepad Look Sensitivity", Min=0.0f, Max=720.0f, Speed=1.0f)
	float GamepadLookSensitivity = 90.0f;
	UPROPERTY(Edit, Save, Category="Sniper|Input", DisplayName="Gamepad Trigger Press Threshold", Min=0.01f, Max=1.0f, Speed=0.01f)
	float GamepadTriggerPressThreshold = 0.35f;
	UPROPERTY(Edit, Save, Category="Sniper|Input", DisplayName="Min Camera Pitch", Min=-89.0f, Max=89.0f, Speed=0.1f)
	float MinCameraPitch = -80.0f;
	UPROPERTY(Edit, Save, Category="Sniper|Input", DisplayName="Max Camera Pitch", Min=-89.0f, Max=89.0f, Speed=0.1f)
	float MaxCameraPitch = 60.0f;
	UPROPERTY(Edit, Save, Category="Sniper|Input", DisplayName="Invert Mouse Y")
	bool bInvertMouseY = false;
	UPROPERTY(Edit, Save, Category="Sniper|Input", DisplayName="Right Click Zoom Toggle Mode")
	bool bRightClickZoomToggleMode = false;
	UPROPERTY(Edit, Save, Category="Sniper|Movement", DisplayName="Enable WASD Movement")
	bool bEnableWASDMovement = true;
	UPROPERTY(Edit, Save, Category="Sniper|Movement", DisplayName="Move Speed", Min=0.0f, Max=1000.0f, Speed=1.0f)
	float SniperMoveSpeed = 4.0f;
	UPROPERTY(Edit, Save, Category="Sniper|Movement", DisplayName="Footstep SFX Path")
	FString FootstepSFXPath = "Foot1.mp3";
	UPROPERTY(Edit, Save, Category="Sniper|Movement", DisplayName="Footstep Interval", Min=0.05f, Max=2.0f, Speed=0.01f)
	float FootstepInterval = 0.45f;
	UPROPERTY(Edit, Save, Category="Sniper|Movement", DisplayName="Footstep Volume", Min=0.0f, Max=1.0f, Speed=0.01f)
	float FootstepVolume = 0.65f;
	UPROPERTY(Edit, Save, Category="Sniper|Movement", DisplayName="Movement Sweep Pullback Distance", Min=0.0f, Max=1.0f, Speed=0.001f)
	float SniperMovementSweepPullbackDistance = 0.01f;
	UPROPERTY(Edit, Save, Category="Sniper|Movement", DisplayName="Use Explicit Movement Bounds")
	bool bUseExplicitSniperMovementBounds = false;
	UPROPERTY(Edit, Save, Category="Sniper|Movement", DisplayName="Movement Min Location", Type=Vec3, Min=0.0f, Max=0.0f, Speed=0.1f)
	FVector SniperMovementMinLocation = FVector(-130.0f, 129.0f, 0.0f);
	UPROPERTY(Edit, Save, Category="Sniper|Movement", DisplayName="Movement Max Location", Type=Vec3, Min=0.0f, Max=0.0f, Speed=0.1f)
	FVector SniperMovementMaxLocation = FVector(-88.0f, 138.0f, 0.0f);

	UPROPERTY(Edit, Save, Category="Sniper|Scope", DisplayName="Scoped FOV", Member=ScopeState.ScopedFOV, Type=Float, Min=0.05f, Max=3.14f, Speed=0.01f);
	UPROPERTY(Edit, Save, Category="Sniper|Scope", DisplayName="Min Zoom Magnification", Member=ScopeState.MinZoomMagnification, Type=Float, Min=1.0f, Max=64.0f, Speed=0.1f);
	UPROPERTY(Edit, Save, Category="Sniper|Scope", DisplayName="Max Zoom Magnification", Member=ScopeState.MaxZoomMagnification, Type=Float, Min=1.0f, Max=64.0f, Speed=0.1f);
	UPROPERTY(Edit, Save, Category="Sniper|Scope", DisplayName="Default Zoom Magnification", Member=ScopeState.DefaultZoomMagnification, Type=Float, Min=1.0f, Max=64.0f, Speed=0.1f);
	UPROPERTY(Edit, Save, Category="Sniper|Scope", DisplayName="Zoom Step", Member=ScopeState.ZoomStep, Type=Float, Min=0.1f, Max=16.0f, Speed=0.1f);
	UPROPERTY(Edit, Save, Category="Sniper|Scope", DisplayName="Normal Sensitivity", Member=ScopeState.NormalSensitivity, Type=Float, Min=0.0f, Max=10.0f, Speed=0.01f);
	UPROPERTY(Edit, Save, Category="Sniper|Scope", DisplayName="Min Zoom Scoped Sensitivity", Member=ScopeState.ScopedSensitivity, Type=Float, Min=0.0f, Max=10.0f, Speed=0.01f);
	UPROPERTY(Edit, Save, Category="Sniper|Scope", DisplayName="Max Zoom Scoped Sensitivity", Member=ScopeState.MaxZoomScopedSensitivity, Type=Float, Min=0.0f, Max=10.0f, Speed=0.01f);
	UPROPERTY(Edit, Save, Category="Sniper|Scope", DisplayName="FOV Blend Speed", Member=ScopeState.ScopeBlendSpeed, Type=Float, Min=0.0f, Max=60.0f, Speed=0.1f);

	UPROPERTY(Edit, Save, Category="Sniper|Aim Sway", DisplayName="Base Sway Amount", Member=AimSwayState.BaseSwayAmount, Type=Float, Min=0.0f, Max=0.05f, Speed=0.0001f);
	UPROPERTY(Edit, Save, Category="Sniper|Aim Sway", DisplayName="Scoped Sway Amount", Member=AimSwayState.ScopedSwayAmount, Type=Float, Min=0.0f, Max=0.05f, Speed=0.0001f);
	UPROPERTY(Edit, Save, Category="Sniper|Aim Sway", DisplayName="Hold Breath Sway Multiplier", Min=0.0f, Max=1.0f, Speed=0.01f)
	float HoldBreathSwayMultiplier = 0.04f;
	UPROPERTY(Edit, Save, Category="Sniper|Aim Sway", DisplayName="Exhausted Sway Multiplier", Min=1.0f, Max=10.0f, Speed=0.1f)
	float ExhaustedSwayMultiplier = 10.0f;
	UPROPERTY(Edit, Save, Category="Sniper|Aim Sway", DisplayName="Hold Breath Sway Blend Speed", Min=0.0f, Max=60.0f, Speed=0.1f)
	float HoldBreathSwayBlendSpeed = 4.0f;
	UPROPERTY(Edit, Save, Category="Sniper|Aim Sway", DisplayName="Hold Breath Reentry Delay", Min=0.0f, Max=10.0f, Speed=0.1f)
	float HoldBreathReentryDelay = 2.0f;
	UPROPERTY(Edit, Save, Category="Sniper|Aim Sway", DisplayName="Sway Pitch Frequency", Min=0.0f, Max=20.0f, Speed=0.01f)
	float SwayPitchFrequency = 1.85f;
	UPROPERTY(Edit, Save, Category="Sniper|Aim Sway", DisplayName="Sway Yaw Frequency", Min=0.0f, Max=20.0f, Speed=0.01f)
	float SwayYawFrequency = 1.43f;
	UPROPERTY(Edit, Save, Category="Sniper|Aim Sway", DisplayName="Max Hold Breath Gauge", Member=AimSwayState.MaxHoldBreathGauge, Type=Float, Min=0.0f, Max=30.0f, Speed=0.1f);
	UPROPERTY(Edit, Save, Category="Sniper|Aim Sway", DisplayName="Hold Breath Recover Speed", Member=AimSwayState.HoldBreathRecoverSpeed, Type=Float, Min=0.0f, Max=30.0f, Speed=0.1f);
	UPROPERTY(Edit, Save, Category="Sniper|Aim Sway", DisplayName="Hold Breath Consume Speed", Member=AimSwayState.HoldBreathConsumeSpeed, Type=Float, Min=0.0f, Max=30.0f, Speed=0.1f);

	UPROPERTY(Edit, Save, Category="Sniper|Scope Lens", DisplayName="Radius", Min=0.01f, Max=1.0f, Speed=0.01f)
	float ScopeLensRadius = 0.688889f;
	UPROPERTY(Edit, Save, Category="Sniper|Scope Lens", DisplayName="Center X", Min=0.0f, Max=1.0f, Speed=0.001f)
	float ScopeLensCenterX = 0.5f;
	UPROPERTY(Edit, Save, Category="Sniper|Scope Lens", DisplayName="Center Y", Min=0.0f, Max=1.0f, Speed=0.001f)
	float ScopeLensCenterY = 0.5f;
	UPROPERTY(Edit, Save, Category="Sniper|Scope Lens", DisplayName="Feather", Min=0.001f, Max=0.5f, Speed=0.01f)
	float ScopeLensFeather = 0.08f;
	UPROPERTY(Edit, Save, Category="Sniper|Scope Lens", DisplayName="Outer Blur Radius", Min=0.0f, Max=32.0f, Speed=0.1f)
	float ScopeLensOuterBlurRadius = 4.0f;
	UPROPERTY(Edit, Save, Category="Sniper|Scope Lens", DisplayName="Edge Blur Radius", Min=0.0f, Max=16.0f, Speed=0.1f)
	float ScopeLensEdgeBlurRadius = 1.5f;
	UPROPERTY(Edit, Save, Category="Sniper|Scope Lens", DisplayName="Intensity", Min=0.0f, Max=1.0f, Speed=0.01f)
	float ScopeLensIntensity = 1.0f;
	UPROPERTY(Edit, Save, Category="Sniper|Scope Lens", DisplayName="Center Offset X", Min=-1.0f, Max=1.0f, Speed=0.001f)
	float ScopeLensCenterOffsetX = 0.0f;
	UPROPERTY(Edit, Save, Category="Sniper|Scope Lens", DisplayName="Center Offset Y", Min=-1.0f, Max=1.0f, Speed=0.001f)
	float ScopeLensCenterOffsetY = -0.105556f;
	UPROPERTY(Edit, Save, Category="Sniper|Scope Lens", DisplayName="Blend Time", Min=0.0f, Max=2.0f, Speed=0.01f)
	float ScopeLensBlendTime = 0.08f;

	UPROPERTY(Edit, Save, Category="Sniper|Presentation", DisplayName="Enable Bullet Flight Slomo")
	bool bEnableBulletFlightSlomo = true;
	UPROPERTY(Edit, Save, Category="Sniper|Presentation", DisplayName="Bullet Flight Slomo Duration", Min=0.0f, Max=1.0f, Speed=0.01f)
	float BulletFlightSlomoDuration = 0.18f;
	UPROPERTY(Edit, Save, Category="Sniper|Presentation", DisplayName="Bullet Flight Slomo Time Dilation", Min=0.01f, Max=1.0f, Speed=0.01f)
	float BulletFlightSlomoTimeDilation = 0.22f;
	UPROPERTY(Edit, Save, Category="Sniper|Weapon Visual", DisplayName="Enable Weapon Visual")
	bool bEnableWeaponVisual = true;
	UPROPERTY(Edit, Save, Category="Sniper|Weapon Visual", DisplayName="Enable Weapon Hands Mesh")
	bool bEnableWeaponHandsMesh = true;
	UPROPERTY(Edit, Save, Category="Sniper|Weapon Visual", DisplayName="Weapon Hands Mesh", AssetType="SkeletalMesh")
	FSoftObjectPtr WeaponHandsMeshPath = "Content/Data/CombatAI/SK_Bandit_SkeletalMesh.uasset";
	UPROPERTY(Edit, Save, Category="Sniper|Weapon Visual", DisplayName="Weapon Hands Idle Animation")
	FString WeaponHandsIdleAnimationPath = "Content/Data/CombatAI/retargeted_Crouch_Idle_Anim_Unreal_Take.uasset";
	UPROPERTY(Edit, Save, Category="Sniper|Weapon Visual", DisplayName="Weapon Hands Reload Animation")
	FString WeaponHandsReloadAnimationPath = "Content/Data/CombatAI/Reload_Anim_Unreal_Take.uasset";
	UPROPERTY(Edit, Save, Category="Sniper|Weapon Visual", DisplayName="Weapon Hands Location", Type=Vec3, Min=0.0f, Max=0.0f, Speed=0.1f)
	FVector WeaponHandsRelativeLocation = FVector(10.0f, 0.0f, -16.0f);
	UPROPERTY(Edit, Save, Category="Sniper|Weapon Visual", DisplayName="Weapon Hands Rotation", Type=Rotator, Min=0.0f, Max=0.0f, Speed=0.1f)
	FRotator WeaponHandsRelativeRotation = FRotator(0.0f, 90.0f, 0.0f);
	UPROPERTY(Edit, Save, Category="Sniper|Weapon Visual", DisplayName="Weapon Hands Scale", Type=Vec3, Min=0.0f, Max=0.0f, Speed=0.01f)
	FVector WeaponHandsRelativeScale = FVector(1.0f, 1.0f, 1.0f);
	UPROPERTY(Edit, Save, Category="Sniper|Weapon Visual", DisplayName="Weapon Visual Mesh", AssetType="StaticMesh")
	FSoftObjectPtr WeaponVisualMeshPath = "Content/Data/Sniper_Rifle/Sniper_Rifle_merge_StaticMesh.uasset";
	UPROPERTY(Edit, Save, Category="Sniper|Weapon Visual", DisplayName="Weapon Visual Socket")
	FName WeaponVisualSocketName = "GunSocket";
	UPROPERTY(Edit, Save, Category="Sniper|Weapon Visual", DisplayName="Weapon Visual Location", Type=Vec3, Min=0.0f, Max=0.0f, Speed=0.1f)
	FVector WeaponVisualRelativeLocation = FVector(0.03f, 0.125f, 0.07f);
	UPROPERTY(Edit, Save, Category="Sniper|Weapon Visual", DisplayName="Weapon Visual Rotation", Type=Rotator, Min=0.0f, Max=0.0f, Speed=0.1f)
	FRotator WeaponVisualRelativeRotation = FRotator(13.8f, 170.0f, -6.0f);
	UPROPERTY(Edit, Save, Category="Sniper|Weapon Visual", DisplayName="Weapon Visual Scale", Type=Vec3, Min=0.0f, Max=0.0f, Speed=0.01f)
	FVector WeaponVisualRelativeScale = FVector(1.1f, 1.1f, 1.1f);

private:
	void EnsureWeaponVisualComponents();
	void CacheComponentReferences();
	void CacheInputSensitivityBases();
	void SyncSniperRuntimeState();
	void SyncWeaponVisualComponent();
	void UpdateWeaponVisualScopeVisibility();
	bool PlayWeaponHandsAnimation(const FString& AnimationPath, bool bLooping);
	bool PlayWeaponHandsAnimationSyncedToDuration(const FString& AnimationPath, float TargetDuration);
	void PlayWeaponHandsIdleAnimation();
	void UpdateWeaponHandsReloadAnimation();
	void UpdateBulletFlightSlomo(float DeltaTime);
	void UpdateScopeState(float DeltaTime);
	void UpdateHoldBreathState(float DeltaTime);
	void UpdateAimSwayState(float DeltaTime);
	void UpdateRecoilState(float DeltaTime);
	void ApplySniperMovement(float DeltaTime);
	void ApplySniperControlRotation();
	void ConfigureSniperMovementCapsule();
	void ConfigureSniperMovementWalls();
	bool TryMoveSniperWithCollision(const FVector& MoveDelta);
	bool TryApplySniperMovementDelta(const FVector& MoveDelta);
	bool TrySweepSniperMovement(const FVector& MoveDelta, FHitResult& OutHit) const;
	bool TryGetSniperMovementBounds(FVector& OutMinLocation, FVector& OutMaxLocation) const;
	bool IsSniperMovementWallActor(const AActor* Actor) const;
	UBoxComponent* FindSniperMovementWallBox(const FString& ActorName) const;
	UCapsuleComponent* GetSniperMovementCapsule() const;
	void PlaySniperFootstep();
	FRotator BuildEffectiveAimRotation() const;
	bool CanEnterScope() const;
	float ClampScopeZoomMagnification(float Magnification) const;
	float ComputeScopedFOVForMagnification(float Magnification) const;
	float ComputeScopedSensitivityForMagnification(float Magnification) const;
	void AdjustScopeZoomStep(int32 StepDelta);
	void HandleTurnInput(float Value);
	void HandleLookUpInput(float Value);
	void HandleGamepadTurnInput(float Value);
	void HandleGamepadLookUpInput(float Value);
	void HandleMoveForwardInput(float Value);
	void HandleMoveRightInput(float Value);
	void HandleScopeZoomAxis(float Value);
	void HandleGamepadScopeAxis(float Value);
	void HandleGamepadFireAxis(float Value);
	void HandleFirePressed();
	void HandleScopePressed();
	void HandleScopeReleased();
	void HandleHoldBreathPressed();
	void HandleHoldBreathReleased();
	void HandleGamepadHoldBreathPressed();
	void HandleGamepadHoldBreathReleased();
	void HandleSwitchAmmoNormalPressed();
	void HandleSwitchAmmoAntiMaterialPressed();
	void HandleScopeZoomInPressed();
	void HandleScopeZoomOutPressed();
	void HandleReloadPressed();
	void RefreshScopeHeldState();
	void RefreshHoldBreathHeldState();
	void ApplyFireRecoil();
	bool FireCurrentRound();

	TWeakObjectPtr<USceneComponent> SniperRoot;
	TWeakObjectPtr<UCameraComponent> Camera;
	TWeakObjectPtr<USkeletalMeshComponent> WeaponHandsMeshComponent;
	TWeakObjectPtr<UStaticMeshComponent> WeaponVisualComponent;
	TWeakObjectPtr<USniperWeaponComponent> WeaponComponent;
	TWeakObjectPtr<UBallisticBulletManagerComponent> BulletManagerComponent;
	TWeakObjectPtr<UActionComponent> ActionComponent;

	FSniperInputState InputState;
	FScopeState ScopeState;
	FAimSwayState AimSwayState;
	FRecoilState RecoilState;
	float CachedInputDeltaTime = 1.0f / 60.0f;
	bool bMouseScopeInputHeld = false;
	bool bGamepadScopeInputHeld = false;
	bool bKeyboardHoldBreathInputHeld = false;
	bool bGamepadHoldBreathInputHeld = false;
	bool bGamepadFireTriggerHeld = false;
	bool bBulletFlightSlomoActive = false;
	bool bWasWeaponReloading = false;
	bool bWeaponHandsReloadAnimationActive = false;
	bool bInputSensitivityBaseInitialized = false;
	float SniperMoveForwardInput = 0.0f;
	float SniperMoveRightInput = 0.0f;
	float FootstepCooldownRemaining = 0.0f;
	float BaseMouseSensitivity = 0.2f;
	float BaseGamepadLookSensitivity = 90.0f;
};
