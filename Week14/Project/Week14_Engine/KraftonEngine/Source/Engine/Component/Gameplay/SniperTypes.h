#pragma once

#include "Core/Delegate.h"
#include "Core/Types/CoreTypes.h"
#include "Math/Vector.h"
#include "Object/FName.h"
#include "Object/Reflection/ObjectMacros.h"
#include "Object/Reflection/UStruct.h"

#include "Source/Engine/Component/Gameplay/SniperTypes.generated.h"

class AActor;

UENUM()
enum class ESniperAmmoType : uint8
{
	Normal = 0,
	AntiMaterial = 1,

	COUNT
};

UENUM()
enum class ESniperHitOutcome : uint8
{
	Normal = 0,
	Blocked = 1,
	Ricochet = 2,
	Penetrated = 3,

	COUNT
};

UENUM()
enum class ESniperHitRegion : uint8
{
	Unknown = 0,
	Head = 1,
	Torso = 2,
	Arm = 3,
	Leg = 4,

	COUNT
};

inline const char* GetSniperHitRegionName(ESniperHitRegion Region)
{
	switch (Region)
	{
	case ESniperHitRegion::Head:
		return "Head";
	case ESniperHitRegion::Torso:
		return "Torso";
	case ESniperHitRegion::Arm:
		return "Arm";
	case ESniperHitRegion::Leg:
		return "Leg";
	case ESniperHitRegion::Unknown:
	default:
		return "Unknown";
	}
}

inline const char* GetSniperHitRegionDisplayName(ESniperHitRegion Region)
{
	switch (Region)
	{
	case ESniperHitRegion::Head:
		return "HEAD";
	case ESniperHitRegion::Torso:
		return "TORSO";
	case ESniperHitRegion::Arm:
		return "ARM";
	case ESniperHitRegion::Leg:
		return "LEG";
	case ESniperHitRegion::Unknown:
	default:
		return "UNKNOWN";
	}
}

inline float GetDefaultSniperHitScoreMultiplier(ESniperHitRegion Region)
{
	switch (Region)
	{
	case ESniperHitRegion::Head:
		return 2.0f;
	case ESniperHitRegion::Torso:
		return 1.0f;
	case ESniperHitRegion::Arm:
		return 0.65f;
	case ESniperHitRegion::Leg:
		return 0.75f;
	case ESniperHitRegion::Unknown:
	default:
		return 1.0f;
	}
}

inline int32 GetDefaultSniperHitScoreValue(ESniperHitRegion Region)
{
	switch (Region)
	{
	case ESniperHitRegion::Head:
		return 50;
	case ESniperHitRegion::Torso:
		return 20;
	case ESniperHitRegion::Arm:
	case ESniperHitRegion::Leg:
		return 10;
	case ESniperHitRegion::Unknown:
	default:
		return 5;
	}
}

USTRUCT()
struct FSniperInputState
{
	GENERATED_BODY()

	float MouseDeltaX = 0.0f;
	float MouseDeltaY = 0.0f;
	bool bFirePressed = false;
	bool bScopeHeld = false;
	bool bHoldBreathHeld = false;
	bool bReloadPressed = false;
	bool bSwitchAmmoPressed = false;
};

USTRUCT()
struct FScopeState
{
	GENERATED_BODY()

	bool bIsScoped = false;
	float NormalFOV = 1.22173048f;
	float ScopedFOV = 0.26179939f;
	float CurrentFOV = 1.22173048f;
	float TargetFOV = 1.22173048f;
	float NormalSensitivity = 1.0f;
	float ScopedSensitivity = 0.25f;
	float MaxZoomScopedSensitivity = 0.10f;
	float CurrentSensitivity = 1.0f;
	float ScopeBlendSpeed = 12.0f;
	float MinZoomMagnification = 4.0f;
	float MaxZoomMagnification = 16.0f;
	float DefaultZoomMagnification = 4.0f;
	float CurrentZoomMagnification = 4.0f;
	float TargetZoomMagnification = 4.0f;
	float ZoomStep = 1.0f;
};

USTRUCT()
struct FAimSwayState
{
	GENERATED_BODY()

	float Time = 0.0f;
	float BaseSwayAmount = 0.00024f;
	float ScopedSwayAmount = 0.00065f;
	float CurrentSwayPitch = 0.0f;
	float CurrentSwayYaw = 0.0f;
	float BreathMultiplier = 1.0f;
	float HoldBreathGauge = 10.0f;
	float MaxHoldBreathGauge = 10.0f;
	float HoldBreathRecoverSpeed = 1.5f;
	float HoldBreathConsumeSpeed = 1.0f;
	float HoldBreathCooldownRemaining = 0.0f;
	bool bForcedRecovery = false;
	bool bRequireHoldBreathRelease = false;
	bool bWasHoldBreathActive = false;
};

USTRUCT()
struct FRecoilState
{
	GENERATED_BODY()

	float CurrentRecoilPitch = 0.0f;
	float CurrentRecoilYaw = 0.0f;
	float RecoilRecoverSpeed = 8.0f;
	float LastShotRecoilPitch = 0.0f;
	float LastShotRecoilYaw = 0.0f;
};

USTRUCT()
struct FAmmoBallisticData
{
	GENERATED_BODY()

	ESniperAmmoType AmmoType = ESniperAmmoType::Normal;
	float InitialSpeed = 760.0f;
	float MuzzleVelocityVariance = 0.0f;
	float GravityScale = 1.0f;
	float BallisticCoefficient = 0.30f;
	float DragScale = 1.0f;
	float Damage = 100.0f;
	float BulletRadius = 0.03f;
	float VisualScale = 0.06f;
	float VisualTracerWidth = 0.02f;
	float VisualTracerLengthScale = 1.35f;
	float VisualTracerMinLength = 0.10f;
	float VisualTracerMaxLength = 0.75f;
	float LifeTime = 5.0f;
	float FireInterval = 1.0f;
	float WindInfluenceScale = 1.0f;
	float RecoilPitch = 1.2f;
	float RecoilYawRandomRange = 0.25f;
	bool bCanDamageArmor = false;
};

USTRUCT()
struct FBallisticBullet
{
	GENERATED_BODY()

	int32 BulletId = 0;
	FVector Position = FVector::ZeroVector;
	FVector PreviousPosition = FVector::ZeroVector;
	FVector Velocity = FVector::ZeroVector;
	float Damage = 0.0f;
	float Radius = 0.0f;
	float VisualScale = 0.06f;
	float VisualTracerWidth = 0.02f;
	float VisualTracerLengthScale = 1.35f;
	float VisualTracerMinLength = 0.10f;
	float VisualTracerMaxLength = 0.75f;
	float LifeTime = 0.0f;
	float GravityScale = 1.0f;
	float BallisticCoefficient = 0.30f;
	float DragScale = 1.0f;
	float WindInfluenceScale = 1.0f;
	float TraveledDistance = 0.0f;
	ESniperAmmoType AmmoType = ESniperAmmoType::Normal;
	AActor* Owner = nullptr;
	bool bIsAlive = false;
	bool bWasScopedShot = false;
	bool bCanDamageArmor = false;
};

USTRUCT()
struct FBulletCinematicSnapshot
{
	GENERATED_BODY()

	int32 BulletId = 0;
	FVector Position = FVector::ZeroVector;
	FVector PreviousPosition = FVector::ZeroVector;
	FVector Velocity = FVector::ZeroVector;
	float TraveledDistance = 0.0f;
	float LifeTime = 0.0f;
	ESniperAmmoType AmmoType = ESniperAmmoType::Normal;
	AActor* Owner = nullptr;
	bool bIsAlive = false;
	bool bWasScopedShot = false;
};

USTRUCT()
struct FSniperHitInfo
{
	GENERATED_BODY()

	int32 BulletId = 0;
	AActor* HitActor = nullptr;
	FVector HitLocation = FVector::ZeroVector;
	FVector HitNormal = FVector::ZeroVector;
	FVector ShotDirection = FVector::ZeroVector;
	float Damage = 0.0f;
	float TravelDistance = 0.0f;
	float ImpactSpeed = 0.0f;
	float RagdollImpulseStrength = 0.0f;
	ESniperAmmoType AmmoType = ESniperAmmoType::Normal;
	ESniperHitOutcome HitOutcome = ESniperHitOutcome::Normal;
	ESniperHitRegion HitRegion = ESniperHitRegion::Unknown;
	bool bIsScopedShot = false;
	bool bIsHeadshot = false;
	bool bIsArmorPiercing = false;
	bool bShouldRagdoll = false;
	bool bKilled = false;
	bool bFriendlyTarget = false;
	AActor* Shooter = nullptr;
	float RegionDamageMultiplier = 1.0f;
	float TargetCurrentHP = 0.0f;
	float TargetMaxHP = 0.0f;
	FName HitBoneName = FName::None;
	FString HitBodyName;
	FString HitRegionName = "Unknown";
	FString HitRegionDisplayName = "UNKNOWN";
	float HitScoreMultiplier = 1.0f;
	int32 HitScoreValue = 0;
	bool bHasHitBodyCenterDistance = false;
	FVector HitBodyCenterLocation = FVector::ZeroVector;
	float HitBodyCenterDistance = 0.0f;
};

DECLARE_MULTICAST_DELEGATE_OneParam(FBulletSpawnedEventSignature, const FBulletCinematicSnapshot&);
DECLARE_MULTICAST_DELEGATE_OneParam(FSniperHitEventSignature, const FSniperHitInfo&);
