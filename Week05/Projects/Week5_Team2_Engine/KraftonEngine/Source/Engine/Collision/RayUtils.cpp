#include "Collision/RayUtils.h"
#include "Collision/PickingTuning.h"
#include "Collision/StaticMeshBVH.h"
#include "Component/PrimitiveComponent.h"

#include <cmath>
#include <cfloat>
#include <algorithm>
#include <cstdint>

bool FRayUtils::CheckRayAABB(const FRay& Ray, const FVector& AABBMin, const FVector& AABBMax)
{
	float NearT = 0.0f;
	return CheckRayAABBNearT(Ray, AABBMin, AABBMax, NearT);
}

bool FRayUtils::CheckRayAABBNearT(const FRay& Ray, const FVector& AABBMin, const FVector& AABBMax, float& OutNearT)
{
	const FRayAABBKernel Kernel = FRayAABBKernel::Build(Ray);
	float FarT = 0.0f;
	return IntersectRayAABBNearT(Ray, Kernel, FBoundingBox(AABBMin, AABBMax), OutNearT, FarT);
}

bool FRayUtils::IntersectTriangle(const FVector& RayOrigin, const FVector& RayDir,
	const FVector& V0, const FVector& V1, const FVector& V2, float& OutT)
{
	const FVector Edge1 = V1 - V0;
	const FVector Edge2 = V2 - V0;
	const FVector PVec = RayDir.Cross(Edge2);
	const float Det = Edge1.Dot(PVec);
	const float DetEps = FPickingTuning::TriangleDetEpsilon();
	if (FPickingTuning::UseBackFaceCull())
	{
		if (Det <= DetEps) return false;
	}
	else
	{
		if (std::abs(Det) <= DetEps) return false;
	}

	const float InvDet = 1.0f / Det;
	const FVector TVec = RayOrigin - V0;
	const float U = TVec.Dot(PVec) * InvDet;
	if (U < 0.0f || U > 1.0f) return false;

	const FVector QVec = TVec.Cross(Edge1);
	const float V = RayDir.Dot(QVec) * InvDet;
	if (V < 0.0f || U + V > 1.0f) return false;

	OutT = Edge2.Dot(QVec) * InvDet;
	return OutT > 0.0f;
}

bool FRayUtils::RaycastTriangles(
	const FRay& WorldRay,
	const FMatrix& WorldMatrix,
	const void* PositionData,
	uint32 PositionStride,
	const TArray<uint32>& Indices,
	FHitResult& OutHitResult,
	float InClosestT)
{
	if (!PositionData || Indices.empty()) return false;

	const FMatrix InvWorld = WorldMatrix.GetInverse();
	const FVector LocalOrigin = InvWorld.TransformPositionWithW(WorldRay.Origin);
	FVector LocalDir = InvWorld.TransformVector(WorldRay.Direction);
	const float LocalDirLen = LocalDir.Length();
	if (LocalDirLen <= 1e-8f)
	{
		OutHitResult = {};
		return false;
	}
	LocalDir.Normalize();

	const float LocalToWorldScale = WorldMatrix.TransformVector(LocalDir).Length();
	const float LocalClosestLimit = (InClosestT == FLT_MAX || LocalToWorldScale <= 1e-8f)
		? FLT_MAX
		: (InClosestT / LocalToWorldScale);
	float ClosestT = LocalClosestLimit;
	int HitFaceIndex = -1;
	bool bHit = false;

	const uint8* BasePtr = static_cast<const uint8*>(PositionData);
	for (size_t i = 0; i + 2 < Indices.size(); i += 3)
	{
		const FVector& V0 = *reinterpret_cast<const FVector*>(BasePtr + Indices[i] * PositionStride);
		const FVector& V1 = *reinterpret_cast<const FVector*>(BasePtr + Indices[i + 1] * PositionStride);
		const FVector& V2 = *reinterpret_cast<const FVector*>(BasePtr + Indices[i + 2] * PositionStride);

		float T = 0.0f;
		if (IntersectTriangle(LocalOrigin, LocalDir, V0, V1, V2, T) && T > 0.0f && T < ClosestT)
		{
			ClosestT = T;
			HitFaceIndex = static_cast<int>(i);
			bHit = true;
		}
	}

	OutHitResult = {};
	OutHitResult.bHit = bHit;
	if (bHit)
	{
		OutHitResult.FaceIndex = HitFaceIndex;
		const FVector LocalHitPoint = LocalOrigin + LocalDir * ClosestT;
		const FVector WorldHitPoint = WorldMatrix.TransformPositionWithW(LocalHitPoint);
		OutHitResult.Distance = FVector::Distance(WorldRay.Origin, WorldHitPoint);
	}

	return bHit;
}

bool FRayUtils::RaycastComponent(UPrimitiveComponent* Component, const FRay& Ray, FHitResult& OutHitResult)
{
	if (!Component || !Component->IsVisible())
	{
		return false;
	}

	Component->UpdateWorldAABB();
	const FBoundingBox AABB = Component->GetWorldBoundingBox();
	if (!CheckRayAABB(Ray, AABB.Min, AABB.Max))
	{
		return false;
	}

	return Component->LineTraceComponent(Ray, OutHitResult);
}

bool FRayUtils::RaycastTrianglesBVH(
	const FRay& WorldRay,
	const FMatrix& WorldMatrix,
	const void* PositionData,
	uint32 PositionStride,
	const FStaticMeshBVH& BVH,
	FHitResult& OutHitResult,
	float InClosestT)
{
	if (!PositionData || !BVH.IsBuilt())
	{
		return false;
	}

	const FMatrix InvWorld = WorldMatrix.GetInverse();
	const FVector LocalOrigin = InvWorld.TransformPositionWithW(WorldRay.Origin);
	FVector LocalDir = InvWorld.TransformVector(WorldRay.Direction);
	const float LocalDirLen = LocalDir.Length();
	if (LocalDirLen <= 1e-8f)
	{
		OutHitResult = {};
		return false;
	}
	LocalDir.Normalize();

	const float LocalToWorldScale = WorldMatrix.TransformVector(LocalDir).Length();
	if (LocalToWorldScale <= 1e-8f)
	{
		OutHitResult = {};
		return false;
	}

	FRay LocalRay;
	LocalRay.Origin = LocalOrigin;
	LocalRay.Direction = LocalDir;

	const float LocalClosestT = (InClosestT == FLT_MAX) ? FLT_MAX : (InClosestT / LocalToWorldScale);

	FHitResult LocalHit;
	if (!BVH.IntersectLocalRay(LocalRay, PositionData, PositionStride, LocalHit, LocalClosestT))
	{
		return false;
	}

	OutHitResult = LocalHit;
	const FVector LocalHitPoint = LocalOrigin + LocalDir * LocalHit.Distance;
	const FVector WorldHitPoint = WorldMatrix.TransformPositionWithW(LocalHitPoint);
	OutHitResult.WorldHitLocation = WorldHitPoint;
	OutHitResult.Distance = FVector::Distance(WorldRay.Origin, WorldHitPoint);
	OutHitResult.bHit = true;
	return true;
}
