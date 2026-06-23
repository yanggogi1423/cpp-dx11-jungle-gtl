#include "Render/Pipeline/FixedWorldOctree.h"
#include <algorithm>

FFixedWorldOctree::FFixedWorldOctree(const FBoundingBox& InWorldBounds, int32 InMaxDepth, int32 InMaxItemsPerNode)
	: WorldBounds(InWorldBounds)
	, MaxDepth(InMaxDepth)
	, MaxItemsPerNode(InMaxItemsPerNode)
{
	NodePool.reserve(1024);
	RootIdx = AllocateNode(WorldBounds, 0);
}

void FFixedWorldOctree::Clear()
{
	NodePool.clear();
	OutsideItems.clear();
	LastDebugStats = {};
	bTopologyStatsDirty = true;
	CachedTotalNodes = 0;
	CachedIndexedItems = 0;
	RootIdx = AllocateNode(WorldBounds, 0);
}

int32 FFixedWorldOctree::AllocateNode(const FBoundingBox& InBounds, int32 InDepth)
{
	int32 Idx = (int32)NodePool.size();
	NodePool.emplace_back();
	NodePool[Idx].Bounds = InBounds;
	NodePool[Idx].Depth = InDepth;
	return Idx;
}

void FFixedWorldOctree::Insert(FPrimitiveProxy* Proxy, const FBoundingBox& Bounds)
{
	if (!Proxy || !Bounds.IsValid())
	{
		return;
	}

	FOctreeItem Item = { Proxy, Bounds };
	if (!ContainsAABB(WorldBounds, Bounds))
	{
		OutsideItems.push_back(Item);
		bTopologyStatsDirty = true;
		return;
	}

	InsertNode(RootIdx, Item);
	bTopologyStatsDirty = true;
}

void FFixedWorldOctree::Warmup()
{
	RefreshTopologyStatsCacheIfNeeded();
}

void FFixedWorldOctree::QueryFrustum(const FFrustumPlanes& Frustum, TArray<FPrimitiveProxy*>& OutProxies) const
{
	LastDebugStats = {};
	LastDebugStats.OutsideItems = static_cast<int32>(OutsideItems.size());
	if (RootIdx < 0 || NodePool.empty())
	{
		return;
	}

	RefreshTopologyStatsCacheIfNeeded();
	LastDebugStats.TotalNodes = CachedTotalNodes;
	LastDebugStats.TotalItems = CachedIndexedItems + LastDebugStats.OutsideItems;

	QueryNodeByFrustum(RootIdx, Frustum, OutProxies);

	for (const FOctreeItem& Item : OutsideItems)
	{
		if (FFrustumCulling::IntersectsAABB(Frustum, Item.Bounds))
		{
			OutProxies.push_back(Item.Proxy);
			++LastDebugStats.FrustumCandidateItems;
		}
	}
}

void FFixedWorldOctree::QueryRay(const FRay& Ray, TArray<FPrimitiveProxy*>& OutProxies) const
{
	OutProxies.clear();
	(void)Ray;
}

void FFixedWorldOctree::QueryRayWithNearT(const FRay& Ray, TArray<FRayQueryCandidate>& OutCandidates, float MaxNearT) const
{
	OutCandidates.clear();
	(void)Ray;
	(void)MaxNearT;
}

void FFixedWorldOctree::AccumulateNodeStats(int32 NodeIdx, int32& OutNodeCount, int32& OutItemCount) const
{
	const FNode& Node = NodePool[NodeIdx];
	++OutNodeCount;
	OutItemCount += Node.GetTotalItemCount();

	for (int32 ChildIdx : Node.Children)
	{
		if (ChildIdx != -1)
		{
			AccumulateNodeStats(ChildIdx, OutNodeCount, OutItemCount);
		}
	}
}

void FFixedWorldOctree::RefreshTopologyStatsCacheIfNeeded() const
{
	if (!bTopologyStatsDirty)
	{
		return;
	}

	CachedTotalNodes = 0;
	CachedIndexedItems = 0;
	if (RootIdx != -1)
	{
		AccumulateNodeStats(RootIdx, CachedTotalNodes, CachedIndexedItems);
	}

	bTopologyStatsDirty = false;
}

bool FFixedWorldOctree::ContainsAABB(const FBoundingBox& Outer, const FBoundingBox& Inner)
{
	return Inner.Min.X >= Outer.Min.X && Inner.Max.X <= Outer.Max.X
		&& Inner.Min.Y >= Outer.Min.Y && Inner.Max.Y <= Outer.Max.Y
		&& Inner.Min.Z >= Outer.Min.Z && Inner.Max.Z <= Outer.Max.Z;
}

FBoundingBox FFixedWorldOctree::BuildChildBounds(const FBoundingBox& Parent, int32 ChildIndex) const
{
	const FVector Center = Parent.GetCenter();

	const bool bUpperX = (ChildIndex & 1) != 0;
	const bool bUpperY = (ChildIndex & 2) != 0;
	const bool bUpperZ = (ChildIndex & 4) != 0;

	const FVector Min(
		bUpperX ? Center.X : Parent.Min.X,
		bUpperY ? Center.Y : Parent.Min.Y,
		bUpperZ ? Center.Z : Parent.Min.Z);

	const FVector Max(
		bUpperX ? Parent.Max.X : Center.X,
		bUpperY ? Parent.Max.Y : Center.Y,
		bUpperZ ? Parent.Max.Z : Center.Z);

	return FBoundingBox(Min, Max);
}

int32 FFixedWorldOctree::GetContainingChildIndex(const FBoundingBox& ParentBounds, const FBoundingBox& ItemBounds) const
{
	const FVector Center = ParentBounds.GetCenter();
	int32 ChildIndex = 0;

	// Must be fully on one side of each split plane; straddling stays in parent.
	if (ItemBounds.Max.X <= Center.X)
	{
		ChildIndex |= 0;
	}
	else if (ItemBounds.Min.X >= Center.X)
	{
		ChildIndex |= 1;
	}
	else
	{
		return -1;
	}

	if (ItemBounds.Max.Y <= Center.Y)
	{
		ChildIndex |= 0;
	}
	else if (ItemBounds.Min.Y >= Center.Y)
	{
		ChildIndex |= 2;
	}
	else
	{
		return -1;
	}

	if (ItemBounds.Max.Z <= Center.Z)
	{
		ChildIndex |= 0;
	}
	else if (ItemBounds.Min.Z >= Center.Z)
	{
		ChildIndex |= 4;
	}
	else
	{
		return -1;
	}

	return ChildIndex;
}

void FFixedWorldOctree::InsertNode(int32 NodeIdx, const FOctreeItem& Item)
{
	if (NodePool[NodeIdx].Depth >= MaxDepth)
	{
		NodePool[NodeIdx].RemainderItems.push_back(Item);
		if (NodePool[NodeIdx].RemainderItems.size() == 8)
		{
			FOctreeItemSoA8 Batch;
			for (int i = 0; i < 8; ++i)
			{
				Batch.MinX[i] = NodePool[NodeIdx].RemainderItems[i].Bounds.Min.X;
				Batch.MinY[i] = NodePool[NodeIdx].RemainderItems[i].Bounds.Min.Y;
				Batch.MinZ[i] = NodePool[NodeIdx].RemainderItems[i].Bounds.Min.Z;
				Batch.MaxX[i] = NodePool[NodeIdx].RemainderItems[i].Bounds.Max.X;
				Batch.MaxY[i] = NodePool[NodeIdx].RemainderItems[i].Bounds.Max.Y;
				Batch.MaxZ[i] = NodePool[NodeIdx].RemainderItems[i].Bounds.Max.Z;
				Batch.Proxies[i] = NodePool[NodeIdx].RemainderItems[i].Proxy;
			}
			NodePool[NodeIdx].ItemBatches8.push_back(Batch);
			NodePool[NodeIdx].RemainderItems.clear();
		}
		return;
	}

	const int32 ChildIndex = GetContainingChildIndex(NodePool[NodeIdx].Bounds, Item.Bounds);
	if (ChildIndex >= 0)
	{
		if (NodePool[NodeIdx].Children[ChildIndex] == -1)
		{
			FBoundingBox CBounds = BuildChildBounds(NodePool[NodeIdx].Bounds, ChildIndex);
			int32 NewIdx = AllocateNode(CBounds, NodePool[NodeIdx].Depth + 1);
			NodePool[NodeIdx].Children[ChildIndex] = NewIdx;
		}
		InsertNode(NodePool[NodeIdx].Children[ChildIndex], Item);
	}
	else
	{
		NodePool[NodeIdx].RemainderItems.push_back(Item);
		if (NodePool[NodeIdx].RemainderItems.size() == 8)
		{
			FOctreeItemSoA8 Batch;
			for (int i = 0; i < 8; ++i)
			{
				Batch.MinX[i] = NodePool[NodeIdx].RemainderItems[i].Bounds.Min.X;
				Batch.MinY[i] = NodePool[NodeIdx].RemainderItems[i].Bounds.Min.Y;
				Batch.MinZ[i] = NodePool[NodeIdx].RemainderItems[i].Bounds.Min.Z;
				Batch.MaxX[i] = NodePool[NodeIdx].RemainderItems[i].Bounds.Max.X;
				Batch.MaxY[i] = NodePool[NodeIdx].RemainderItems[i].Bounds.Max.Y;
				Batch.MaxZ[i] = NodePool[NodeIdx].RemainderItems[i].Bounds.Max.Z;
				Batch.Proxies[i] = NodePool[NodeIdx].RemainderItems[i].Proxy;
			}
			NodePool[NodeIdx].ItemBatches8.push_back(Batch);
			NodePool[NodeIdx].RemainderItems.clear();
		}
	}
}

void FFixedWorldOctree::QueryNodeByFrustum(int32 NodeIdx, const FFrustumPlanes& Frustum, TArray<FPrimitiveProxy*>& OutProxies) const
{
	const FNode& Node = NodePool[NodeIdx];
	if (!FFrustumCulling::IntersectsAABB(Frustum, Node.Bounds))
	{
		return;
	}

	++LastDebugStats.FrustumIntersectedNodes;

	for (const auto& Batch : Node.ItemBatches8)
	{
		for (int i = 0; i < 8; ++i)
		{
			FBoundingBox Bounds(FVector(Batch.MinX[i], Batch.MinY[i], Batch.MinZ[i]), FVector(Batch.MaxX[i], Batch.MaxY[i], Batch.MaxZ[i]));
			if (FFrustumCulling::IntersectsAABB(Frustum, Bounds))
			{
				OutProxies.push_back(Batch.Proxies[i]);
				++LastDebugStats.FrustumCandidateItems;
			}
		}
	}

	for (const FOctreeItem& Item : Node.RemainderItems)
	{
		if (FFrustumCulling::IntersectsAABB(Frustum, Item.Bounds))
		{
			OutProxies.push_back(Item.Proxy);
			++LastDebugStats.FrustumCandidateItems;
		}
	}

	for (int32 ChildIdx : Node.Children)
	{
		if (ChildIdx != -1)
		{
			QueryNodeByFrustum(ChildIdx, Frustum, OutProxies);
		}
	}
}
