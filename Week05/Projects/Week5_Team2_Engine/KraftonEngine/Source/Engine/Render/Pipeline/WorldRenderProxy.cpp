#include "WorldRenderProxy.h"
#include "PrimitiveProxy.h"
#include "Collision/PickingTuning.h"
#include "Render/Pipeline/FrustumCulling.h"
#include "Render/Pipeline/FixedWorldOctree.h"
#include "Render/Pipeline/WorldBVH.h"
#include "Render/Pipeline/IPrimitiveSpatialQuery.h"
#include "Render/Pipeline/OcclusionManager.h"
#include "GameFramework/AActor.h"
#include "Component/PrimitiveComponent.h"
#include "Component/StaticMeshComponent.h"
#include "Component/BillboardComponent.h"
#include "Component/GizmoComponent.h"
#include "Component/TextRenderComponent.h"
#include "Mesh/StaticMesh.h"
#include "Profiling/Stats.h"

#include <algorithm>
#include <cmath>

namespace
{
	inline void GatherRayCandidatesFromSoA(
		const FRay& Ray,
		const FRayAABBKernel& Kernel,
		const FRayPickableSoA& SoA,
		const FViewport* OcclusionViewport,
		bool bUseOcclusionGate,
		float MaxNearT,
		TArray<FRayQueryCandidate>& OutCandidates,
		uint64* OutAABBTests = nullptr,
		uint64* OutAABBHits = nullptr)
	{
		const size_t Count = SoA.Size();
		OutCandidates.reserve(OutCandidates.size() + Count);
		const bool bUsePacketSIMD = FPickingTuning::EnableRayAABBPacketSIMD() && (Count >= static_cast<size_t>(FPickingTuning::RayAABBPacketMinCount()));

		size_t i = 0;
		if (bUsePacketSIMD)
		{
			for (; i + 4 <= Count; i += 4)
			{
				float NearT4[4] = {};
				if (OutAABBTests) { *OutAABBTests += 4u; }
				const uint32 HitMask = IntersectRayAABBNearTMinMax4(
					Ray,
					Kernel,
					&SoA.MinX[i], &SoA.MinY[i], &SoA.MinZ[i],
					&SoA.MaxX[i], &SoA.MaxY[i], &SoA.MaxZ[i],
					MaxNearT,
					NearT4);

				for (uint32 Lane = 0; Lane < 4u; ++Lane)
				{
					if ((HitMask & (1u << Lane)) == 0u)
					{
						continue;
					}
					if (OutAABBHits) { ++(*OutAABBHits); }
					FPrimitiveProxy* Proxy = SoA.Proxies[i + Lane];
					if (!Proxy)
					{
						continue;
					}
					if (bUseOcclusionGate && OcclusionViewport && !FOcclusionManager::Get().IsVisible(OcclusionViewport, Proxy->CachedProxyId))
					{
						continue;
					}
					OutCandidates.push_back({ Proxy, NearT4[Lane] });
				}
			}
		}

		for (; i < Count; ++i)
		{
			float NearT = 0.0f;
			float FarT = 0.0f;
			if (OutAABBTests) { ++(*OutAABBTests); }
			if (!IntersectRayAABBNearTMinMax(
				Ray,
				Kernel,
				SoA.MinX[i], SoA.MinY[i], SoA.MinZ[i],
				SoA.MaxX[i], SoA.MaxY[i], SoA.MaxZ[i],
				NearT, FarT) || NearT > MaxNearT)
			{
				continue;
			}
			if (OutAABBHits) { ++(*OutAABBHits); }
			FPrimitiveProxy* Proxy = SoA.Proxies[i];
			if (!Proxy)
			{
				continue;
			}
			if (bUseOcclusionGate && OcclusionViewport && !FOcclusionManager::Get().IsVisible(OcclusionViewport, Proxy->CachedProxyId))
			{
				continue;
			}
			OutCandidates.push_back({ Proxy, NearT });
		}
	}
}

FWorldRenderProxy::FWorldRenderProxy()
{
	FrustumSpatialIndex = new FFixedWorldOctree();
	RaySpatialIndex = new FWorldBVH();
	VisibleRaySpatialIndex = new FWorldBVH();
}

FWorldRenderProxy::~FWorldRenderProxy()
{
	delete FrustumSpatialIndex;
	FrustumSpatialIndex = nullptr;
	delete RaySpatialIndex;
	RaySpatialIndex = nullptr;
	delete VisibleRaySpatialIndex;
	VisibleRaySpatialIndex = nullptr;
	Proxies.clear();
}

void FWorldRenderProxy::AddProxy(FPrimitiveProxy* Proxy)
{
	if (!this || !Proxy) return;

	auto it = std::find(Proxies.begin(), Proxies.end(), Proxy);
	if (it == Proxies.end())
	{
		Proxies.push_back(Proxy);
		Proxy->SetWorldRenderProxy(this);
		++SpatialChangeSerial;
		bSpatialIndexDirty = true;
		FrustumVisiblePickFrameTag = 0u;
		FrustumVisiblePickableCache.clear();
		FrustumVisiblePickableSoA.Clear();
		RayPickableSoA.Clear();
		bFrustumVisiblePickCacheDirty = true;
		bVisibleRaySpatialIndexDirty = true;
		bVisibleRaySpatialIndexBuiltWithOcclusion = false;
		VisibleRaySpatialIndexFrameTag = 0u;
	}
}

void FWorldRenderProxy::MarkSpatialIndexDirty()
{
	if (SpatialIndexDeferDepth > 0)
	{
		if (!bDeferredSpatialIndexDirtyPending)
		{
			++SpatialChangeSerial;
		}
		bDeferredSpatialIndexDirtyPending = true;
		FrustumVisiblePickFrameTag = 0u;
		FrustumVisiblePickableCache.clear();
		FrustumVisiblePickableSoA.Clear();
		bFrustumVisiblePickCacheDirty = true;
		bVisibleRaySpatialIndexDirty = true;
		bVisibleRaySpatialIndexBuiltWithOcclusion = false;
		VisibleRaySpatialIndexFrameTag = 0u;
		return;
	}

	if (!bSpatialIndexDirty)
	{
		++SpatialChangeSerial;
	}
	bSpatialIndexDirty = true;
	FrustumVisiblePickFrameTag = 0u;
	FrustumVisiblePickableCache.clear();
	FrustumVisiblePickableSoA.Clear();
	bFrustumVisiblePickCacheDirty = true;
	bVisibleRaySpatialIndexDirty = true;
	bVisibleRaySpatialIndexBuiltWithOcclusion = false;
	VisibleRaySpatialIndexFrameTag = 0u;
}

void FWorldRenderProxy::BeginDeferSpatialIndexInvalidation()
{
	++SpatialIndexDeferDepth;
}

void FWorldRenderProxy::EndDeferSpatialIndexInvalidation()
{
	if (SpatialIndexDeferDepth <= 0)
	{
		SpatialIndexDeferDepth = 0;
		return;
	}

	--SpatialIndexDeferDepth;
	if (SpatialIndexDeferDepth == 0 && bDeferredSpatialIndexDirtyPending)
	{
		bSpatialIndexDirty = true;
		bDeferredSpatialIndexDirtyPending = false;
		FrustumVisiblePickFrameTag = 0u;
		FrustumVisiblePickableCache.clear();
		FrustumVisiblePickableSoA.Clear();
		bFrustumVisiblePickCacheDirty = true;
		bVisibleRaySpatialIndexDirty = true;
		bVisibleRaySpatialIndexBuiltWithOcclusion = false;
		VisibleRaySpatialIndexFrameTag = 0u;
	}
}

void FWorldRenderProxy::WarmupSpatialIndices()
{
	RebuildSpatialIndexIfDirty(false, true);
	if (FrustumSpatialIndex)
	{
		FrustumSpatialIndex->Warmup();
	}
}

void FWorldRenderProxy::RemoveProxy(FPrimitiveProxy* Proxy)
{
	if (!this || !Proxy) return;

	auto it = std::find(Proxies.begin(), Proxies.end(), Proxy);
	if (it != Proxies.end())
	{
		Proxies.erase(it);
		Proxy->SetWorldRenderProxy(nullptr);
		++SpatialChangeSerial;
		bSpatialIndexDirty = true;
		FrustumVisiblePickFrameTag = 0u;
		FrustumVisiblePickableCache.clear();
		FrustumVisiblePickableSoA.Clear();
		RayPickableSoA.Clear();
		bFrustumVisiblePickCacheDirty = true;
		bVisibleRaySpatialIndexDirty = true;
		bVisibleRaySpatialIndexBuiltWithOcclusion = false;
		VisibleRaySpatialIndexFrameTag = 0u;
	}
}

void FWorldRenderProxy::GatherCandidates(FViewContext& Context)
{
	if (!this) return;
	if (!Context.GetShowFlags().bPrimitives) return;

	// 통계 초기화
	LastCullingStats = {};
	LastCullingStats.RegisteredProxyCount = static_cast<int32>(Proxies.size());
	CachedOcclusionViewport = Context.GetViewport();

	const FFrustumPlanes Frustum = FFrustumCulling::BuildFrustumPlanes(Context.GetView(), Context.GetProj());

	RebuildSpatialIndexIfDirty(true, true);
	if (FrustumSpatialIndex)
	{
		FrustumSpatialIndex->Warmup();
	}
	if (RaySpatialIndex)
	{
		RaySpatialIndex->Warmup();
	}

	TArray<FPrimitiveProxy*> LocalCandidates;
	if (FrustumSpatialIndex)
	{
		FrustumSpatialIndex->QueryFrustum(Frustum, LocalCandidates);
		const FSpatialQueryDebugStats& SpatialStats = FrustumSpatialIndex->GetLastDebugStats();
		LastCullingStats.SpatialTotalNodes = SpatialStats.TotalNodes;
		LastCullingStats.SpatialTotalItems = SpatialStats.TotalItems;
		LastCullingStats.SpatialOutsideItems = SpatialStats.OutsideItems;
		LastCullingStats.SpatialFrustumIntersectedNodes = SpatialStats.FrustumIntersectedNodes;
		LastCullingStats.SpatialFrustumCandidateItems = SpatialStats.FrustumCandidateItems;
	}

	++FrustumVisiblePickFrameTag;
	if (FrustumVisiblePickFrameTag == 0u)
	{
		FrustumVisiblePickFrameTag = 1u;
	}
	bFrustumVisiblePickCacheDirty = true;

	const bool bUseFrustumPickTagging = bRayFrustumGateOptimizationEnabled;
	for (FPrimitiveProxy* Proxy : LocalCandidates)
	{
		if (bUseFrustumPickTagging && Proxy)
		{
			if (UPrimitiveComponent* Owner = Proxy->GetOwner())
			{
				const bool bExcluded = Owner->IsA<UTextRenderComponent>() || Owner->IsA<UGizmoComponent>();
				if (!bExcluded && Owner->IsVisible())
				{
					if (AActor* ActorOwner = Owner->GetOwner())
					{
						if (ActorOwner->IsVisible() && ActorOwner->GetRootComponent())
						{
							Proxy->MarkFrustumVisibleForPick(FrustumVisiblePickFrameTag);
						}
					}
				}
			}
		}

		Context.AddCandidateProxy(Proxy);
	}

	LastCullingStats.CandidateProxyCount = static_cast<int32>(LocalCandidates.size());
	bVisibleRaySpatialIndexDirty = true;
}

void FWorldRenderProxy::BuildFrustumVisiblePickCacheIfNeeded()
{
	if (!bFrustumVisiblePickCacheDirty)
	{
		return;
	}

	FrustumVisiblePickableCache.clear();
	FrustumVisiblePickableSoA.Clear();

	if (FrustumVisiblePickFrameTag == 0u)
	{
		bFrustumVisiblePickCacheDirty = false;
		return;
	}

	const size_t Count = RayPickableSoA.Size();
	if (Count > 0)
	{
		FrustumVisiblePickableCache.reserve(Count);
		FrustumVisiblePickableSoA.Reserve(Count);
	}

	for (size_t i = 0; i < Count; ++i)
	{
		FPrimitiveProxy* Proxy = RayPickableSoA.Proxies[i];
		if (!Proxy || !Proxy->IsFrustumVisibleForPick(FrustumVisiblePickFrameTag))
		{
			continue;
		}

		FrustumVisiblePickableCache.push_back(Proxy);
		FrustumVisiblePickableSoA.MinX.push_back(RayPickableSoA.MinX[i]);
		FrustumVisiblePickableSoA.MinY.push_back(RayPickableSoA.MinY[i]);
		FrustumVisiblePickableSoA.MinZ.push_back(RayPickableSoA.MinZ[i]);
		FrustumVisiblePickableSoA.MaxX.push_back(RayPickableSoA.MaxX[i]);
		FrustumVisiblePickableSoA.MaxY.push_back(RayPickableSoA.MaxY[i]);
		FrustumVisiblePickableSoA.MaxZ.push_back(RayPickableSoA.MaxZ[i]);
		FrustumVisiblePickableSoA.Proxies.push_back(Proxy);
	}

	bFrustumVisiblePickCacheDirty = false;
}

void FWorldRenderProxy::QueryByRay(const FRay& Ray, TArray<FPrimitiveProxy*>& OutCandidates)
{
	thread_local TArray<FRayQueryCandidate> CandidatesWithNearT;
	CandidatesWithNearT.clear();
	QueryByRayWithNearT(Ray, CandidatesWithNearT, FLT_MAX);

	OutCandidates.clear();
	OutCandidates.reserve(CandidatesWithNearT.size());
	for (const FRayQueryCandidate& Candidate : CandidatesWithNearT)
	{
		OutCandidates.push_back(Candidate.Proxy);
	}
}

bool FWorldRenderProxy::QueryClosestByRayWithNearT(const FRay& Ray, FRayQueryCandidate& OutCandidate, float MaxNearT)
{
	OutCandidate = {};
	if (SpatialIndexDeferDepth > 0 || bDeferredSpatialIndexDirtyPending)
	{
		const FRayAABBKernel Kernel = FRayAABBKernel::Build(Ray);
		float BestNearT = MaxNearT;
		FPrimitiveProxy* BestProxy = nullptr;
		const size_t Count = RayPickableSoA.Size();
		for (size_t i = 0; i < Count; ++i)
		{
			float NearT = 0.0f;
			float FarT = 0.0f;
			if (!IntersectRayAABBNearTMinMax(
				Ray, Kernel,
				RayPickableSoA.MinX[i], RayPickableSoA.MinY[i], RayPickableSoA.MinZ[i],
				RayPickableSoA.MaxX[i], RayPickableSoA.MaxY[i], RayPickableSoA.MaxZ[i],
				NearT, FarT) || NearT > BestNearT)
			{
				continue;
			}
			BestNearT = NearT;
			BestProxy = RayPickableSoA.Proxies[i];
		}

		if (!BestProxy)
		{
			return false;
		}

		OutCandidate.Proxy = BestProxy;
		OutCandidate.NearT = BestNearT;
		return true;
	}

	if (bSpatialIndexDirty)
	{
		RebuildSpatialIndexIfDirty(false, false);
	}

	const bool bBypassFrustumGate = bSpatialIndexDirty || (SpatialIndexDeferDepth > 0) || bDeferredSpatialIndexDirtyPending;
	const bool bUseFrustumGate = bRayFrustumGateOptimizationEnabled && !bBypassFrustumGate && (FrustumVisiblePickFrameTag != 0u);
	const bool bUseOcclusionGate = bUseFrustumGate && FPickingTuning::UseRayOcclusionGate();
	if (bUseFrustumGate)
	{
		BuildFrustumVisiblePickCacheIfNeeded();
	}
	const size_t VisibleCount = FrustumVisiblePickableCache.size();

	auto PickNearestFromSoA = [&](const FRayPickableSoA& SoA) -> bool
	{
		const FRayAABBKernel Kernel = FRayAABBKernel::Build(Ray);
		float BestNearT = MaxNearT;
		FPrimitiveProxy* BestProxy = nullptr;
		for (size_t i = 0; i < SoA.Size(); ++i)
		{
			float NearT = 0.0f;
			float FarT = 0.0f;
			if (!IntersectRayAABBNearTMinMax(
				Ray, Kernel,
				SoA.MinX[i], SoA.MinY[i], SoA.MinZ[i],
				SoA.MaxX[i], SoA.MaxY[i], SoA.MaxZ[i],
				NearT, FarT) || NearT > BestNearT)
			{
				continue;
			}
			FPrimitiveProxy* Proxy = SoA.Proxies[i];
			if (!Proxy)
			{
				continue;
			}
			if (bUseOcclusionGate && CachedOcclusionViewport && !FOcclusionManager::Get().IsVisible(CachedOcclusionViewport, Proxy->CachedProxyId))
			{
				continue;
			}
			BestNearT = NearT;
			BestProxy = Proxy;
		}

		if (!BestProxy)
		{
			return false;
		}
		OutCandidate.Proxy = BestProxy;
		OutCandidate.NearT = BestNearT;
		return true;
	};

	if (bUseFrustumGate && VisibleCount > 0)
	{
		if (VisibleRaySpatialIndex && bVisibleRaySpatialIndexDirty)
		{
			RebuildVisibleRaySpatialIndexIfNeeded(bUseOcclusionGate);
		}

		const uint32 LinearThreshold = FPickingTuning::BroadLinearVisibleThreshold();
		if (VisibleCount <= LinearThreshold)
		{
			return PickNearestFromSoA(FrustumVisiblePickableSoA);
		}

		if (VisibleRaySpatialIndex &&
			!bVisibleRaySpatialIndexDirty &&
			(VisibleRaySpatialIndexFrameTag == FrustumVisiblePickFrameTag))
		{
			if (FWorldBVH* VisibleBVH = static_cast<FWorldBVH*>(VisibleRaySpatialIndex))
			{
				if (!VisibleBVH->QueryClosestRayCandidateWithNearT(Ray, OutCandidate, MaxNearT))
				{
					return false;
				}
				if (bUseOcclusionGate &&
					(!OutCandidate.Proxy || (CachedOcclusionViewport && !FOcclusionManager::Get().IsVisible(CachedOcclusionViewport, OutCandidate.Proxy->CachedProxyId))))
				{
					return false;
				}
				return true;
			}
		}
	}

	if (FWorldBVH* WorldBVH = static_cast<FWorldBVH*>(RaySpatialIndex))
	{
		return WorldBVH->QueryClosestRayCandidateWithNearT(Ray, OutCandidate, MaxNearT);
	}

	return false;
}

void FWorldRenderProxy::PrepareRayPickingCachesForQuery()
{
	// Build-only prep for click path measurement isolation:
	// keep expensive rebuild/cache prep right before pick algorithm timing.
	if (bSpatialIndexDirty)
	{
		RebuildSpatialIndexIfDirty(false, false);
	}

	const bool bBypassFrustumGate = bSpatialIndexDirty || (SpatialIndexDeferDepth > 0) || bDeferredSpatialIndexDirtyPending;
	const bool bUseFrustumGate = bRayFrustumGateOptimizationEnabled && !bBypassFrustumGate && (FrustumVisiblePickFrameTag != 0u);
	if (!bUseFrustumGate)
	{
		return;
	}

	BuildFrustumVisiblePickCacheIfNeeded();

	const size_t VisibleCount = FrustumVisiblePickableCache.size();
	const uint32 LinearThreshold = FPickingTuning::BroadLinearVisibleThreshold();
	if (!VisibleRaySpatialIndex || VisibleCount <= static_cast<size_t>(LinearThreshold))
	{
		return;
	}

	const bool bUseOcclusionGate = FPickingTuning::UseRayOcclusionGate();
	RebuildVisibleRaySpatialIndexIfNeeded(bUseOcclusionGate);
}

void FWorldRenderProxy::RebuildVisibleRaySpatialIndexIfNeeded(bool bUseOcclusionGate)
{
	if (!VisibleRaySpatialIndex)
	{
		return;
	}

	const bool bFrameMismatch = (VisibleRaySpatialIndexFrameTag != FrustumVisiblePickFrameTag);
	const bool bOcclusionModeMismatch = (bVisibleRaySpatialIndexBuiltWithOcclusion != bUseOcclusionGate);
	if (!bVisibleRaySpatialIndexDirty && !bFrameMismatch && !bOcclusionModeMismatch)
	{
		return;
	}

	VisibleRaySpatialIndex->Clear();
	for (FPrimitiveProxy* Proxy : FrustumVisiblePickableCache)
	{
		if (!Proxy)
		{
			continue;
		}
		UPrimitiveComponent* Owner = Proxy->GetOwner();
		if (!Owner || !Owner->IsVisible())
		{
			continue;
		}
		if (bUseOcclusionGate && CachedOcclusionViewport && !FOcclusionManager::Get().IsVisible(CachedOcclusionViewport, Proxy->CachedProxyId))
		{
			continue;
		}
		VisibleRaySpatialIndex->Insert(Proxy, Owner->GetWorldBoundingBox());
	}
	VisibleRaySpatialIndex->Warmup();

	bVisibleRaySpatialIndexDirty = false;
	bVisibleRaySpatialIndexBuiltWithOcclusion = bUseOcclusionGate;
	VisibleRaySpatialIndexFrameTag = FrustumVisiblePickFrameTag;
}

void FWorldRenderProxy::QueryByRayWithNearT(const FRay& Ray, TArray<FRayQueryCandidate>& OutCandidates, float MaxNearT)
{
	OutCandidates.clear();
	LastRayBroadDebugCounters = {};
	uint64 TraversalAABBTests = 0u;
	uint64 TraversalAABBHits = 0u;

	// During transform defer window, spatial index can be stale; bypass to direct broad test.
	if (SpatialIndexDeferDepth > 0 || bDeferredSpatialIndexDirtyPending)
	{
		const FRayAABBKernel Kernel = FRayAABBKernel::Build(Ray);
		GatherRayCandidatesFromSoA(Ray, Kernel, RayPickableSoA, CachedOcclusionViewport, false, MaxNearT, OutCandidates, &TraversalAABBTests, &TraversalAABBHits);
		LastRayBroadDebugCounters.AABBTests = TraversalAABBTests;
		LastRayBroadDebugCounters.AABBHits = TraversalAABBHits;
		LastRayBroadDebugCounters.LinearAABBTests = TraversalAABBTests;
		LastRayBroadDebugCounters.CandidatesEmitted = static_cast<uint64>(OutCandidates.size());
		LastRayBroadDebugCounters.CandidatesAfterFilter = LastRayBroadDebugCounters.CandidatesEmitted;
		return;
	}

	if (bSpatialIndexDirty)
	{
		RebuildSpatialIndexIfDirty(false, false);
	}
	if (!RaySpatialIndex)
	{
		return;
	}

	// TODO: expose camera-jump signal to bypass gate on large view deltas.
	const bool bBypassFrustumGate = bSpatialIndexDirty || (SpatialIndexDeferDepth > 0) || bDeferredSpatialIndexDirtyPending;
	const bool bUseFrustumGate = bRayFrustumGateOptimizationEnabled && !bBypassFrustumGate && (FrustumVisiblePickFrameTag != 0u);
	const bool bUseOcclusionGate = bUseFrustumGate && FPickingTuning::UseRayOcclusionGate();
	if (bUseFrustumGate)
	{
		BuildFrustumVisiblePickCacheIfNeeded();
	}
	const uint32 LinearThreshold = FPickingTuning::BroadLinearVisibleThreshold();
	const size_t VisibleCount = FrustumVisiblePickableCache.size();
	bool bUsedVisibleSource = false;

	if (bUseFrustumGate && VisibleCount > 0)
	{
		if (VisibleRaySpatialIndex && bVisibleRaySpatialIndexDirty)
		{
			RebuildVisibleRaySpatialIndexIfNeeded(bUseOcclusionGate);
		}

		if (VisibleCount <= LinearThreshold)
		{
			const FRayAABBKernel Kernel = FRayAABBKernel::Build(Ray);
			GatherRayCandidatesFromSoA(Ray, Kernel, FrustumVisiblePickableSoA, CachedOcclusionViewport, bUseOcclusionGate, MaxNearT, OutCandidates, &TraversalAABBTests, &TraversalAABBHits);
			LastRayBroadDebugCounters.AABBTests = TraversalAABBTests;
			LastRayBroadDebugCounters.AABBHits = TraversalAABBHits;
			LastRayBroadDebugCounters.LinearAABBTests = TraversalAABBTests;
			LastRayBroadDebugCounters.CandidatesEmitted = static_cast<uint64>(OutCandidates.size());
			LastRayBroadDebugCounters.CandidatesAfterFilter = LastRayBroadDebugCounters.CandidatesEmitted;
			return;
		}

		if (VisibleRaySpatialIndex &&
			!bVisibleRaySpatialIndexDirty &&
			(VisibleRaySpatialIndexFrameTag == FrustumVisiblePickFrameTag))
		{
			VisibleRaySpatialIndex->QueryRayWithNearT(Ray, OutCandidates, MaxNearT);
			if (bUseOcclusionGate && !bVisibleRaySpatialIndexBuiltWithOcclusion && !OutCandidates.empty())
			{
				size_t WriteIndex = 0;
				for (size_t ReadIndex = 0; ReadIndex < OutCandidates.size(); ++ReadIndex)
				{
					const FRayQueryCandidate& Candidate = OutCandidates[ReadIndex];
					if (!Candidate.Proxy || (CachedOcclusionViewport && !FOcclusionManager::Get().IsVisible(CachedOcclusionViewport, Candidate.Proxy->CachedProxyId)))
					{
						continue;
					}
					OutCandidates[WriteIndex++] = Candidate;
				}
				OutCandidates.resize(WriteIndex);
			}
			const FSpatialQueryDebugStats& RayStats = VisibleRaySpatialIndex->GetLastDebugStats();
			LastRayBroadDebugCounters.NodeVisits = static_cast<uint64>((std::max)(0, RayStats.RayIntersectedNodes));
			LastRayBroadDebugCounters.BVHAABBTests = static_cast<uint64>((std::max)(0, RayStats.RayAABBTests));
			LastRayBroadDebugCounters.AABBTests = LastRayBroadDebugCounters.BVHAABBTests;
			LastRayBroadDebugCounters.AABBHits = static_cast<uint64>((std::max)(0, RayStats.RayAABBHits));
			LastRayBroadDebugCounters.CandidatesEmitted = static_cast<uint64>(OutCandidates.size());
			LastRayBroadDebugCounters.CandidatesAfterFilter = LastRayBroadDebugCounters.CandidatesEmitted;
			bUsedVisibleSource = true;
		}
	}

	if (!bUsedVisibleSource)
	{
		RaySpatialIndex->QueryRayWithNearT(Ray, OutCandidates, MaxNearT);
		const FSpatialQueryDebugStats& RayStats = RaySpatialIndex->GetLastDebugStats();
		LastRayBroadDebugCounters.NodeVisits = static_cast<uint64>((std::max)(0, RayStats.RayIntersectedNodes));
		LastRayBroadDebugCounters.BVHAABBTests = static_cast<uint64>((std::max)(0, RayStats.RayAABBTests));
		LastRayBroadDebugCounters.AABBTests = LastRayBroadDebugCounters.BVHAABBTests;
		LastRayBroadDebugCounters.AABBHits = static_cast<uint64>((std::max)(0, RayStats.RayAABBHits));
		LastRayBroadDebugCounters.CandidatesEmitted = static_cast<uint64>(OutCandidates.size());
	}

	if (bUseFrustumGate && !bUsedVisibleSource)
	{
		size_t WriteIndex = 0;
		const size_t RawCount = OutCandidates.size();
		for (size_t ReadIndex = 0; ReadIndex < RawCount; ++ReadIndex)
		{
			const FRayQueryCandidate& Candidate = OutCandidates[ReadIndex];
			if (!Candidate.Proxy)
			{
				continue;
			}
			if (!Candidate.Proxy->IsFrustumVisibleForPick(FrustumVisiblePickFrameTag))
			{
				continue;
			}
			if (bUseOcclusionGate && CachedOcclusionViewport && !FOcclusionManager::Get().IsVisible(CachedOcclusionViewport, Candidate.Proxy->CachedProxyId))
			{
				continue;
			}

			OutCandidates[WriteIndex++] = Candidate;
		}
		OutCandidates.resize(WriteIndex);
	}
	LastRayBroadDebugCounters.CandidatesAfterFilter = static_cast<uint64>(OutCandidates.size());
	if (LastRayBroadDebugCounters.CandidatesEmitted == 0u)
	{
		LastRayBroadDebugCounters.CandidatesEmitted = LastRayBroadDebugCounters.CandidatesAfterFilter;
	}
}

void FWorldRenderProxy::SubmitRenderCommands(FViewContext& Context, const TArray<AActor*>& SelectedActors)
{
	SCOPE_STAT("Render.SubmitCommands");
	if (!this) return;

	const TArray<FPrimitiveProxy*>& CandidateProxies = Context.GetCandidateProxies();
	LastCullingStats.RenderedProxyCount = 0;

	for (FPrimitiveProxy* Proxy : CandidateProxies)
	{
		if (!Proxy) continue;

		// 현재 WorldRenderProxy에 속한 프록시만 처리 (Stats 관리 및 중복 제출 방지)
		if (Proxy->GetWorldRenderProxy() != this) continue;

		UPrimitiveComponent* Owner = Proxy->GetOwner();
		if (!Owner || !Owner->IsVisible()) continue;

		bool bSelected = false;
		if (AActor* ActorOwner = Owner->GetOwner())
		{
			if (ActorOwner->IsVisible())
			{
				if (!SelectedActors.empty())
				{
					bSelected = std::find(SelectedActors.begin(), SelectedActors.end(), ActorOwner) != SelectedActors.end();
				}
			}
			else
			{
				continue;
			}
		}

		Proxy->SetSelected(bSelected);
		Proxy->SubmitRenderCommand(Context);
		LastCullingStats.RenderedProxyCount++;
	}
}

void FWorldRenderProxy::InjectAlwaysVisibleCandidates(FViewContext& Context, const TArray<AActor*>& SelectedActors, bool bIncludeGizmo)
{
	if (!this)
	{
		return;
	}

	// 1. 선택된 액터의 프록시를 즉시 주입 (전체 순회 제거)
	for (AActor* ActorOwner : SelectedActors)
	{
		if (!ActorOwner || !ActorOwner->IsVisible())
		{
			continue;
		}

		TArray<UPrimitiveComponent*> Components;
		Components = ActorOwner->GetPrimitiveComponents();

		for (UPrimitiveComponent* Owner : Components)
		{
			if (!Owner || !Owner->IsVisible())
			{
				continue;
			}

			if (FPrimitiveProxy* Proxy = Owner->GetProxy())
			{
				if (Proxy->GetWorldRenderProxy() == this)
				{
					Context.AddCandidateProxyUnique(Proxy);
				}
			}
		}
	}

	// 2. 기즈모 처리 (필요한 경우만 순회)
	if (bIncludeGizmo)
	{
		for (FPrimitiveProxy* Proxy : Proxies)
		{
			if (!Proxy) continue;

			UPrimitiveComponent* Owner = Proxy->GetOwner();
			if (Owner && Owner->IsVisible() && Owner->IsA<UGizmoComponent>())
			{
				Context.AddCandidateProxyUnique(Proxy);
			}
		}
	}
}

void FWorldRenderProxy::CollectWorld(FViewContext& Context, const TArray<AActor*>& SelectedActors)
{
	SCOPE_STAT("Render.CollectWorld");
	if (!this) return;
	LastCullingStats = {};
	LastCullingStats.RegisteredProxyCount = static_cast<int32>(Proxies.size());

	GatherCandidates(Context);
	SubmitRenderCommands(Context, SelectedActors);
}

void FWorldRenderProxy::RebuildSpatialIndexIfDirty(bool bTrackInsertedStats, bool bPrewarmStaticMeshBVH)
{
	if (!bSpatialIndexDirty)
	{
		return;
	}
	SCOPE_STAT("Render.SpatialBuild");

	if (FrustumSpatialIndex) FrustumSpatialIndex->Clear();
	if (RaySpatialIndex) RaySpatialIndex->Clear();
	if (VisibleRaySpatialIndex) VisibleRaySpatialIndex->Clear();
	RayPickableSoA.Clear();
	RayPickableSoA.Reserve(Proxies.size());

	for (FPrimitiveProxy* Proxy : Proxies)
	{
		if (!Proxy)
		{
			continue;
		}

		UPrimitiveComponent* Owner = Proxy->GetOwner();
		if (!Owner)
		{
			continue;
		}

		// Keep this off the broad query path to avoid first-click broad spikes.
		if (bPrewarmStaticMeshBVH)
		{
			if (UStaticMeshComponent* StaticMeshComp = Cast<UStaticMeshComponent>(Owner))
			{
				if (UStaticMesh* StaticMesh = StaticMeshComp->GetStaticMesh())
				{
					if (FStaticMesh* Asset = StaticMesh->GetStaticMeshAsset())
					{
						if (!Asset->Vertices.empty() && !Asset->Indices.empty() && !Asset->GetBVH())
						{
							Asset->BuildBVH();
						}
					}
				}
			}
		}

		Owner->GetWorldMatrix();
		Owner->UpdateWorldAABB();
		FBoundingBox Bounds = Owner->GetWorldBoundingBox();

		// Billboard 계열은 SceneComponent attach/dirty 타이밍 영향으로
		// cached AABB가 일시적으로 원점에 남는 케이스가 있어,
		// spatial index 구축 시 월드 위치 기반 bounds를 직접 사용한다.
		if (Owner->IsA<UBillboardComponent>())
		{
			const FVector WorldScale = Owner->GetWorldScale();
			const float ExtentScalar = (std::max)({ std::abs(WorldScale.X), std::abs(WorldScale.Y), std::abs(WorldScale.Z), 0.01f });
			const FVector Center = Owner->GetWorldLocation();
			const FVector Extent(ExtentScalar, ExtentScalar, ExtentScalar);
			Bounds = FBoundingBox(Center - Extent, Center + Extent);
		}

		if (FrustumSpatialIndex) FrustumSpatialIndex->Insert(Proxy, Bounds);
		if (RaySpatialIndex)
		{
			const bool bOwnerVisible = Owner->IsVisible();
			const bool bExcludedComponent = Owner->IsA<UTextRenderComponent>() || Owner->IsA<UGizmoComponent>();
			AActor* ActorOwner = Owner->GetOwner();
			const bool bActorPickable = (ActorOwner != nullptr) && ActorOwner->IsVisible() && (ActorOwner->GetRootComponent() != nullptr);

			if (bOwnerVisible && !bExcludedComponent && bActorPickable)
			{
				RaySpatialIndex->Insert(Proxy, Bounds);
				RayPickableSoA.Add(Bounds, Proxy);
			}
		}

		if (bTrackInsertedStats)
		{
			++LastCullingStats.InsertedProxyCount;
		}
	}

	bSpatialIndexDirty = false;
	bFrustumVisiblePickCacheDirty = true;
	bVisibleRaySpatialIndexDirty = true;
	bVisibleRaySpatialIndexBuiltWithOcclusion = false;
	VisibleRaySpatialIndexFrameTag = 0u;
}
