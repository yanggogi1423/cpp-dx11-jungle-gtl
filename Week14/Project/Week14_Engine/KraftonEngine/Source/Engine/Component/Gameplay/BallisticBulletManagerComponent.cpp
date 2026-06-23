#include "Component/Gameplay/BallisticBulletManagerComponent.h"

#include "Component/Gameplay/CombatCoverAgentComponent.h"
#include "Component/Gameplay/SniperDamageReceiverComponent.h"
#include "Component/Gameplay/SniperWeaponComponent.h"
#include "Component/Primitive/BillboardComponent.h"
#include "Component/Primitive/DecalComponent.h"
#include "Component/PrimitiveComponent.h"
#include "Component/Primitive/SkeletalMeshComponent.h"
#include "Component/Shape/CapsuleComponent.h"
#include "Core/Logging/Log.h"
#include "GameFramework/Pawn/CombatCharacter.h"
#include "Core/Types/CollisionTypes.h"
#include "Debug/DrawDebugHelpers.h"
#include "GameFramework/AActor.h"
#include "GameFramework/Actor/SniperKillCamDirector.h"
#include "GameFramework/Camera/PlayerCameraManager.h"
#include "GameFramework/GameMode/GameplayStatics.h"
#include "GameFramework/GameMode/PlayerController.h"
#include "GameFramework/World.h"
#include "Materials/Material.h"
#include "Materials/MaterialManager.h"
#include "Math/Matrix.h"
#include "Math/Quat.h"
#include "Mesh/Skeletal/SkeletalMesh.h"
#include "Physics/IPhysicsScene.h"
#include "Physics/PhysicsAsset.h"
#include "Physics/PhysicsAssetInstance.h"
#include "Physics/PhysicsAssetPreviewUtils.h"
#include "Profiling/Time/Timer.h"
#include "Runtime/Engine.h"

#include <cmath>
#include <cctype>

#include <algorithm>
#include <cfloat>

namespace
{
	constexpr bool SniperDefaultWindEnabled = true;
	const FVector SniperDefaultWindAcceleration = FVector(0.0f, 1.5f, 0.0f);
	constexpr uint32 SniperBulletQueryObjectMask =
		ObjectTypeBit(ECollisionChannel::WorldStatic) |
		ObjectTypeBit(ECollisionChannel::WorldDynamic) |
		ObjectTypeBit(ECollisionChannel::Pawn);
	constexpr float SniperDebugTrailDuration = 1.5f;
	constexpr float SniperDebugMarkerMinRadius = 0.15f;
	constexpr int32 SniperDebugMarkerSegments = 12;
	constexpr float SniperDebugGravityMultiplier = 1.0f;
	constexpr float SniperBulletMinSweepRadius = 0.01f;
	constexpr float SniperDebugHitMarkerRadius = 0.2f;
	constexpr float SniperRagdollImpactSpeedThreshold = 300.0f;
	constexpr const char* SniperDefaultBulletVisualMaterialPath = "Content/Material/Particle/ParticleSprite.uasset";
	constexpr const char* SniperDefaultBulletImpactDecalMaterialPath = "Content/Material/Editor/DefaultDecal.uasset";
	constexpr float SniperBulletVisualMinScale = 0.04f;
	constexpr float SniperBulletTracerMinWidth = 0.01f;
	constexpr float SniperBulletTracerDefaultThickness = 1.0f;
	constexpr float SniperBallisticSubstepMinDeltaTime = 1.0f / 480.0f;
	constexpr float SniperSpeedOfSoundMetersPerSecond = 343.0f;
	constexpr float SniperBaseDragScale = 0.00008f;
	constexpr float SniperPhysicsAssetHitMinShapeSize = 0.001f;
	constexpr float SniperPhysicsAssetHitEpsilon = 1.0e-6f;
	constexpr int32 SniperBulletIgnoredHitMaxSkips = 8;
	const FName SniperFloorActorTag("Floor");
	constexpr float SniperBulletIgnoredHitAdvanceDistance = 0.05f;
	constexpr float SniperFloorBoundsPadding = 0.25f;
	constexpr float SniperFloorMinHalfThickness = 0.25f;

	struct FSniperPoseShapeHit
	{
		bool bHit = false;
		float T = FLT_MAX;
		FName BoneName = FName::None;
		FVector WorldNormal = FVector::ZeroVector;
		int32 BodyIndex = -1;
		int32 ShapeIndex = -1;
	};

	uint32 HashBulletImpactDecalSeed(int32 BulletId, const FVector& Location)
	{
		uint32 Hash = static_cast<uint32>(BulletId) * 747796405u + 2891336453u;
		Hash ^= static_cast<uint32>(std::abs(Location.X) * 1000.0f) + 0x9e3779b9u + (Hash << 6) + (Hash >> 2);
		Hash ^= static_cast<uint32>(std::abs(Location.Y) * 1000.0f) + 0x9e3779b9u + (Hash << 6) + (Hash >> 2);
		Hash ^= static_cast<uint32>(std::abs(Location.Z) * 1000.0f) + 0x9e3779b9u + (Hash << 6) + (Hash >> 2);
		Hash ^= Hash >> 16;
		Hash *= 2246822519u;
		Hash ^= Hash >> 13;
		return Hash;
	}

	FQuat MakeRotationWithForwardX(const FVector& ForwardX, const FVector& UpHint)
	{
		FVector Forward = ForwardX.IsNearlyZero() ? FVector::ForwardVector : ForwardX.Normalized();
		FVector Up = UpHint.IsNearlyZero() ? FVector::UpVector : UpHint.Normalized();
		if (std::abs(Forward.Dot(Up)) > 0.98f)
		{
			Up = std::abs(Forward.Dot(FVector::UpVector)) > 0.98f ? FVector::RightVector : FVector::UpVector;
		}

		FVector Right = Up.Cross(Forward).Normalized();
		if (Right.IsNearlyZero())
		{
			Right = FVector::RightVector;
		}
		Up = Forward.Cross(Right).Normalized();

		FMatrix RotationMatrix = FMatrix::Identity;
		RotationMatrix.M[0][0] = Forward.X;
		RotationMatrix.M[0][1] = Forward.Y;
		RotationMatrix.M[0][2] = Forward.Z;
		RotationMatrix.M[1][0] = Right.X;
		RotationMatrix.M[1][1] = Right.Y;
		RotationMatrix.M[1][2] = Right.Z;
		RotationMatrix.M[2][0] = Up.X;
		RotationMatrix.M[2][1] = Up.Y;
		RotationMatrix.M[2][2] = Up.Z;
		return RotationMatrix.ToQuat().GetNormalized();
	}

	bool StartsWithToken(const FString& Value, const char* Prefix)
	{
		if (!Prefix)
		{
			return false;
		}

		const size_t PrefixLength = std::char_traits<char>::length(Prefix);
		return Value.size() >= PrefixLength && Value.compare(0, PrefixLength, Prefix) == 0;
	}

	bool IsTokenSeparator(char Character)
	{
		return Character == '_';
	}

	bool HasNormalizedBoneToken(const FString& NormalizedBoneName, const char* Token)
	{
		if (!Token || *Token == '\0' || NormalizedBoneName.empty())
		{
			return false;
		}

		size_t SegmentStart = 0;
		while (SegmentStart < NormalizedBoneName.size())
		{
			size_t SegmentEnd = SegmentStart;
			while (SegmentEnd < NormalizedBoneName.size() && !IsTokenSeparator(NormalizedBoneName[SegmentEnd]))
			{
				++SegmentEnd;
			}

			if (SegmentEnd > SegmentStart)
			{
				const FString Segment = NormalizedBoneName.substr(SegmentStart, SegmentEnd - SegmentStart);
				if (Segment == Token || StartsWithToken(Segment, Token))
				{
					return true;
				}
			}

			SegmentStart = SegmentEnd + 1;
		}

		return false;
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

	FVector ComputeBallisticDragAcceleration(const FBallisticBullet& Bullet)
	{
		const float Speed = Bullet.Velocity.Length();
		if (Speed < 1.0f)
		{
			return FVector::ZeroVector;
		}

		const FVector Direction = Bullet.Velocity / Speed;
		const float SafeBallisticCoefficient = (std::max)(Bullet.BallisticCoefficient, 0.01f);
		const float MachFactor = ComputeMachDragMultiplier(Speed);

		return Direction * -1.0f
			* Speed
			* Speed
			* SniperBaseDragScale
			* MachFactor
			* Bullet.DragScale
			/ SafeBallisticCoefficient;
	}

	FTransform ComposeSniperPhysicsAssetTransforms(const FTransform& ParentWorld, const FTransform& Local)
	{
		FTransform Result = Local;
		Result.Location = ParentWorld.Location + ParentWorld.Rotation.RotateVector(Local.Location);
		Result.Rotation = (ParentWorld.Rotation * Local.Rotation).GetNormalized();
		Result.Scale = FVector::OneVector;
		return Result;
	}

	FVector TransformWorldPositionToShapeLocal(const FVector& WorldPosition, const FTransform& ShapeWorld)
	{
		const FQuat InverseRotation = ShapeWorld.Rotation.GetNormalized().Inverse();
		return InverseRotation.RotateVector(WorldPosition - ShapeWorld.Location);
	}

	float GetAxisValue(const FVector& Value, int32 Axis)
	{
		return Value.Data[Axis];
	}

	float ClampUnit(float Value)
	{
		return FMath::Clamp(Value, 0.0f, 1.0f);
	}

	const char* GetSniperPreciseHitQueryModeName(ESniperPreciseHitQueryMode QueryMode)
	{
		switch (QueryMode)
		{
		case ESniperPreciseHitQueryMode::CharacterQueryBody:
			return "CharacterQueryBody";
		case ESniperPreciseHitQueryMode::LiveRagdollBody:
			return "LiveRagdollBody";
		case ESniperPreciseHitQueryMode::PosePhysicsAssetFallback:
			return "PosePhysicsAssetFallback";
		case ESniperPreciseHitQueryMode::None:
		default:
			return "None";
		}
	}

	const char* GetSniperPreciseHitRejectReasonName(ESniperPreciseHitRejectReason RejectReason)
	{
		switch (RejectReason)
		{
		case ESniperPreciseHitRejectReason::NoSkeletalMesh:
			return "NoSkeletalMesh";
		case ESniperPreciseHitRejectReason::NoPhysicsScene:
			return "NoPhysicsScene";
		case ESniperPreciseHitRejectReason::QueryBodySyncFailed:
			return "QueryBodySyncFailed";
		case ESniperPreciseHitRejectReason::PreciseMissAfterBroadHit:
			return "PreciseMissAfterBroadHit";
		case ESniperPreciseHitRejectReason::BroadPreciseDistanceExceeded:
			return "BroadPreciseDistanceExceeded";
		case ESniperPreciseHitRejectReason::PoseFallbackNoHit:
			return "PoseFallbackNoHit";
		case ESniperPreciseHitRejectReason::None:
		default:
			return "None";
		}
	}

	float ComputePreciseHitTravelDelta(const FHitResult& BroadHit, const FHitResult& PreciseHit)
	{
		if (BroadHit.Distance >= FLT_MAX || PreciseHit.Distance >= FLT_MAX)
		{
			return 0.0f;
		}

		return std::abs(BroadHit.Distance - PreciseHit.Distance);
	}

	bool ShouldComparePreciseHitByTravelDelta(const FHitResult& BroadHit)
	{
		return Cast<UCapsuleComponent>(BroadHit.HitComponent) != nullptr ||
			BroadHit.HitBoneName == FName::None;
	}

	bool ShouldSkipBroadPreciseThreshold(const FHitResult& BroadHit, const FHitResult& PreciseHit)
	{
		return BroadHit.HitActor != nullptr &&
			BroadHit.HitActor == PreciseHit.HitActor &&
			Cast<UCapsuleComponent>(BroadHit.HitComponent) != nullptr;
	}

	bool IsPreciseHitWithinThreshold(
		const FHitResult& BroadHit,
		const FHitResult& PreciseHit,
		float MaxDistance,
		float& OutWorldDelta,
		float& OutTravelDelta)
	{
		OutWorldDelta = (BroadHit.WorldHitLocation - PreciseHit.WorldHitLocation).Length();
		OutTravelDelta = ComputePreciseHitTravelDelta(BroadHit, PreciseHit);
		if (ShouldSkipBroadPreciseThreshold(BroadHit, PreciseHit))
		{
			return true;
		}

		if (MaxDistance <= 0.0f)
		{
			return true;
		}

		if (ShouldComparePreciseHitByTravelDelta(BroadHit))
		{
			return OutTravelDelta <= MaxDistance;
		}

		return FVector::DistSquared(BroadHit.WorldHitLocation, PreciseHit.WorldHitLocation) <= MaxDistance * MaxDistance;
	}

	FVector ComputeBoxNormalLocal(const FVector& LocalPoint, const FVector& HalfExtent)
	{
		const float DistToX = std::abs(HalfExtent.X - std::abs(LocalPoint.X));
		const float DistToY = std::abs(HalfExtent.Y - std::abs(LocalPoint.Y));
		const float DistToZ = std::abs(HalfExtent.Z - std::abs(LocalPoint.Z));
		if (DistToX <= DistToY && DistToX <= DistToZ)
		{
			return FVector(LocalPoint.X >= 0.0f ? 1.0f : -1.0f, 0.0f, 0.0f);
		}
		if (DistToY <= DistToZ)
		{
			return FVector(0.0f, LocalPoint.Y >= 0.0f ? 1.0f : -1.0f, 0.0f);
		}
		return FVector(0.0f, 0.0f, LocalPoint.Z >= 0.0f ? 1.0f : -1.0f);
	}

	bool IntersectSegmentLocalBox(const FVector& Start, const FVector& End, const FVector& HalfExtent, float& OutT)
	{
		float TMin = 0.0f;
		float TMax = 1.0f;
		const FVector Delta = End - Start;
		const float MinBounds[3] = { -HalfExtent.X, -HalfExtent.Y, -HalfExtent.Z };
		const float MaxBounds[3] = {  HalfExtent.X,  HalfExtent.Y,  HalfExtent.Z };

		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			const float Origin = GetAxisValue(Start, Axis);
			const float Direction = GetAxisValue(Delta, Axis);
			if (std::abs(Direction) < SniperPhysicsAssetHitEpsilon)
			{
				if (Origin < MinBounds[Axis] || Origin > MaxBounds[Axis])
				{
					return false;
				}
				continue;
			}

			float T1 = (MinBounds[Axis] - Origin) / Direction;
			float T2 = (MaxBounds[Axis] - Origin) / Direction;
			if (T1 > T2)
			{
				std::swap(T1, T2);
			}

			TMin = (std::max)(TMin, T1);
			TMax = (std::min)(TMax, T2);
			if (TMin > TMax)
			{
				return false;
			}
		}

		OutT = ClampUnit(TMin);
		return true;
	}

	bool IsSniperMovementLimitWallActor(const AActor* Actor)
	{
		if (!Actor)
		{
			return false;
		}

		const FString ActorName = Actor->GetName();
		return ActorName == "Wall_0" ||
			ActorName == "Wall_1" ||
			ActorName == "Wall_2" ||
			ActorName == "Wall_3";
	}

	bool UpdateRayAabbInterval(
		float RayStart,
		float RayDirection,
		float BoundsMin,
		float BoundsMax,
		float& InOutEnter,
		float& InOutExit)
	{
		if (std::abs(RayDirection) <= SniperPhysicsAssetHitEpsilon)
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

	FBoundingBox ExpandSniperFloorBounds(FBoundingBox Bounds)
	{
		if (!Bounds.IsValid())
		{
			return Bounds;
		}

		Bounds.Min.X -= SniperFloorBoundsPadding;
		Bounds.Min.Y -= SniperFloorBoundsPadding;
		Bounds.Min.Z -= SniperFloorBoundsPadding;
		Bounds.Max.X += SniperFloorBoundsPadding;
		Bounds.Max.Y += SniperFloorBoundsPadding;
		Bounds.Max.Z += SniperFloorBoundsPadding;

		const float CenterZ = (Bounds.Min.Z + Bounds.Max.Z) * 0.5f;
		const float HalfThickness = (Bounds.Max.Z - Bounds.Min.Z) * 0.5f;
		if (HalfThickness < SniperFloorMinHalfThickness)
		{
			Bounds.Min.Z = CenterZ - SniperFloorMinHalfThickness;
			Bounds.Max.Z = CenterZ + SniperFloorMinHalfThickness;
		}
		return Bounds;
	}

	float ComputeIgnoredBulletHitAdvanceDistance(
		const FHitResult& Hit,
		const FVector& QueryStart,
		const FVector& QueryDirection,
		float RemainingSegmentLength)
	{
		float AdvanceDistance = (std::max)(
			Hit.Distance + SniperBulletIgnoredHitAdvanceDistance,
			SniperBulletIgnoredHitAdvanceDistance);

		if (!Hit.HitComponent)
		{
			return AdvanceDistance;
		}

		const FBoundingBox Bounds = Hit.HitComponent->GetWorldBoundingBox();
		if (!Bounds.IsValid())
		{
			return AdvanceDistance;
		}

		float EnterDistance = -FLT_MAX;
		float ExitDistance = FLT_MAX;
		if (!UpdateRayAabbInterval(QueryStart.X, QueryDirection.X, Bounds.Min.X, Bounds.Max.X, EnterDistance, ExitDistance) ||
			!UpdateRayAabbInterval(QueryStart.Y, QueryDirection.Y, Bounds.Min.Y, Bounds.Max.Y, EnterDistance, ExitDistance) ||
			!UpdateRayAabbInterval(QueryStart.Z, QueryDirection.Z, Bounds.Min.Z, Bounds.Max.Z, EnterDistance, ExitDistance))
		{
			return AdvanceDistance;
		}

		if (ExitDistance > 0.0f && ExitDistance < FLT_MAX)
		{
			AdvanceDistance = (std::max)(AdvanceDistance, ExitDistance + SniperBulletIgnoredHitAdvanceDistance);
		}

		return (std::min)(AdvanceDistance, RemainingSegmentLength);
	}

	bool IntersectSegmentLocalSphere(const FVector& Start, const FVector& End, float Radius, float& OutT)
	{
		const FVector Delta = End - Start;
		const float A = Delta.Dot(Delta);
		if (A < SniperPhysicsAssetHitEpsilon)
		{
			if (Start.Dot(Start) <= Radius * Radius)
			{
				OutT = 0.0f;
				return true;
			}
			return false;
		}

		const float B = 2.0f * Start.Dot(Delta);
		const float C = Start.Dot(Start) - Radius * Radius;
		const float Discriminant = B * B - 4.0f * A * C;
		if (Discriminant < 0.0f)
		{
			return false;
		}

		const float SqrtDisc = sqrtf(Discriminant);
		const float InvDenominator = 1.0f / (2.0f * A);
		const float Candidates[2] = {
			(-B - SqrtDisc) * InvDenominator,
			(-B + SqrtDisc) * InvDenominator
		};

		float BestT = FLT_MAX;
		for (float T : Candidates)
		{
			if (T >= 0.0f && T <= 1.0f)
			{
				BestT = (std::min)(BestT, T);
			}
		}

		if (BestT == FLT_MAX)
		{
			return false;
		}

		OutT = BestT;
		return true;
	}

	bool IntersectSegmentLocalCylinderZ(const FVector& Start, const FVector& End, float Radius, float CylinderHalfHeight, float& OutT)
	{
		if (CylinderHalfHeight <= 0.0f)
		{
			return false;
		}

		const FVector Delta = End - Start;
		const float A = Delta.X * Delta.X + Delta.Y * Delta.Y;
		if (A < SniperPhysicsAssetHitEpsilon)
		{
			return false;
		}

		const float B = 2.0f * (Start.X * Delta.X + Start.Y * Delta.Y);
		const float C = Start.X * Start.X + Start.Y * Start.Y - Radius * Radius;
		const float Discriminant = B * B - 4.0f * A * C;
		if (Discriminant < 0.0f)
		{
			return false;
		}

		const float SqrtDisc = sqrtf(Discriminant);
		const float InvDenominator = 1.0f / (2.0f * A);
		const float Candidates[2] = {
			(-B - SqrtDisc) * InvDenominator,
			(-B + SqrtDisc) * InvDenominator
		};

		float BestT = FLT_MAX;
		for (float T : Candidates)
		{
			if (T < 0.0f || T > 1.0f)
			{
				continue;
			}

			const float Z = Start.Z + Delta.Z * T;
			if (Z >= -CylinderHalfHeight && Z <= CylinderHalfHeight)
			{
				BestT = (std::min)(BestT, T);
			}
		}

		if (BestT == FLT_MAX)
		{
			return false;
		}

		OutT = BestT;
		return true;
	}

	bool IntersectSegmentLocalCapsuleZ(const FVector& Start, const FVector& End, float Radius, float HalfHeight, float& OutT)
	{
		const float SafeRadius = (std::max)(Radius, SniperPhysicsAssetHitMinShapeSize);
		const float SafeHalfHeight = (std::max)(HalfHeight, SafeRadius);
		const float CylinderHalfHeight = (std::max)(0.0f, SafeHalfHeight - SafeRadius);

		float BestT = FLT_MAX;
		float T = 0.0f;
		if (IntersectSegmentLocalCylinderZ(Start, End, SafeRadius, CylinderHalfHeight, T))
		{
			BestT = (std::min)(BestT, T);
		}

		if (IntersectSegmentLocalSphere(Start - FVector(0.0f, 0.0f, CylinderHalfHeight), End - FVector(0.0f, 0.0f, CylinderHalfHeight), SafeRadius, T))
		{
			BestT = (std::min)(BestT, T);
		}

		if (IntersectSegmentLocalSphere(Start - FVector(0.0f, 0.0f, -CylinderHalfHeight), End - FVector(0.0f, 0.0f, -CylinderHalfHeight), SafeRadius, T))
		{
			BestT = (std::min)(BestT, T);
		}

		if (BestT == FLT_MAX)
		{
			return false;
		}

		OutT = BestT;
		return true;
	}

	FVector ComputeCapsuleNormalLocal(const FVector& LocalPoint, float Radius, float HalfHeight)
	{
		const float SafeRadius = (std::max)(Radius, SniperPhysicsAssetHitMinShapeSize);
		const float SafeHalfHeight = (std::max)(HalfHeight, SafeRadius);
		const float CylinderHalfHeight = (std::max)(0.0f, SafeHalfHeight - SafeRadius);
		const float ClampedZ = FMath::Clamp(LocalPoint.Z, -CylinderHalfHeight, CylinderHalfHeight);
		FVector Normal = LocalPoint - FVector(0.0f, 0.0f, ClampedZ);
		if (Normal.IsNearlyZero())
		{
			Normal = LocalPoint.Z >= 0.0f ? FVector::UpVector : FVector::DownVector;
		}
		else
		{
			Normal.Normalize();
		}
		return Normal;
	}
}

UBallisticBulletManagerComponent::UBallisticBulletManagerComponent()
{
	bTickEnable = true;
}

void UBallisticBulletManagerComponent::BeginPlay()
{
	UActorComponent::BeginPlay();
	ResolveWeaponComponent();
}

void UBallisticBulletManagerComponent::EndPlay()
{
	ResetBullets();
	HideAllBulletVisuals();
	UActorComponent::EndPlay();
}

bool UBallisticBulletManagerComponent::IsWindEnabled() const
{
	const AActor* OwnerActor = GetOwner();
	const UWorld* World = OwnerActor ? OwnerActor->GetWorld() : nullptr;
	return World ? World->GetWorldSettings().bEnableBallisticWind : SniperDefaultWindEnabled;
}

void UBallisticBulletManagerComponent::SetWindEnabled(bool bInEnableWind)
{
	AActor* OwnerActor = GetOwner();
	UWorld* World = OwnerActor ? OwnerActor->GetWorld() : nullptr;
	if (World)
	{
		World->GetWorldSettings().bEnableBallisticWind = bInEnableWind;
	}
}

FVector UBallisticBulletManagerComponent::GetWindAcceleration() const
{
	const AActor* OwnerActor = GetOwner();
	const UWorld* World = OwnerActor ? OwnerActor->GetWorld() : nullptr;
	return World ? World->GetCurrentBallisticWindAcceleration() : SniperDefaultWindAcceleration;
}

void UBallisticBulletManagerComponent::SetWindAcceleration(const FVector& InWindAcceleration)
{
	AActor* OwnerActor = GetOwner();
	UWorld* World = OwnerActor ? OwnerActor->GetWorld() : nullptr;
	if (World)
	{
		World->SetBallisticWindAcceleration(InWindAcceleration);
	}
}

bool UBallisticBulletManagerComponent::SpawnBullet(const FBallisticBullet& Bullet)
{
	if (!Bullet.bIsAlive)
	{
		return false;
	}

	FBallisticBullet SpawnedBullet = Bullet;
	if (SpawnedBullet.BulletId == 0)
	{
		SpawnedBullet.BulletId = NextBulletId++;
		if (NextBulletId == 0)
		{
			NextBulletId = 1;
		}
	}

	ActiveBullets.push_back(SpawnedBullet);
	const FBulletCinematicSnapshot Snapshot = BuildBulletSnapshot(SpawnedBullet);
	OnBulletSpawned.Broadcast(Snapshot);
	ASniperKillCamDirector::NotifyBulletSpawned(this, Snapshot);
	return true;
}

void UBallisticBulletManagerComponent::ResetBullets()
{
	ActiveBullets.clear();
	HideAllBulletVisuals();
}

bool UBallisticBulletManagerComponent::GetBulletSnapshotById(int32 BulletId, FBulletCinematicSnapshot& OutSnapshot) const
{
	if (BulletId == 0)
	{
		return false;
	}

	for (const FBallisticBullet& Bullet : ActiveBullets)
	{
		if (Bullet.BulletId == BulletId)
		{
			OutSnapshot = BuildBulletSnapshot(Bullet);
			return true;
		}
	}

	return false;
}

FBulletCinematicSnapshot UBallisticBulletManagerComponent::GetLatestBulletSnapshot() const
{
	if (ActiveBullets.empty())
	{
		return FBulletCinematicSnapshot();
	}

	return BuildBulletSnapshot(ActiveBullets.back());
}

void UBallisticBulletManagerComponent::TickComponent(
	float DeltaTime,
	ELevelTick TickType,
	FActorComponentTickFunction& ThisTickFunction)
{
	UActorComponent::TickComponent(DeltaTime, TickType, ThisTickFunction);
	(void)TickType;
	(void)ThisTickFunction;

	UpdateBullets(DeltaTime);
	CompactDeadBullets();
	UpdateImpactVisuals(DeltaTime);
	SyncBulletVisuals();
}

void UBallisticBulletManagerComponent::UpdateBullets(float DeltaTime)
{
	if (DeltaTime <= 0.0f || ActiveBullets.empty())
	{
		return;
	}

	AActor* OwnerActor = GetOwner();
	UWorld* World = OwnerActor ? OwnerActor->GetWorld() : nullptr;
	const FVector WorldGravity = World ? World->GetWorldSettings().Gravity : FVector(0.0f, 0.0f, -9.81f);
	const bool bWindEnabled = World ? World->GetWorldSettings().bEnableBallisticWind : SniperDefaultWindEnabled;
	const FVector WorldWindAcceleration = World ? World->GetCurrentBallisticWindAcceleration() : SniperDefaultWindAcceleration;
	const FVector AppliedWindAcceleration = bWindEnabled ? WorldWindAcceleration : FVector::ZeroVector;

	int32 SubstepCount = 1;
	if (bEnableBallisticSubsteps && MaxBallisticSubsteps > 1 && MaxBallisticSubstepDeltaTime > 0.0f)
	{
		SubstepCount = static_cast<int32>(std::ceil(DeltaTime / MaxBallisticSubstepDeltaTime));
		SubstepCount = std::clamp(SubstepCount, 1, MaxBallisticSubsteps);
	}

	const float SubstepDeltaTime = DeltaTime / static_cast<float>(SubstepCount);
	if (SubstepDeltaTime < SniperBallisticSubstepMinDeltaTime)
	{
		SubstepCount = 1;
	}

	const FTimer* Timer = GEngine ? GEngine->GetTimer() : nullptr;
	const float RawFrameDeltaTime = Timer ? Timer->GetRawDeltaTime() : DeltaTime;
	const float LifetimeStepDeltaTime = RawFrameDeltaTime / static_cast<float>(SubstepCount);

	for (int32 SubstepIndex = 0; SubstepIndex < SubstepCount; ++SubstepIndex)
	{
		for (FBallisticBullet& Bullet : ActiveBullets)
		{
			UpdateSingleBullet(
				Bullet,
				WorldGravity,
				AppliedWindAcceleration,
				DeltaTime / static_cast<float>(SubstepCount),
				LifetimeStepDeltaTime,
				World);
		}
	}
}

void UBallisticBulletManagerComponent::UpdateSingleBullet(
	FBallisticBullet& Bullet,
	const FVector& WorldGravity,
	const FVector& AppliedWindAcceleration,
	float DeltaTime,
	float LifetimeDeltaTime,
	UWorld* World)
{
	if (!Bullet.bIsAlive)
	{
		return;
	}

	Bullet.PreviousPosition = Bullet.Position;

	const FVector GravityAcceleration = WorldGravity * Bullet.GravityScale * SniperDebugGravityMultiplier;
	FVector WindDriftAcceleration = AppliedWindAcceleration * Bullet.WindInfluenceScale;
	if (!AppliedWindAcceleration.IsNearlyZero() && !Bullet.Velocity.IsNearlyZero())
	{
		const FVector BulletDirection = Bullet.Velocity.Normalized();
		const FVector ParallelWindAcceleration = BulletDirection * AppliedWindAcceleration.Dot(BulletDirection);
		const FVector CrosswindAcceleration = AppliedWindAcceleration - ParallelWindAcceleration;
		WindDriftAcceleration =
			(ParallelWindAcceleration + CrosswindAcceleration * (std::max)(0.0f, CrosswindInfluenceMultiplier))
			* Bullet.WindInfluenceScale;
	}

	const FVector DragAcceleration = ComputeBallisticDragAcceleration(Bullet);
	const FVector TotalAcceleration = GravityAcceleration + WindDriftAcceleration + DragAcceleration;
	Bullet.Position += Bullet.Velocity * DeltaTime + TotalAcceleration * (0.5f * DeltaTime * DeltaTime);
	Bullet.Velocity += TotalAcceleration * DeltaTime;
	Bullet.LifeTime -= LifetimeDeltaTime;
	const float SegmentDistance = (Bullet.Position - Bullet.PreviousPosition).Length();

	FHitResult Hit;
	if (World && QueryBulletHit(Bullet, World, Hit))
	{
		HandleBulletHit(Bullet, Hit, World);
	}
	else
	{
		Bullet.TraveledDistance += SegmentDistance;
	}

	if (World && bDrawDebugBallistics)
	{
		DrawDebugLine(World, Bullet.PreviousPosition, Bullet.Position, FColor(0, 220, 255), SniperDebugTrailDuration);
		DrawDebugSphere(
			World,
			Bullet.Position,
			(std::max)(Bullet.Radius * 3.0f, SniperDebugMarkerMinRadius),
			SniperDebugMarkerSegments,
			FColor(255, 80, 80),
			SniperDebugTrailDuration);
	}

	const bool bExpired = Bullet.LifeTime <= 0.0f;
	const bool bInvalidPosition =
		!std::isfinite(Bullet.Position.X) ||
		!std::isfinite(Bullet.Position.Y) ||
		!std::isfinite(Bullet.Position.Z);

	if (bExpired || bInvalidPosition)
	{
		Bullet.bIsAlive = false;
	}
}

void UBallisticBulletManagerComponent::UpdateImpactVisuals(float DeltaTime)
{
	if (ImpactVisualPool.empty())
	{
		return;
	}

	for (int32 VisualIndex = 0; VisualIndex < static_cast<int32>(ImpactVisualPool.size()); ++VisualIndex)
	{
		UBillboardComponent* Visual = ImpactVisualPool[VisualIndex].Get();
		if (!Visual)
		{
			continue;
		}

		if (VisualIndex >= static_cast<int32>(ImpactVisualRemainingTimes.size()))
		{
			Visual->SetVisibility(false);
			continue;
		}

		float& RemainingTime = ImpactVisualRemainingTimes[VisualIndex];
		if (RemainingTime > 0.0f)
		{
			RemainingTime -= DeltaTime;
		}

		if (RemainingTime <= 0.0f)
		{
			RemainingTime = 0.0f;
			Visual->SetVisibility(false);
		}
	}
}

void UBallisticBulletManagerComponent::SyncBulletVisuals()
{
	if (!bEnableBulletVisuals)
	{
		HideAllBulletVisuals();
		return;
	}

	for (int32 BulletIndex = 0; BulletIndex < static_cast<int32>(ActiveBullets.size()); ++BulletIndex)
	{
		UBillboardComponent* HeadVisual = GetOrCreateBulletHeadVisual(BulletIndex);
		UBillboardComponent* TracerVisual = GetOrCreateBulletTracerVisual(BulletIndex);
		if (!HeadVisual || !TracerVisual)
		{
			continue;
		}

		const FBallisticBullet& Bullet = ActiveBullets[BulletIndex];
		const float SafeHeadScaleMultiplier = (std::max)(BulletHeadVisualScaleMultiplier, 0.01f);
		const float HeadScale = (std::max)(Bullet.VisualScale * SafeHeadScaleMultiplier, SniperBulletVisualMinScale);
		HeadVisual->SetWorldLocation(Bullet.Position);
		HeadVisual->SetRelativeScale(FVector(1.0f, HeadScale, HeadScale));
		HeadVisual->SetVisibility(Bullet.bIsAlive);

		const FVector Segment = Bullet.Position - Bullet.PreviousPosition;
		const float SegmentDistance = Segment.Length();
		const FVector TracerLocation = Bullet.PreviousPosition + Segment * 0.5f;
		const float SafeTracerWidthMultiplier = (std::max)(BulletTracerWidthMultiplier, 0.01f);
		const float SafeTracerLengthMultiplier = (std::max)(BulletTracerLengthMultiplier, 0.01f);
		const float TracerWidth = (std::max)(Bullet.VisualTracerWidth * SafeTracerWidthMultiplier, SniperBulletTracerMinWidth);
		const float SpeedBasedLength = Bullet.Velocity.Length() * 0.0012f;
		float TracerLength = (std::max)(SegmentDistance, SpeedBasedLength) * Bullet.VisualTracerLengthScale * SafeTracerLengthMultiplier;
		TracerLength = (std::max)(TracerLength, Bullet.VisualTracerMinLength * SafeTracerLengthMultiplier);
		TracerLength = (std::min)(TracerLength, Bullet.VisualTracerMaxLength * SafeTracerLengthMultiplier);
		TracerLength = (std::max)(TracerLength, TracerWidth);

		TracerVisual->SetWorldLocation(TracerLocation);
		TracerVisual->SetRelativeScale(FVector(
			SniperBulletTracerDefaultThickness,
			TracerWidth,
			TracerLength));
		TracerVisual->SetVisibility(Bullet.bIsAlive);
	}

	for (int32 VisualIndex = static_cast<int32>(ActiveBullets.size()); VisualIndex < static_cast<int32>(BulletHeadVisualPool.size()); ++VisualIndex)
	{
		if (UBillboardComponent* Visual = BulletHeadVisualPool[VisualIndex].Get())
		{
			Visual->SetVisibility(false);
		}
	}

	for (int32 VisualIndex = static_cast<int32>(ActiveBullets.size()); VisualIndex < static_cast<int32>(BulletTracerVisualPool.size()); ++VisualIndex)
	{
		if (UBillboardComponent* Visual = BulletTracerVisualPool[VisualIndex].Get())
		{
			Visual->SetVisibility(false);
		}
	}
}

void UBallisticBulletManagerComponent::HideAllBulletVisuals()
{
	for (TWeakObjectPtr<UBillboardComponent>& VisualEntry : BulletHeadVisualPool)
	{
		if (UBillboardComponent* Visual = VisualEntry.Get())
		{
			Visual->SetVisibility(false);
		}
	}

	for (TWeakObjectPtr<UBillboardComponent>& VisualEntry : BulletTracerVisualPool)
	{
		if (UBillboardComponent* Visual = VisualEntry.Get())
		{
			Visual->SetVisibility(false);
		}
	}

	for (TWeakObjectPtr<UBillboardComponent>& VisualEntry : ImpactVisualPool)
	{
		if (UBillboardComponent* Visual = VisualEntry.Get())
		{
			Visual->SetVisibility(false);
		}
	}

	for (float& RemainingTime : ImpactVisualRemainingTimes)
	{
		RemainingTime = 0.0f;
	}
}

UBillboardComponent* UBallisticBulletManagerComponent::GetOrCreateBulletHeadVisual(int32 VisualIndex)
{
	if (VisualIndex < 0)
	{
		return nullptr;
	}

	if (VisualIndex < static_cast<int32>(BulletHeadVisualPool.size()))
	{
		if (UBillboardComponent* Existing = BulletHeadVisualPool[VisualIndex].Get())
		{
			return Existing;
		}
	}

	AActor* OwnerActor = GetOwner();
	if (!OwnerActor)
	{
		return nullptr;
	}

	UBillboardComponent* Visual = OwnerActor->AddComponent<UBillboardComponent>();
	if (!Visual)
	{
		return nullptr;
	}

	if (USceneComponent* RootComponent = OwnerActor->GetRootComponent())
	{
		Visual->AttachToComponent(RootComponent);
	}

	Visual->SetAbsoluteScale(true);
	Visual->SetHiddenInComponentTree(true);
	Visual->SetVisibility(false);

	if (UMaterial* VisualMaterial = ResolveBulletHeadVisualMaterial())
	{
		Visual->SetMaterial(VisualMaterial);
	}

	if (VisualIndex >= static_cast<int32>(BulletHeadVisualPool.size()))
	{
		BulletHeadVisualPool.resize(VisualIndex + 1);
	}

	BulletHeadVisualPool[VisualIndex] = Visual;
	return Visual;
}

UBillboardComponent* UBallisticBulletManagerComponent::GetOrCreateBulletTracerVisual(int32 VisualIndex)
{
	if (VisualIndex < 0)
	{
		return nullptr;
	}

	if (VisualIndex < static_cast<int32>(BulletTracerVisualPool.size()))
	{
		if (UBillboardComponent* Existing = BulletTracerVisualPool[VisualIndex].Get())
		{
			return Existing;
		}
	}

	AActor* OwnerActor = GetOwner();
	if (!OwnerActor)
	{
		return nullptr;
	}

	UBillboardComponent* Visual = OwnerActor->AddComponent<UBillboardComponent>();
	if (!Visual)
	{
		return nullptr;
	}

	if (USceneComponent* RootComponent = OwnerActor->GetRootComponent())
	{
		Visual->AttachToComponent(RootComponent);
	}

	Visual->SetAbsoluteScale(true);
	Visual->SetHiddenInComponentTree(true);
	Visual->SetVisibility(false);

	if (UMaterial* VisualMaterial = ResolveBulletTracerVisualMaterial())
	{
		Visual->SetMaterial(VisualMaterial);
	}

	if (VisualIndex >= static_cast<int32>(BulletTracerVisualPool.size()))
	{
		BulletTracerVisualPool.resize(VisualIndex + 1);
	}

	BulletTracerVisualPool[VisualIndex] = Visual;
	return Visual;
}

UMaterial* UBallisticBulletManagerComponent::ResolveBulletHeadVisualMaterial()
{
	if (UMaterial* Existing = BulletHeadVisualMaterial.Get())
	{
		return Existing;
	}

	const FString MaterialPath =
		(!BulletHeadVisualMaterialPath.empty() && BulletHeadVisualMaterialPath != "None")
		? static_cast<FString>(BulletHeadVisualMaterialPath)
		: FString(SniperDefaultBulletVisualMaterialPath);

	UMaterial* LoadedMaterial = FMaterialManager::Get().GetOrCreateMaterial(MaterialPath);
	BulletHeadVisualMaterial = LoadedMaterial;
	return LoadedMaterial;
}

UMaterial* UBallisticBulletManagerComponent::ResolveBulletTracerVisualMaterial()
{
	if (UMaterial* Existing = BulletTracerVisualMaterial.Get())
	{
		return Existing;
	}

	const FString MaterialPath =
		(!BulletTracerVisualMaterialPath.empty() && BulletTracerVisualMaterialPath != "None")
		? static_cast<FString>(BulletTracerVisualMaterialPath)
		: FString(SniperDefaultBulletVisualMaterialPath);

	UMaterial* LoadedMaterial = FMaterialManager::Get().GetOrCreateMaterial(MaterialPath);
	BulletTracerVisualMaterial = LoadedMaterial;
	return LoadedMaterial;
}

UBillboardComponent* UBallisticBulletManagerComponent::GetOrCreateImpactVisual(int32 VisualIndex)
{
	if (VisualIndex < 0)
	{
		return nullptr;
	}

	if (VisualIndex < static_cast<int32>(ImpactVisualPool.size()))
	{
		if (UBillboardComponent* Existing = ImpactVisualPool[VisualIndex].Get())
		{
			return Existing;
		}
	}

	AActor* OwnerActor = GetOwner();
	if (!OwnerActor)
	{
		return nullptr;
	}

	UBillboardComponent* Visual = OwnerActor->AddComponent<UBillboardComponent>();
	if (!Visual)
	{
		return nullptr;
	}

	if (USceneComponent* RootComponent = OwnerActor->GetRootComponent())
	{
		Visual->AttachToComponent(RootComponent);
	}

	Visual->SetAbsoluteScale(true);
	Visual->SetHiddenInComponentTree(true);
	Visual->SetVisibility(false);

	if (UMaterial* VisualMaterial = ResolveImpactVisualMaterial())
	{
		Visual->SetMaterial(VisualMaterial);
	}

	if (VisualIndex >= static_cast<int32>(ImpactVisualPool.size()))
	{
		ImpactVisualPool.resize(VisualIndex + 1);
	}

	if (VisualIndex >= static_cast<int32>(ImpactVisualRemainingTimes.size()))
	{
		ImpactVisualRemainingTimes.resize(VisualIndex + 1, 0.0f);
	}

	ImpactVisualPool[VisualIndex] = Visual;
	return Visual;
}

UMaterial* UBallisticBulletManagerComponent::ResolveImpactVisualMaterial()
{
	if (UMaterial* Existing = ImpactVisualMaterial.Get())
	{
		return Existing;
	}

	const FString MaterialPath =
		(!ImpactVisualMaterialPath.empty() && ImpactVisualMaterialPath != "None")
		? static_cast<FString>(ImpactVisualMaterialPath)
		: FString(SniperDefaultBulletVisualMaterialPath);

	UMaterial* LoadedMaterial = FMaterialManager::Get().GetOrCreateMaterial(MaterialPath);
	ImpactVisualMaterial = LoadedMaterial;
	return LoadedMaterial;
}

UMaterial* UBallisticBulletManagerComponent::ResolveBulletImpactDecalMaterial()
{
	if (UMaterial* Existing = BulletImpactDecalMaterial.Get())
	{
		return Existing;
	}

	const FString MaterialPath =
		(!BulletImpactDecalMaterialPath.empty() && BulletImpactDecalMaterialPath != "None")
		? static_cast<FString>(BulletImpactDecalMaterialPath)
		: FString(SniperDefaultBulletImpactDecalMaterialPath);

	UMaterial* LoadedMaterial = FMaterialManager::Get().GetOrCreateMaterial(MaterialPath);
	BulletImpactDecalMaterial = LoadedMaterial;
	return LoadedMaterial;
}

void UBallisticBulletManagerComponent::SpawnImpactVisual(const FVector& ImpactLocation)
{
	if (!bEnableImpactVisuals)
	{
		return;
	}

	int32 FreeVisualIndex = -1;
	for (int32 VisualIndex = 0; VisualIndex < static_cast<int32>(ImpactVisualRemainingTimes.size()); ++VisualIndex)
	{
		if (ImpactVisualRemainingTimes[VisualIndex] <= 0.0f)
		{
			FreeVisualIndex = VisualIndex;
			break;
		}
	}

	if (FreeVisualIndex < 0)
	{
		FreeVisualIndex = static_cast<int32>(ImpactVisualPool.size());
	}

	UBillboardComponent* Visual = GetOrCreateImpactVisual(FreeVisualIndex);
	if (!Visual)
	{
		return;
	}

	if (FreeVisualIndex >= static_cast<int32>(ImpactVisualRemainingTimes.size()))
	{
		ImpactVisualRemainingTimes.resize(FreeVisualIndex + 1, 0.0f);
	}

	ImpactVisualRemainingTimes[FreeVisualIndex] = ImpactVisualLifetime;
	Visual->SetWorldLocation(ImpactLocation);
	Visual->SetRelativeScale(FVector(1.0f, ImpactVisualScale, ImpactVisualScale));
	Visual->SetVisibility(true);
}

bool UBallisticBulletManagerComponent::ShouldSpawnBulletImpactDecal(const FHitResult& Hit) const
{
	if (!bEnableBulletImpactDecals || !Hit.bHit)
	{
		return false;
	}

	if (Hit.HitActor && Cast<ACombatCharacter>(Hit.HitActor))
	{
		return false;
	}

	if (Hit.HitComponent && Cast<USkeletalMeshComponent>(Hit.HitComponent))
	{
		return false;
	}

	return Hit.HitComponent != nullptr || Hit.HitActor != nullptr;
}

FVector4 UBallisticBulletManagerComponent::PickBulletImpactDecalAtlasRect(const FBallisticBullet& Bullet, const FHitResult& Hit) const
{
	const int32 Columns = (std::max)(1, BulletImpactDecalAtlasColumns);
	const int32 Rows = (std::max)(1, BulletImpactDecalAtlasRows);
	const int32 TileCount = (std::max)(1, Columns * Rows);
	const uint32 TileIndex = HashBulletImpactDecalSeed(Bullet.BulletId, Hit.WorldHitLocation) % static_cast<uint32>(TileCount);
	const int32 TileX = static_cast<int32>(TileIndex % static_cast<uint32>(Columns));
	const int32 TileY = static_cast<int32>(TileIndex / static_cast<uint32>(Columns));
	const float TileW = 1.0f / static_cast<float>(Columns);
	const float TileH = 1.0f / static_cast<float>(Rows);
	return FVector4(TileX * TileW, TileY * TileH, TileW, TileH);
}

void UBallisticBulletManagerComponent::SpawnBulletImpactDecal(const FBallisticBullet& Bullet, const FHitResult& Hit, UWorld* World)
{
	if (!World || !ShouldSpawnBulletImpactDecal(Hit))
	{
		return;
	}

	UMaterial* DecalMaterial = ResolveBulletImpactDecalMaterial();
	if (!DecalMaterial)
	{
		return;
	}

	FVector SurfaceNormal = !Hit.ImpactNormal.IsNearlyZero() ? Hit.ImpactNormal : Hit.WorldNormal;
	if (SurfaceNormal.IsNearlyZero())
	{
		SurfaceNormal = FVector::UpVector;
	}
	SurfaceNormal.Normalize();

	FVector BulletDirection = !Bullet.Velocity.IsNearlyZero()
		? Bullet.Velocity.Normalized()
		: (Hit.WorldHitLocation - Bullet.PreviousPosition).Normalized();
	if (BulletDirection.IsNearlyZero())
	{
		BulletDirection = SurfaceNormal * -1.0f;
	}

	// +X is decal front/projection direction. Use the bullet travel direction when it
	// meaningfully enters the surface, otherwise fall back to the contact normal.
	const float DirectionNormalDot = BulletDirection.Dot(SurfaceNormal);
	if (DirectionNormalDot > 0.0f)
	{
		BulletDirection *= -1.0f;
	}
	else if (std::abs(DirectionNormalDot) < 0.15f)
	{
		BulletDirection = SurfaceNormal * -1.0f;
	}

	const float DecalDepth = (std::max)(0.001f, BulletImpactDecalDepth);
	const float DecalSize = (std::max)(0.001f, BulletImpactDecalSize);
	const FVector DecalLocation =
		Hit.WorldHitLocation +
		SurfaceNormal * BulletImpactDecalSurfaceOffset +
		BulletDirection * (DecalDepth * 0.5f);

	AActor* DecalActor = World->SpawnActor<AActor>();
	if (!DecalActor)
	{
		return;
	}
	DecalActor->SetFName(FName("BulletImpact_Decal"));

	UDecalComponent* DecalComponent = DecalActor->AddComponent<UDecalComponent>();
	if (!DecalComponent)
	{
		World->DestroyActor(DecalActor);
		return;
	}

	DecalActor->SetRootComponent(DecalComponent);
	DecalComponent->SetHiddenInComponentTree(true);
	DecalComponent->SetMaterial(DecalMaterial);
	DecalComponent->SetAtlasRect(PickBulletImpactDecalAtlasRect(Bullet, Hit));
	DecalComponent->SetWorldLocation(DecalLocation);
	DecalComponent->SetWorldRotation(MakeRotationWithForwardX(BulletDirection, SurfaceNormal));
	DecalComponent->SetRelativeScale(FVector(DecalDepth, DecalSize, DecalSize));
}

bool UBallisticBulletManagerComponent::QueryTaggedFloorBoundsHit(
	const FBallisticBullet& Bullet,
	UWorld* World,
	FHitResult& OutHit) const
{
	if (!World)
	{
		return false;
	}

	const FVector Segment = Bullet.Position - Bullet.PreviousPosition;
	const float SegmentLength = Segment.Length();
	const bool bHasSegment = SegmentLength > SniperBulletMinSweepRadius;
	const FVector SegmentDirection = bHasSegment ? Segment / SegmentLength : FVector::ZeroVector;
	float BestDistance = FLT_MAX;
	FVector BestLocation = Bullet.Position;
	AActor* BestActor = nullptr;
	UPrimitiveComponent* BestComponent = nullptr;
	for (AActor* FloorActor : FGameplayStatics::FindActorsByTag(World, SniperFloorActorTag))
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

			const FBoundingBox Bounds = ExpandSniperFloorBounds(Primitive->GetWorldBoundingBox());
			if (!Bounds.IsValid())
			{
				continue;
			}

			if (!bHasSegment)
			{
				if (Bounds.IsContains(Bullet.Position) && 0.0f < BestDistance)
				{
					BestDistance = 0.0f;
					BestLocation = Bullet.Position;
					BestActor = FloorActor;
					BestComponent = Primitive;
				}
				continue;
			}

			float EnterDistance = 0.0f;
			float ExitDistance = SegmentLength;
			if (!UpdateRayAabbInterval(Bullet.PreviousPosition.X, SegmentDirection.X, Bounds.Min.X, Bounds.Max.X, EnterDistance, ExitDistance) ||
				!UpdateRayAabbInterval(Bullet.PreviousPosition.Y, SegmentDirection.Y, Bounds.Min.Y, Bounds.Max.Y, EnterDistance, ExitDistance) ||
				!UpdateRayAabbInterval(Bullet.PreviousPosition.Z, SegmentDirection.Z, Bounds.Min.Z, Bounds.Max.Z, EnterDistance, ExitDistance))
			{
				continue;
			}

			if (EnterDistance >= 0.0f && EnterDistance <= SegmentLength && EnterDistance < BestDistance)
			{
				BestDistance = EnterDistance;
				BestLocation = Bullet.PreviousPosition + SegmentDirection * BestDistance;
				BestActor = FloorActor;
				BestComponent = Primitive;
			}
		}
	}

	if (!BestActor || !BestComponent || BestDistance == FLT_MAX)
	{
		return false;
	}

	OutHit = FHitResult();
	OutHit.bHit = true;
	OutHit.HitActor = BestActor;
	OutHit.HitComponent = BestComponent;
	OutHit.WorldHitLocation = BestLocation;
	OutHit.Distance = BestDistance;
	OutHit.WorldNormal = FVector::UpVector;
	OutHit.ImpactNormal = FVector::UpVector;
	return true;
}

bool UBallisticBulletManagerComponent::QueryBulletHit(const FBallisticBullet& Bullet, UWorld* World, FHitResult& OutHit) const
{
	if (!World)
	{
		return false;
	}

	const FVector Segment = Bullet.Position - Bullet.PreviousPosition;
	const float SegmentLength = Segment.Length();
	if (SegmentLength <= SniperBulletMinSweepRadius)
	{
		return QueryTaggedFloorBoundsHit(Bullet, World, OutHit);
	}

	const FVector SegmentDirection = Segment / SegmentLength;
	auto QueryFloorFallback = [this, &Bullet, World, &OutHit]() -> bool
	{
		return QueryTaggedFloorBoundsHit(Bullet, World, OutHit);
	};
	auto FinalizeBulletHit = [this, &Bullet, World, &OutHit](const FHitResult& CandidateHit) -> bool
	{
		if (!CandidateHit.bHit)
		{
			return false;
		}

		OutHit = CandidateHit;
		USkeletalMeshComponent* SkeletalMeshComponent = ResolveHitSkeletalMeshComponent(OutHit);
		const bool bHasQueryBodies = SkeletalMeshComponent && SkeletalMeshComponent->HasPhysicsAssetQueryBodies();
		const bool bHasLivePhysicsBodies =
			SkeletalMeshComponent &&
			SkeletalMeshComponent->GetPhysicsAssetInstance() &&
			SkeletalMeshComponent->GetPhysicsAssetInstance()->HasLivePhysicsObjects();

		FHitResult PreciseHit;
		FSniperPreciseHitQueryDiagnostics PreciseDiagnostics;
		if (ShouldRunPreciseCharacterHitQuery(OutHit))
		{
			if (QueryPreciseCharacterHit(Bullet, World, OutHit, PreciseHit, &PreciseDiagnostics))
			{
				OutHit = PreciseHit;
				if (bLogPreciseCharacterHitDiagnostics)
				{
					UE_LOG(
						"[SniperDebug] Character precise hit accepted. Mode=%s Actor=%s BroadBone=%s PreciseBone=%s Delta=%.3f TravelDelta=%.3f SyncAttempted=%d SyncSucceeded=%d",
						GetSniperPreciseHitQueryModeName(PreciseDiagnostics.QueryMode),
						OutHit.HitActor ? OutHit.HitActor->GetName().c_str() : "None",
						PreciseDiagnostics.BroadHitBoneName.ToString().c_str(),
						PreciseDiagnostics.PreciseHitBoneName.ToString().c_str(),
						PreciseDiagnostics.BroadToPreciseDistance,
						PreciseDiagnostics.BroadToPreciseTravelDelta,
						PreciseDiagnostics.bSyncAttempted ? 1 : 0,
						PreciseDiagnostics.bSyncSucceeded ? 1 : 0);
				}
				return true;
			}

			if (PreciseDiagnostics.QueryMode == ESniperPreciseHitQueryMode::CharacterQueryBody &&
				PreciseDiagnostics.RejectReason == ESniperPreciseHitRejectReason::QueryBodySyncFailed &&
				bAllowPoseFallbackWhenQueryBodySyncFails)
			{
				if (QueryPosePhysicsAssetCharacterHit(Bullet, OutHit, PreciseHit, &PreciseDiagnostics))
				{
					OutHit = PreciseHit;
					if (bLogPreciseCharacterHitDiagnostics)
					{
						UE_LOG(
							"[SniperDebug] Character pose fallback accepted after query-body sync failure. Actor=%s BroadBone=%s PreciseBone=%s Delta=%.3f TravelDelta=%.3f",
							OutHit.HitActor ? OutHit.HitActor->GetName().c_str() : "None",
							PreciseDiagnostics.BroadHitBoneName.ToString().c_str(),
							PreciseDiagnostics.PreciseHitBoneName.ToString().c_str(),
							PreciseDiagnostics.BroadToPreciseDistance,
							PreciseDiagnostics.BroadToPreciseTravelDelta);
					}
					return true;
				}
			}

			const bool bCharacterQueryBodyPreciseMiss =
				PreciseDiagnostics.QueryMode == ESniperPreciseHitQueryMode::CharacterQueryBody &&
				PreciseDiagnostics.RejectReason == ESniperPreciseHitRejectReason::PreciseMissAfterBroadHit;
			const bool bAllowBroadHitFallbackAfterQueryBodyMiss =
				bCharacterQueryBodyPreciseMiss && bHasQueryBodies && !bHasLivePhysicsBodies;
			if (bAllowBroadHitFallbackAfterQueryBodyMiss)
			{
				// Character query bodies are an optional precision pass layered on top of the
				// capsule broad hit. If their pose/shape setup is too small or slightly out
				// of sync, rejecting the already-confirmed capsule hit makes living characters
				// look bullet-proof. Keep strict rejection for live ragdoll bodies, but fall
				// back to the broad hit for ordinary character query-body misses.
				if (bLogPreciseCharacterHitDiagnostics)
				{
					UE_LOG(
						"[SniperDebug] Character query-body precise miss; accepting broad hit fallback. Mode=%s Reason=%s Actor=%s Component=%s BroadBone=%s QueryBodies=%d LiveBodies=%d SyncAttempted=%d SyncSucceeded=%d",
						GetSniperPreciseHitQueryModeName(PreciseDiagnostics.QueryMode),
						GetSniperPreciseHitRejectReasonName(PreciseDiagnostics.RejectReason),
						OutHit.HitActor ? OutHit.HitActor->GetName().c_str() : "None",
						OutHit.HitComponent ? OutHit.HitComponent->GetName().c_str() : "None",
						PreciseDiagnostics.BroadHitBoneName.ToString().c_str(),
						bHasQueryBodies ? 1 : 0,
						bHasLivePhysicsBodies ? 1 : 0,
						PreciseDiagnostics.bSyncAttempted ? 1 : 0,
						PreciseDiagnostics.bSyncSucceeded ? 1 : 0);
				}
				return true;
			}

			if ((bHasQueryBodies && bRejectBroadHitWhenQueryBodyPreciseMisses) || bHasLivePhysicsBodies)
			{
				if (bLogPreciseCharacterHitDiagnostics)
				{
					UE_LOG(
						"[SniperDebug] Character broad hit rejected by live precision query. Mode=%s Reason=%s Actor=%s Component=%s BroadBone=%s PreciseBone=%s Delta=%.3f TravelDelta=%.3f QueryBodies=%d LiveBodies=%d SyncAttempted=%d SyncSucceeded=%d",
						GetSniperPreciseHitQueryModeName(PreciseDiagnostics.QueryMode),
						GetSniperPreciseHitRejectReasonName(PreciseDiagnostics.RejectReason),
						OutHit.HitActor ? OutHit.HitActor->GetName().c_str() : "None",
						OutHit.HitComponent ? OutHit.HitComponent->GetName().c_str() : "None",
						PreciseDiagnostics.BroadHitBoneName.ToString().c_str(),
						PreciseDiagnostics.PreciseHitBoneName.ToString().c_str(),
						PreciseDiagnostics.BroadToPreciseDistance,
						PreciseDiagnostics.BroadToPreciseTravelDelta,
						bHasQueryBodies ? 1 : 0,
						bHasLivePhysicsBodies ? 1 : 0,
						PreciseDiagnostics.bSyncAttempted ? 1 : 0,
						PreciseDiagnostics.bSyncSucceeded ? 1 : 0);
				}
				return false;
			}
		}

		if (ShouldRunPosePhysicsAssetHitQuery(OutHit))
		{
			if (QueryPosePhysicsAssetCharacterHit(Bullet, OutHit, PreciseHit, &PreciseDiagnostics))
			{
				OutHit = PreciseHit;
				if (bLogPreciseCharacterHitDiagnostics)
				{
					UE_LOG(
						"[SniperDebug] Character pose precision accepted. Actor=%s BroadBone=%s PreciseBone=%s Delta=%.3f TravelDelta=%.3f",
						OutHit.HitActor ? OutHit.HitActor->GetName().c_str() : "None",
						PreciseDiagnostics.BroadHitBoneName.ToString().c_str(),
						PreciseDiagnostics.PreciseHitBoneName.ToString().c_str(),
						PreciseDiagnostics.BroadToPreciseDistance,
						PreciseDiagnostics.BroadToPreciseTravelDelta);
				}
				return true;
			}

			if (bLogPreciseCharacterHitDiagnostics)
			{
				UE_LOG(
					"[SniperDebug] Character broad hit rejected by PhysicsAsset precision. Mode=%s Reason=%s Actor=%s Component=%s BroadBone=%s PreciseBone=%s Delta=%.3f TravelDelta=%.3f",
					GetSniperPreciseHitQueryModeName(PreciseDiagnostics.QueryMode),
					GetSniperPreciseHitRejectReasonName(PreciseDiagnostics.RejectReason),
					OutHit.HitActor ? OutHit.HitActor->GetName().c_str() : "None",
					OutHit.HitComponent ? OutHit.HitComponent->GetName().c_str() : "None",
					PreciseDiagnostics.BroadHitBoneName.ToString().c_str(),
					PreciseDiagnostics.PreciseHitBoneName.ToString().c_str(),
					PreciseDiagnostics.BroadToPreciseDistance,
					PreciseDiagnostics.BroadToPreciseTravelDelta);
			}
			return false;
		}

		return true;
	};

	if (Bullet.Radius > SniperBulletMinSweepRadius)
	{
		const FCollisionShape SweepShape = FCollisionShape::MakeSphere(Bullet.Radius);
		FVector QueryStart = Bullet.PreviousPosition;
		for (int32 SkipCount = 0; SkipCount <= SniperBulletIgnoredHitMaxSkips; ++SkipCount)
		{
			const FVector RemainingSegment = Bullet.Position - QueryStart;
			const float RemainingLength = RemainingSegment.Length();
			if (RemainingLength <= SniperBulletMinSweepRadius)
			{
				return QueryFloorFallback();
			}

			FHitResult CandidateHit;
			if (!World->PhysicsSweepByObjectTypes(
				QueryStart,
				Bullet.Position,
				FQuat::Identity,
				SweepShape,
				CandidateHit,
				SniperBulletQueryObjectMask,
				Bullet.Owner))
			{
				break;
			}

			if (!CandidateHit.bHit)
			{
				return QueryFloorFallback();
			}

			if (!IsSniperMovementLimitWallActor(CandidateHit.HitActor))
			{
				return FinalizeBulletHit(CandidateHit);
			}

			const float AdvanceDistance = ComputeIgnoredBulletHitAdvanceDistance(
				CandidateHit,
				QueryStart,
				SegmentDirection,
				RemainingLength);
			if (AdvanceDistance >= RemainingLength)
			{
				return QueryFloorFallback();
			}

			QueryStart = QueryStart + SegmentDirection * AdvanceDistance;
		}
	}

	FVector QueryStart = Bullet.PreviousPosition;
	for (int32 SkipCount = 0; SkipCount <= SniperBulletIgnoredHitMaxSkips; ++SkipCount)
	{
		const FVector RemainingSegment = Bullet.Position - QueryStart;
		const float RemainingLength = RemainingSegment.Length();
		if (RemainingLength <= SniperBulletMinSweepRadius)
		{
			return QueryFloorFallback();
		}

		FHitResult CandidateHit;
		if (!World->PhysicsRaycastByObjectTypes(
			QueryStart,
			SegmentDirection,
			RemainingLength,
			CandidateHit,
			SniperBulletQueryObjectMask,
			Bullet.Owner))
		{
			return QueryFloorFallback();
		}

		if (!CandidateHit.bHit)
		{
			return QueryFloorFallback();
		}

		if (!IsSniperMovementLimitWallActor(CandidateHit.HitActor))
		{
			return FinalizeBulletHit(CandidateHit);
		}

		const float AdvanceDistance = ComputeIgnoredBulletHitAdvanceDistance(
			CandidateHit,
			QueryStart,
			SegmentDirection,
			RemainingLength);
		if (AdvanceDistance >= RemainingLength)
		{
			return QueryFloorFallback();
		}

		QueryStart = QueryStart + SegmentDirection * AdvanceDistance;
	}

	return QueryFloorFallback();
}

bool UBallisticBulletManagerComponent::ShouldRunPreciseCharacterHitQuery(const FHitResult& BroadHit) const
{
	if (!bEnablePreciseCharacterHitQuery || !BroadHit.bHit || !BroadHit.HitActor)
	{
		return false;
	}

	if (!Cast<ACombatCharacter>(BroadHit.HitActor) &&
		!BroadHit.HitActor->GetComponentByClass<USniperDamageReceiverComponent>())
	{
		return false;
	}

	USkeletalMeshComponent* SkeletalMeshComponent = ResolveHitSkeletalMeshComponent(BroadHit);
	if (!SkeletalMeshComponent)
	{
		return false;
	}

	if (SkeletalMeshComponent->HasPhysicsAssetQueryBodies())
	{
		return true;
	}

	FPhysicsAssetInstance* PhysicsAssetInstance = SkeletalMeshComponent->GetPhysicsAssetInstance();
	return PhysicsAssetInstance && PhysicsAssetInstance->HasLivePhysicsObjects();
}

bool UBallisticBulletManagerComponent::ShouldRunPosePhysicsAssetHitQuery(const FHitResult& BroadHit) const
{
	if (!bEnablePreciseCharacterHitQuery || !BroadHit.bHit || !BroadHit.HitActor)
	{
		return false;
	}

	if (!Cast<ACombatCharacter>(BroadHit.HitActor) &&
		!BroadHit.HitActor->GetComponentByClass<USniperDamageReceiverComponent>())
	{
		return false;
	}

	USkeletalMeshComponent* SkeletalMeshComponent = ResolveHitSkeletalMeshComponent(BroadHit);
	if (!SkeletalMeshComponent)
	{
		return false;
	}

	if (SkeletalMeshComponent->HasPhysicsAssetQueryBodies())
	{
		return false;
	}

	if (FPhysicsAssetInstance* PhysicsAssetInstance = SkeletalMeshComponent->GetPhysicsAssetInstance())
	{
		if (PhysicsAssetInstance->HasLivePhysicsObjects())
		{
			return false;
		}
	}

	UPhysicsAsset* PhysicsAsset = SkeletalMeshComponent->GetEffectivePhysicsAsset();
	if (!PhysicsAsset || PhysicsAsset->GetBodySetups().empty())
	{
		return false;
	}

	if (Cast<UCapsuleComponent>(BroadHit.HitComponent))
	{
		return true;
	}

	if (!Cast<USkeletalMeshComponent>(BroadHit.HitComponent))
	{
		return true;
	}

	return BroadHit.HitBoneName == FName::None;
}

bool UBallisticBulletManagerComponent::EnsurePreciseHitQueryBodies(USkeletalMeshComponent* SkeletalMeshComponent, bool& bOutCreatedTemporaryBodies) const
{
	bOutCreatedTemporaryBodies = false;
	if (!SkeletalMeshComponent)
	{
		return false;
	}

	// Do not auto-create temporary PhysicsAsset bodies for precise hit queries.
	// If the character is not already running with live bodies, we skip the precise pass
	// and keep the normal broad-hit path so gameplay actors never enter a transient
	// physics-pose state during ordinary PIE startup or hits.
	if (FPhysicsAssetInstance* ExistingInstance = SkeletalMeshComponent->GetPhysicsAssetInstance())
	{
		return ExistingInstance->HasLivePhysicsObjects();
	}

	return false;
}

float UBallisticBulletManagerComponent::ResolveBroadToPreciseDistanceThreshold(ESniperPreciseHitQueryMode QueryMode) const
{
	switch (QueryMode)
	{
	case ESniperPreciseHitQueryMode::CharacterQueryBody:
		return MaxQueryBodyBroadToPreciseDistance > 0.0f
			? MaxQueryBodyBroadToPreciseDistance
			: MaxPreciseCharacterHitDistance;
	case ESniperPreciseHitQueryMode::LiveRagdollBody:
		return MaxRagdollBroadToPreciseDistance > 0.0f
			? MaxRagdollBroadToPreciseDistance
			: MaxPreciseCharacterHitDistance;
	case ESniperPreciseHitQueryMode::PosePhysicsAssetFallback:
		return MaxPoseFallbackBroadToPreciseDistance > 0.0f
			? MaxPoseFallbackBroadToPreciseDistance
			: MaxPreciseCharacterHitDistance;
	case ESniperPreciseHitQueryMode::None:
	default:
		return MaxPreciseCharacterHitDistance;
	}
}

bool UBallisticBulletManagerComponent::QueryCharacterQueryBodyHit(
	const FBallisticBullet& Bullet,
	UWorld* World,
	const FHitResult& BroadHit,
	FHitResult& OutPreciseHit,
	FSniperPreciseHitQueryDiagnostics* OutDiagnostics) const
{
	OutPreciseHit = FHitResult();
	if (OutDiagnostics)
	{
		*OutDiagnostics = FSniperPreciseHitQueryDiagnostics();
		OutDiagnostics->QueryMode = ESniperPreciseHitQueryMode::CharacterQueryBody;
		OutDiagnostics->BroadHitBoneName = BroadHit.HitBoneName;
		OutDiagnostics->BroadHitLocation = BroadHit.WorldHitLocation;
	}

	if (!World || !BroadHit.HitActor)
	{
		return false;
	}

	USkeletalMeshComponent* SkeletalMeshComponent = ResolveHitSkeletalMeshComponent(BroadHit);
	if (!SkeletalMeshComponent || !SkeletalMeshComponent->HasPhysicsAssetQueryBodies())
	{
		if (OutDiagnostics)
		{
			OutDiagnostics->RejectReason = ESniperPreciseHitRejectReason::NoSkeletalMesh;
		}
		return false;
	}

	IPhysicsScene* PhysicsScene = World->GetPhysicsScene();
	if (!PhysicsScene)
	{
		if (OutDiagnostics)
		{
			OutDiagnostics->RejectReason = ESniperPreciseHitRejectReason::NoPhysicsScene;
		}
		return false;
	}

	if (OutDiagnostics)
	{
		OutDiagnostics->bSyncAttempted = true;
	}

	if (!SkeletalMeshComponent->EnsurePhysicsAssetQueryBodiesSyncedForFrame(World->GetPhysicsFrameIndex()))
	{
		if (OutDiagnostics)
		{
			OutDiagnostics->RejectReason = ESniperPreciseHitRejectReason::QueryBodySyncFailed;
		}
		if (bLogPreciseCharacterHitDiagnostics)
		{
			UE_LOG(
				"[SniperDebug] Character query-body sync failed. Actor=%s Mesh=%s Frame=%llu",
				BroadHit.HitActor ? BroadHit.HitActor->GetName().c_str() : "None",
				SkeletalMeshComponent->GetName().c_str(),
				static_cast<unsigned long long>(World->GetPhysicsFrameIndex()));
		}
		return false;
	}

	if (OutDiagnostics)
	{
		OutDiagnostics->bSyncSucceeded = true;
	}

	bool bPreciseHit = false;
	if (Bullet.Radius > SniperBulletMinSweepRadius)
	{
		const FCollisionShape SweepShape = FCollisionShape::MakeSphere(Bullet.Radius);
		bPreciseHit = PhysicsScene->SweepCharacterQueryBodiesByObjectTypes(
			Bullet.PreviousPosition,
			Bullet.Position,
			FQuat::Identity,
			SweepShape,
			OutPreciseHit,
			ObjectTypeBit(ECollisionChannel::Pawn),
			BroadHit.HitActor,
			Bullet.Owner);
	}
	else
	{
		const FVector Segment = Bullet.Position - Bullet.PreviousPosition;
		const float SegmentLength = Segment.Length();
		if (SegmentLength <= SniperBulletMinSweepRadius)
		{
			return false;
		}

		bPreciseHit = PhysicsScene->RaycastCharacterQueryBodiesByObjectTypes(
			Bullet.PreviousPosition,
			Segment / SegmentLength,
			SegmentLength,
			OutPreciseHit,
			ObjectTypeBit(ECollisionChannel::Pawn),
			BroadHit.HitActor,
			Bullet.Owner);
	}

	if (!bPreciseHit)
	{
		if (OutDiagnostics)
		{
			OutDiagnostics->RejectReason = ESniperPreciseHitRejectReason::PreciseMissAfterBroadHit;
		}
		return false;
	}

	if (OutDiagnostics)
	{
		OutDiagnostics->PreciseHitBoneName = OutPreciseHit.HitBoneName;
		OutDiagnostics->PreciseHitLocation = OutPreciseHit.WorldHitLocation;
	}

	const float MaxDistance = ResolveBroadToPreciseDistanceThreshold(ESniperPreciseHitQueryMode::CharacterQueryBody);
	float WorldDelta = 0.0f;
	float TravelDelta = 0.0f;
	const bool bWithinThreshold = IsPreciseHitWithinThreshold(
		BroadHit,
		OutPreciseHit,
		MaxDistance,
		WorldDelta,
		TravelDelta);
	if (OutDiagnostics)
	{
		OutDiagnostics->BroadToPreciseDistance = WorldDelta;
		OutDiagnostics->BroadToPreciseTravelDelta = TravelDelta;
	}
	if (!bWithinThreshold)
	{
		if (OutDiagnostics)
		{
			OutDiagnostics->RejectReason = ESniperPreciseHitRejectReason::BroadPreciseDistanceExceeded;
		}
		OutPreciseHit = FHitResult();
		return false;
	}

	if (OutDiagnostics)
	{
		OutDiagnostics->bAccepted = true;
	}

	return true;
}

bool UBallisticBulletManagerComponent::QueryPreciseCharacterHit(
	const FBallisticBullet& Bullet,
	UWorld* World,
	const FHitResult& BroadHit,
	FHitResult& OutPreciseHit,
	FSniperPreciseHitQueryDiagnostics* OutDiagnostics) const
{
	OutPreciseHit = FHitResult();
	if (OutDiagnostics)
	{
		*OutDiagnostics = FSniperPreciseHitQueryDiagnostics();
		OutDiagnostics->BroadHitBoneName = BroadHit.HitBoneName;
		OutDiagnostics->BroadHitLocation = BroadHit.WorldHitLocation;
	}

	if (!World || !BroadHit.HitActor)
	{
		return false;
	}

	USkeletalMeshComponent* SkeletalMeshComponent = ResolveHitSkeletalMeshComponent(BroadHit);
	if (!SkeletalMeshComponent)
	{
		if (OutDiagnostics)
		{
			OutDiagnostics->RejectReason = ESniperPreciseHitRejectReason::NoSkeletalMesh;
		}
		return false;
	}

	if (SkeletalMeshComponent->HasPhysicsAssetQueryBodies())
	{
		return QueryCharacterQueryBodyHit(Bullet, World, BroadHit, OutPreciseHit, OutDiagnostics);
	}

	IPhysicsScene* PhysicsScene = World->GetPhysicsScene();
	if (!PhysicsScene)
	{
		if (OutDiagnostics)
		{
			OutDiagnostics->QueryMode = ESniperPreciseHitQueryMode::LiveRagdollBody;
			OutDiagnostics->RejectReason = ESniperPreciseHitRejectReason::NoPhysicsScene;
		}
		return false;
	}

	bool bCreatedTemporaryBodies = false;
	if (!EnsurePreciseHitQueryBodies(SkeletalMeshComponent, bCreatedTemporaryBodies))
	{
		if (OutDiagnostics)
		{
			OutDiagnostics->QueryMode = ESniperPreciseHitQueryMode::LiveRagdollBody;
			OutDiagnostics->RejectReason = ESniperPreciseHitRejectReason::PreciseMissAfterBroadHit;
		}
		return false;
	}

	const auto CleanupTemporaryBodies = [&]()
	{
		if (bCreatedTemporaryBodies)
		{
			if (FPhysicsAssetInstance* PhysicsAssetInstance = SkeletalMeshComponent->GetPhysicsAssetInstance())
			{
				PhysicsAssetInstance->DestroyBodiesAndConstraints();
			}
		}
	};

	bool bPreciseHit = false;
	if (OutDiagnostics)
	{
		OutDiagnostics->QueryMode = ESniperPreciseHitQueryMode::LiveRagdollBody;
	}

	if (Bullet.Radius > SniperBulletMinSweepRadius)
	{
		const FCollisionShape SweepShape = FCollisionShape::MakeSphere(Bullet.Radius);
		bPreciseHit = PhysicsScene->SweepRagdollBodiesByObjectTypes(
			Bullet.PreviousPosition,
			Bullet.Position,
			FQuat::Identity,
			SweepShape,
			OutPreciseHit,
			ObjectTypeBit(ECollisionChannel::Pawn),
			BroadHit.HitActor,
			Bullet.Owner);
	}
	else
	{
		const FVector Segment = Bullet.Position - Bullet.PreviousPosition;
		const float SegmentLength = Segment.Length();
		if (SegmentLength > SniperBulletMinSweepRadius)
		{
			bPreciseHit = PhysicsScene->RaycastRagdollBodiesByObjectTypes(
				Bullet.PreviousPosition,
				Segment / SegmentLength,
				SegmentLength,
				OutPreciseHit,
				ObjectTypeBit(ECollisionChannel::Pawn),
				BroadHit.HitActor,
				Bullet.Owner);
		}
	}

	if (!bPreciseHit)
	{
		if (OutDiagnostics)
		{
			OutDiagnostics->RejectReason = ESniperPreciseHitRejectReason::PreciseMissAfterBroadHit;
		}
		CleanupTemporaryBodies();
		return false;
	}

	if (OutDiagnostics)
	{
		OutDiagnostics->PreciseHitBoneName = OutPreciseHit.HitBoneName;
		OutDiagnostics->PreciseHitLocation = OutPreciseHit.WorldHitLocation;
	}

	const float MaxDistance = ResolveBroadToPreciseDistanceThreshold(ESniperPreciseHitQueryMode::LiveRagdollBody);
	float WorldDelta = 0.0f;
	float TravelDelta = 0.0f;
	const bool bWithinThreshold = IsPreciseHitWithinThreshold(
		BroadHit,
		OutPreciseHit,
		MaxDistance,
		WorldDelta,
		TravelDelta);
	if (OutDiagnostics)
	{
		OutDiagnostics->BroadToPreciseDistance = WorldDelta;
		OutDiagnostics->BroadToPreciseTravelDelta = TravelDelta;
	}
	if (bPreciseHit && !bWithinThreshold)
	{
		bPreciseHit = false;
		if (OutDiagnostics)
		{
			OutDiagnostics->RejectReason = ESniperPreciseHitRejectReason::BroadPreciseDistanceExceeded;
		}
		OutPreciseHit = FHitResult();
	}

	if (bPreciseHit && OutDiagnostics)
	{
		OutDiagnostics->bAccepted = true;
	}

	CleanupTemporaryBodies();
	return bPreciseHit;
}

bool UBallisticBulletManagerComponent::QueryPosePhysicsAssetCharacterHit(
	const FBallisticBullet& Bullet,
	const FHitResult& BroadHit,
	FHitResult& OutPreciseHit,
	FSniperPreciseHitQueryDiagnostics* OutDiagnostics) const
{
	OutPreciseHit = FHitResult();
	if (OutDiagnostics)
	{
		*OutDiagnostics = FSniperPreciseHitQueryDiagnostics();
		OutDiagnostics->QueryMode = ESniperPreciseHitQueryMode::PosePhysicsAssetFallback;
		OutDiagnostics->BroadHitBoneName = BroadHit.HitBoneName;
		OutDiagnostics->BroadHitLocation = BroadHit.WorldHitLocation;
	}

	if (!BroadHit.bHit || !BroadHit.HitActor)
	{
		return false;
	}

	USkeletalMeshComponent* SkeletalMeshComponent = ResolveHitSkeletalMeshComponent(BroadHit);
	if (!SkeletalMeshComponent)
	{
		if (OutDiagnostics)
		{
			OutDiagnostics->RejectReason = ESniperPreciseHitRejectReason::NoSkeletalMesh;
		}
		return false;
	}

	UPhysicsAsset* PhysicsAsset = SkeletalMeshComponent->GetEffectivePhysicsAsset();
	if (!PhysicsAsset)
	{
		return false;
	}

	const FVector Segment = Bullet.Position - Bullet.PreviousPosition;
	const float SegmentLength = Segment.Length();
	if (SegmentLength <= SniperBulletMinSweepRadius)
	{
		return false;
	}

	FPhysicsAssetPreviewPoseCache PoseCache;
	if (!PoseCache.Initialize(SkeletalMeshComponent, PhysicsAsset))
	{
		if (bLogPreciseCharacterHitDiagnostics)
		{
			UE_LOG(
				"[SniperDebug] PhysicsAsset precision skipped: pose cache unavailable. Actor=%s Mesh=%s",
				BroadHit.HitActor ? BroadHit.HitActor->GetName().c_str() : "None",
				SkeletalMeshComponent->GetName().c_str());
		}
		return false;
	}

	const float BulletRadius = (std::max)(Bullet.Radius, 0.0f);
	const TArray<FPhysicsAssetBodySetup>& BodySetups = PhysicsAsset->GetBodySetups();
	FSniperPoseShapeHit BestHit;

	for (int32 BodyIndex = 0; BodyIndex < static_cast<int32>(BodySetups.size()); ++BodyIndex)
	{
		const FPhysicsAssetBodySetup& BodySetup = BodySetups[BodyIndex];
		if (!BodySetup.BoneName.IsValid() || BodySetup.BoneName == FName::None || BodySetup.Shapes.empty())
		{
			continue;
		}

		const FString NormalizedBodyBoneName = NormalizeBoneNameForHitClassification(BodySetup.BoneName);
		if (NormalizedBodyBoneName.empty() || IsAuxiliaryBoneNameNormalized(NormalizedBodyBoneName))
		{
			continue;
		}

		FTransform BodyWorld;
		if (!PoseCache.ComputeBodyWorldTransform(BodyIndex, BodyWorld))
		{
			continue;
		}

		for (int32 ShapeIndex = 0; ShapeIndex < static_cast<int32>(BodySetup.Shapes.size()); ++ShapeIndex)
		{
			const FPhysicsAssetShapeSetup& ShapeSetup = BodySetup.Shapes[ShapeIndex];
			const FTransform ShapeWorld = ComposeSniperPhysicsAssetTransforms(BodyWorld, ShapeSetup.LocalTransform);
			const FVector LocalStart = TransformWorldPositionToShapeLocal(Bullet.PreviousPosition, ShapeWorld);
			const FVector LocalEnd = TransformWorldPositionToShapeLocal(Bullet.Position, ShapeWorld);

			float T = 0.0f;
			bool bShapeHit = false;
			FVector LocalNormal = FVector::ZeroVector;
			switch (ShapeSetup.Type)
			{
			case EPhysicsAssetShapeType::Box:
			{
				const FVector HalfExtent(
					(std::max)(ShapeSetup.BoxHalfExtent.X, SniperPhysicsAssetHitMinShapeSize) + BulletRadius,
					(std::max)(ShapeSetup.BoxHalfExtent.Y, SniperPhysicsAssetHitMinShapeSize) + BulletRadius,
					(std::max)(ShapeSetup.BoxHalfExtent.Z, SniperPhysicsAssetHitMinShapeSize) + BulletRadius);
				bShapeHit = IntersectSegmentLocalBox(LocalStart, LocalEnd, HalfExtent, T);
				if (bShapeHit)
				{
					LocalNormal = ComputeBoxNormalLocal(FVector::Lerp(LocalStart, LocalEnd, T), HalfExtent);
				}
				break;
			}
			case EPhysicsAssetShapeType::Sphere:
			{
				const float Radius = (std::max)(ShapeSetup.SphereRadius, SniperPhysicsAssetHitMinShapeSize) + BulletRadius;
				bShapeHit = IntersectSegmentLocalSphere(LocalStart, LocalEnd, Radius, T);
				if (bShapeHit)
				{
					LocalNormal = FVector::Lerp(LocalStart, LocalEnd, T);
					if (LocalNormal.IsNearlyZero())
					{
						LocalNormal = FVector::ForwardVector;
					}
					else
					{
						LocalNormal.Normalize();
					}
				}
				break;
			}
			case EPhysicsAssetShapeType::Capsule:
			{
				const float ShapeRadius = (std::max)(ShapeSetup.CapsuleRadius, SniperPhysicsAssetHitMinShapeSize);
				const float ShapeHalfHeight = (std::max)(ShapeSetup.CapsuleHalfHeight, ShapeRadius);
				const float Radius = ShapeRadius + BulletRadius;
				const float HalfHeight = ShapeHalfHeight + BulletRadius;
				bShapeHit = IntersectSegmentLocalCapsuleZ(LocalStart, LocalEnd, Radius, HalfHeight, T);
				if (bShapeHit)
				{
					LocalNormal = ComputeCapsuleNormalLocal(FVector::Lerp(LocalStart, LocalEnd, T), Radius, HalfHeight);
				}
				break;
			}
			default:
				break;
			}

			if (!bShapeHit || T < 0.0f || T > 1.0f || T >= BestHit.T)
			{
				continue;
			}

			BestHit.bHit = true;
			BestHit.T = T;
			BestHit.BoneName = BodySetup.BoneName;
			BestHit.WorldNormal = ShapeWorld.Rotation.GetNormalized().RotateVector(LocalNormal);
			if (!BestHit.WorldNormal.IsNearlyZero())
			{
				BestHit.WorldNormal.Normalize();
			}
			BestHit.BodyIndex = BodyIndex;
			BestHit.ShapeIndex = ShapeIndex;
		}
	}

	if (!BestHit.bHit)
	{
		if (OutDiagnostics)
		{
			OutDiagnostics->RejectReason = ESniperPreciseHitRejectReason::PoseFallbackNoHit;
		}
		return false;
	}

	OutPreciseHit = BroadHit;
	OutPreciseHit.bHit = true;
	OutPreciseHit.HitActor = BroadHit.HitActor;
	OutPreciseHit.HitComponent = SkeletalMeshComponent;
	OutPreciseHit.HitBoneName = BestHit.BoneName;
	OutPreciseHit.WorldHitLocation = Bullet.PreviousPosition + Segment * BestHit.T;
	OutPreciseHit.Distance = SegmentLength * BestHit.T;
	OutPreciseHit.WorldNormal = !BestHit.WorldNormal.IsNearlyZero() ? BestHit.WorldNormal : BroadHit.WorldNormal;
	OutPreciseHit.ImpactNormal = !BestHit.WorldNormal.IsNearlyZero() ? BestHit.WorldNormal : BroadHit.ImpactNormal;

	if (OutDiagnostics)
	{
		OutDiagnostics->PreciseHitBoneName = OutPreciseHit.HitBoneName;
		OutDiagnostics->PreciseHitLocation = OutPreciseHit.WorldHitLocation;
	}

	const float MaxDistance = ResolveBroadToPreciseDistanceThreshold(ESniperPreciseHitQueryMode::PosePhysicsAssetFallback);
	float WorldDelta = 0.0f;
	float TravelDelta = 0.0f;
	const bool bWithinThreshold = IsPreciseHitWithinThreshold(
		BroadHit,
		OutPreciseHit,
		MaxDistance,
		WorldDelta,
		TravelDelta);
	if (OutDiagnostics)
	{
		OutDiagnostics->BroadToPreciseDistance = WorldDelta;
		OutDiagnostics->BroadToPreciseTravelDelta = TravelDelta;
	}
	if (!bWithinThreshold)
	{
		if (OutDiagnostics)
		{
			OutDiagnostics->RejectReason = ESniperPreciseHitRejectReason::BroadPreciseDistanceExceeded;
		}
		OutPreciseHit = FHitResult();
		return false;
	}

	if (OutDiagnostics)
	{
		OutDiagnostics->bAccepted = true;
	}

	if (bLogPreciseCharacterHitDiagnostics)
	{
		UE_LOG(
			"[SniperDebug] PhysicsAsset precise hit accepted: Actor=%s Bone=%s Body=%d Shape=%d Distance=%.2f Delta=%.3f TravelDelta=%.3f",
			BroadHit.HitActor ? BroadHit.HitActor->GetName().c_str() : "None",
			BestHit.BoneName.ToString().c_str(),
			BestHit.BodyIndex,
			BestHit.ShapeIndex,
			OutPreciseHit.Distance,
			OutDiagnostics ? OutDiagnostics->BroadToPreciseDistance : (BroadHit.WorldHitLocation - OutPreciseHit.WorldHitLocation).Length(),
			OutDiagnostics ? OutDiagnostics->BroadToPreciseTravelDelta : ComputePreciseHitTravelDelta(BroadHit, OutPreciseHit));
	}

	return true;
}

bool UBallisticBulletManagerComponent::IsFloorHit(const FHitResult& Hit) const
{
	if (AActor* HitActor = Hit.HitActor)
	{
		if (HitActor->HasTag(SniperFloorActorTag))
		{
			return true;
		}
	}

	return Hit.HitComponent != nullptr && Hit.HitComponent->HasTag(SniperFloorActorTag);
}

void UBallisticBulletManagerComponent::HandleBulletHit(FBallisticBullet& Bullet, const FHitResult& Hit, UWorld* World)
{
	Bullet.Position = Hit.WorldHitLocation;
	Bullet.bIsAlive = false;

	SpawnImpactVisual(Hit.WorldHitLocation);
	SpawnBulletImpactDecal(Bullet, Hit, World);

	if (World && bDrawDebugImpactMarker)
	{
		DrawDebugSphere(
			World,
			Hit.WorldHitLocation,
			SniperDebugHitMarkerRadius,
			SniperDebugMarkerSegments,
			FColor(255, 255, 0),
			SniperDebugTrailDuration);
	}

	if (IsFloorHit(Hit))
	{
		FBulletCinematicSnapshot Snapshot = BuildBulletSnapshot(Bullet);
		Snapshot.Position = Hit.WorldHitLocation;
		Snapshot.PreviousPosition = Hit.WorldHitLocation -
			(Bullet.Velocity.IsNearlyZero() ? FVector::ForwardVector : Bullet.Velocity.Normalized()) * 0.1f;
		Snapshot.bIsAlive = false;
		ASniperKillCamDirector::NotifyBulletFloorHit(Snapshot);
		UE_LOG(
			"[SniperDebug] Bullet floor hit: Actor=%s Component=%s BulletId=%d",
			Hit.HitActor ? Hit.HitActor->GetName().c_str() : "None",
			Hit.HitComponent ? Hit.HitComponent->GetName().c_str() : "None",
			Bullet.BulletId);
		return;
	}

	FSniperHitInfo HitInfo = BuildSniperHitInfo(Bullet, Hit);
	UE_LOG(
		"[SniperDebug] Bullet hit: Actor=%s Component=%s RawBone=%s ResolvedBone=%s Region=%d Distance=%.2f Speed=%.2f BodyCenterDistance=%.3f HasBodyCenterDistance=%d",
		HitInfo.HitActor ? HitInfo.HitActor->GetName().c_str() : "None",
		Hit.HitComponent ? Hit.HitComponent->GetName().c_str() : "None",
		Hit.HitBoneName.ToString().c_str(),
		HitInfo.HitBoneName.ToString().c_str(),
		static_cast<int32>(HitInfo.HitRegion),
		HitInfo.TravelDistance,
		HitInfo.ImpactSpeed,
		HitInfo.HitBodyCenterDistance,
		HitInfo.bHasHitBodyCenterDistance ? 1 : 0);
	if (AActor* HitActor = HitInfo.HitActor)
	{
		float HealthBeforeHit = -1.0f;
		float HealthAfterHit = -1.0f;
		const FString RawHitBoneName = Hit.HitBoneName.ToString();
		const FString ResolvedHitBoneName = HitInfo.HitBoneName.ToString();
		const FString NormalizedHitBoneName = NormalizeBoneNameForHitClassification(HitInfo.HitBoneName);
		const bool bUsedFallbackBone =
			Hit.HitBoneName == FName::None || Hit.HitBoneName != HitInfo.HitBoneName;
		if (USniperDamageReceiverComponent* DamageReceiver = HitActor->GetComponentByClass<USniperDamageReceiverComponent>())
		{
			HealthBeforeHit = DamageReceiver->GetCurrentHP();
			HitInfo = DamageReceiver->ResolveSniperHit(HitInfo);
			DamageReceiver->ApplyResolvedSniperHit(HitInfo);
			HealthAfterHit = DamageReceiver->GetCurrentHP();
		}

		if (ACombatCharacter* CombatCharacter = Cast<ACombatCharacter>(HitActor))
		{
			UCombatCoverAgentComponent* CombatAgent = CombatCharacter->GetCombatCoverAgentComponent();
			const float FallbackCurrentHealth = CombatAgent ? CombatAgent->GetHealth() : -1.0f;
			if (HealthBeforeHit < 0.0f)
			{
				HealthBeforeHit = FallbackCurrentHealth;
			}
			if (HealthAfterHit < 0.0f)
			{
				HealthAfterHit = FallbackCurrentHealth;
			}

			UE_LOG(
				"[SniperDebug] Bullet hit CombatCharacter: Actor=%s Team=%s HealthBefore=%.1f HealthAfter=%.1f Damage=%.1f RegionMultiplier=%.2f Outcome=%d Region=%d Killed=%d Headshot=%d RawHitBone=%s HitBone=%s NormalizedHitBone=%s UsedFallbackBone=%d",
				CombatCharacter->GetName().c_str(),
				CombatAgent ? CombatAgent->GetTeamTag().c_str() : "Unknown",
				HealthBeforeHit,
				HealthAfterHit,
				HitInfo.Damage,
				HitInfo.RegionDamageMultiplier,
				static_cast<int32>(HitInfo.HitOutcome),
				static_cast<int32>(HitInfo.HitRegion),
				HealthBeforeHit > 0.0f && HealthAfterHit <= 0.0f ? 1 : 0,
				HitInfo.bIsHeadshot ? 1 : 0,
				RawHitBoneName.c_str(),
				ResolvedHitBoneName.c_str(),
				NormalizedHitBoneName.c_str(),
				bUsedFallbackBone ? 1 : 0);
		}
	}

	if (USniperWeaponComponent* SniperWeapon = WeaponComponent.Get())
	{
		SniperWeapon->NotifySniperHit(HitInfo);
	}

	if (ShouldNotifyKillCamForHit(HitInfo))
	{
		ASniperKillCamDirector::NotifyBulletHit(HitInfo);
	}
	else
	{
		UE_LOG(
			"[SniperDebug] KillCam skipped by precision filter: Actor=%s Bone=%s Distance=%.3f Max=%.3f HasDistance=%d",
			HitInfo.HitActor ? HitInfo.HitActor->GetName().c_str() : "None",
			HitInfo.HitBoneName.ToString().c_str(),
			HitInfo.HitBodyCenterDistance,
			MaxKillCamBodyCenterDistance,
			HitInfo.bHasHitBodyCenterDistance ? 1 : 0);
	}
}

FSniperHitInfo UBallisticBulletManagerComponent::BuildSniperHitInfo(const FBallisticBullet& Bullet, const FHitResult& Hit) const
{
	FSniperHitInfo HitInfo;
	const FName ResolvedHitBoneName = ResolvePreciseHitBoneName(Hit);
	const ESniperHitRegion HitRegion = ClassifyHitRegion(ResolvedHitBoneName);
	HitInfo.BulletId = Bullet.BulletId;
	HitInfo.HitActor = Hit.HitActor;
	HitInfo.HitLocation = Hit.WorldHitLocation;
	HitInfo.HitNormal = !Hit.ImpactNormal.IsNearlyZero() ? Hit.ImpactNormal : Hit.WorldNormal;
	HitInfo.ShotDirection = Bullet.Velocity.IsNearlyZero() ? FVector::ZeroVector : Bullet.Velocity.Normalized();
	HitInfo.Damage = Bullet.Damage;
	const float HitSegmentDistance = (Hit.WorldHitLocation - Bullet.PreviousPosition).Length();
	HitInfo.TravelDistance = Bullet.TraveledDistance + HitSegmentDistance;
	HitInfo.ImpactSpeed = Bullet.Velocity.Length();
	HitInfo.RagdollImpulseStrength = Bullet.Damage + HitInfo.ImpactSpeed * 0.1f;
	HitInfo.AmmoType = Bullet.AmmoType;
	HitInfo.HitOutcome = ESniperHitOutcome::Normal;
	HitInfo.HitRegion = HitRegion;
	HitInfo.bIsScopedShot = Bullet.bWasScopedShot;
	HitInfo.bIsHeadshot = HitRegion == ESniperHitRegion::Head;
	HitInfo.bIsArmorPiercing = Bullet.bCanDamageArmor;
	HitInfo.bShouldRagdoll = HitInfo.ImpactSpeed >= SniperRagdollImpactSpeedThreshold;
	HitInfo.bKilled = false;
	HitInfo.bFriendlyTarget = false;
	HitInfo.Shooter = Bullet.Owner;
	HitInfo.RegionDamageMultiplier = 1.0f;
	HitInfo.TargetCurrentHP = 0.0f;
	HitInfo.TargetMaxHP = 0.0f;
	HitInfo.HitBoneName = ResolvedHitBoneName;
	HitInfo.HitBodyName = ResolvedHitBoneName.IsValid() && ResolvedHitBoneName != FName::None
		? ResolvedHitBoneName.ToString()
		: FString();
	HitInfo.HitRegionName = GetSniperHitRegionName(HitRegion);
	HitInfo.HitRegionDisplayName = GetSniperHitRegionDisplayName(HitRegion);
	HitInfo.HitScoreMultiplier = GetDefaultSniperHitScoreMultiplier(HitRegion);
	HitInfo.HitScoreValue = GetDefaultSniperHitScoreValue(HitRegion);
	FVector HitBodyCenter = FVector::ZeroVector;
	float HitBodyCenterDistance = 0.0f;
	if (ResolveHitBodyCenterMetrics(Hit, ResolvedHitBoneName, HitBodyCenter, HitBodyCenterDistance))
	{
		HitInfo.bHasHitBodyCenterDistance = true;
		HitInfo.HitBodyCenterLocation = HitBodyCenter;
		HitInfo.HitBodyCenterDistance = HitBodyCenterDistance;
	}
	return HitInfo;
}

USkeletalMeshComponent* UBallisticBulletManagerComponent::ResolveHitSkeletalMeshComponent(const FHitResult& Hit) const
{
	if (USkeletalMeshComponent* HitSkeletalMesh = Cast<USkeletalMeshComponent>(Hit.HitComponent))
	{
		return HitSkeletalMesh;
	}

	if (AActor* HitActor = Hit.HitActor)
	{
		return HitActor->GetComponentByClass<USkeletalMeshComponent>();
	}

	return nullptr;
}

bool UBallisticBulletManagerComponent::ResolveHitBodyCenterMetrics(
	const FHitResult& Hit,
	const FName& HitBoneName,
	FVector& OutBodyCenter,
	float& OutDistance) const
{
	OutBodyCenter = FVector::ZeroVector;
	OutDistance = 0.0f;
	if (!Hit.bHit || !HitBoneName.IsValid() || HitBoneName == FName::None)
	{
		return false;
	}

	USkeletalMeshComponent* SkeletalMeshComponent = ResolveHitSkeletalMeshComponent(Hit);
	if (!SkeletalMeshComponent)
	{
		return false;
	}

	UPhysicsAsset* PhysicsAsset = SkeletalMeshComponent->GetEffectivePhysicsAsset();
	if (!PhysicsAsset)
	{
		return false;
	}

	const int32 BodyIndex = PhysicsAsset->FindBodySetupIndexByBoneName(HitBoneName);
	if (BodyIndex < 0)
	{
		return false;
	}

	FPhysicsAssetPreviewPoseCache PoseCache;
	if (!PoseCache.Initialize(SkeletalMeshComponent, PhysicsAsset))
	{
		return false;
	}

	FTransform BodyWorld;
	if (!PoseCache.ComputeBodyWorldTransform(BodyIndex, BodyWorld))
	{
		return false;
	}

	OutBodyCenter = BodyWorld.Location;
	OutDistance = FVector::Distance(Hit.WorldHitLocation, OutBodyCenter);
	return true;
}

bool UBallisticBulletManagerComponent::ShouldNotifyKillCamForHit(const FSniperHitInfo& HitInfo) const
{
	if (!HitInfo.HitActor)
	{
		return false;
	}

	const bool bCharacterLikeHit =
		Cast<ACombatCharacter>(HitInfo.HitActor) ||
		HitInfo.HitActor->GetComponentByClass<USniperDamageReceiverComponent>() != nullptr;
	if (!bCharacterLikeHit)
	{
		return false;
	}

	if (!bEnableKillCamBodyCenterDistanceFilter)
	{
		return true;
	}

	const FString NormalizedHitBoneName = NormalizeBoneNameForHitClassification(HitInfo.HitBoneName);
	const bool bKillCamEligibleBone =
		HasNormalizedBoneToken(NormalizedHitBoneName, "head") ||
		HasNormalizedBoneToken(NormalizedHitBoneName, "spine") ||
		HasNormalizedBoneToken(NormalizedHitBoneName, "pelvis");
	if (!bKillCamEligibleBone)
	{
		return false;
	}

	return HitInfo.bHasHitBodyCenterDistance &&
		HitInfo.HitBodyCenterDistance <= (std::max)(MaxKillCamBodyCenterDistance, 0.0f);
}

FName UBallisticBulletManagerComponent::ResolvePreciseHitBoneName(const FHitResult& Hit, bool* bOutUsedFallback) const
{
	if (bOutUsedFallback)
	{
		*bOutUsedFallback = false;
	}

	if (Hit.HitBoneName.IsValid() && Hit.HitBoneName != FName::None)
	{
		const FString NormalizedRawBoneName = NormalizeBoneNameForHitClassification(Hit.HitBoneName);
		if (!NormalizedRawBoneName.empty() && !IsAuxiliaryBoneNameNormalized(NormalizedRawBoneName))
		{
			return Hit.HitBoneName;
		}
	}

	USkeletalMeshComponent* SkeletalMeshComponent = ResolveHitSkeletalMeshComponent(Hit);
	if (!SkeletalMeshComponent)
	{
		return FName::None;
	}

	if (FPhysicsAssetInstance* PhysicsAssetInstance = SkeletalMeshComponent->GetPhysicsAssetInstance())
	{
		FName NearestBodyBoneName = FName::None;
		FVector NearestBodyLocation = FVector::ZeroVector;
		if (PhysicsAssetInstance->FindNearestBodyToWorldLocation(
				Hit.WorldHitLocation,
				NearestBodyBoneName,
				NearestBodyLocation))
		{
			const FString NormalizedNearestBodyBoneName = NormalizeBoneNameForHitClassification(NearestBodyBoneName);
			if (!NormalizedNearestBodyBoneName.empty() && !IsAuxiliaryBoneNameNormalized(NormalizedNearestBodyBoneName))
			{
				if (bOutUsedFallback)
				{
					*bOutUsedFallback = true;
				}
				return NearestBodyBoneName;
			}
		}
	}

	USkeletalMesh* SkeletalMesh = SkeletalMeshComponent->GetSkeletalMesh();
	FSkeletalMesh* MeshAsset = SkeletalMesh ? SkeletalMesh->GetSkeletalMeshAsset() : nullptr;
	if (!MeshAsset || MeshAsset->Bones.empty())
	{
		return FName::None;
	}

	float BestDistanceSquared = 0.0f;
	int32 BestBoneIndex = -1;
	for (int32 BoneIndex = 0; BoneIndex < static_cast<int32>(MeshAsset->Bones.size()); ++BoneIndex)
	{
		const FString& BoneName = MeshAsset->Bones[BoneIndex].Name;
		if (BoneName.empty())
		{
			continue;
		}

		const FString NormalizedBoneName = NormalizeBoneNameForHitClassification(FName(BoneName));
		if (NormalizedBoneName.empty() || IsAuxiliaryBoneNameNormalized(NormalizedBoneName))
		{
			continue;
		}

		const FVector BoneLocation = SkeletalMeshComponent->GetBoneLocationByIndex(BoneIndex);
		const float DistanceSquared = FVector::DistSquared(Hit.WorldHitLocation, BoneLocation);
		if (BestBoneIndex < 0 || DistanceSquared < BestDistanceSquared)
		{
			BestDistanceSquared = DistanceSquared;
			BestBoneIndex = BoneIndex;
		}
	}

	if (BestBoneIndex < 0 || BestBoneIndex >= static_cast<int32>(MeshAsset->Bones.size()))
	{
		return FName::None;
	}

	if (bOutUsedFallback)
	{
		*bOutUsedFallback = true;
	}
	return FName(MeshAsset->Bones[BestBoneIndex].Name);
}

FString UBallisticBulletManagerComponent::NormalizeBoneNameForHitClassification(const FName& BoneName) const
{
	if (!BoneName.IsValid() || BoneName == FName::None)
	{
		return FString();
	}

	const FString RawBoneName = BoneName.ToString();
	FString NormalizedBoneName;
	NormalizedBoneName.reserve(RawBoneName.size());

	bool bLastCharacterWasSeparator = false;
	for (char Character : RawBoneName)
	{
		const unsigned char UnsignedCharacter = static_cast<unsigned char>(Character);
		if (std::isalnum(UnsignedCharacter) != 0)
		{
			NormalizedBoneName.push_back(static_cast<char>(std::tolower(UnsignedCharacter)));
			bLastCharacterWasSeparator = false;
		}
		else if (!bLastCharacterWasSeparator && !NormalizedBoneName.empty())
		{
			NormalizedBoneName.push_back('_');
			bLastCharacterWasSeparator = true;
		}
	}

	while (!NormalizedBoneName.empty() && NormalizedBoneName.back() == '_')
	{
		NormalizedBoneName.pop_back();
	}

	return NormalizedBoneName;
}

bool UBallisticBulletManagerComponent::IsAuxiliaryBoneNameNormalized(const FString& BoneName) const
{
	return HasNormalizedBoneToken(BoneName, "ik") ||
		HasNormalizedBoneToken(BoneName, "weapon") ||
		HasNormalizedBoneToken(BoneName, "camera") ||
		HasNormalizedBoneToken(BoneName, "twist") ||
		HasNormalizedBoneToken(BoneName, "socket") ||
		HasNormalizedBoneToken(BoneName, "ctrl") ||
		HasNormalizedBoneToken(BoneName, "control") ||
		HasNormalizedBoneToken(BoneName, "target") ||
		HasNormalizedBoneToken(BoneName, "pole") ||
		HasNormalizedBoneToken(BoneName, "end") ||
		HasNormalizedBoneToken(BoneName, "nub") ||
		HasNormalizedBoneToken(BoneName, "offset") ||
		HasNormalizedBoneToken(BoneName, "attach") ||
		HasNormalizedBoneToken(BoneName, "helper");
}

ESniperHitRegion UBallisticBulletManagerComponent::ClassifyHitRegionNormalized(const FString& BoneName) const
{
	if (BoneName.empty())
	{
		return ESniperHitRegion::Unknown;
	}

	if (IsHeadshotBoneNameNormalized(BoneName))
	{
		return ESniperHitRegion::Head;
	}

	if (HasNormalizedBoneToken(BoneName, "spine") ||
		HasNormalizedBoneToken(BoneName, "pelvis") ||
		HasNormalizedBoneToken(BoneName, "hips") ||
		HasNormalizedBoneToken(BoneName, "hip") ||
		HasNormalizedBoneToken(BoneName, "chest") ||
		HasNormalizedBoneToken(BoneName, "upperchest") ||
		HasNormalizedBoneToken(BoneName, "torso") ||
		HasNormalizedBoneToken(BoneName, "rib") ||
		HasNormalizedBoneToken(BoneName, "clavicle") ||
		HasNormalizedBoneToken(BoneName, "neck"))
	{
		return ESniperHitRegion::Torso;
	}

	if (HasNormalizedBoneToken(BoneName, "arm") ||
		HasNormalizedBoneToken(BoneName, "shoulder") ||
		HasNormalizedBoneToken(BoneName, "elbow") ||
		HasNormalizedBoneToken(BoneName, "forearm") ||
		HasNormalizedBoneToken(BoneName, "hand") ||
		HasNormalizedBoneToken(BoneName, "wrist"))
	{
		return ESniperHitRegion::Arm;
	}

	if (HasNormalizedBoneToken(BoneName, "leg") ||
		HasNormalizedBoneToken(BoneName, "thigh") ||
		HasNormalizedBoneToken(BoneName, "calf") ||
		HasNormalizedBoneToken(BoneName, "knee") ||
		HasNormalizedBoneToken(BoneName, "foot") ||
		HasNormalizedBoneToken(BoneName, "ankle") ||
		HasNormalizedBoneToken(BoneName, "toe"))
	{
		return ESniperHitRegion::Leg;
	}

	return ESniperHitRegion::Unknown;
}

ESniperHitRegion UBallisticBulletManagerComponent::ClassifyHitRegion(const FName& BoneName) const
{
	return ClassifyHitRegionNormalized(NormalizeBoneNameForHitClassification(BoneName));
}

bool UBallisticBulletManagerComponent::IsHeadshotBoneNameNormalized(const FString& BoneName) const
{
	return HasNormalizedBoneToken(BoneName, "head") ||
		HasNormalizedBoneToken(BoneName, "skull") ||
		HasNormalizedBoneToken(BoneName, "face") ||
		HasNormalizedBoneToken(BoneName, "jaw") ||
		HasNormalizedBoneToken(BoneName, "eye");
}

bool UBallisticBulletManagerComponent::IsHeadshotBoneName(const FName& BoneName) const
{
	return IsHeadshotBoneNameNormalized(NormalizeBoneNameForHitClassification(BoneName));
}

FBulletCinematicSnapshot UBallisticBulletManagerComponent::BuildBulletSnapshot(const FBallisticBullet& Bullet) const
{
	FBulletCinematicSnapshot Snapshot;
	Snapshot.BulletId = Bullet.BulletId;
	Snapshot.Position = Bullet.Position;
	Snapshot.PreviousPosition = Bullet.PreviousPosition;
	Snapshot.Velocity = Bullet.Velocity;
	Snapshot.TraveledDistance = Bullet.TraveledDistance;
	Snapshot.LifeTime = Bullet.LifeTime;
	Snapshot.AmmoType = Bullet.AmmoType;
	Snapshot.Owner = IsValid(Bullet.Owner) ? Bullet.Owner : nullptr;
	Snapshot.bIsAlive = Bullet.bIsAlive;
	Snapshot.bWasScopedShot = Bullet.bWasScopedShot;
	return Snapshot;
}

void UBallisticBulletManagerComponent::CompactDeadBullets()
{
	ActiveBullets.erase(
		std::remove_if(
			ActiveBullets.begin(),
			ActiveBullets.end(),
			[](const FBallisticBullet& Bullet)
			{
				return !Bullet.bIsAlive;
			}),
		ActiveBullets.end());
}

void UBallisticBulletManagerComponent::ResolveWeaponComponent()
{
	if (USniperWeaponComponent* Existing = WeaponComponent.Get())
	{
		if (Existing->GetOwner() == GetOwner())
		{
			return;
		}
	}

	WeaponComponent = GetOwner() ? GetOwner()->GetComponentByClass<USniperWeaponComponent>() : nullptr;
}
