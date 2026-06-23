#pragma once

#include "Core/CoreTypes.h"
#include "Core/EngineTypes.h"
#include "Render/Pipeline/FrustumCulling.h"
#include "Render/Pipeline/IPrimitiveSpatialQuery.h"

#include <memory>
#include <vector>
#include <cfloat>

class FFixedWorldOctree : public IPrimitiveSpatialQuery
{
public:
	//	고정 월드 바운드 기반 Octree (초기 버전: 동적 루트 확장 없음)
	FFixedWorldOctree(
		const FBoundingBox& InWorldBounds = FBoundingBox(FVector(-10000.0f, -10000.0f, -10000.0f), FVector(10000.0f, 10000.0f, 10000.0f)),
		int32 InMaxDepth = 6,
		int32 InMaxItemsPerNode = 16);

	//	프레임 단위 재구축을 위한 초기화
	void Clear() override;
	//	프록시의 월드 AABB를 공간 인덱스에 삽입
	void Insert(FPrimitiveProxy* Proxy, const FBoundingBox& Bounds) override;
	void Warmup() override;
	//	프러스텀과 겹치는 후보 프록시만 수집
	void QueryFrustum(const FFrustumPlanes& Frustum, TArray<FPrimitiveProxy*>& OutProxies) const override;
	void QueryRay(const FRay& Ray, TArray<FPrimitiveProxy*>& OutProxies) const override;
	void QueryRayWithNearT(const FRay& Ray, TArray<FRayQueryCandidate>& OutCandidates, float MaxNearT = FLT_MAX) const override;
	const FSpatialQueryDebugStats& GetLastDebugStats() const override { return LastDebugStats; }

private:
	struct FOctreeItem
	{
		FPrimitiveProxy* Proxy = nullptr;
		FBoundingBox Bounds;
	};

	// 8-wide SoA batch for AVX2 accelerated intersection
	struct FOctreeItemSoA8
	{
		alignas(32) float MinX[8];
		alignas(32) float MinY[8];
		alignas(32) float MinZ[8];
		alignas(32) float MaxX[8];
		alignas(32) float MaxY[8];
		alignas(32) float MaxZ[8];
		FPrimitiveProxy* Proxies[8];
	};

	struct FNode
	{
		FNode()
		{
			for (int i = 0; i < 8; ++i) Children[i] = -1;
		}

		FBoundingBox Bounds;
		int32 Depth = 0;
		
		// Optimization: Use SoA batches for fast SIMD testing
		TArray<FOctreeItemSoA8> ItemBatches8;
		TArray<FOctreeItem> RemainderItems;
		
		int32 Children[8];

		bool HasChildren() const
		{
			for (int i = 0; i < 8; ++i) if (Children[i] != -1) return true;
			return false;
		}

		uint32 GetTotalItemCount() const
		{
			return static_cast<uint32>(ItemBatches8.size() * 8 + RemainderItems.size());
		}
	};

	static bool ContainsAABB(const FBoundingBox& Outer, const FBoundingBox& Inner);
	FBoundingBox BuildChildBounds(const FBoundingBox& Parent, int32 ChildIndex) const;
	int32 GetContainingChildIndex(const FBoundingBox& ParentBounds, const FBoundingBox& ItemBounds) const;
	void InsertNode(int32 NodeIdx, const FOctreeItem& Item);
	void QueryNodeByFrustum(int32 NodeIdx, const FFrustumPlanes& Frustum, TArray<FPrimitiveProxy*>& OutProxies) const;
	void AccumulateNodeStats(int32 NodeIdx, int32& OutNodeCount, int32& OutItemCount) const;
	void RefreshTopologyStatsCacheIfNeeded() const;

	int32 AllocateNode(const FBoundingBox& InBounds, int32 InDepth);

	FBoundingBox WorldBounds;
	int32 MaxDepth = 6;
	int32 MaxItemsPerNode = 16;
	
	mutable TArray<FNode> NodePool;
	int32 RootIdx = -1;

	TArray<FOctreeItem> OutsideItems;
	mutable FSpatialQueryDebugStats LastDebugStats;
	mutable bool bTopologyStatsDirty = true;
	mutable int32 CachedTotalNodes = 0;
	mutable int32 CachedIndexedItems = 0;
};
