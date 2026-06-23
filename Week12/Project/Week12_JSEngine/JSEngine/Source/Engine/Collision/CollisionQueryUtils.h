#pragma once

#include "Core/CoreMinimal.h"
#include "Core/CollisionTypes.h"
#include "Geometry/OBB.h"
#include "Geometry/Ray.h"

struct FCollisionQueryUtils
{
	static bool RaycastOBB(const FOBB& Box, const FRay& Ray, bool bInitialOverlapAsHit,
		FHitResult& OutHitResult);

	static bool RaycastSphere(const FVector& Center, float Radius, const FRay& Ray,
		bool bInitialOverlapAsHit, FHitResult& OutHitResult);

	static bool RaycastCapsule(const FVector& A, const FVector& B, float Radius, const FRay& Ray,
		bool bInitialOverlapAsHit, FHitResult& OutHitResult);

	static bool IsPointInTriangle(const FVector& Point, const FVector& A, const FVector& B, const FVector& C);
	static FVector ClosestPointOnTriangle(const FVector& Point, const FVector& A, const FVector& B, const FVector& C);
	static FVector ChooseNormalOpposingDirection(const FVector& Normal, const FVector& Direction);

	static bool RaySphereSweep(const FVector& Start, const FVector& Direction, float SegmentLength,
		const FVector& Center, float Radius, float& OutDistance, FVector& OutNormal);

	static bool RayCapsuleEdgeSweep(const FVector& Start, const FVector& Direction, float SegmentLength,
		const FVector& A, const FVector& B, float Radius, float& OutDistance, FVector& OutNormal,
		FVector& OutClosestPoint);

	static bool IsBetterSweepHit(const FHitResult& Candidate, const FHitResult& CurrentBest,
		bool bHasBest, const FVector& Direction);
};
