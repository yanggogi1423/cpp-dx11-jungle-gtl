#pragma once
#include "Core/CoreTypes.h"
#include "Core/RayTypes.h"
#include "Render/Pipeline/IPrimitiveSpatialQuery.h"
#include "Render/Pipeline/ViewContext.h"
#include <cfloat>

class FPrimitiveProxy;
class AActor;
class FViewport;

struct FWorldProxyCullingStats
{
	int32 RegisteredProxyCount = 0;
	int32 InsertedProxyCount = 0;
	int32 CandidateProxyCount = 0;
	int32 RenderedProxyCount = 0;

	int32 SpatialTotalNodes = 0;
	int32 SpatialTotalItems = 0;
	int32 SpatialOutsideItems = 0;
	int32 SpatialFrustumIntersectedNodes = 0;
	int32 SpatialFrustumCandidateItems = 0;
};

struct FRayBroadDebugCounters
{
	uint64 AABBTests = 0;
	uint64 AABBHits = 0;
	uint64 CandidatesEmitted = 0;
	uint64 CandidatesAfterFilter = 0;
	uint64 NodeVisits = 0;
	uint64 LinearAABBTests = 0;
	uint64 BVHAABBTests = 0;
};

struct FRayPickableSoA
{
	TArray<float> MinX, MinY, MinZ;
	TArray<float> MaxX, MaxY, MaxZ;
	TArray<FPrimitiveProxy*> Proxies;

	void Clear()
	{
		MinX.clear(); MinY.clear(); MinZ.clear();
		MaxX.clear(); MaxY.clear(); MaxZ.clear();
		Proxies.clear();
	}

	void Reserve(size_t Capacity)
	{
		MinX.reserve(Capacity); MinY.reserve(Capacity); MinZ.reserve(Capacity);
		MaxX.reserve(Capacity); MaxY.reserve(Capacity); MaxZ.reserve(Capacity);
		Proxies.reserve(Capacity);
	}

	void Add(const FBoundingBox& Box, FPrimitiveProxy* Proxy)
	{
		MinX.push_back(Box.Min.X); MinY.push_back(Box.Min.Y); MinZ.push_back(Box.Min.Z);
		MaxX.push_back(Box.Max.X); MaxY.push_back(Box.Max.Y); MaxZ.push_back(Box.Max.Z);
		Proxies.push_back(Proxy);
	}

	size_t Size() const { return Proxies.size(); }
};

class FWorldRenderProxy
{
public:
	FWorldRenderProxy();
	~FWorldRenderProxy();

	void AddProxy(FPrimitiveProxy* Proxy);
	void RemoveProxy(FPrimitiveProxy* Proxy);
	void MarkSpatialIndexDirty();
	void BeginDeferSpatialIndexInvalidation();
	void EndDeferSpatialIndexInvalidation();
	void WarmupSpatialIndices();

	// Phase 1: 씬으로부터 잠재적 후보군 수집
	void GatherCandidates(FViewContext& context);

	// Phase 2: 최종 생존한 후보들에 대해 렌더 커맨드 생성
	void SubmitRenderCommands(FViewContext& context, const TArray<AActor*>& SelectedActors);
	void InjectAlwaysVisibleCandidates(FViewContext& context, const TArray<AActor*>& SelectedActors, bool bIncludeGizmo);

	// 레거시 지원 (필요시)
	void CollectWorld(FViewContext& context, const TArray<AActor*>& SelectedActors);
	void QueryByRay(const FRay& Ray, TArray<FPrimitiveProxy*>& OutCandidates);
	void QueryByRayWithNearT(const FRay& Ray, TArray<FRayQueryCandidate>& OutCandidates, float MaxNearT = FLT_MAX);
	bool QueryClosestByRayWithNearT(const FRay& Ray, FRayQueryCandidate& OutCandidate, float MaxNearT = FLT_MAX);
	void PrepareRayPickingCachesForQuery();
	bool IsSpatialIndexDirtyForQueries() const { return bSpatialIndexDirty || bDeferredSpatialIndexDirtyPending || (SpatialIndexDeferDepth > 0); }
	uint64 GetSpatialChangeSerial() const { return SpatialChangeSerial; }

	const FWorldProxyCullingStats& GetLastCullingStats() const { return LastCullingStats; }
	const FRayBroadDebugCounters& GetLastRayBroadDebugCounters() const { return LastRayBroadDebugCounters; }

private:
	void RebuildSpatialIndexIfDirty(bool bTrackInsertedStats, bool bPrewarmStaticMeshBVH);
	void RebuildVisibleRaySpatialIndexIfNeeded(bool bUseOcclusionGate);
	void BuildFrustumVisiblePickCacheIfNeeded();

	TArray<FPrimitiveProxy*> Proxies;
	IPrimitiveSpatialQuery* FrustumSpatialIndex = nullptr;
	IPrimitiveSpatialQuery* RaySpatialIndex = nullptr;
	IPrimitiveSpatialQuery* VisibleRaySpatialIndex = nullptr;
	uint32 FrustumVisiblePickFrameTag = 0;
	TArray<FPrimitiveProxy*> FrustumVisiblePickableCache;
	FRayPickableSoA FrustumVisiblePickableSoA;
	FRayPickableSoA RayPickableSoA;
	const FViewport* CachedOcclusionViewport = nullptr;
	bool bFrustumVisiblePickCacheDirty = true;
	bool bRayFrustumGateOptimizationEnabled = true;
	bool bSpatialIndexDirty = true;
	bool bVisibleRaySpatialIndexDirty = true;
	bool bVisibleRaySpatialIndexBuiltWithOcclusion = false;
	uint32 VisibleRaySpatialIndexFrameTag = 0u;
	int32 SpatialIndexDeferDepth = 0;
	bool bDeferredSpatialIndexDirtyPending = false;
	uint64 SpatialChangeSerial = 1u;
	FWorldProxyCullingStats LastCullingStats;
	FRayBroadDebugCounters LastRayBroadDebugCounters;
};
