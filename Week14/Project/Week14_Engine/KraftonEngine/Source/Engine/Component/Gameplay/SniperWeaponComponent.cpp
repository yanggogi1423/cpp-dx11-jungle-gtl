#include "Component/Gameplay/SniperWeaponComponent.h"

#include "Audio/AudioManager.h"
#include "Component/Gameplay/BallisticBulletManagerComponent.h"
#include "GameFramework/AActor.h"
#include "GameFramework/World.h"

#include <algorithm>
#include <cstdlib>
#include <cmath>

namespace
{
	constexpr float SniperZeroingMinRangeMeters = 1.0f;
	constexpr float SniperZeroingMaxPitchDegrees = 12.0f;
	constexpr float SniperZeroingSimulationStepSeconds = 1.0f / 240.0f;
	constexpr int32 SniperZeroingSimulationMaxSteps = 2400;
	constexpr int32 SniperZeroingBinarySearchSteps = 16;
	constexpr float SniperSpeedOfSoundMetersPerSecond = 343.0f;
	constexpr float SniperBaseDragScale = 0.00008f;

	float RandomSignedUnit()
	{
		const float Alpha = static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX);
		return (Alpha * 2.0f) - 1.0f;
	}

	FVector RotateAroundAxis(const FVector& Vector, const FVector& Axis, float AngleRadians)
	{
		const FVector NormalizedAxis = Axis.Normalized();
		const float CosTheta = std::cos(AngleRadians);
		const float SinTheta = std::sin(AngleRadians);
		return Vector * CosTheta
			+ FVector::Cross(NormalizedAxis, Vector) * SinTheta
			+ NormalizedAxis * (NormalizedAxis.Dot(Vector) * (1.0f - CosTheta));
	}

	float ComputeMachDragMultiplier(float Speed)
	{
		const float Mach = Speed / SniperSpeedOfSoundMetersPerSecond;
		if (Mach > 1.2f)
		{
			return 1.0f;
		}

		if (Mach > 0.9f)
		{
			const float Alpha = (1.2f - Mach) / 0.3f;
			return FMath::Lerp(1.0f, 1.4f, Alpha);
		}

		return 0.9f;
	}

	FVector ComputeBallisticDragAcceleration(const FVector& Velocity, const FAmmoBallisticData& AmmoData)
	{
		const float Speed = Velocity.Length();
		if (Speed < 1.0f)
		{
			return FVector::ZeroVector;
		}

		const FVector Direction = Velocity / Speed;
		const float SafeBallisticCoefficient = (std::max)(AmmoData.BallisticCoefficient, 0.01f);
		const float MachFactor = ComputeMachDragMultiplier(Speed);

		return Direction * -1.0f
			* Speed
			* Speed
			* SniperBaseDragScale
			* MachFactor
			* AmmoData.DragScale
			/ SafeBallisticCoefficient;
	}

	float SampleZeroingVerticalOffset(
		const FVector& BaseDirection,
		const FVector& PlaneUp,
		const FVector& RightAxis,
		float PitchOffsetDegrees,
		float TargetForwardDistance,
		const FAmmoBallisticData& AmmoData,
		const FVector& WorldGravity)
	{
		// In this engine, negative pitch aims upward, so invert the usual sign here.
		const float PitchOffsetRadians = -PitchOffsetDegrees * (3.14159265358979323846f / 180.0f);
		const FVector SimulatedDirection = RotateAroundAxis(BaseDirection, RightAxis, PitchOffsetRadians).Normalized();
		FVector Position = FVector::ZeroVector;
		FVector Velocity = SimulatedDirection * AmmoData.InitialSpeed;

		float PreviousForwardDistance = 0.0f;
		float PreviousVerticalOffset = 0.0f;

		for (int32 StepIndex = 0; StepIndex < SniperZeroingSimulationMaxSteps; ++StepIndex)
		{
			const FVector DragAcceleration = ComputeBallisticDragAcceleration(Velocity, AmmoData);
			const FVector TotalAcceleration = WorldGravity * AmmoData.GravityScale + DragAcceleration;
			Position += Velocity * SniperZeroingSimulationStepSeconds
				+ TotalAcceleration * (0.5f * SniperZeroingSimulationStepSeconds * SniperZeroingSimulationStepSeconds);
			Velocity += TotalAcceleration * SniperZeroingSimulationStepSeconds;

			const float ForwardDistance = Position.Dot(BaseDirection);
			const float VerticalOffset = Position.Dot(PlaneUp);
			if (ForwardDistance >= TargetForwardDistance)
			{
				const float DistanceSpan = ForwardDistance - PreviousForwardDistance;
				if (DistanceSpan <= 1.0e-6f)
				{
					return VerticalOffset;
				}

				const float Alpha = (TargetForwardDistance - PreviousForwardDistance) / DistanceSpan;
				return PreviousVerticalOffset + (VerticalOffset - PreviousVerticalOffset) * Alpha;
			}

			PreviousForwardDistance = ForwardDistance;
			PreviousVerticalOffset = VerticalOffset;
		}

		return PreviousVerticalOffset;
	}
}

USniperWeaponComponent::USniperWeaponComponent()
{
	bTickEnable = true;
	InitializeDefaultAmmoData();
}

void USniperWeaponComponent::BeginPlay()
{
	UActorComponent::BeginPlay();
	NormalizeMagazineState();
	ResolveBulletManagerComponent();
}

void USniperWeaponComponent::EndPlay()
{
	FireCooldownRemaining = 0.0f;
	ReloadRemaining = 0.0f;
	bIsReloading = false;
	UActorComponent::EndPlay();
}

bool USniperWeaponComponent::SetCurrentAmmoType(ESniperAmmoType InAmmoType)
{
	if (!GetAmmoData(InAmmoType))
	{
		return false;
	}

	CurrentAmmoType = InAmmoType;
	return true;
}

void USniperWeaponComponent::SetZeroRangeMeters(float InZeroRangeMeters)
{
	ZeroRangeMeters = InZeroRangeMeters < 0.0f ? 0.0f : InZeroRangeMeters;
}

const FAmmoBallisticData* USniperWeaponComponent::GetCurrentAmmoData() const
{
	return GetAmmoData(CurrentAmmoType);
}

const FAmmoBallisticData* USniperWeaponComponent::GetAmmoData(ESniperAmmoType InAmmoType) const
{
	for (const FAmmoBallisticData& Entry : AmmoBallisticTable)
	{
		if (Entry.AmmoType == InAmmoType)
		{
			return &Entry;
		}
	}

	return nullptr;
}

bool USniperWeaponComponent::CanFire() const
{
	return FireCooldownRemaining <= 0.0f &&
		!bIsReloading &&
		AmmoInMagazine > 0 &&
		GetCurrentAmmoData() != nullptr;
}

bool USniperWeaponComponent::RequestFire(
	const FVector& MuzzlePosition,
	const FVector& ShotDirection,
	bool bWasScopedShot,
	AActor* Shooter)
{
	const FAmmoBallisticData* AmmoData = GetCurrentAmmoData();
	UBallisticBulletManagerComponent* BulletManager = BulletManagerComponent.Get();
	if (!AmmoData || !BulletManager)
	{
		return false;
	}

	if (ShotDirection.IsNearlyZero())
	{
		return false;
	}

	if (FireCooldownRemaining > 0.0f || bIsReloading)
	{
		return false;
	}

	if (AmmoInMagazine <= 0)
	{
		PlayWeaponSFX(EmptyTriggerSFXPath, EmptyTriggerSFXVolume);
		return false;
	}

	const FVector ZeroedShotDirection = BuildZeroedShotDirection(ShotDirection, *AmmoData);
	if (ZeroedShotDirection.IsNearlyZero())
	{
		return false;
	}

	FBallisticBullet Bullet;
	Bullet.Position = MuzzlePosition;
	Bullet.PreviousPosition = MuzzlePosition;
	const float VelocityVarianceAlpha = AmmoData->MuzzleVelocityVariance > 0.0f
		? RandomSignedUnit() * AmmoData->MuzzleVelocityVariance
		: 0.0f;
	const float FinalInitialSpeed = AmmoData->InitialSpeed * (1.0f + VelocityVarianceAlpha);
	Bullet.Velocity = ZeroedShotDirection * FinalInitialSpeed;
	Bullet.Damage = AmmoData->Damage;
	Bullet.Radius = AmmoData->BulletRadius;
	Bullet.VisualScale = AmmoData->VisualScale;
	Bullet.VisualTracerWidth = AmmoData->VisualTracerWidth;
	Bullet.VisualTracerLengthScale = AmmoData->VisualTracerLengthScale;
	Bullet.VisualTracerMinLength = AmmoData->VisualTracerMinLength;
	Bullet.VisualTracerMaxLength = AmmoData->VisualTracerMaxLength;
	Bullet.LifeTime = AmmoData->LifeTime;
	Bullet.GravityScale = AmmoData->GravityScale;
	Bullet.BallisticCoefficient = AmmoData->BallisticCoefficient;
	Bullet.DragScale = AmmoData->DragScale;
	Bullet.WindInfluenceScale = AmmoData->WindInfluenceScale;
	Bullet.AmmoType = AmmoData->AmmoType;
	Bullet.Owner = Shooter;
	Bullet.bIsAlive = true;
	Bullet.bWasScopedShot = bWasScopedShot;
	Bullet.bCanDamageArmor = AmmoData->bCanDamageArmor;

	if (!BulletManager->SpawnBullet(Bullet))
	{
		return false;
	}

	PlayWeaponSFX(FireSFXPath, FireSFXVolume);
	FireCooldownRemaining = AmmoData->FireInterval;
	AmmoInMagazine = (std::max)(0, AmmoInMagazine - 1);
	return true;
}

bool USniperWeaponComponent::RequestReload()
{
	NormalizeMagazineState();
	if (bIsReloading || MagazineCapacity <= 0 || AmmoInMagazine >= MagazineCapacity)
	{
		return false;
	}

	const float SafeReloadDuration = (std::max)(ReloadDuration, 0.0f);
	if (SafeReloadDuration <= 0.0f)
	{
		CompleteReload();
		return AmmoInMagazine > 0;
	}

	bIsReloading = true;
	ReloadRemaining = SafeReloadDuration;
	PlayWeaponSFX(ReloadSFXPath, ReloadSFXVolume);
	return true;
}

void USniperWeaponComponent::CancelReload()
{
	bIsReloading = false;
	ReloadRemaining = 0.0f;
}

float USniperWeaponComponent::GetReloadProgress() const
{
	if (!bIsReloading)
	{
		return 1.0f;
	}

	if (ReloadDuration <= 0.0f)
	{
		return 1.0f;
	}

	return FMath::Clamp(1.0f - (ReloadRemaining / ReloadDuration), 0.0f, 1.0f);
}

void USniperWeaponComponent::NotifySniperHit(const FSniperHitInfo& HitInfo)
{
	OnSniperHit.Broadcast(HitInfo);
}

void USniperWeaponComponent::TickComponent(
	float DeltaTime,
	ELevelTick TickType,
	FActorComponentTickFunction& ThisTickFunction)
{
	UActorComponent::TickComponent(DeltaTime, TickType, ThisTickFunction);
	(void)TickType;
	(void)ThisTickFunction;

	if (FireCooldownRemaining > 0.0f)
	{
		FireCooldownRemaining -= DeltaTime;
		if (FireCooldownRemaining < 0.0f)
		{
			FireCooldownRemaining = 0.0f;
		}
	}

	if (bIsReloading)
	{
		ReloadRemaining -= DeltaTime;
		if (ReloadRemaining <= 0.0f)
		{
			CompleteReload();
		}
	}
}

FVector USniperWeaponComponent::BuildZeroedShotDirection(const FVector& ShotDirection, const FAmmoBallisticData& AmmoData) const
{
	const FVector BaseDirection = ShotDirection.Normalized();
	if (!bEnableZeroing || ZeroRangeMeters < SniperZeroingMinRangeMeters || BaseDirection.IsNearlyZero())
	{
		return BaseDirection;
	}

	AActor* OwnerActor = GetOwner();
	UWorld* World = OwnerActor ? OwnerActor->GetWorld() : nullptr;
	const FVector WorldGravity = World ? World->GetWorldSettings().Gravity : FVector(0.0f, 0.0f, -9.81f);
	const FVector WorldUp = FVector::UpVector;
	FVector RightAxis = FVector::Cross(WorldUp, BaseDirection);
	if (RightAxis.IsNearlyZero())
	{
		RightAxis = FVector::RightVector;
	}
	else
	{
		RightAxis = RightAxis.Normalized();
	}

	FVector PlaneUp = FVector::Cross(BaseDirection, RightAxis);
	if (PlaneUp.IsNearlyZero())
	{
		return BaseDirection;
	}
	PlaneUp = PlaneUp.Normalized();

	const float UnzeroedOffset = SampleZeroingVerticalOffset(
		BaseDirection,
		PlaneUp,
		RightAxis,
		0.0f,
		ZeroRangeMeters,
		AmmoData,
		WorldGravity);
	if (UnzeroedOffset >= 0.0f)
	{
		return BaseDirection;
	}

	float LowPitch = 0.0f;
	float HighPitch = SniperZeroingMaxPitchDegrees;
	float HighOffset = SampleZeroingVerticalOffset(
		BaseDirection,
		PlaneUp,
		RightAxis,
		HighPitch,
		ZeroRangeMeters,
		AmmoData,
		WorldGravity);

	if (HighOffset < 0.0f)
	{
		return RotateAroundAxis(BaseDirection, RightAxis, -HighPitch * (3.14159265358979323846f / 180.0f)).Normalized();
	}

	for (int32 SearchIndex = 0; SearchIndex < SniperZeroingBinarySearchSteps; ++SearchIndex)
	{
		const float MidPitch = (LowPitch + HighPitch) * 0.5f;
		const float MidOffset = SampleZeroingVerticalOffset(
			BaseDirection,
			PlaneUp,
			RightAxis,
			MidPitch,
			ZeroRangeMeters,
			AmmoData,
			WorldGravity);

		if (MidOffset >= 0.0f)
		{
			HighPitch = MidPitch;
		}
		else
		{
			LowPitch = MidPitch;
		}
	}

	return RotateAroundAxis(BaseDirection, RightAxis, -HighPitch * (3.14159265358979323846f / 180.0f)).Normalized();
}

void USniperWeaponComponent::NormalizeMagazineState()
{
	MagazineCapacity = (std::max)(MagazineCapacity, 1);
	AmmoInMagazine = (std::max)(0, (std::min)(AmmoInMagazine, MagazineCapacity));
	ReloadDuration = (std::max)(ReloadDuration, 0.0f);
	if (!bIsReloading)
	{
		ReloadRemaining = 0.0f;
	}
	else
	{
		ReloadRemaining = FMath::Clamp(ReloadRemaining, 0.0f, ReloadDuration);
	}
}

void USniperWeaponComponent::CompleteReload()
{
	NormalizeMagazineState();
	AmmoInMagazine = MagazineCapacity;
	ReloadRemaining = 0.0f;
	bIsReloading = false;
	NormalizeMagazineState();
}

void USniperWeaponComponent::PlayWeaponSFX(const FString& SoundPath, float VolumeScale) const
{
	if (SoundPath.empty() || VolumeScale <= 0.0f)
	{
		return;
	}

	FAudioManager::Get().PlaySFX(SoundPath, FMath::Clamp(VolumeScale, 0.0f, 1.0f));
}

void USniperWeaponComponent::InitializeDefaultAmmoData()
{
	AmmoBallisticTable.clear();

	FAmmoBallisticData NormalAmmo;
	NormalAmmo.AmmoType = ESniperAmmoType::Normal;
	NormalAmmo.InitialSpeed = 760.0f;
	NormalAmmo.MuzzleVelocityVariance = 0.01f;
	NormalAmmo.GravityScale = 1.0f;
	NormalAmmo.BallisticCoefficient = 0.28f;
	NormalAmmo.DragScale = 1.0f;
	NormalAmmo.Damage = 100.0f;
	NormalAmmo.BulletRadius = 0.03f;
	NormalAmmo.VisualScale = 0.035f;
	NormalAmmo.VisualTracerWidth = 0.015f;
	NormalAmmo.VisualTracerLengthScale = 1.20f;
	NormalAmmo.VisualTracerMinLength = 0.08f;
	NormalAmmo.VisualTracerMaxLength = 0.55f;
	NormalAmmo.LifeTime = 5.0f;
	NormalAmmo.FireInterval = 1.0f;
	NormalAmmo.WindInfluenceScale = 1.0f;
	NormalAmmo.RecoilPitch = 1.2f;
	NormalAmmo.RecoilYawRandomRange = 0.25f;
	NormalAmmo.bCanDamageArmor = false;
	AmmoBallisticTable.push_back(NormalAmmo);

	FAmmoBallisticData AntiMaterialAmmo;
	AntiMaterialAmmo.AmmoType = ESniperAmmoType::AntiMaterial;
	AntiMaterialAmmo.InitialSpeed = 920.0f;
	AntiMaterialAmmo.MuzzleVelocityVariance = 0.005f;
	AntiMaterialAmmo.GravityScale = 0.9f;
	AntiMaterialAmmo.BallisticCoefficient = 0.50f;
	AntiMaterialAmmo.DragScale = 0.9f;
	AntiMaterialAmmo.Damage = 300.0f;
	AntiMaterialAmmo.BulletRadius = 0.05f;
	AntiMaterialAmmo.VisualScale = 0.050f;
	AntiMaterialAmmo.VisualTracerWidth = 0.022f;
	AntiMaterialAmmo.VisualTracerLengthScale = 1.35f;
	AntiMaterialAmmo.VisualTracerMinLength = 0.10f;
	AntiMaterialAmmo.VisualTracerMaxLength = 0.80f;
	AntiMaterialAmmo.LifeTime = 5.0f;
	AntiMaterialAmmo.FireInterval = 1.5f;
	AntiMaterialAmmo.WindInfluenceScale = 0.7f;
	AntiMaterialAmmo.RecoilPitch = 2.5f;
	AntiMaterialAmmo.RecoilYawRandomRange = 0.5f;
	AntiMaterialAmmo.bCanDamageArmor = true;
	AmmoBallisticTable.push_back(AntiMaterialAmmo);
}

void USniperWeaponComponent::ResolveBulletManagerComponent()
{
	if (UBallisticBulletManagerComponent* Existing = BulletManagerComponent.Get())
	{
		if (Existing->GetOwner() == GetOwner())
		{
			return;
		}
	}

	BulletManagerComponent = GetOwner() ? GetOwner()->GetComponentByClass<UBallisticBulletManagerComponent>() : nullptr;
}
