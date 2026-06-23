#pragma once

#include "Core/CoreTypes.h"
#include "Core/EngineTypes.h"
#include "Core/RayTypes.h"
#include "Core/CollisionTypes.h"
#include "Collision/PickingTuning.h"

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>

struct FStaticMeshBVH
{
	struct FTriangleRef
	{
		uint32 FirstIndex = 0;
		FBoundingBox Bounds;
		FVector Centroid;
	};

	struct FNode
	{
		FBoundingBox Bounds;
		int32 Left = -1;
		int32 Right = -1;
		uint32 FirstTri = 0;
		uint32 TriCount = 0;

		bool IsLeaf() const
		{
			return Left < 0 && Right < 0;
		}
	};

	void Build(const void* PositionData, uint32 PositionStride, const TArray<uint32>& InIndices)
	{
		Triangles.clear();
		TriOrder.clear();
		Nodes.clear();
		TriFirstIndex.clear();
		TriV0X.clear(); TriV0Y.clear(); TriV0Z.clear();
		TriE1X.clear(); TriE1Y.clear(); TriE1Z.clear();
		TriE2X.clear(); TriE2Y.clear(); TriE2Z.clear();

		if (!PositionData || PositionStride == 0 || InIndices.size() < 3)
		{
			return;
		}

		const size_t TriangleCount = InIndices.size() / 3;
		Triangles.resize(TriangleCount);
		TriOrder.resize(TriangleCount);
		TriFirstIndex.resize(TriangleCount);
		TriV0X.resize(TriangleCount); TriV0Y.resize(TriangleCount); TriV0Z.resize(TriangleCount);
		TriE1X.resize(TriangleCount); TriE1Y.resize(TriangleCount); TriE1Z.resize(TriangleCount);
		TriE2X.resize(TriangleCount); TriE2Y.resize(TriangleCount); TriE2Z.resize(TriangleCount);

		const uint8* BasePtr = static_cast<const uint8*>(PositionData);
#if defined(_OPENMP)
#pragma omp parallel for if(FPickingTuning::EnableOpenMP() && TriangleCount >= FPickingTuning::OpenMPMinWorkItems())
#endif
		for (int64 TriIdx64 = 0; TriIdx64 < static_cast<int64>(TriangleCount); ++TriIdx64)
		{
			const uint32 TriIdx = static_cast<uint32>(TriIdx64);
			const uint32 i = TriIdx * 3u;
			const uint32 I0 = InIndices[i];
			const uint32 I1 = InIndices[i + 1];
			const uint32 I2 = InIndices[i + 2];

			const FVector& V0 = *reinterpret_cast<const FVector*>(BasePtr + static_cast<size_t>(I0) * PositionStride);
			const FVector& V1 = *reinterpret_cast<const FVector*>(BasePtr + static_cast<size_t>(I1) * PositionStride);
			const FVector& V2 = *reinterpret_cast<const FVector*>(BasePtr + static_cast<size_t>(I2) * PositionStride);
			const FVector E1 = V1 - V0;
			const FVector E2 = V2 - V0;

			FTriangleRef Tri;
			Tri.FirstIndex = i;
			Tri.Bounds = FBoundingBox(V0, V0);
			Tri.Bounds.Expand(V1);
			Tri.Bounds.Expand(V2);
			Tri.Centroid = (V0 + V1 + V2) / 3.0f;
			Triangles[TriIdx] = Tri;
			TriOrder[TriIdx] = TriIdx;

			TriFirstIndex[TriIdx] = i;
			TriV0X[TriIdx] = V0.X; TriV0Y[TriIdx] = V0.Y; TriV0Z[TriIdx] = V0.Z;
			TriE1X[TriIdx] = E1.X; TriE1Y[TriIdx] = E1.Y; TriE1Z[TriIdx] = E1.Z;
			TriE2X[TriIdx] = E2.X; TriE2Y[TriIdx] = E2.Y; TriE2Z[TriIdx] = E2.Z;
		}

		if (!Triangles.empty())
		{
			BuildNode(0, static_cast<uint32>(Triangles.size()), 0);
		}
	}

	bool IntersectLocalRay(const FRay& LocalRay, const void* PositionData, uint32 PositionStride, FHitResult& OutHitResult, float InClosestT = FLT_MAX) const
	{
		if (Nodes.empty() || Triangles.empty() || !PositionData || PositionStride == 0)
		{
			OutHitResult = {};
			return false;
		}
		(void)PositionData;
		(void)PositionStride;

		bool bHit = false;
		float ClosestT = InClosestT;
		int32 HitFaceIndex = -1;
		const bool bBackFaceCull = FPickingTuning::UseBackFaceCull();
		const float DetEps = FPickingTuning::TriangleDetEpsilon();

		const float RayOx = LocalRay.Origin.X;
		const float RayOy = LocalRay.Origin.Y;
		const float RayOz = LocalRay.Origin.Z;
		const float RayDx = LocalRay.Direction.X;
		const float RayDy = LocalRay.Direction.Y;
		const float RayDz = LocalRay.Direction.Z;
		const FRayAABBKernel Kernel = FRayAABBKernel::Build(LocalRay);

		float RootNearT = 0.0f;
		float RootFarT = 0.0f;
		if (!IntersectRayAABBNearT(LocalRay, Kernel, Nodes[0].Bounds, RootNearT, RootFarT) || RootNearT >= ClosestT)
		{
			OutHitResult = {};
			return false;
		}

		thread_local std::array<FNodeVisit, 128> FixedStack;
		thread_local TArray<FNodeVisit> OverflowStack;
		int32 FixedSize = 0;
		OverflowStack.clear();
		FixedStack[FixedSize++] = { 0, RootNearT };

		auto PushVisit = [&](const FNodeVisit& Visit)
		{
			if (FixedSize < static_cast<int32>(FixedStack.size()))
			{
				FixedStack[FixedSize++] = Visit;
				return;
			}
			OverflowStack.push_back(Visit);
		};

		auto PopVisit = [&]() -> FNodeVisit
		{
			if (FixedSize > 0)
			{
				return FixedStack[--FixedSize];
			}
			const FNodeVisit Visit = OverflowStack.back();
			OverflowStack.pop_back();
			return Visit;
		};

		while (FixedSize > 0 || !OverflowStack.empty())
		{
			const FNodeVisit Visit = PopVisit();
			if (Visit.NearT >= ClosestT)
			{
				continue;
			}

			const int32 NodeIndex = Visit.NodeIndex;
			if (NodeIndex < 0 || NodeIndex >= static_cast<int32>(Nodes.size()))
			{
				continue;
			}

			const FNode& Node = Nodes[NodeIndex];
			if (Node.IsLeaf())
			{
				const uint32 EndTri = Node.FirstTri + Node.TriCount;
				for (uint32 TriSlot = Node.FirstTri; TriSlot < EndTri; ++TriSlot)
				{
					const uint32 TriIndex = TriOrder[TriSlot];
					float T = 0.0f;
					if (IntersectTriangleSoA(RayOx, RayOy, RayOz, RayDx, RayDy, RayDz, TriIndex, ClosestT, bBackFaceCull, DetEps, T))
					{
						ClosestT = T;
						HitFaceIndex = static_cast<int32>(TriFirstIndex[TriIndex]);
						bHit = true;
					}
				}
			}
			else
			{
				float LeftNearT = FLT_MAX;
				float LeftFarT = FLT_MAX;
				float RightNearT = FLT_MAX;
				float RightFarT = FLT_MAX;
				const bool bHitLeft = (Node.Left >= 0) && IntersectRayAABBNearT(LocalRay, Kernel, Nodes[Node.Left].Bounds, LeftNearT, LeftFarT);
				const bool bHitRight = (Node.Right >= 0) && IntersectRayAABBNearT(LocalRay, Kernel, Nodes[Node.Right].Bounds, RightNearT, RightFarT);

				if (bHitLeft && LeftNearT < ClosestT && bHitRight && RightNearT < ClosestT)
				{
					if (LeftNearT <= RightNearT)
					{
						PushVisit({ Node.Right, RightNearT });
						PushVisit({ Node.Left, LeftNearT });
					}
					else
					{
						PushVisit({ Node.Left, LeftNearT });
						PushVisit({ Node.Right, RightNearT });
					}
				}
				else if (bHitLeft && LeftNearT < ClosestT)
				{
					PushVisit({ Node.Left, LeftNearT });
				}
				else if (bHitRight && RightNearT < ClosestT)
				{
					PushVisit({ Node.Right, RightNearT });
				}
			}
		}

		OutHitResult = {};
		OutHitResult.bHit = bHit;
		if (bHit)
		{
			OutHitResult.Distance = ClosestT;
			OutHitResult.FaceIndex = HitFaceIndex;
		}

		return bHit;
	}

	bool IsBuilt() const
	{
		return !Nodes.empty();
	}

private:
	static float ComputeBoundsSurfaceArea(const FBoundingBox& Bounds)
	{
		if (!Bounds.IsValid())
		{
			return 0.0f;
		}

		const FVector Size = Bounds.Max - Bounds.Min;
		const float X = (std::max)(0.0f, Size.X);
		const float Y = (std::max)(0.0f, Size.Y);
		const float Z = (std::max)(0.0f, Size.Z);
		return 2.0f * (X * Y + Y * Z + Z * X);
	}

	static uint32 GetLeafTriangleThreshold(uint32 TriCount)
	{
		return (TriCount >= FPickingTuning::StaticMeshBVHLargeObjectCutoff())
			? FPickingTuning::StaticMeshBVHLeafCountLarge()
			: FPickingTuning::StaticMeshBVHLeafCountSmall();
	}

	struct FNodeVisit
	{
		int32 NodeIndex = -1;
		float NearT = FLT_MAX;
	};

	int32 BuildNode(uint32 FirstTri, uint32 TriCount, uint32 Depth)
	{
		const int32 NodeIndex = static_cast<int32>(Nodes.size());
		Nodes.emplace_back();

		Nodes[NodeIndex].FirstTri = FirstTri;
		Nodes[NodeIndex].TriCount = TriCount;

		FBoundingBox Bounds;
		for (uint32 i = FirstTri; i < FirstTri + TriCount; ++i)
		{
			Bounds.Expand(Triangles[TriOrder[i]].Bounds.Min);
			Bounds.Expand(Triangles[TriOrder[i]].Bounds.Max);
		}
		Nodes[NodeIndex].Bounds = Bounds;

		const uint32 MaxLeafTriangles = GetLeafTriangleThreshold(TriCount);
		constexpr uint32 MaxDepth = 40;
		if (TriCount <= MaxLeafTriangles || Depth >= MaxDepth)
		{
			return NodeIndex;
		}

		FBoundingBox CentroidBounds;
		for (uint32 i = FirstTri; i < FirstTri + TriCount; ++i)
		{
			CentroidBounds.Expand(Triangles[TriOrder[i]].Centroid);
		}
		const FVector CentroidExtent = CentroidBounds.GetExtent();
		if (CentroidExtent.X < 1e-6f && CentroidExtent.Y < 1e-6f && CentroidExtent.Z < 1e-6f)
		{
			return NodeIndex;
		}

		constexpr int32 BinCount = 12;
		float BestCost = FLT_MAX;
		int32 BestAxis = -1;
		int32 BestSplitBin = -1;
		float BestAxisMin = 0.0f;
		float BestAxisScale = 0.0f;

		const bool bUseSAH = FPickingTuning::UseStaticMeshBVHSAH();
		for (int32 Axis = 0; Axis < 3 && bUseSAH; ++Axis)
		{
			const float AxisMin = CentroidBounds.Min.Data[Axis];
			const float AxisMax = CentroidBounds.Max.Data[Axis];
			const float AxisRange = AxisMax - AxisMin;
			if (AxisRange < 1e-6f)
			{
				continue;
			}

			struct FBin
			{
				uint32 Count = 0;
				FBoundingBox Bounds;
			};
			std::array<FBin, BinCount> Bins;
			const float AxisScale = static_cast<float>(BinCount) / AxisRange;
			for (uint32 i = FirstTri; i < FirstTri + TriCount; ++i)
			{
				const FTriangleRef& Tri = Triangles[TriOrder[i]];
				int32 BinIndex = static_cast<int32>((Tri.Centroid.Data[Axis] - AxisMin) * AxisScale);
				if (BinIndex < 0) BinIndex = 0;
				if (BinIndex >= BinCount) BinIndex = BinCount - 1;

				FBin& Bin = Bins[BinIndex];
				++Bin.Count;
				Bin.Bounds.Expand(Tri.Bounds.Min);
				Bin.Bounds.Expand(Tri.Bounds.Max);
			}

			std::array<uint32, BinCount> PrefixCount = {};
			std::array<FBoundingBox, BinCount> PrefixBounds;
			std::array<uint32, BinCount> SuffixCount = {};
			std::array<FBoundingBox, BinCount> SuffixBounds;

			uint32 RunningPrefixCount = 0;
			FBoundingBox RunningPrefixBounds;
			for (int32 i = 0; i < BinCount; ++i)
			{
				RunningPrefixCount += Bins[i].Count;
				if (Bins[i].Count > 0)
				{
					RunningPrefixBounds.Expand(Bins[i].Bounds.Min);
					RunningPrefixBounds.Expand(Bins[i].Bounds.Max);
				}
				PrefixCount[i] = RunningPrefixCount;
				PrefixBounds[i] = RunningPrefixBounds;
			}

			uint32 RunningSuffixCount = 0;
			FBoundingBox RunningSuffixBounds;
			for (int32 i = BinCount - 1; i >= 0; --i)
			{
				RunningSuffixCount += Bins[i].Count;
				if (Bins[i].Count > 0)
				{
					RunningSuffixBounds.Expand(Bins[i].Bounds.Min);
					RunningSuffixBounds.Expand(Bins[i].Bounds.Max);
				}
				SuffixCount[i] = RunningSuffixCount;
				SuffixBounds[i] = RunningSuffixBounds;
			}

			for (int32 SplitBin = 0; SplitBin < BinCount - 1; ++SplitBin)
			{
				const uint32 LeftCount = PrefixCount[SplitBin];
				const uint32 RightCount = SuffixCount[SplitBin + 1];
				if (LeftCount == 0 || RightCount == 0)
				{
					continue;
				}

				const float LeftArea = ComputeBoundsSurfaceArea(PrefixBounds[SplitBin]);
				const float RightArea = ComputeBoundsSurfaceArea(SuffixBounds[SplitBin + 1]);
				const float Cost = LeftArea * static_cast<float>(LeftCount) + RightArea * static_cast<float>(RightCount);
				if (Cost < BestCost)
				{
					BestCost = Cost;
					BestAxis = Axis;
					BestSplitBin = SplitBin;
					BestAxisMin = AxisMin;
					BestAxisScale = AxisScale;
				}
			}
		}

		uint32 Mid = FirstTri + TriCount / 2;
		if (bUseSAH && BestAxis >= 0 && BestSplitBin >= 0)
		{
			auto MidIt = std::partition(
				TriOrder.begin() + FirstTri,
				TriOrder.begin() + (FirstTri + TriCount),
				[&](uint32 TriIdx)
				{
					int32 BinIndex = static_cast<int32>((Triangles[TriIdx].Centroid.Data[BestAxis] - BestAxisMin) * BestAxisScale);
					if (BinIndex < 0) BinIndex = 0;
					if (BinIndex >= BinCount) BinIndex = BinCount - 1;
					return BinIndex <= BestSplitBin;
				});
			Mid = static_cast<uint32>(MidIt - TriOrder.begin());
		}
		else
		{
			int32 Axis = 0;
			if (CentroidExtent.Y > CentroidExtent.X && CentroidExtent.Y >= CentroidExtent.Z)
			{
				Axis = 1;
			}
			else if (CentroidExtent.Z > CentroidExtent.X && CentroidExtent.Z >= CentroidExtent.Y)
			{
				Axis = 2;
			}

			std::nth_element(
				TriOrder.begin() + FirstTri,
				TriOrder.begin() + Mid,
				TriOrder.begin() + (FirstTri + TriCount),
				[&](uint32 LHS, uint32 RHS)
				{
					return Triangles[LHS].Centroid.Data[Axis] < Triangles[RHS].Centroid.Data[Axis];
				});
		}

		const uint32 LeftCount = Mid - FirstTri;
		const uint32 RightCount = TriCount - LeftCount;
		if (LeftCount == 0 || RightCount == 0)
		{
			return NodeIndex;
		}

		const int32 LeftNode = BuildNode(FirstTri, LeftCount, Depth + 1);
		const int32 RightNode = BuildNode(Mid, RightCount, Depth + 1);
		Nodes[NodeIndex].Left = LeftNode;
		Nodes[NodeIndex].Right = RightNode;
		return NodeIndex;
	}

	bool IntersectTriangleSoA(float RayOx, float RayOy, float RayOz,
		float RayDx, float RayDy, float RayDz,
		uint32 TriIndex,
		float InClosestT,
		bool bBackFaceCull,
		float DetEps,
		float& OutT) const
	{
		const float V0x = TriV0X[TriIndex];
		const float V0y = TriV0Y[TriIndex];
		const float V0z = TriV0Z[TriIndex];
		const float E1x = TriE1X[TriIndex];
		const float E1y = TriE1Y[TriIndex];
		const float E1z = TriE1Z[TriIndex];
		const float E2x = TriE2X[TriIndex];
		const float E2y = TriE2Y[TriIndex];
		const float E2z = TriE2Z[TriIndex];

		const float PVecX = RayDy * E2z - RayDz * E2y;
		const float PVecY = RayDz * E2x - RayDx * E2z;
		const float PVecZ = RayDx * E2y - RayDy * E2x;
		const float Det = E1x * PVecX + E1y * PVecY + E1z * PVecZ;
		if (bBackFaceCull)
		{
			if (Det <= DetEps)
			{
				return false;
			}
		}
		else
		{
			if (std::abs(Det) <= DetEps)
			{
				return false;
			}
		}

		const float InvDet = 1.0f / Det;
		const float TVecX = RayOx - V0x;
		const float TVecY = RayOy - V0y;
		const float TVecZ = RayOz - V0z;
		const float U = (TVecX * PVecX + TVecY * PVecY + TVecZ * PVecZ) * InvDet;
		if (U < 0.0f || U > 1.0f)
		{
			return false;
		}

		const float QVecX = TVecY * E1z - TVecZ * E1y;
		const float QVecY = TVecZ * E1x - TVecX * E1z;
		const float QVecZ = TVecX * E1y - TVecY * E1x;
		const float V = (RayDx * QVecX + RayDy * QVecY + RayDz * QVecZ) * InvDet;
		if (V < 0.0f || U + V > 1.0f)
		{
			return false;
		}

		OutT = (E2x * QVecX + E2y * QVecY + E2z * QVecZ) * InvDet;
		return OutT > 0.0f && OutT < InClosestT;
	}

	TArray<FTriangleRef> Triangles;
	TArray<uint32> TriOrder;
	TArray<uint32> TriFirstIndex;
	TArray<float> TriV0X, TriV0Y, TriV0Z;
	TArray<float> TriE1X, TriE1Y, TriE1Z;
	TArray<float> TriE2X, TriE2Y, TriE2Z;
	TArray<FNode> Nodes;
};
