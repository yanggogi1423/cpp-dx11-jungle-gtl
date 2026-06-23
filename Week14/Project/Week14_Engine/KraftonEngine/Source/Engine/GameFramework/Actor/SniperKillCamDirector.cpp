#include "GameFramework/Actor/SniperKillCamDirector.h"

#include "Animation/ActorSequence.h"
#include "Component/ActorSequenceComponent.h"
#include "Component/Camera/CameraComponent.h"
#include "Component/Gameplay/BallisticBulletManagerComponent.h"
#include "Component/Gameplay/KillCamRailRigComponent.h"
#include "Component/PrimitiveComponent.h"
#include "Component/Primitive/StaticMeshComponent.h"
#include "Core/Types/CollisionTypes.h"
#include "GameFramework/Camera/PlayerCameraManager.h"
#include "GameFramework/GameMode/GameplayStatics.h"
#include "GameFramework/GameMode/PlayerController.h"
#include "GameFramework/World.h"
#include "Materials/MaterialManager.h"
#include "Math/MathUtils.h"
#include "Math/Quat.h"
#include "Profiling/Time/Timer.h"
#include "Runtime/Engine.h"
#include "Serialization/PrefabManager.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <unordered_map>

#include "Core/Logging/Log.h"

namespace
{
	TArray<int32> GPendingKillCamBulletIds;
	TArray<int32> GActiveKillCamBulletIds;
	std::unordered_map<int32, TWeakObjectPtr<UBallisticBulletManagerComponent>> GBulletManagersById;
	std::unordered_map<int32, FBulletCinematicSnapshot> GBulletSpawnSnapshotsById;
	std::unordered_map<int32, FBulletCinematicSnapshot> GBulletHitSnapshotsById;
	std::unordered_map<int32, FBulletCinematicSnapshot> GBulletFloorHitSnapshotsById;
	const FName SniperKillCamFloorActorTag("Floor");
	constexpr float SniperKillCamFloorBoundsPadding = 0.25f;
	constexpr float SniperKillCamFloorMinHalfThickness = 0.25f;

	float SmoothStep(float Value)
	{
		const float X = FMath::Clamp(Value, 0.0f, 1.0f);
		return X * X * (3.0f - 2.0f * X);
	}

	float SafeDeltaAlpha(float DeltaTime, float Speed)
	{
		if (DeltaTime <= 0.0f || Speed <= 0.0f)
		{
			return 1.0f;
		}
		return FMath::Clamp(1.0f - std::exp(-Speed * DeltaTime), 0.0f, 1.0f);
	}

	FVector SafeNormal(const FVector& Vector, const FVector& Fallback)
	{
		const float Length = Vector.Length();
		return Length > 1.0e-4f ? Vector / Length : Fallback;
	}

	FVector ScaleVector(const FVector& Vector, float Scale)
	{
		return FVector(Vector.X * Scale, Vector.Y * Scale, Vector.Z * Scale);
	}

	FVector ScaleVectorComponents(const FVector& Vector, const FVector& Scale)
	{
		return FVector(Vector.X * Scale.X, Vector.Y * Scale.Y, Vector.Z * Scale.Z);
	}

	FRotator MakeLookAtRotation(const FVector& Eye, const FVector& Target, const FRotator& Fallback)
	{
		FVector Diff = Target - Eye;
		if (Diff.IsNearlyZero())
		{
			return Fallback;
		}
		Diff.Normalize();

		constexpr float Rad2Deg = 180.0f / 3.14159265358979f;
		FRotator LookRotation = Fallback;
		LookRotation.Pitch = -asinf(FMath::Clamp(Diff.Z, -1.0f, 1.0f)) * Rad2Deg;
		if (std::abs(Diff.Z) < 0.999f)
		{
			LookRotation.Yaw = atan2f(Diff.Y, Diff.X) * Rad2Deg;
		}
		LookRotation.Roll = 0.0f;
		return LookRotation;
	}

	float ApplyUnitAlphaPower(float Alpha, float Power)
	{
		if (Power <= 0.0f || std::abs(Power - 1.0f) <= 0.0001f || Alpha < 0.0f || Alpha > 1.0f)
		{
			return Alpha;
		}
		return std::pow(Alpha, Power);
	}

	float ResolveDrivenRailAlpha(
		float BaseAlpha,
		float OverrideAlpha,
		float AlphaScale,
		float AlphaOffset,
		float AlphaEase,
		float AlphaPower,
		const UKillCamRailRigComponent* Rig)
	{
		float Alpha = OverrideAlpha >= 0.0f
			? OverrideAlpha
			: BaseAlpha * AlphaScale + AlphaOffset;
		Alpha = ApplyUnitAlphaPower(Alpha, AlphaPower);
		if (AlphaEase > 0.0f)
		{
			Alpha = FMath::Lerp(Alpha, SmoothStep(Alpha), FMath::Clamp(AlphaEase, 0.0f, 1.0f));
		}
		if (Rig && Rig->bClampAuthoredRailAlpha)
		{
			const float ClampMin = (std::min)(Rig->RailAlphaClampMin, Rig->RailAlphaClampMax);
			const float ClampMax = (std::max)(Rig->RailAlphaClampMin, Rig->RailAlphaClampMax);
			Alpha = FMath::Clamp(Alpha, ClampMin, ClampMax);
		}
		return Alpha;
	}

	bool HasUsablePath(const FString& Path)
	{
		return !Path.empty() && Path != "None";
	}

	void SetActorPrimitiveVisibility(AActor* Actor, bool bVisible)
	{
		if (!Actor)
		{
			return;
		}

		Actor->SetVisible(bVisible);
		for (UPrimitiveComponent* Primitive : Actor->GetPrimitiveComponents())
		{
			if (Primitive)
			{
				Primitive->SetVisibility(bVisible);
			}
		}
	}

	FRotator DirectionToRotator(const FVector& Direction)
	{
		const FVector SafeDirection = SafeNormal(Direction, FVector::ForwardVector);
		constexpr float Rad2Deg = 180.0f / 3.14159265358979323846f;
		const float Pitch = -std::asin(FMath::Clamp(SafeDirection.Z, -1.0f, 1.0f)) * Rad2Deg;
		const float Yaw = std::atan2(SafeDirection.Y, SafeDirection.X) * Rad2Deg;
		return FRotator(Pitch, Yaw, 0.0f);
	}

	FQuat DirectionToQuat(const FVector& Direction)
	{
		return DirectionToRotator(Direction).ToQuaternion().GetNormalized();
	}

	bool UpdateSegmentAabbInterval(
		float RayStart,
		float RayDirection,
		float BoundsMin,
		float BoundsMax,
		float& InOutEnter,
		float& InOutExit)
	{
		if (std::abs(RayDirection) <= 1.0e-6f)
		{
			return RayStart >= BoundsMin && RayStart <= BoundsMax;
		}

		float T0 = (BoundsMin - RayStart) / RayDirection;
		float T1 = (BoundsMax - RayStart) / RayDirection;
		if (T0 > T1)
		{
			std::swap(T0, T1);
		}

		InOutEnter = (std::max)(InOutEnter, T0);
		InOutExit = (std::min)(InOutExit, T1);
		return InOutEnter <= InOutExit;
	}

	bool IntersectSegmentAabb(const FVector& Start, const FVector& End, const FBoundingBox& Bounds, float& OutT)
	{
		if (!Bounds.IsValid())
		{
			return false;
		}

		const FVector Delta = End - Start;
		if (Delta.IsNearlyZero())
		{
			return false;
		}

		float Enter = 0.0f;
		float Exit = 1.0f;
		if (!UpdateSegmentAabbInterval(Start.X, Delta.X, Bounds.Min.X, Bounds.Max.X, Enter, Exit) ||
			!UpdateSegmentAabbInterval(Start.Y, Delta.Y, Bounds.Min.Y, Bounds.Max.Y, Enter, Exit) ||
			!UpdateSegmentAabbInterval(Start.Z, Delta.Z, Bounds.Min.Z, Bounds.Max.Z, Enter, Exit))
		{
			return false;
		}

		OutT = FMath::Clamp(Enter, 0.0f, 1.0f);
		return true;
	}

	bool ResolveSegmentAabbHitT(const FVector& Start, const FVector& End, const FBoundingBox& Bounds, float& OutT)
	{
		if (!Bounds.IsValid())
		{
			return false;
		}

		if (Bounds.IsContains(Start))
		{
			OutT = 0.0f;
			return true;
		}

		if (Bounds.IsContains(End))
		{
			OutT = 1.0f;
			return true;
		}

		if ((End - Start).IsNearlyZero())
		{
			return false;
		}

		return IntersectSegmentAabb(Start, End, Bounds, OutT);
	}

	FBoundingBox ExpandFloorBounds(FBoundingBox Bounds)
	{
		if (!Bounds.IsValid())
		{
			return Bounds;
		}

		Bounds.Min.X -= SniperKillCamFloorBoundsPadding;
		Bounds.Min.Y -= SniperKillCamFloorBoundsPadding;
		Bounds.Min.Z -= SniperKillCamFloorBoundsPadding;
		Bounds.Max.X += SniperKillCamFloorBoundsPadding;
		Bounds.Max.Y += SniperKillCamFloorBoundsPadding;
		Bounds.Max.Z += SniperKillCamFloorBoundsPadding;

		const float CenterZ = (Bounds.Min.Z + Bounds.Max.Z) * 0.5f;
		const float HalfThickness = (Bounds.Max.Z - Bounds.Min.Z) * 0.5f;
		if (HalfThickness < SniperKillCamFloorMinHalfThickness)
		{
			Bounds.Min.Z = CenterZ - SniperKillCamFloorMinHalfThickness;
			Bounds.Max.Z = CenterZ + SniperKillCamFloorMinHalfThickness;
		}
		return Bounds;
	}

	bool TryBuildFloorHitSnapshot(
		UWorld* World,
		const FBulletCinematicSnapshot& PreviousSnapshot,
		const FBulletCinematicSnapshot& CurrentSnapshot,
		FBulletCinematicSnapshot& OutSnapshot)
	{
		if (!World || CurrentSnapshot.BulletId == 0)
		{
			return false;
		}

		const FVector Start = PreviousSnapshot.Position;
		const FVector End = CurrentSnapshot.Position;

		float BestT = FLT_MAX;
		bool bFoundFloor = false;
		for (AActor* FloorActor : FGameplayStatics::FindActorsByTag(World, SniperKillCamFloorActorTag))
		{
			if (!FloorActor)
			{
				continue;
			}

			for (UPrimitiveComponent* Primitive : FloorActor->GetPrimitiveComponents())
			{
				if (!Primitive)
				{
					continue;
				}

				float HitT = 0.0f;
				const FBoundingBox Bounds = ExpandFloorBounds(Primitive->GetWorldBoundingBox());
				if (ResolveSegmentAabbHitT(Start, End, Bounds, HitT) && HitT < BestT)
				{
					BestT = HitT;
					bFoundFloor = true;
				}
			}
		}

		if (!bFoundFloor || BestT == FLT_MAX)
		{
			return false;
		}

		OutSnapshot = CurrentSnapshot;
		OutSnapshot.Position = Start + (End - Start) * BestT;
		OutSnapshot.PreviousPosition = Start;
		OutSnapshot.bIsAlive = false;
		return true;
	}
}

ASniperKillCamDirector::ASniperKillCamDirector()
{
	bNeedsTick = true;
	bTickInEditor = false;
	PrimaryActorTick.bTickEvenWhenPaused = true;
}

void ASniperKillCamDirector::InitDefaultComponents()
{
	EnsureCameraComponent();
	ResolveRailRigComponent();
	ResolveRailSequenceComponent();
}

void ASniperKillCamDirector::BeginPlay()
{
	AActor::BeginPlay();
	InitDefaultComponents();
}

void ASniperKillCamDirector::EndPlay()
{
	StopKillCam();
	if (APlayerCameraManager* Manager = ResolveCameraManager())
	{
		if (UCameraComponent* Camera = CinematicCamera.Get())
		{
			Manager->UnregisterCamera(Camera);
		}
	}
	AActor::EndPlay();
}

void ASniperKillCamDirector::Tick(float DeltaTime)
{
	AActor::Tick(DeltaTime);
	if (!bPlaying)
	{
		return;
	}
	if (GBulletFloorHitSnapshotsById.find(ActiveBulletId) != GBulletFloorHitSnapshotsById.end())
	{
		SetBulletVisualVisible(false);
		return;
	}

	const FTimer* Timer = GEngine ? GEngine->GetTimer() : nullptr;
	const float KillCamDeltaTime = (DeltaTime > 0.0f)
		? DeltaTime
		: (Timer ? Timer->GetRawDeltaTime() : 0.0f);
	Elapsed += KillCamDeltaTime;
	const FBulletCinematicSnapshot PreviousSnapshot = LastSnapshot;
	FBulletCinematicSnapshot Snapshot;
	const float PreviousRailAlpha = Duration > 0.0f ? FMath::Clamp((Elapsed - KillCamDeltaTime) / Duration, 0.0f, 1.0f) : 0.0f;
	float RailAlpha = Duration > 0.0f ? FMath::Clamp(Elapsed / Duration, 0.0f, 1.0f) : 1.0f;
	if (bHasHitSnapshot)
	{
		Snapshot = BuildPlaybackSnapshot(RailAlpha);
		LastSnapshot = Snapshot;
	}
	else if (ResolveBulletSnapshot(Snapshot))
	{
		LastSnapshot = Snapshot;
	}
	else
	{
		Snapshot = LastSnapshot;
		Snapshot.Position += Snapshot.Velocity * KillCamDeltaTime;
		Snapshot.PreviousPosition = LastSnapshot.Position;
		Snapshot.bIsAlive = false;
		LastSnapshot = Snapshot;
	}

	const FBulletCinematicSnapshot CurrentVisualSnapshot = BuildBulletVisualCollisionSnapshot(Snapshot, RailAlpha);
	const FBulletCinematicSnapshot PreviousVisualSnapshot = bHasLastVisualCollisionSnapshot
		? LastVisualCollisionSnapshot
		: BuildBulletVisualCollisionSnapshot(PreviousSnapshot, PreviousRailAlpha);
	FBulletCinematicSnapshot FloorSnapshot;
	if (TryBuildFloorHitSnapshot(GetWorld(), PreviousVisualSnapshot, CurrentVisualSnapshot, FloorSnapshot))
	{
		NotifyBulletFloorHit(FloorSnapshot);
		UE_LOG(
			"[SniperKillCam] Visual bullet floor hit: BulletId=%d Position=(%.3f, %.3f, %.3f)",
			FloorSnapshot.BulletId,
			FloorSnapshot.Position.X,
			FloorSnapshot.Position.Y,
			FloorSnapshot.Position.Z);
		SetBulletVisualVisible(false);
		return;
	}
	LastVisualCollisionSnapshot = CurrentVisualSnapshot;
	bHasLastVisualCollisionSnapshot = true;

	ScrubRailSequence(RailAlpha);
	UpdateCameraFromSnapshot(Snapshot, KillCamDeltaTime, RailAlpha);
	UpdateBulletVisualFromSnapshot(Snapshot, RailAlpha);
	UpdateShockWaveFromSnapshot(Snapshot, RailAlpha);
}

bool ASniperKillCamDirector::StartForBulletId(int32 BulletId, float InDuration, int32 CameraMode)
{
	if (BulletId == 0)
	{
		return false;
	}

	EnsureCameraComponent();
	UCameraComponent* Camera = CinematicCamera.Get();
	APlayerCameraManager* Manager = ResolveCameraManager();
	if (!Camera || !Manager)
	{
		return false;
	}

	auto ManagerIt = GBulletManagersById.find(BulletId);
	ActiveBulletManager = ManagerIt != GBulletManagersById.end() ? ManagerIt->second.Get() : nullptr;

	ActiveBulletId = BulletId;
	Duration = InDuration > 0.0f ? InDuration : DefaultDuration;
	Elapsed = 0.0f;
	const int32 ClampedCameraMode = (std::max)(0, (std::min)(CameraMode, 2));
	ActiveCameraMode = static_cast<ESniperKillCamCameraMode>(ClampedCameraMode);

	auto HitSnapshotIt = GBulletHitSnapshotsById.find(BulletId);
	auto SpawnSnapshotIt = GBulletSpawnSnapshotsById.find(BulletId);
	if (SpawnSnapshotIt != GBulletSpawnSnapshotsById.end())
	{
		LastSnapshot = SpawnSnapshotIt->second;
	}
	else if (HitSnapshotIt != GBulletHitSnapshotsById.end())
	{
		const FVector Direction = SafeNormal(HitSnapshotIt->second.Velocity, FVector::ForwardVector);
		LastSnapshot = HitSnapshotIt->second;
		LastSnapshot.Position = HitSnapshotIt->second.Position - Direction * (std::max)(HitSnapshotIt->second.TraveledDistance, 10.0f);
		LastSnapshot.PreviousPosition = LastSnapshot.Position - Direction * 0.1f;
		LastSnapshot.TraveledDistance = 0.0f;
		LastSnapshot.bIsAlive = true;
	}
	else if (!ResolveBulletSnapshot(LastSnapshot))
	{
		return false;
	}
	StartSnapshot = LastSnapshot;
	if (HitSnapshotIt != GBulletHitSnapshotsById.end())
	{
		HitSnapshot = HitSnapshotIt->second;
		bHasHitSnapshot = true;
	}
	else
	{
		HitSnapshot = FBulletCinematicSnapshot();
		bHasHitSnapshot = false;
	}
	EnsureBulletVisualComponent();
	ScrubRailSequence(0.0f);

	PreviousViewTarget = Manager->GetViewTarget();
	PreviousActiveCamera = Manager->GetActiveCamera();
	if (UKillCamRailRigComponent* Rig = bUseRailRigComponent ? ResolveRailRigComponent() : nullptr)
	{
		Camera->SetFOV(Rig->FOV);
	}
	else
	{
		Camera->SetFOV(KillCamFOV);
	}
	Manager->RegisterCamera(Camera);
	Manager->SetViewTarget(this);
	Manager->SetDepthOfField(1.0f, DOFFocusRange, DOFBlurRadius);
	Camera->SetLetterboxEnabled(false);
	Camera->SetLetterboxAmount(0.0f);
	Camera->SetLetterboxThickness(0.0f);

	GActiveKillCamBulletIds.erase(
		std::remove(GActiveKillCamBulletIds.begin(), GActiveKillCamBulletIds.end(), ActiveBulletId),
		GActiveKillCamBulletIds.end());
	GActiveKillCamBulletIds.push_back(ActiveBulletId);
	bPlaying = true;
	UpdateCameraFromSnapshot(LastSnapshot, 0.0f, 0.0f);
	UpdateBulletVisualFromSnapshot(LastSnapshot, 0.0f);
	UpdateShockWaveFromSnapshot(LastSnapshot, 0.0f);
	LastVisualCollisionSnapshot = BuildBulletVisualCollisionSnapshot(LastSnapshot, 0.0f);
	bHasLastVisualCollisionSnapshot = true;
	SetBulletVisualVisible(true);
	return true;
}

void ASniperKillCamDirector::StopKillCam()
{
	if (!bPlaying)
	{
		return;
	}

	const int32 StoppedBulletId = ActiveBulletId;
	ClearShockWave();
	bPlaying = false;
	ActiveBulletId = 0;
	Elapsed = 0.0f;
	Duration = 0.0f;
	ActiveBulletManager = nullptr;
	GBulletManagersById.erase(StoppedBulletId);
	GBulletSpawnSnapshotsById.erase(StoppedBulletId);
	GBulletHitSnapshotsById.erase(StoppedBulletId);
	GBulletFloorHitSnapshotsById.erase(StoppedBulletId);
	GActiveKillCamBulletIds.erase(
		std::remove(GActiveKillCamBulletIds.begin(), GActiveKillCamBulletIds.end(), StoppedBulletId),
		GActiveKillCamBulletIds.end());
	StartSnapshot = FBulletCinematicSnapshot();
	HitSnapshot = FBulletCinematicSnapshot();
	LastSnapshot = FBulletCinematicSnapshot();
	LastVisualCollisionSnapshot = FBulletCinematicSnapshot();
	bHasHitSnapshot = false;
	bHasLastVisualCollisionSnapshot = false;
	SetBulletVisualVisible(false);
	DestroyBulletVisualActor();
	RestorePreviousCamera();
}

void ASniperKillCamDirector::NotifyBulletSpawned(
	UBallisticBulletManagerComponent* Manager,
	const FBulletCinematicSnapshot& Snapshot)
{
	if (!IsValid(Manager) || Snapshot.BulletId == 0)
	{
		return;
	}

	GBulletManagersById[Snapshot.BulletId] = Manager;
	GBulletSpawnSnapshotsById[Snapshot.BulletId] = Snapshot;
}

void ASniperKillCamDirector::NotifyBulletHit(const FSniperHitInfo& HitInfo)
{
	if (HitInfo.BulletId == 0)
	{
		return;
	}

	const FVector ShotDirection = SafeNormal(HitInfo.ShotDirection, FVector::ForwardVector);
	const float TravelDistance = (std::max)(HitInfo.TravelDistance, 0.0f);
	const FVector ReconstructedStart = TravelDistance > 0.001f
		? HitInfo.HitLocation - ShotDirection * TravelDistance
		: HitInfo.HitLocation - ShotDirection * 10.0f;
	const float ImpactSpeed = (std::max)(HitInfo.ImpactSpeed, 1.0f);

	FBulletCinematicSnapshot ReconstructedStartSnapshot;
	ReconstructedStartSnapshot.BulletId = HitInfo.BulletId;
	ReconstructedStartSnapshot.Position = ReconstructedStart;
	ReconstructedStartSnapshot.PreviousPosition = ReconstructedStart - ShotDirection * 0.1f;
	ReconstructedStartSnapshot.Velocity = ShotDirection * ImpactSpeed;
	ReconstructedStartSnapshot.TraveledDistance = 0.0f;
	ReconstructedStartSnapshot.LifeTime = 0.0f;
	ReconstructedStartSnapshot.AmmoType = HitInfo.AmmoType;
	ReconstructedStartSnapshot.Owner = HitInfo.Shooter;
	ReconstructedStartSnapshot.bIsAlive = true;
	ReconstructedStartSnapshot.bWasScopedShot = HitInfo.bIsScopedShot;

	FBulletCinematicSnapshot Snapshot;
	Snapshot.BulletId = HitInfo.BulletId;
	Snapshot.Position = HitInfo.HitLocation;
	Snapshot.PreviousPosition = HitInfo.HitLocation - ShotDirection * 0.1f;
	Snapshot.Velocity = ShotDirection * ImpactSpeed;
	Snapshot.TraveledDistance = TravelDistance;
	Snapshot.LifeTime = 0.0f;
	Snapshot.AmmoType = HitInfo.AmmoType;
	Snapshot.Owner = HitInfo.Shooter;
	Snapshot.bIsAlive = false;
	Snapshot.bWasScopedShot = HitInfo.bIsScopedShot;

	GBulletSpawnSnapshotsById[HitInfo.BulletId] = ReconstructedStartSnapshot;
	GBulletHitSnapshotsById[HitInfo.BulletId] = Snapshot;
	GPendingKillCamBulletIds.push_back(HitInfo.BulletId);
}

void ASniperKillCamDirector::NotifyBulletFloorHit(const FBulletCinematicSnapshot& Snapshot)
{
	if (Snapshot.BulletId == 0)
	{
		return;
	}

	FBulletCinematicSnapshot FloorSnapshot = Snapshot;
	FloorSnapshot.bIsAlive = false;
	const bool bIsActiveKillCamBullet =
		std::find(GActiveKillCamBulletIds.begin(), GActiveKillCamBulletIds.end(), Snapshot.BulletId) !=
		GActiveKillCamBulletIds.end();
	if (bIsActiveKillCamBullet)
	{
		GBulletFloorHitSnapshotsById[Snapshot.BulletId] = FloorSnapshot;
	}
	GBulletManagersById.erase(Snapshot.BulletId);
	GBulletSpawnSnapshotsById.erase(Snapshot.BulletId);
	GBulletHitSnapshotsById.erase(Snapshot.BulletId);
	GPendingKillCamBulletIds.erase(
		std::remove(GPendingKillCamBulletIds.begin(), GPendingKillCamBulletIds.end(), Snapshot.BulletId),
		GPendingKillCamBulletIds.end());
}

bool ASniperKillCamDirector::GetHitSnapshotForBulletId(int32 BulletId, FBulletCinematicSnapshot& OutSnapshot)
{
	if (BulletId == 0)
	{
		return false;
	}

	auto HitSnapshotIt = GBulletHitSnapshotsById.find(BulletId);
	if (HitSnapshotIt == GBulletHitSnapshotsById.end())
	{
		return false;
	}

	OutSnapshot = HitSnapshotIt->second;
	return true;
}

bool ASniperKillCamDirector::ConsumeFloorHitForBulletId(int32 BulletId, FBulletCinematicSnapshot& OutSnapshot)
{
	if (BulletId == 0)
	{
		return false;
	}

	auto FloorHitIt = GBulletFloorHitSnapshotsById.find(BulletId);
	if (FloorHitIt == GBulletFloorHitSnapshotsById.end())
	{
		return false;
	}

	OutSnapshot = FloorHitIt->second;
	GBulletFloorHitSnapshotsById.erase(FloorHitIt);
	GBulletManagersById.erase(BulletId);
	GBulletSpawnSnapshotsById.erase(BulletId);
	GBulletHitSnapshotsById.erase(BulletId);
	GPendingKillCamBulletIds.erase(
		std::remove(GPendingKillCamBulletIds.begin(), GPendingKillCamBulletIds.end(), BulletId),
		GPendingKillCamBulletIds.end());
	return true;
}

bool ASniperKillCamDirector::HasBulletRecord(int32 BulletId)
{
	if (BulletId == 0)
	{
		return false;
	}

	return std::find(GPendingKillCamBulletIds.begin(), GPendingKillCamBulletIds.end(), BulletId) != GPendingKillCamBulletIds.end()
		|| std::find(GActiveKillCamBulletIds.begin(), GActiveKillCamBulletIds.end(), BulletId) != GActiveKillCamBulletIds.end()
		|| GBulletManagersById.find(BulletId) != GBulletManagersById.end()
		|| GBulletSpawnSnapshotsById.find(BulletId) != GBulletSpawnSnapshotsById.end()
		|| GBulletHitSnapshotsById.find(BulletId) != GBulletHitSnapshotsById.end()
		|| GBulletFloorHitSnapshotsById.find(BulletId) != GBulletFloorHitSnapshotsById.end();
}

bool ASniperKillCamDirector::IsBulletPendingOrActive(int32 BulletId)
{
	if (BulletId == 0)
	{
		return false;
	}

	return std::find(GPendingKillCamBulletIds.begin(), GPendingKillCamBulletIds.end(), BulletId) != GPendingKillCamBulletIds.end()
		|| std::find(GActiveKillCamBulletIds.begin(), GActiveKillCamBulletIds.end(), BulletId) != GActiveKillCamBulletIds.end();
}

bool ASniperKillCamDirector::CheckFloorHitInWorld(UWorld* World, int32 BulletId, FBulletCinematicSnapshot& OutSnapshot)
{
	ASniperKillCamDirector* Director = FindDirectorForWorld(World);
	if (!Director || BulletId == 0 || Director->ActiveBulletId != BulletId)
	{
		return false;
	}

	return Director->CheckFloorHitNow(OutSnapshot);
}

int32 ASniperKillCamDirector::ConsumePendingBulletId()
{
	if (GPendingKillCamBulletIds.empty())
	{
		return 0;
	}

	const int32 BulletId = GPendingKillCamBulletIds.front();
	GPendingKillCamBulletIds.erase(GPendingKillCamBulletIds.begin());
	return BulletId;
}

void ASniperKillCamDirector::ClearPendingBullets()
{
	for (int32 BulletId : GPendingKillCamBulletIds)
	{
		GBulletManagersById.erase(BulletId);
		GBulletSpawnSnapshotsById.erase(BulletId);
		GBulletHitSnapshotsById.erase(BulletId);
	}
	GPendingKillCamBulletIds.clear();
	GActiveKillCamBulletIds.clear();
	GBulletFloorHitSnapshotsById.clear();
}

ASniperKillCamDirector* ASniperKillCamDirector::EnsureDirectorForWorld(UWorld* World)
{
	if (!World)
	{
		return nullptr;
	}

	if (ASniperKillCamDirector* Existing = FindDirectorForWorld(World))
	{
		return Existing;
	}

	ASniperKillCamDirector* Director = World->SpawnActor<ASniperKillCamDirector>();
	if (Director)
	{
		Director->SetFName(FName("SniperKillCamDirector"));
		Director->EnsureCameraComponent();
	}
	return Director;
}

ASniperKillCamDirector* ASniperKillCamDirector::FindDirectorForWorld(UWorld* World)
{
	if (!World)
	{
		return nullptr;
	}

	for (AActor* Actor : World->GetActors())
	{
		if (ASniperKillCamDirector* Director = Cast<ASniperKillCamDirector>(Actor))
		{
			return Director;
		}
	}
	return nullptr;
}

bool ASniperKillCamDirector::StartForBulletIdInWorld(UWorld* World, int32 BulletId, float Duration, int32 CameraMode)
{
	ASniperKillCamDirector* Director = EnsureDirectorForWorld(World);
	return Director ? Director->StartForBulletId(BulletId, Duration, CameraMode) : false;
}

void ASniperKillCamDirector::StopInWorld(UWorld* World)
{
	if (ASniperKillCamDirector* Director = FindDirectorForWorld(World))
	{
		Director->StopKillCam();
	}
}

bool ASniperKillCamDirector::IsPlayingInWorld(UWorld* World)
{
	if (ASniperKillCamDirector* Director = FindDirectorForWorld(World))
	{
		return Director->IsPlaying();
	}
	return false;
}

bool ASniperKillCamDirector::SetRailRigScalarInWorld(UWorld* World, const FString& PropertyName, float Value)
{
	ASniperKillCamDirector* Director = EnsureDirectorForWorld(World);
	return Director ? Director->SetRailRigScalar(PropertyName, Value) : false;
}

float ASniperKillCamDirector::GetRailRigScalarInWorld(
	UWorld* World,
	const FString& PropertyName,
	float DefaultValue)
{
	ASniperKillCamDirector* Director = FindDirectorForWorld(World);
	return Director ? Director->GetRailRigScalar(PropertyName, DefaultValue) : DefaultValue;
}

bool ASniperKillCamDirector::SetKillCamScalarInWorld(UWorld* World, const FString& PropertyName, float Value)
{
	ASniperKillCamDirector* Director = EnsureDirectorForWorld(World);
	return Director ? Director->SetKillCamScalar(PropertyName, Value) : false;
}

float ASniperKillCamDirector::GetKillCamScalarInWorld(
	UWorld* World,
	const FString& PropertyName,
	float DefaultValue)
{
	ASniperKillCamDirector* Director = FindDirectorForWorld(World);
	return Director ? Director->GetKillCamScalar(PropertyName, DefaultValue) : DefaultValue;
}

bool ASniperKillCamDirector::SetKillCamStringInWorld(
	UWorld* World,
	const FString& PropertyName,
	const FString& Value)
{
	ASniperKillCamDirector* Director = EnsureDirectorForWorld(World);
	return Director ? Director->SetKillCamString(PropertyName, Value) : false;
}

FString ASniperKillCamDirector::GetKillCamStringInWorld(
	UWorld* World,
	const FString& PropertyName,
	const FString& DefaultValue)
{
	ASniperKillCamDirector* Director = FindDirectorForWorld(World);
	return Director ? Director->GetKillCamString(PropertyName, DefaultValue) : DefaultValue;
}

bool ASniperKillCamDirector::SetKillCamVectorInWorld(
	UWorld* World,
	const FString& PropertyName,
	const FVector& Value)
{
	ASniperKillCamDirector* Director = EnsureDirectorForWorld(World);
	return Director ? Director->SetKillCamVector(PropertyName, Value) : false;
}

FVector ASniperKillCamDirector::GetKillCamVectorInWorld(
	UWorld* World,
	const FString& PropertyName,
	const FVector& DefaultValue)
{
	ASniperKillCamDirector* Director = FindDirectorForWorld(World);
	return Director ? Director->GetKillCamVector(PropertyName, DefaultValue) : DefaultValue;
}

bool ASniperKillCamDirector::SetKillCamRotatorInWorld(
	UWorld* World,
	const FString& PropertyName,
	const FRotator& Value)
{
	ASniperKillCamDirector* Director = EnsureDirectorForWorld(World);
	return Director ? Director->SetKillCamRotator(PropertyName, Value) : false;
}

FRotator ASniperKillCamDirector::GetKillCamRotatorInWorld(
	UWorld* World,
	const FString& PropertyName,
	const FRotator& DefaultValue)
{
	ASniperKillCamDirector* Director = FindDirectorForWorld(World);
	return Director ? Director->GetKillCamRotator(PropertyName, DefaultValue) : DefaultValue;
}

bool ASniperKillCamDirector::SetRailRigScalar(const FString& PropertyName, float Value)
{
	UKillCamRailRigComponent* Rig = ResolveRailRigComponent();
	if (!Rig || PropertyName.empty())
	{
		return false;
	}

#define SET_RIG_FLOAT(Name) if (PropertyName == #Name) { Rig->Name = Value; return true; }
#define SET_RIG_BOOL(Name) if (PropertyName == #Name) { Rig->Name = Value != 0.0f; return true; }
	SET_RIG_FLOAT(ForwardOffset)
	SET_RIG_FLOAT(SideOffset)
	SET_RIG_FLOAT(UpOffset)
	SET_RIG_FLOAT(LookAhead)
	SET_RIG_FLOAT(LookSideOffset)
	SET_RIG_FLOAT(LookUpOffset)
	SET_RIG_BOOL(bLookAtBulletVisual)
	SET_RIG_FLOAT(CameraRailAlphaOverride)
	SET_RIG_FLOAT(CameraRailAlphaScale)
	SET_RIG_FLOAT(CameraRailAlphaOffset)
	SET_RIG_FLOAT(CameraRailAlphaEase)
	SET_RIG_FLOAT(CameraRailAlphaPower)
	SET_RIG_FLOAT(LookRailAlphaOverride)
	SET_RIG_FLOAT(LookRailAlphaScale)
	SET_RIG_FLOAT(LookRailAlphaOffset)
	SET_RIG_FLOAT(LookRailAlphaEase)
	SET_RIG_FLOAT(LookRailAlphaPower)
	SET_RIG_BOOL(bAllowRailExtrapolation)
	SET_RIG_BOOL(bClampAuthoredRailAlpha)
	SET_RIG_FLOAT(RailAlphaClampMin)
	SET_RIG_FLOAT(RailAlphaClampMax)
	SET_RIG_BOOL(bScaleOffsetsByShotDistance)
	SET_RIG_FLOAT(ReferenceDistance)
	SET_RIG_FLOAT(MinDistanceScale)
	SET_RIG_FLOAT(MaxDistanceScale)
	SET_RIG_FLOAT(LinearOffsetDistanceScaleBlend)
	SET_RIG_FLOAT(OrbitRadiusDistanceScaleBlend)
	SET_RIG_FLOAT(FOV)
	SET_RIG_FLOAT(Roll)
	SET_RIG_FLOAT(CameraLagSpeed)
	SET_RIG_FLOAT(LookLagSpeed)
	SET_RIG_FLOAT(CameraShakeAmplitude)
	SET_RIG_FLOAT(CameraShakeFrequency)
	SET_RIG_FLOAT(OrbitBlend)
	SET_RIG_FLOAT(OrbitYaw)
	SET_RIG_FLOAT(OrbitPitch)
	SET_RIG_FLOAT(OrbitRadius)
	SET_RIG_FLOAT(OrbitPivotForwardOffset)
	SET_RIG_FLOAT(OrbitPivotSideOffset)
	SET_RIG_FLOAT(OrbitPivotUpOffset)
	SET_RIG_FLOAT(DOFFocusRange)
	SET_RIG_FLOAT(DOFBlurRadius)
	SET_RIG_FLOAT(BulletForwardOffset)
	SET_RIG_FLOAT(BulletSideOffset)
	SET_RIG_FLOAT(BulletUpOffset)
	SET_RIG_FLOAT(BulletScaleMultiplier)
	SET_RIG_FLOAT(BulletScaleXMultiplier)
	SET_RIG_FLOAT(BulletScaleYMultiplier)
	SET_RIG_FLOAT(BulletScaleZMultiplier)
	SET_RIG_FLOAT(BulletPitchOffset)
	SET_RIG_FLOAT(BulletYawOffset)
	SET_RIG_FLOAT(BulletRollOffset)
	SET_RIG_FLOAT(BulletSpinRevolutions)
	SET_RIG_FLOAT(BulletSpinPhase)
	SET_RIG_FLOAT(BulletRailAlphaOverride)
	SET_RIG_FLOAT(BulletRailAlphaScale)
	SET_RIG_FLOAT(BulletRailAlphaOffset)
	SET_RIG_FLOAT(BulletRailAlphaEase)
	SET_RIG_FLOAT(BulletRailAlphaPower)
	SET_RIG_BOOL(bEnableShockWave)
	SET_RIG_FLOAT(ShockWaveForwardOffset)
	SET_RIG_FLOAT(ShockWaveSideOffset)
	SET_RIG_FLOAT(ShockWaveUpOffset)
	SET_RIG_FLOAT(ShockWaveRadius)
	SET_RIG_FLOAT(ShockWaveStartRadiusBoost)
	SET_RIG_FLOAT(ShockWaveWidth)
	SET_RIG_FLOAT(ShockWaveStrength)
	SET_RIG_FLOAT(ShockWaveStartStrengthBoost)
	SET_RIG_FLOAT(ShockWaveFalloff)
	SET_RIG_FLOAT(ShockWaveDirectionalStretch)
	SET_RIG_FLOAT(ShockWaveDecay)
#undef SET_RIG_BOOL
#undef SET_RIG_FLOAT
	return false;
}

float ASniperKillCamDirector::GetRailRigScalar(const FString& PropertyName, float DefaultValue) const
{
	const UKillCamRailRigComponent* Rig = GetComponentByClass<UKillCamRailRigComponent>();
	if (!Rig || PropertyName.empty())
	{
		return DefaultValue;
	}

#define GET_RIG_FLOAT(Name) if (PropertyName == #Name) { return Rig->Name; }
#define GET_RIG_BOOL(Name) if (PropertyName == #Name) { return Rig->Name ? 1.0f : 0.0f; }
	GET_RIG_FLOAT(ForwardOffset)
	GET_RIG_FLOAT(SideOffset)
	GET_RIG_FLOAT(UpOffset)
	GET_RIG_FLOAT(LookAhead)
	GET_RIG_FLOAT(LookSideOffset)
	GET_RIG_FLOAT(LookUpOffset)
	GET_RIG_BOOL(bLookAtBulletVisual)
	GET_RIG_FLOAT(CameraRailAlphaOverride)
	GET_RIG_FLOAT(CameraRailAlphaScale)
	GET_RIG_FLOAT(CameraRailAlphaOffset)
	GET_RIG_FLOAT(CameraRailAlphaEase)
	GET_RIG_FLOAT(CameraRailAlphaPower)
	GET_RIG_FLOAT(LookRailAlphaOverride)
	GET_RIG_FLOAT(LookRailAlphaScale)
	GET_RIG_FLOAT(LookRailAlphaOffset)
	GET_RIG_FLOAT(LookRailAlphaEase)
	GET_RIG_FLOAT(LookRailAlphaPower)
	GET_RIG_BOOL(bAllowRailExtrapolation)
	GET_RIG_BOOL(bClampAuthoredRailAlpha)
	GET_RIG_FLOAT(RailAlphaClampMin)
	GET_RIG_FLOAT(RailAlphaClampMax)
	GET_RIG_BOOL(bScaleOffsetsByShotDistance)
	GET_RIG_FLOAT(ReferenceDistance)
	GET_RIG_FLOAT(MinDistanceScale)
	GET_RIG_FLOAT(MaxDistanceScale)
	GET_RIG_FLOAT(LinearOffsetDistanceScaleBlend)
	GET_RIG_FLOAT(OrbitRadiusDistanceScaleBlend)
	GET_RIG_FLOAT(FOV)
	GET_RIG_FLOAT(Roll)
	GET_RIG_FLOAT(CameraLagSpeed)
	GET_RIG_FLOAT(LookLagSpeed)
	GET_RIG_FLOAT(CameraShakeAmplitude)
	GET_RIG_FLOAT(CameraShakeFrequency)
	GET_RIG_FLOAT(OrbitBlend)
	GET_RIG_FLOAT(OrbitYaw)
	GET_RIG_FLOAT(OrbitPitch)
	GET_RIG_FLOAT(OrbitRadius)
	GET_RIG_FLOAT(OrbitPivotForwardOffset)
	GET_RIG_FLOAT(OrbitPivotSideOffset)
	GET_RIG_FLOAT(OrbitPivotUpOffset)
	GET_RIG_FLOAT(DOFFocusRange)
	GET_RIG_FLOAT(DOFBlurRadius)
	GET_RIG_FLOAT(BulletForwardOffset)
	GET_RIG_FLOAT(BulletSideOffset)
	GET_RIG_FLOAT(BulletUpOffset)
	GET_RIG_FLOAT(BulletScaleMultiplier)
	GET_RIG_FLOAT(BulletScaleXMultiplier)
	GET_RIG_FLOAT(BulletScaleYMultiplier)
	GET_RIG_FLOAT(BulletScaleZMultiplier)
	GET_RIG_FLOAT(BulletPitchOffset)
	GET_RIG_FLOAT(BulletYawOffset)
	GET_RIG_FLOAT(BulletRollOffset)
	GET_RIG_FLOAT(BulletSpinRevolutions)
	GET_RIG_FLOAT(BulletSpinPhase)
	GET_RIG_FLOAT(BulletRailAlphaOverride)
	GET_RIG_FLOAT(BulletRailAlphaScale)
	GET_RIG_FLOAT(BulletRailAlphaOffset)
	GET_RIG_FLOAT(BulletRailAlphaEase)
	GET_RIG_FLOAT(BulletRailAlphaPower)
	GET_RIG_BOOL(bEnableShockWave)
	GET_RIG_FLOAT(ShockWaveForwardOffset)
	GET_RIG_FLOAT(ShockWaveSideOffset)
	GET_RIG_FLOAT(ShockWaveUpOffset)
	GET_RIG_FLOAT(ShockWaveRadius)
	GET_RIG_FLOAT(ShockWaveStartRadiusBoost)
	GET_RIG_FLOAT(ShockWaveWidth)
	GET_RIG_FLOAT(ShockWaveStrength)
	GET_RIG_FLOAT(ShockWaveStartStrengthBoost)
	GET_RIG_FLOAT(ShockWaveFalloff)
	GET_RIG_FLOAT(ShockWaveDirectionalStretch)
	GET_RIG_FLOAT(ShockWaveDecay)
#undef GET_RIG_BOOL
#undef GET_RIG_FLOAT
	return DefaultValue;
}

bool ASniperKillCamDirector::SetKillCamScalar(const FString& PropertyName, float Value)
{
	if (PropertyName.empty())
	{
		return false;
	}

#define SET_DIRECTOR_FLOAT(Name) if (PropertyName == #Name) { Name = Value; return true; }
#define SET_DIRECTOR_BOOL(Name) if (PropertyName == #Name) { Name = Value != 0.0f; return true; }
	SET_DIRECTOR_FLOAT(DefaultDuration)
	SET_DIRECTOR_FLOAT(StartForwardDistance)
	SET_DIRECTOR_FLOAT(StartSideDistance)
	SET_DIRECTOR_FLOAT(StartUpDistance)
	SET_DIRECTOR_FLOAT(FollowBackDistance)
	SET_DIRECTOR_FLOAT(FollowSideDistance)
	SET_DIRECTOR_FLOAT(FollowUpDistance)
	SET_DIRECTOR_FLOAT(LookAheadDistance)
	SET_DIRECTOR_FLOAT(CameraLagSpeed)
	SET_DIRECTOR_FLOAT(TransitionToTailTime)
	SET_DIRECTOR_FLOAT(KillCamFOV)
	SET_DIRECTOR_FLOAT(DOFFocusRange)
	SET_DIRECTOR_FLOAT(DOFBlurRadius)
	SET_DIRECTOR_BOOL(bUseRailRigComponent)
	SET_DIRECTOR_BOOL(bAutoCreateRailRigComponent)
	SET_DIRECTOR_BOOL(bScrubRailSequenceByRailAlpha)
	SET_DIRECTOR_BOOL(bAutoCreateRailSequenceComponent)
#undef SET_DIRECTOR_BOOL
#undef SET_DIRECTOR_FLOAT

	return SetRailRigScalar(PropertyName, Value);
}

float ASniperKillCamDirector::GetKillCamScalar(const FString& PropertyName, float DefaultValue) const
{
	if (PropertyName.empty())
	{
		return DefaultValue;
	}

#define GET_DIRECTOR_FLOAT(Name) if (PropertyName == #Name) { return Name; }
#define GET_DIRECTOR_BOOL(Name) if (PropertyName == #Name) { return Name ? 1.0f : 0.0f; }
	GET_DIRECTOR_FLOAT(DefaultDuration)
	GET_DIRECTOR_FLOAT(StartForwardDistance)
	GET_DIRECTOR_FLOAT(StartSideDistance)
	GET_DIRECTOR_FLOAT(StartUpDistance)
	GET_DIRECTOR_FLOAT(FollowBackDistance)
	GET_DIRECTOR_FLOAT(FollowSideDistance)
	GET_DIRECTOR_FLOAT(FollowUpDistance)
	GET_DIRECTOR_FLOAT(LookAheadDistance)
	GET_DIRECTOR_FLOAT(CameraLagSpeed)
	GET_DIRECTOR_FLOAT(TransitionToTailTime)
	GET_DIRECTOR_FLOAT(KillCamFOV)
	GET_DIRECTOR_FLOAT(DOFFocusRange)
	GET_DIRECTOR_FLOAT(DOFBlurRadius)
	GET_DIRECTOR_BOOL(bUseRailRigComponent)
	GET_DIRECTOR_BOOL(bAutoCreateRailRigComponent)
	GET_DIRECTOR_BOOL(bScrubRailSequenceByRailAlpha)
	GET_DIRECTOR_BOOL(bAutoCreateRailSequenceComponent)
#undef GET_DIRECTOR_BOOL
#undef GET_DIRECTOR_FLOAT

	return GetRailRigScalar(PropertyName, DefaultValue);
}

bool ASniperKillCamDirector::SetKillCamString(const FString& PropertyName, const FString& Value)
{
	if (PropertyName.empty())
	{
		return false;
	}

	if (PropertyName == "CinematicBulletPrefabPath")
	{
		if (CinematicBulletPrefabPath != Value)
		{
			DestroyBulletVisualActor();
			CinematicBulletPrefabPath = Value;
			if (bPlaying)
			{
				EnsureBulletVisualComponent();
				SetBulletVisualVisible(true);
			}
		}
		return true;
	}
	if (PropertyName == "CinematicBulletMeshPath")
	{
		CinematicBulletMeshPath = Value;
		if (UStaticMeshComponent* MeshComponent = CinematicBulletVisual.Get())
		{
			if (HasUsablePath(CinematicBulletMeshPath))
			{
				MeshComponent->SetStaticMeshByPath(CinematicBulletMeshPath);
			}
		}
		return true;
	}
	if (PropertyName == "CinematicBulletMaterialPath")
	{
		CinematicBulletMaterialPath = Value;
		if (UStaticMeshComponent* MeshComponent = CinematicBulletVisual.Get())
		{
			if (HasUsablePath(CinematicBulletMaterialPath))
			{
				MeshComponent->SetMaterial(0, FMaterialManager::Get().GetOrCreateMaterial(CinematicBulletMaterialPath));
			}
		}
		return true;
	}

	return false;
}

FString ASniperKillCamDirector::GetKillCamString(const FString& PropertyName, const FString& DefaultValue) const
{
	if (PropertyName == "CinematicBulletPrefabPath")
	{
		return CinematicBulletPrefabPath;
	}
	if (PropertyName == "CinematicBulletMeshPath")
	{
		return CinematicBulletMeshPath;
	}
	if (PropertyName == "CinematicBulletMaterialPath")
	{
		return CinematicBulletMaterialPath;
	}
	return DefaultValue;
}

bool ASniperKillCamDirector::SetKillCamVector(const FString& PropertyName, const FVector& Value)
{
	if (PropertyName == "CinematicBulletScale")
	{
		CinematicBulletScale = Value;
		return true;
	}
	return false;
}

FVector ASniperKillCamDirector::GetKillCamVector(const FString& PropertyName, const FVector& DefaultValue) const
{
	if (PropertyName == "CinematicBulletScale")
	{
		return CinematicBulletScale;
	}
	return DefaultValue;
}

bool ASniperKillCamDirector::SetKillCamRotator(const FString& PropertyName, const FRotator& Value)
{
	if (PropertyName == "CinematicBulletRotationOffset")
	{
		CinematicBulletRotationOffset = Value;
		return true;
	}
	return false;
}

FRotator ASniperKillCamDirector::GetKillCamRotator(const FString& PropertyName, const FRotator& DefaultValue) const
{
	if (PropertyName == "CinematicBulletRotationOffset")
	{
		return CinematicBulletRotationOffset;
	}
	return DefaultValue;
}

void ASniperKillCamDirector::EnsureCameraComponent()
{
	if (CinematicCamera)
	{
		return;
	}

	UCameraComponent* Camera = GetComponentByClass<UCameraComponent>();
	if (!Camera)
	{
		Camera = AddComponent<UCameraComponent>();
	}

	if (Camera)
	{
		CinematicCamera = Camera;
		SetRootComponent(Camera);
		Camera->SetFOV(KillCamFOV);
	}
}

void ASniperKillCamDirector::EnsureBulletVisualComponent()
{
	if (CinematicBulletActor && CinematicBulletVisual)
	{
		return;
	}
	if (CinematicBulletActor && !CinematicBulletVisual)
	{
		DestroyBulletVisualActor();
	}
	CinematicBulletVisual = nullptr;

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	if (HasUsablePath(CinematicBulletPrefabPath))
	{
		AActor* BulletActor = FPrefabManager::SpawnActorFromPrefab(World, CinematicBulletPrefabPath);
		if (!BulletActor)
		{
			UE_LOG("[SniperKillCam] Failed to spawn cinematic bullet prefab: %s", CinematicBulletPrefabPath.c_str());
		}
		else
		{
			BulletActor->SetFName(FName("SniperKillCam_CinematicBullet"));
			CinematicBulletPrefabBaseScale = BulletActor->GetActorScale();
			if (std::abs(CinematicBulletPrefabBaseScale.X) <= 0.0001f
				&& std::abs(CinematicBulletPrefabBaseScale.Y) <= 0.0001f
				&& std::abs(CinematicBulletPrefabBaseScale.Z) <= 0.0001f)
			{
				CinematicBulletPrefabBaseScale = FVector::OneVector;
			}

			USceneComponent* SpawnedRoot = BulletActor->GetRootComponent();
			UStaticMeshComponent* BulletMeshComponent = Cast<UStaticMeshComponent>(SpawnedRoot);
			if (!BulletMeshComponent)
			{
				for (UActorComponent* Component : BulletActor->GetComponents())
				{
					if (UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(Component))
					{
						BulletMeshComponent = StaticMeshComponent;
						break;
					}
				}
			}

			if (BulletMeshComponent && BulletMeshComponent->GetStaticMesh())
			{
				if (BulletMeshComponent != SpawnedRoot)
				{
					BulletMeshComponent->SetParent(nullptr);
					BulletActor->SetRootComponent(BulletMeshComponent);
					BulletMeshComponent->SetRelativeLocation(FVector::ZeroVector);
					BulletMeshComponent->SetRelativeRotation(FQuat::Identity);
					BulletMeshComponent->SetRelativeScale(FVector::OneVector);
				}
				BulletMeshComponent->SetHiddenInComponentTree(false);
				BulletMeshComponent->SetCastShadow(false);
				BulletMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				BulletMeshComponent->SetVisibility(false);

				for (UPrimitiveComponent* Primitive : BulletActor->GetPrimitiveComponents())
				{
					if (Primitive && Primitive != BulletMeshComponent)
					{
						Primitive->SetVisibility(false);
					}
				}

				CinematicBulletActor = BulletActor;
				CinematicBulletVisual = BulletMeshComponent;
				UE_LOG("[SniperKillCam] Spawned cinematic bullet prefab: %s", CinematicBulletPrefabPath.c_str());
				return;
			}

			UE_LOG("[SniperKillCam] Cinematic bullet prefab has no usable static mesh. Falling back to mesh path: %s",
				CinematicBulletMeshPath.c_str());
			World->DestroyActor(BulletActor);
		}
	}

	AActor* OwnerActor = World->SpawnActor<AActor>();
	if (!OwnerActor)
	{
		return;
	}
	OwnerActor->SetFName(FName("SniperKillCam_CinematicBulletMesh"));

	UStaticMeshComponent* MeshComponent = OwnerActor->AddComponent<UStaticMeshComponent>();
	if (!MeshComponent)
	{
		World->DestroyActor(OwnerActor);
		return;
	}

	OwnerActor->SetRootComponent(MeshComponent);
	MeshComponent->SetHiddenInComponentTree(true);
	MeshComponent->SetCastShadow(false);
	MeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	MeshComponent->SetRelativeScale(FVector::OneVector);
	MeshComponent->SetVisibility(false);
	if (HasUsablePath(CinematicBulletMeshPath))
	{
		if (!MeshComponent->SetStaticMeshByPath(CinematicBulletMeshPath))
		{
			UE_LOG("[SniperKillCam] Failed to load fallback cinematic bullet mesh: %s", CinematicBulletMeshPath.c_str());
		}
	}
	if (HasUsablePath(CinematicBulletMaterialPath))
	{
		MeshComponent->SetMaterial(0, FMaterialManager::Get().GetOrCreateMaterial(CinematicBulletMaterialPath));
	}

	CinematicBulletActor = OwnerActor;
	CinematicBulletPrefabBaseScale = CinematicBulletScale;
	CinematicBulletVisual = MeshComponent;
	UE_LOG("[SniperKillCam] Spawned fallback cinematic bullet mesh actor: %s", CinematicBulletMeshPath.c_str());
}

UKillCamRailRigComponent* ASniperKillCamDirector::ResolveRailRigComponent()
{
	if (RailRigComponent)
	{
		return RailRigComponent.Get();
	}

	UKillCamRailRigComponent* Rig = GetComponentByClass<UKillCamRailRigComponent>();
	if (!Rig && bAutoCreateRailRigComponent)
	{
		Rig = AddComponent<UKillCamRailRigComponent>();
		if (Rig)
		{
			Rig->SetFName(FName("KillCamRailRig"));
		}
	}

	RailRigComponent = Rig;
	return Rig;
}

UActorSequenceComponent* ASniperKillCamDirector::ResolveRailSequenceComponent()
{
	if (RailSequenceComponent)
	{
		return RailSequenceComponent.Get();
	}

	UActorSequenceComponent* SequenceComponent = GetComponentByClass<UActorSequenceComponent>();
	if (!SequenceComponent && bAutoCreateRailSequenceComponent)
	{
		SequenceComponent = AddComponent<UActorSequenceComponent>();
		if (SequenceComponent)
		{
			SequenceComponent->SetFName(FName("KillCamRailSequence"));
		}
	}

	RailSequenceComponent = SequenceComponent;
	return SequenceComponent;
}

void ASniperKillCamDirector::ScrubRailSequence(float RailAlpha)
{
	if (!bUseRailRigComponent || !bScrubRailSequenceByRailAlpha)
	{
		return;
	}

	UActorSequenceComponent* SequenceComponent = ResolveRailSequenceComponent();
	if (!SequenceComponent)
	{
		return;
	}

	UActorSequence* Sequence = SequenceComponent->GetSequence();
	if (!Sequence || Sequence->GetBindings().empty())
	{
		return;
	}

	UActorSequencePlayer* Player = SequenceComponent->GetSequencePlayer();
	if (!Player)
	{
		return;
	}

	if (Player->IsPlaying())
	{
		Player->Pause();
	}

	const float StartTime = Sequence->GetStartTime();
	const float EndTime = Sequence->GetEndTime();
	const float SequenceTime = EndTime > StartTime
		? FMath::Lerp(StartTime, EndTime, FMath::Clamp(RailAlpha, 0.0f, 1.0f))
		: StartTime;
	Player->SetCurrentTime(SequenceTime);
}

void ASniperKillCamDirector::DestroyBulletVisualActor()
{
	AActor* BulletActor = CinematicBulletActor.Get();
	CinematicBulletActor = nullptr;
	CinematicBulletPrefabBaseScale = FVector::OneVector;
	if (!BulletActor)
	{
		return;
	}

	if (UWorld* World = BulletActor->GetWorld())
	{
		World->DestroyActor(BulletActor);
	}
}

APlayerCameraManager* ASniperKillCamDirector::ResolveCameraManager() const
{
	UWorld* World = GetWorld();
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	return PC ? PC->GetPlayerCameraManager() : nullptr;
}

bool ASniperKillCamDirector::ResolveBulletSnapshot(FBulletCinematicSnapshot& OutSnapshot) const
{
	UBallisticBulletManagerComponent* Manager = ActiveBulletManager.Get();
	return Manager && Manager->GetBulletSnapshotById(ActiveBulletId, OutSnapshot);
}

FBulletCinematicSnapshot ASniperKillCamDirector::BuildPlaybackSnapshot(float Alpha, bool bClampAlpha) const
{
	FBulletCinematicSnapshot Snapshot = StartSnapshot;
	const float ResolvedAlpha = bClampAlpha ? FMath::Clamp(Alpha, 0.0f, 1.0f) : Alpha;
	const float PreviousAlpha = bClampAlpha ? FMath::Clamp(ResolvedAlpha - 0.01f, 0.0f, 1.0f) : ResolvedAlpha - 0.01f;
	Snapshot.Position = FVector::Lerp(StartSnapshot.Position, HitSnapshot.Position, ResolvedAlpha);
	Snapshot.PreviousPosition = ResolvedAlpha <= 0.001f
		? StartSnapshot.PreviousPosition
		: FVector::Lerp(StartSnapshot.Position, HitSnapshot.Position, PreviousAlpha);
	const FVector PathDirection = SafeNormal(HitSnapshot.Position - StartSnapshot.Position, SafeNormal(StartSnapshot.Velocity, FVector::ForwardVector));
	Snapshot.Velocity = PathDirection * (std::max)(HitSnapshot.Velocity.Length(), 1.0f);
	Snapshot.TraveledDistance = FMath::Lerp(StartSnapshot.TraveledDistance, HitSnapshot.TraveledDistance, ResolvedAlpha);
	Snapshot.LifeTime = Duration * (1.0f - ResolvedAlpha);
	Snapshot.bIsAlive = ResolvedAlpha < 1.0f;
	return Snapshot;
}

FBulletCinematicSnapshot ASniperKillCamDirector::ResolveSnapshotAtRailAlpha(
	float RailAlpha,
	const FBulletCinematicSnapshot& FallbackSnapshot,
	const UKillCamRailRigComponent* Rig) const
{
	if (!bHasHitSnapshot)
	{
		return FallbackSnapshot;
	}

	return BuildPlaybackSnapshot(RailAlpha, !(Rig && Rig->bAllowRailExtrapolation));
}

FBulletCinematicSnapshot ASniperKillCamDirector::BuildBulletVisualCollisionSnapshot(
	const FBulletCinematicSnapshot& FallbackSnapshot,
	float RailAlpha)
{
	const UKillCamRailRigComponent* Rig = bUseRailRigComponent ? ResolveRailRigComponent() : nullptr;
	const float BulletRailAlpha = Rig
		? ResolveDrivenRailAlpha(
			RailAlpha,
			Rig->BulletRailAlphaOverride,
			Rig->BulletRailAlphaScale,
			Rig->BulletRailAlphaOffset,
			Rig->BulletRailAlphaEase,
			Rig->BulletRailAlphaPower,
			Rig)
		: RailAlpha;
	FBulletCinematicSnapshot VisualSnapshot = ResolveSnapshotAtRailAlpha(BulletRailAlpha, FallbackSnapshot, Rig);
	const FVector Direction = bHasHitSnapshot
		? SafeNormal(HitSnapshot.Position - StartSnapshot.Position, SafeNormal(StartSnapshot.Velocity, FVector::ForwardVector))
		: SafeNormal(VisualSnapshot.Velocity, FVector::ForwardVector);
	const FVector Side = ComputeSideVector(Direction);
	if (Rig)
	{
		VisualSnapshot.Position = VisualSnapshot.Position
			+ Direction * Rig->BulletForwardOffset
			+ Side * Rig->BulletSideOffset
			+ FVector::UpVector * Rig->BulletUpOffset;
	}
	VisualSnapshot.BulletId = ActiveBulletId;
	return VisualSnapshot;
}

bool ASniperKillCamDirector::CheckFloorHitNow(FBulletCinematicSnapshot& OutSnapshot)
{
	if (!bPlaying || ActiveBulletId == 0)
	{
		return false;
	}

	if (GBulletFloorHitSnapshotsById.find(ActiveBulletId) != GBulletFloorHitSnapshotsById.end())
	{
		OutSnapshot = GBulletFloorHitSnapshotsById[ActiveBulletId];
		SetBulletVisualVisible(false);
		return true;
	}

	const float RailAlpha = Duration > 0.0f ? FMath::Clamp(Elapsed / Duration, 0.0f, 1.0f) : 1.0f;
	const FBulletCinematicSnapshot CurrentVisualSnapshot = BuildBulletVisualCollisionSnapshot(LastSnapshot, RailAlpha);
	const FBulletCinematicSnapshot PreviousVisualSnapshot = bHasLastVisualCollisionSnapshot
		? LastVisualCollisionSnapshot
		: CurrentVisualSnapshot;

	if (!TryBuildFloorHitSnapshot(GetWorld(), PreviousVisualSnapshot, CurrentVisualSnapshot, OutSnapshot))
	{
		LastVisualCollisionSnapshot = CurrentVisualSnapshot;
		bHasLastVisualCollisionSnapshot = true;
		return false;
	}

	NotifyBulletFloorHit(OutSnapshot);
	UE_LOG(
		"[SniperKillCam] Lua-driven visual bullet floor hit: BulletId=%d Position=(%.3f, %.3f, %.3f)",
		OutSnapshot.BulletId,
		OutSnapshot.Position.X,
		OutSnapshot.Position.Y,
		OutSnapshot.Position.Z);
	SetBulletVisualVisible(false);
	return true;
}

void ASniperKillCamDirector::UpdateCameraFromSnapshot(
	const FBulletCinematicSnapshot& Snapshot,
	float DeltaTime,
	float RailAlpha)
{
	UCameraComponent* Camera = CinematicCamera.Get();
	APlayerCameraManager* Manager = ResolveCameraManager();
	if (!Camera || !Manager)
	{
		return;
	}

	const FVector RailDirection = bHasHitSnapshot
		? SafeNormal(HitSnapshot.Position - StartSnapshot.Position, SafeNormal(StartSnapshot.Velocity, FVector::ForwardVector))
		: SafeNormal(Snapshot.Velocity, FVector::ForwardVector);
	const UKillCamRailRigComponent* Rig = bUseRailRigComponent ? ResolveRailRigComponent() : nullptr;
	const float TailAlpha = SmoothStep(Elapsed / (std::max)(TransitionToTailTime, 0.01f));
	const float CameraRailAlpha = Rig
		? ResolveDrivenRailAlpha(
			RailAlpha,
			Rig->CameraRailAlphaOverride,
			Rig->CameraRailAlphaScale,
			Rig->CameraRailAlphaOffset,
			Rig->CameraRailAlphaEase,
			Rig->CameraRailAlphaPower,
			Rig)
		: RailAlpha;
	const float LookRailAlpha = Rig
		? ResolveDrivenRailAlpha(
			RailAlpha,
			Rig->LookRailAlphaOverride,
			Rig->LookRailAlphaScale,
			Rig->LookRailAlphaOffset,
			Rig->LookRailAlphaEase,
			Rig->LookRailAlphaPower,
			Rig)
		: RailAlpha;
	const float BulletRailAlpha = Rig
		? ResolveDrivenRailAlpha(
			RailAlpha,
			Rig->BulletRailAlphaOverride,
			Rig->BulletRailAlphaScale,
			Rig->BulletRailAlphaOffset,
			Rig->BulletRailAlphaEase,
			Rig->BulletRailAlphaPower,
			Rig)
		: RailAlpha;
	const FBulletCinematicSnapshot CameraSnapshot = ResolveSnapshotAtRailAlpha(CameraRailAlpha, Snapshot, Rig);
	const FBulletCinematicSnapshot LookSnapshot = ResolveSnapshotAtRailAlpha(LookRailAlpha, Snapshot, Rig);
	const FBulletCinematicSnapshot BulletSnapshot = ResolveSnapshotAtRailAlpha(BulletRailAlpha, Snapshot, Rig);
	const FVector CameraDirection = RailDirection;
	const FVector CameraSide = ComputeSideVector(CameraDirection);
	const FVector LookDirection = RailDirection;
	const FVector LookSide = ComputeSideVector(LookDirection);
	const FVector BulletDirection = RailDirection;
	const FVector BulletSide = ComputeSideVector(BulletDirection);
	const float DistanceScale = ComputeDistanceScale(Rig);
	const float LinearDistanceScale = Rig
		? FMath::Lerp(1.0f, DistanceScale, FMath::Clamp(Rig->LinearOffsetDistanceScaleBlend, 0.0f, 1.0f))
		: 1.0f;
	const float OrbitDistanceScale = Rig
		? FMath::Lerp(1.0f, DistanceScale, FMath::Clamp(Rig->OrbitRadiusDistanceScaleBlend, 0.0f, 1.0f))
		: 1.0f;
	const FVector LinearDesiredPosition = Rig
		? CameraSnapshot.Position
			+ CameraDirection * (Rig->ForwardOffset * LinearDistanceScale)
			+ CameraSide * (Rig->SideOffset * LinearDistanceScale)
			+ FVector::UpVector * (Rig->UpOffset * LinearDistanceScale)
		: Snapshot.Position + ComputeCameraOffset(RailDirection, TailAlpha);
	const FVector OrbitPivot = Rig
		? CameraSnapshot.Position
			+ CameraDirection * (Rig->OrbitPivotForwardOffset * LinearDistanceScale)
			+ CameraSide * (Rig->OrbitPivotSideOffset * LinearDistanceScale)
			+ FVector::UpVector * (Rig->OrbitPivotUpOffset * LinearDistanceScale)
		: CameraSnapshot.Position;
	const FVector OrbitDesiredPosition = Rig
		? OrbitPivot + ComputeOrbitOffset(
			CameraDirection,
			Rig->OrbitYaw,
			Rig->OrbitPitch,
			Rig->OrbitRadius * OrbitDistanceScale)
		: LinearDesiredPosition;
	const FVector DesiredPosition = Rig
		? FVector::Lerp(LinearDesiredPosition, OrbitDesiredPosition, FMath::Clamp(Rig->OrbitBlend, 0.0f, 1.0f))
		: LinearDesiredPosition;
	const FVector CurrentPosition = Camera->GetWorldLocation();
	const float ResolvedLagSpeed = Rig ? Rig->CameraLagSpeed : CameraLagSpeed;
	const float MoveAlpha = SafeDeltaAlpha(DeltaTime, ResolvedLagSpeed);
	FVector NewPosition = DeltaTime <= 0.0f
		? DesiredPosition
		: FVector::Lerp(CurrentPosition, DesiredPosition, MoveAlpha);
	if (Rig && Rig->CameraShakeAmplitude > 0.0f && Rig->CameraShakeFrequency > 0.0f)
	{
		const float ShakeTime = Elapsed * Rig->CameraShakeFrequency;
		const float ShakeFade = 1.0f - SmoothStep(RailAlpha);
		const float ShakeAmount = Rig->CameraShakeAmplitude * (0.45f + ShakeFade * 0.55f);
		const float SideShake = std::sin(ShakeTime * 6.28318530718f) * ShakeAmount;
		const float UpShake = std::sin(ShakeTime * 8.117f + 1.73f) * ShakeAmount * 0.55f;
		NewPosition += CameraSide * SideShake + FVector::UpVector * UpShake;
	}

	Camera->SetWorldLocation(NewPosition);
	const float ResolvedLookAhead = Rig ? Rig->LookAhead : LookAheadDistance;
	const float ResolvedLookSideOffset = Rig ? Rig->LookSideOffset : 0.0f;
	const float ResolvedLookUpOffset = Rig ? Rig->LookUpOffset : 0.0f;
	const FVector AuthoredLookTarget = LookSnapshot.Position
		+ LookDirection * ResolvedLookAhead
		+ LookSide * ResolvedLookSideOffset
		+ FVector::UpVector * ResolvedLookUpOffset;
	const FVector BulletVisualTarget = Rig
		? BulletSnapshot.Position
			+ BulletDirection * Rig->BulletForwardOffset
			+ BulletSide * Rig->BulletSideOffset
			+ FVector::UpVector * Rig->BulletUpOffset
		: AuthoredLookTarget;
	const FVector LookTarget = Rig && Rig->bLookAtBulletVisual
		? BulletVisualTarget
			+ LookDirection * ResolvedLookAhead
			+ LookSide * ResolvedLookSideOffset
			+ FVector::UpVector * ResolvedLookUpOffset
		: AuthoredLookTarget;
	FRotator DesiredLookRotation = MakeLookAtRotation(NewPosition, LookTarget, Camera->GetWorldRotation());
	if (Rig)
	{
		DesiredLookRotation.Roll = Rig->Roll;
	}
	const float ResolvedLookLagSpeed = Rig ? Rig->LookLagSpeed : CameraLagSpeed;
	const float LookAlpha = SafeDeltaAlpha(DeltaTime, ResolvedLookLagSpeed);
	const FQuat CurrentLookQuat = Camera->GetWorldRotation().ToQuaternion().GetNormalized();
	const FQuat DesiredLookQuat = DesiredLookRotation.ToQuaternion().GetNormalized();
	const FQuat NewLookQuat = DeltaTime <= 0.0f
		? DesiredLookQuat
		: FQuat::Slerp(CurrentLookQuat, DesiredLookQuat, LookAlpha).GetNormalized();
	Camera->SetWorldRotation(NewLookQuat);
	Camera->SetFOV(Rig ? Rig->FOV : KillCamFOV);

	Manager->SetDepthOfField(
		(std::max)((LookTarget - NewPosition).Length(), 0.1f),
		Rig ? Rig->DOFFocusRange : DOFFocusRange,
		Rig ? Rig->DOFBlurRadius : DOFBlurRadius);
}

void ASniperKillCamDirector::UpdateBulletVisualFromSnapshot(const FBulletCinematicSnapshot& Snapshot, float RailAlpha)
{
	const UKillCamRailRigComponent* Rig = bUseRailRigComponent ? ResolveRailRigComponent() : nullptr;
	const float BulletRailAlpha = Rig
		? ResolveDrivenRailAlpha(
			RailAlpha,
			Rig->BulletRailAlphaOverride,
			Rig->BulletRailAlphaScale,
			Rig->BulletRailAlphaOffset,
			Rig->BulletRailAlphaEase,
			Rig->BulletRailAlphaPower,
			Rig)
		: RailAlpha;
	const FBulletCinematicSnapshot VisualSnapshot = ResolveSnapshotAtRailAlpha(BulletRailAlpha, Snapshot, Rig);
	const FVector Direction = bHasHitSnapshot
		? SafeNormal(HitSnapshot.Position - StartSnapshot.Position, SafeNormal(StartSnapshot.Velocity, FVector::ForwardVector))
		: SafeNormal(VisualSnapshot.Velocity, FVector::ForwardVector);
	const FVector Side = ComputeSideVector(Direction);
	const FVector VisualLocation = Rig
		? VisualSnapshot.Position
			+ Direction * Rig->BulletForwardOffset
			+ Side * Rig->BulletSideOffset
			+ FVector::UpVector * Rig->BulletUpOffset
		: VisualSnapshot.Position;
	const float BulletUniformScale = Rig ? (std::max)(Rig->BulletScaleMultiplier, 0.0f) : 1.0f;
	const FVector BulletAxisScale = Rig
		? FVector(
			(std::max)(Rig->BulletScaleXMultiplier, 0.0f),
			(std::max)(Rig->BulletScaleYMultiplier, 0.0f),
			(std::max)(Rig->BulletScaleZMultiplier, 0.0f))
		: FVector::OneVector;
	const FVector MeshVisualScale = ScaleVectorComponents(
		ScaleVector(CinematicBulletScale, BulletUniformScale),
		BulletAxisScale);
	const FVector PrefabVisualScale = ScaleVectorComponents(
		ScaleVector(CinematicBulletPrefabBaseScale, BulletUniformScale),
		BulletAxisScale);
	const float SpinDegrees = Rig
		? Rig->BulletRollOffset + Rig->BulletSpinPhase + BulletRailAlpha * Rig->BulletSpinRevolutions * 360.0f
		: 0.0f;
	constexpr float Deg2Rad = 3.14159265358979323846f / 180.0f;
	const FQuat DirectionQuat = DirectionToQuat(Direction);
	const FQuat MeshOffsetQuat = CinematicBulletRotationOffset.ToQuaternion().GetNormalized();
	const FQuat AuthoredOffsetQuat = Rig
		? FRotator(Rig->BulletPitchOffset, Rig->BulletYawOffset, 0.0f).ToQuaternion().GetNormalized()
		: FQuat::Identity;
	const FQuat SpinQuat = FQuat::FromAxisAngle(FVector::ForwardVector, SpinDegrees * Deg2Rad).GetNormalized();
	const FQuat VisualQuat = (DirectionQuat * MeshOffsetQuat * AuthoredOffsetQuat * SpinQuat).GetNormalized();

	if (AActor* BulletActor = CinematicBulletActor.Get())
	{
		BulletActor->SetActorLocation(VisualLocation);
		if (USceneComponent* Root = BulletActor->GetRootComponent())
		{
			Root->SetWorldRotation(VisualQuat);
		}
		else
		{
			BulletActor->SetActorRotation(VisualQuat.ToRotator());
		}
		BulletActor->SetActorScale(PrefabVisualScale);
		BulletActor->SetVisible(true);
		if (UStaticMeshComponent* MeshComponent = CinematicBulletVisual.Get())
		{
			MeshComponent->SetHiddenInComponentTree(false);
			MeshComponent->SetVisibility(true);
		}
		return;
	}

	UStaticMeshComponent* MeshComponent = CinematicBulletVisual.Get();
	if (!MeshComponent)
	{
		return;
	}

	MeshComponent->SetWorldLocation(VisualLocation);
	MeshComponent->SetWorldRotation(VisualQuat);
	MeshComponent->SetRelativeScale(MeshVisualScale);
	MeshComponent->SetHiddenInComponentTree(false);
	MeshComponent->SetVisibility(true);
}

void ASniperKillCamDirector::UpdateShockWaveFromSnapshot(const FBulletCinematicSnapshot& Snapshot, float RailAlpha)
{
	const UKillCamRailRigComponent* Rig = bUseRailRigComponent ? ResolveRailRigComponent() : nullptr;
	APlayerCameraManager* Manager = ResolveCameraManager();
	if (!Rig || !Rig->bEnableShockWave || !Manager)
	{
		ClearShockWave();
		return;
	}

	const float BulletRailAlpha = ResolveDrivenRailAlpha(
		RailAlpha,
		Rig->BulletRailAlphaOverride,
		Rig->BulletRailAlphaScale,
		Rig->BulletRailAlphaOffset,
		Rig->BulletRailAlphaEase,
		Rig->BulletRailAlphaPower,
		Rig);
	const FBulletCinematicSnapshot VisualSnapshot = ResolveSnapshotAtRailAlpha(BulletRailAlpha, Snapshot, Rig);
	const FVector Direction = bHasHitSnapshot
		? SafeNormal(HitSnapshot.Position - StartSnapshot.Position, SafeNormal(StartSnapshot.Velocity, FVector::ForwardVector))
		: SafeNormal(VisualSnapshot.Velocity, FVector::ForwardVector);
	const FVector Side = ComputeSideVector(Direction);
	const FVector WavePosition = VisualSnapshot.Position
		+ Direction * (Rig->BulletForwardOffset + Rig->ShockWaveForwardOffset)
		+ Side * (Rig->BulletSideOffset + Rig->ShockWaveSideOffset)
		+ FVector::UpVector * (Rig->BulletUpOffset + Rig->ShockWaveUpOffset);
	const float Decay = std::max(Rig->ShockWaveDecay, 0.01f);
	const float Burst = std::exp(-FMath::Clamp(BulletRailAlpha, 0.0f, 1.0f) * Decay);
	const float Radius = std::max(0.0f, Rig->ShockWaveRadius + Rig->ShockWaveStartRadiusBoost * Burst);
	const float Strength = std::max(0.0f, Rig->ShockWaveStrength + Rig->ShockWaveStartStrengthBoost * Burst);
	const float Width = std::max(0.001f, Rig->ShockWaveWidth);
	const float Falloff = std::max(0.01f, Rig->ShockWaveFalloff);
	const float Stretch = std::max(0.0f, Rig->ShockWaveDirectionalStretch);

	if (ShockWaveHandle == 0)
	{
		ShockWaveHandle = Manager->AddWorldShockWave(WavePosition, Direction, 0.0f, Radius, Width, Strength, Falloff, Stretch);
	}
	else if (!Manager->UpdateWorldShockWave(ShockWaveHandle, WavePosition, Direction, Radius, Width, Strength, Falloff, Stretch))
	{
		ShockWaveHandle = Manager->AddWorldShockWave(WavePosition, Direction, 0.0f, Radius, Width, Strength, Falloff, Stretch);
	}
}

void ASniperKillCamDirector::ClearShockWave()
{
	if (ShockWaveHandle == 0)
	{
		return;
	}
	if (APlayerCameraManager* Manager = ResolveCameraManager())
	{
		Manager->ClearWorldShockWave(ShockWaveHandle);
	}
	ShockWaveHandle = 0;
}

void ASniperKillCamDirector::SetBulletVisualVisible(bool bVisible)
{
	if (AActor* BulletActor = CinematicBulletActor.Get())
	{
		if (UStaticMeshComponent* MeshComponent = CinematicBulletVisual.Get())
		{
			BulletActor->SetVisible(true);
			MeshComponent->SetHiddenInComponentTree(!bVisible);
			MeshComponent->SetVisibility(bVisible);
		}
		else
		{
			SetActorPrimitiveVisibility(BulletActor, bVisible);
		}
		return;
	}

	if (UStaticMeshComponent* MeshComponent = CinematicBulletVisual.Get())
	{
		MeshComponent->SetHiddenInComponentTree(!bVisible);
		MeshComponent->SetVisibility(bVisible);
	}
}

void ASniperKillCamDirector::RestorePreviousCamera()
{
	APlayerCameraManager* Manager = ResolveCameraManager();
	if (!Manager)
	{
		return;
	}

	Manager->ClearDepthOfField();
	if (UCameraComponent* Camera = CinematicCamera.Get())
	{
		Camera->SetLetterboxEnabled(false);
		Manager->UnregisterCamera(Camera);
	}

	AActor* Target = PreviousViewTarget.Get();
	UCameraComponent* Camera = PreviousActiveCamera.Get();
	if (!Target && Camera)
	{
		Target = Camera->GetOwner();
	}

	if (Target)
	{
		Manager->SetViewTarget(Target);
	}
	if (Camera)
	{
		Manager->RegisterCamera(Camera);
		Manager->SetActiveCamera(Camera);
	}

	PreviousViewTarget = nullptr;
	PreviousActiveCamera = nullptr;
}

FVector ASniperKillCamDirector::ComputeCameraOffset(const FVector& Direction, float Alpha) const
{
	const FVector Side = ComputeSideVector(Direction);

	if (ActiveCameraMode == ESniperKillCamCameraMode::TailFollow)
	{
		return Direction * -FollowBackDistance + Side * FollowSideDistance + FVector::UpVector * FollowUpDistance;
	}

	if (ActiveCameraMode == ESniperKillCamCameraMode::SideFollow)
	{
		return Side * StartSideDistance + FVector::UpVector * StartUpDistance;
	}

	const FVector StartOffset =
		Direction * StartForwardDistance +
		Side * StartSideDistance +
		FVector::UpVector * StartUpDistance;
	const FVector EndOffset =
		Direction * -FollowBackDistance +
		Side * FollowSideDistance +
		FVector::UpVector * FollowUpDistance;
	return FVector::Lerp(StartOffset, EndOffset, Alpha);
}

FVector ASniperKillCamDirector::ComputeOrbitOffset(
	const FVector& Direction,
	float YawDegrees,
	float PitchDegrees,
	float Radius) const
{
	constexpr float Deg2Rad = 3.14159265358979323846f / 180.0f;
	const float YawRad = YawDegrees * Deg2Rad;
	const float PitchRad = FMath::Clamp(PitchDegrees, -89.0f, 89.0f) * Deg2Rad;
	const FVector SafeDirection = SafeNormal(Direction, FVector::ForwardVector);
	const FVector Side = ComputeSideVector(SafeDirection);
	const FVector Horizontal =
		SafeDirection * (-std::cos(YawRad)) +
		Side * std::sin(YawRad);
	const FVector OrbitDirection =
		SafeNormal(Horizontal, SafeDirection * -1.0f) * std::cos(PitchRad) +
		FVector::UpVector * std::sin(PitchRad);
	return SafeNormal(OrbitDirection, SafeDirection * -1.0f) * (std::max)(Radius, 0.0f);
}

float ASniperKillCamDirector::ComputeDistanceScale(const UKillCamRailRigComponent* Rig) const
{
	if (!Rig || !Rig->bScaleOffsetsByShotDistance)
	{
		return 1.0f;
	}

	const float Reference = (std::max)(Rig->ReferenceDistance, 0.01f);
	const float ShotDistance = bHasHitSnapshot
		? (HitSnapshot.Position - StartSnapshot.Position).Length()
		: (std::max)(LastSnapshot.TraveledDistance - StartSnapshot.TraveledDistance, 0.0f);
	const float RawScale = ShotDistance > 0.0f ? ShotDistance / Reference : 1.0f;
	const float MinScale = (std::min)(Rig->MinDistanceScale, Rig->MaxDistanceScale);
	const float MaxScale = (std::max)(Rig->MinDistanceScale, Rig->MaxDistanceScale);
	return FMath::Clamp(RawScale, MinScale, MaxScale);
}

FVector ASniperKillCamDirector::ComputeSideVector(const FVector& Direction) const
{
	FVector Side = FVector::Cross(FVector::UpVector, Direction);
	if (Side.IsNearlyZero())
	{
		Side = FVector::RightVector;
	}
	return SafeNormal(Side, FVector::RightVector);
}
