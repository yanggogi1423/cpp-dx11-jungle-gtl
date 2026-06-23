#pragma once

#include "Engine/Core/CoreTypes.h"
#include "Engine/Core/EngineTypes.h"
#include "Engine/Math/Vector.h"
#include "Engine/Math/Matrix.h"

class UDecalComponent;
class UStaticMeshComponent;
class UWorld;
class FWorldPrimitivePickingBVH;

/**
 * FDecalGeometryChecker
 *
 * Decal이 실제 씬 지오메트리와 겹치는지를 CPU에서 단계적으로 판별한다.
 *
 * Stage 1: BVH Broad Phase  — WorldPrimitivePickingBVH::QueryAABB
 *           Decal World AABB 근처의 StaticMeshComponent 수집
 *
 * Stage 2: BVH Narrow Phase — MeshTrianglePickingBVH::QueryAABBLocal
 *           각 메시의 로컬 BVH에서 Decal AABB 내 삼각형 수집
 *
 * Stage 3: 로컬 변환
 *           수집된 삼각형을 InvDecalModel로 Decal 로컬 공간([-0.5,0.5])으로 변환
 *
 * Stage 4: Coarse AABB 필터
 *           삼각형 AABB vs 단위 큐브 [-0.5,0.5]^3 빠른 사전 제거
 *
 * Stage 5: SAT 정밀 교차 판별 (13축)
 *           OBB(단위 큐브)-삼각형 Separating Axis Theorem
 *           — 겹치는 삼각형이 하나라도 있으면 true 반환
 */
class FDecalGeometryChecker
{
public:
	/**
	 * Stage 1~5를 순차 실행.
	 * @param Decal     판별 대상 DecalComponent
	 * @param World     씬 월드 (BVH EnsureBuilt 포함)
	 * @return          실제로 겹치는 삼각형이 있으면 true, 없으면 false
	 */
	bool HasOverlappingGeometry(UDecalComponent* Decal, const UWorld& World, int32* OutOverlappingObjectCount = nullptr);

private:
	// ---------- Stage 1 ----------
	// WorldPrimitivePickingBVH AABB 쿼리로 Decal World AABB 근처 메시 수집
	void GatherBroadPhaseCandidates(
		const FBoundingBox& DecalWorldAABB,
		const FWorldPrimitivePickingBVH& WorldBVH,
		TArray<UStaticMeshComponent*>& OutCandidates);

	// ---------- Stage 2 ----------
	// 각 메시의 로컬 BVH에서 Decal AABB 내 삼각형 정점을 월드 공간으로 수집
	// OutWorldTriVerts: [V0, V1, V2, V0, V1, V2, ...] 형태 (3개씩)
	void GatherBVHFilteredTriangles(
		const TArray<UStaticMeshComponent*>& Candidates,
		const FBoundingBox& DecalWorldAABB,
		TArray<FVector>& OutWorldTriVerts);

	// ---------- Stage 3 ----------
	// 월드 공간 삼각형 정점을 Decal 로컬 공간으로 일괄 변환
	void TransformTrianglesToDecalLocal(
		const TArray<FVector>& WorldTriVerts,
		const FMatrix& InvDecalModel,
		TArray<FVector>& OutLocalTriVerts);

	// ---------- Stage 4 ----------
	// 삼각형 AABB vs 단위 큐브 사전 필터 — 통과한 삼각형 인덱스만 남김
	void GatherCoarseOverlapTriangles(
		const TArray<FVector>& LocalTriVerts,
		TArray<int32>& OutPassedTriIndices);

	// ---------- Stage 5 ----------
	// SAT 13축 정밀 교차 판별
	// 겹치는 삼각형이 하나라도 있으면 true
	bool GatherSATOverlapTriangles(
		const TArray<FVector>& LocalTriVerts,
		const TArray<int32>& CoarseTriIndices);

	bool HasOverlapWithCandidate(
		UStaticMeshComponent* Candidate,
		const FBoundingBox& DecalWorldAABB,
		const FMatrix& InvDecalModel);

	// SAT 헬퍼: 단위 OBB([-0.5,0.5]^3)와 삼각형의 SAT 13축 검사
	static bool SATTriangleVsUnitOBB(const FVector& V0, const FVector& V1, const FVector& V2);

	// 분리축에 삼각형을 투영해 [min, max] 반환
	static void ProjectTriangle(const FVector& Axis,
		const FVector& V0, const FVector& V1, const FVector& V2,
		float& OutMin, float& OutMax);

	// 단위 OBB를 주어진 축에 투영한 반경 (|Ax| + |Ay| + |Az|) * 0.5
	static float OBBProjectionRadius(const FVector& Axis);
};