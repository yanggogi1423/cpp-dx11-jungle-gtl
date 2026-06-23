#pragma once

#include "Core/CoreTypes.h"
#include "Core/EngineTypes.h"
#include "Core/RayTypes.h"
#include <cfloat>

class FPrimitiveProxy;
struct FFrustumPlanes;

struct FRayQueryCandidate
{
	FPrimitiveProxy* Proxy = nullptr;
	float NearT = 0.0f;
};

struct FSpatialQueryDebugStats
{
	int32 TotalNodes = 0;
	int32 TotalItems = 0;
	int32 OutsideItems = 0;
	int32 FrustumIntersectedNodes = 0;
	int32 FrustumCandidateItems = 0;
	int32 RayIntersectedNodes = 0;
	int32 RayCandidateItems = 0;
	int32 RayAABBTests = 0;
	int32 RayAABBHits = 0;
};

class IPrimitiveSpatialQuery
{
public:
	virtual ~IPrimitiveSpatialQuery() = default;

	virtual void Clear() = 0;
	virtual void Insert(FPrimitiveProxy* Proxy, const FBoundingBox& Bounds) = 0;
	virtual void Warmup() = 0;
	virtual void QueryFrustum(const FFrustumPlanes& Frustum, TArray<FPrimitiveProxy*>& OutProxies) const = 0;
	virtual void QueryRay(const FRay& Ray, TArray<FPrimitiveProxy*>& OutProxies) const = 0;
	virtual void QueryRayWithNearT(const FRay& Ray, TArray<FRayQueryCandidate>& OutCandidates, float MaxNearT = FLT_MAX) const = 0;
	virtual const FSpatialQueryDebugStats& GetLastDebugStats() const = 0;
};
