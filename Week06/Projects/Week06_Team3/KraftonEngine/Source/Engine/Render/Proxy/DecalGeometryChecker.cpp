#include "Render/Proxy/DecalGeometryChecker.h"

#include "Components/DecalComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Collision/WorldPrimitivePickingBVH.h"
#include "Mesh/StaticMesh.h"
#include "Mesh/StaticMeshAsset.h"
#include "GameFramework/World.h"
#include "Profiling/Stats.h"

#include <cmath>
#include <cfloat>

// ============================================================
// 진입점
// ============================================================

bool FDecalGeometryChecker::HasOverlappingGeometry(UDecalComponent* Decal, const UWorld& World, int32* OutOverlappingObjectCount)
{
  if (OutOverlappingObjectCount)
	{
		*OutOverlappingObjectCount = 0;
	}

	if (!Decal || !Decal->IsVisible())
	{
		return false;
	}

	// ---- 공통 데이터 준비 ----
	const FMatrix DecalModel = Decal->GetTransformIncludingDecalSize();
	const FMatrix InvDecalModel = DecalModel.GetInverse();
	const FBoundingBox DecalWorldAABB = Decal->GetWorldBoundingBox();

	if (!DecalWorldAABB.IsValid())
	{
		return false;
	}

	////// WorldPrimitivePickingBVH는 Editor picking용으로 이미 관리됨.
	////// EnsureBuilt는 dirty일 때만 재빌드하므로 호출 비용이 낮다.
	////World.BuildWorldPrimitivePickingBVHNow();
	//// WorldPrimitivePickingBVH는 RaycastPrimitives와 동일한 방식으로 EnsureBuilt 사용
	//// (dirty일 때만 재빌드하므로 매 프레임 호출해도 비용이 낮다)
	//// RaycastPrimitives: WorldPrimitivePickingBVH.EnsureBuilt(GetActors()) 와 동일
	//{
	//	FHitResult Dummy{};
	//	AActor* DummyActor = nullptr;
	//	// EnsureBuilt를 직접 노출하지 않으므로 RaycastPrimitives의 EnsureBuilt 경로를 활용
	//	// → World.RaycastPrimitives는 내부에서 EnsureBuilt를 호출함
	//	// 대신 GetWorldPrimitivePickingBVH 접근 전에 한 번 호출해두면 충분
	//}
	//// 실제로는 picking BVH가 이미 최신 상태일 가능성이 높으나,
	//// 안전하게 EnsureBuilt 경로를 보장하기 위해 World에 별도 접근자를 추가하는 대신
	//// RaycastPrimitives가 EnsureBuilt를 수행하는 구조를 활용한다.
	//// → DecalGeometryChecker는 BVH가 이미 빌드된 상태를 전제로 동작
	////   (UpdatePerViewport 호출 시점 이전에 WarmupPickingData 또는 RaycastPrimitives가 호출됨)
	//const FWorldPrimitivePickingBVH& WorldBVH = World.GetWorldPrimitivePickingBVH();
	// EnsureBuilt: dirty일 때만 재빌드하므로 매 프레임 호출해도 비용이 낮다
	const FWorldPrimitivePickingBVH& WorldBVH = World.EnsureAndGetWorldPrimitivePickingBVH();

	// ---- Stage 1: Broad Phase ----
	TArray<UStaticMeshComponent*> Candidates;
	{
		SCOPE_STAT_CAT("Decal.BroadPhase", "3_Collect");
		GatherBroadPhaseCandidates(DecalWorldAABB, WorldBVH, Candidates);
	}
	if (Candidates.empty())
	{
		return false;
	}

	if (OutOverlappingObjectCount)
	{
     SCOPE_STAT_CAT("Decal.NarrowPhase", "3_Collect");

		int32 OverlapCount = 0;
		for (UStaticMeshComponent* Candidate : Candidates)
		{
			if (HasOverlapWithCandidate(Candidate, DecalWorldAABB, InvDecalModel))
			{
				++OverlapCount;
			}
		}

		*OutOverlappingObjectCount = OverlapCount;
		return OverlapCount > 0;
	}

	{
		SCOPE_STAT_CAT("Decal.NarrowPhase", "3_Collect");

		// ---- Stage 2: Narrow Phase (BVH 삼각형 수집) ----
		TArray<FVector> WorldTriVerts; // [V0,V1,V2, V0,V1,V2, ...]
		GatherBVHFilteredTriangles(Candidates, DecalWorldAABB, WorldTriVerts);
		if (WorldTriVerts.empty())
		{
			return false;
		}

		// ---- Stage 3: Decal 로컬 공간 변환 ----
		TArray<FVector> LocalTriVerts;
		TransformTrianglesToDecalLocal(WorldTriVerts, InvDecalModel, LocalTriVerts);

		// ---- Stage 4: Coarse AABB 필터 ----
		TArray<int32> CoarseTriIndices;
		GatherCoarseOverlapTriangles(LocalTriVerts, CoarseTriIndices);
		if (CoarseTriIndices.empty())
		{
			return false;
		}

		// ---- Stage 5: SAT 정밀 판별 ----
		return GatherSATOverlapTriangles(LocalTriVerts, CoarseTriIndices);
	}
}

bool FDecalGeometryChecker::HasOverlapWithCandidate(
	UStaticMeshComponent* Candidate,
	const FBoundingBox& DecalWorldAABB,
	const FMatrix& InvDecalModel)
{
	if (!Candidate)
	{
		return false;
	}

	UStaticMesh* StaticMesh = Candidate->GetStaticMesh();
	if (!StaticMesh)
	{
		return false;
	}

	StaticMesh->EnsureMeshTrianglePickingBVHBuilt();

	const FStaticMesh* MeshAsset = StaticMesh->GetStaticMeshAsset();
	if (!MeshAsset || MeshAsset->Vertices.empty())
	{
		return false;
	}

	const FMatrix& InvMeshWorld = Candidate->GetWorldInverseMatrix();

	FVector WorldCorners[8];
	DecalWorldAABB.GetCorners(WorldCorners);

	FBoundingBox MeshLocalBox;
	for (const FVector& Corner : WorldCorners)
	{
		MeshLocalBox.Expand(InvMeshWorld.TransformPositionWithW(Corner));
	}

	TArray<FVector> MeshLocalTriVerts;
	StaticMesh->QueryMeshTrianglesInBox(MeshLocalBox, MeshLocalTriVerts);
	if (MeshLocalTriVerts.empty())
	{
		return false;
	}

	const FMatrix& MeshWorld = Candidate->GetWorldMatrix();
	TArray<FVector> LocalTriVerts;
	LocalTriVerts.reserve(MeshLocalTriVerts.size());
	for (const FVector& V : MeshLocalTriVerts)
	{
		const FVector WorldV = MeshWorld.TransformPositionWithW(V);
		LocalTriVerts.push_back(InvDecalModel.TransformPositionWithW(WorldV));
	}

	TArray<int32> CoarseTriIndices;
	GatherCoarseOverlapTriangles(LocalTriVerts, CoarseTriIndices);
	if (CoarseTriIndices.empty())
	{
		return false;
	}

	return GatherSATOverlapTriangles(LocalTriVerts, CoarseTriIndices);
}

// ============================================================
// Stage 1: Broad Phase — WorldPrimitivePickingBVH AABB 쿼리
// ============================================================

void FDecalGeometryChecker::GatherBroadPhaseCandidates(
	const FBoundingBox& DecalWorldAABB,
	const FWorldPrimitivePickingBVH& WorldBVH,
	TArray<UStaticMeshComponent*>& OutCandidates)
{
	WorldBVH.QueryAABB(DecalWorldAABB, OutCandidates);
}

// ============================================================
// Stage 2: Narrow Phase — 각 메시 로컬 BVH에서 삼각형 수집
// ============================================================

void FDecalGeometryChecker::GatherBVHFilteredTriangles(
	const TArray<UStaticMeshComponent*>& Candidates,
	const FBoundingBox& DecalWorldAABB,
	TArray<FVector>& OutWorldTriVerts)
{
	for (UStaticMeshComponent* Comp : Candidates)
	{
		if (!Comp)
		{
			continue;
		}

		UStaticMesh* StaticMesh = Comp->GetStaticMesh();
		if (!StaticMesh)
		{
			continue;
		}

		// 메시 BVH가 아직 빌드되지 않았으면 빌드
		StaticMesh->EnsureMeshTrianglePickingBVHBuilt();

		const FStaticMesh* MeshAsset = StaticMesh->GetStaticMeshAsset();
		if (!MeshAsset || MeshAsset->Vertices.empty())
		{
			continue;
		}

		// Decal World AABB → 메시 로컬 공간으로 변환
		// 메시 로컬 AABB를 구하기 위해 8개 코너를 역행렬로 변환
		const FMatrix& InvMeshWorld = Comp->GetWorldInverseMatrix();

		FVector WorldCorners[8];
		DecalWorldAABB.GetCorners(WorldCorners);

		FBoundingBox MeshLocalBox;
		for (const FVector& C : WorldCorners)
		{
			MeshLocalBox.Expand(InvMeshWorld.TransformPositionWithW(C));
		}

		//// 메시 로컬 BVH에서 해당 AABB와 겹치는 삼각형 정점 수집 (로컬 공간)
		//TArray<FVector> LocalVerts;
		//StaticMesh->RaycastMeshTrianglesWithBVHLocal; // 이 함수는 Raycast 전용

		//// MeshTrianglePickingBVH에 직접 접근 (QueryAABBLocal 사용)
		//// UStaticMesh는 BVH를 mutable 멤버로 가지므로 const_cast 불필요 (EnsureBuilt가 mutable)
		//// StaticMesh->QueryMeshTrianglesInBox를 사용
		//// → StaticMesh.h에 아직 없으므로 직접 BVH를 노출하거나 래퍼를 추가해야 함
		//// 현재 구조: UStaticMesh::EnsureMeshTrianglePickingBVHBuilt() 는 있으나
		////            QueryAABBLocal 래퍼가 없음 → UStaticMesh에 추가

		// 메시 로컬 BVH에서 Decal AABB와 겹치는 삼각형 정점 수집 (메시 로컬 공간)
		TArray<FVector> MeshLocalTriVerts;
		StaticMesh->QueryMeshTrianglesInBox(MeshLocalBox, MeshLocalTriVerts);

		if (MeshLocalTriVerts.empty())
		{
			continue;
		}

		// 메시 로컬 → 월드 공간 변환
		const FMatrix& MeshWorld = Comp->GetWorldMatrix();
		for (const FVector& V : MeshLocalTriVerts)
		{
			OutWorldTriVerts.push_back(MeshWorld.TransformPositionWithW(V));
		}
	}
}

// ============================================================
// Stage 3: 월드 → Decal 로컬 공간 변환
// ============================================================

void FDecalGeometryChecker::TransformTrianglesToDecalLocal(
	const TArray<FVector>& WorldTriVerts,
	const FMatrix& InvDecalModel,
	TArray<FVector>& OutLocalTriVerts)
{
	OutLocalTriVerts.reserve(WorldTriVerts.size());
	for (const FVector& V : WorldTriVerts)
	{
		OutLocalTriVerts.push_back(InvDecalModel.TransformPositionWithW(V));
	}
}

// ============================================================
// Stage 4: Coarse AABB 필터
// 삼각형 AABB vs [-0.5, 0.5]^3
// ============================================================

void FDecalGeometryChecker::GatherCoarseOverlapTriangles(
	const TArray<FVector>& LocalTriVerts,
	TArray<int32>& OutPassedTriIndices)
{
	static const FBoundingBox UnitBox(FVector(-0.5f, -0.5f, -0.5f), FVector(0.5f, 0.5f, 0.5f));

	const int32 TriCount = static_cast<int32>(LocalTriVerts.size()) / 3;
	for (int32 i = 0; i < TriCount; ++i)
	{
		const FVector& V0 = LocalTriVerts[i * 3 + 0];
		const FVector& V1 = LocalTriVerts[i * 3 + 1];
		const FVector& V2 = LocalTriVerts[i * 3 + 2];

		FBoundingBox TriBox;
		TriBox.Expand(V0);
		TriBox.Expand(V1);
		TriBox.Expand(V2);

		if (UnitBox.IsIntersected(TriBox))
		{
			OutPassedTriIndices.push_back(i);
		}
	}
}

// ============================================================
// Stage 5: SAT 정밀 교차 판별
// ============================================================

bool FDecalGeometryChecker::GatherSATOverlapTriangles(
	const TArray<FVector>& LocalTriVerts,
	const TArray<int32>& CoarseTriIndices)
{
	for (int32 TriIdx : CoarseTriIndices)
	{
		const FVector& V0 = LocalTriVerts[TriIdx * 3 + 0];
		const FVector& V1 = LocalTriVerts[TriIdx * 3 + 1];
		const FVector& V2 = LocalTriVerts[TriIdx * 3 + 2];

		if (SATTriangleVsUnitOBB(V0, V1, V2))
		{
			return true; // 하나라도 겹치면 즉시 반환
		}
	}
	return false;
}

// ============================================================
// SAT 헬퍼 구현
// ============================================================

void FDecalGeometryChecker::ProjectTriangle(
	const FVector& Axis,
	const FVector& V0, const FVector& V1, const FVector& V2,
	float& OutMin, float& OutMax)
{
	const float d0 = Axis.Dot(V0);
	const float d1 = Axis.Dot(V1);
	const float d2 = Axis.Dot(V2);
	OutMin = std::min({ d0, d1, d2 });
	OutMax = std::max({ d0, d1, d2 });
}

float FDecalGeometryChecker::OBBProjectionRadius(const FVector& Axis)
{
	// 단위 OBB([-0.5,0.5]^3)의 축 투영 반경
	// r = 0.5 * (|Ax| + |Ay| + |Az|)
	return 0.5f * (std::abs(Axis.X) + std::abs(Axis.Y) + std::abs(Axis.Z));
}

bool FDecalGeometryChecker::SATTriangleVsUnitOBB(
	const FVector& V0, const FVector& V1, const FVector& V2)
{
	// 삼각형 엣지
	const FVector E0 = V1 - V0;
	const FVector E1 = V2 - V1;
	const FVector E2 = V0 - V2;

	// OBB 주축 (단위 큐브이므로 좌표축)
	const FVector OBBAxes[3] = {
		FVector(1.f, 0.f, 0.f),
		FVector(0.f, 1.f, 0.f),
		FVector(0.f, 0.f, 1.f)
	};
	const FVector TriEdges[3] = { E0, E1, E2 };

	// ---- 축 1~3: OBB 면 법선 (= 좌표축) ----
	// 이 테스트는 Stage4 AABB 필터와 동일하지만, 분리가 확실하지 않은 경우 보완
	for (int32 i = 0; i < 3; ++i)
	{
		float TMin, TMax;
		ProjectTriangle(OBBAxes[i], V0, V1, V2, TMin, TMax);
		const float R = 0.5f; // 단위 큐브 반경
		if (TMax < -R || TMin > R)
		{
			return false; // 분리됨
		}
	}

	// ---- 축 4: 삼각형 법선 ----
	{
		FVector TriNormal = E0.Cross(E1);
		const float Len = TriNormal.Dot(TriNormal);
		if (Len > 1e-12f)
		{
			TriNormal = TriNormal * (1.f / std::sqrt(Len));

			float TMin, TMax;
			ProjectTriangle(TriNormal, V0, V1, V2, TMin, TMax);
			const float R = OBBProjectionRadius(TriNormal);
			if (TMax < -R || TMin > R)
			{
				return false;
			}
		}
		// 법선이 0벡터(degenerate 삼각형)면 이 축은 건너뜀
	}

	// ---- 축 5~13: 엣지 크로스 곱 (OBBEdge_i × TriEdge_j) ----
	for (int32 i = 0; i < 3; ++i)
	{
		for (int32 j = 0; j < 3; ++j)
		{
			FVector Axis = OBBAxes[i].Cross(TriEdges[j]);
			const float Len = Axis.Dot(Axis);
			if (Len < 1e-12f)
			{
				continue; // 평행한 엣지 — 분리축으로 쓸 수 없음, 건너뜀
			}
			Axis = Axis * (1.f / std::sqrt(Len));

			float TMin, TMax;
			ProjectTriangle(Axis, V0, V1, V2, TMin, TMax);
			const float R = OBBProjectionRadius(Axis);
			if (TMax < -R || TMin > R)
			{
				return false; // 분리됨
			}
		}
	}

	// 13개 축 모두에서 분리되지 않음 → 교차
	return true;
}