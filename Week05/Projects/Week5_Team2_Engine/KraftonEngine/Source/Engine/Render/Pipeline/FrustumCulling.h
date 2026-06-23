#pragma once
#pragma once

#include "Core/EngineTypes.h"
#include "Math/Matrix.h"

struct FPlane
{
	//	평면 방정식: Normal * X + Distance = 0
	FVector Normal;
	float Distance = 0.0f;

	void Normalize();
	float SignedDistance(const FVector& Point) const;
};

struct FFrustumPlanes
{
	//	Left, Right, Bottom, Top, Near, Far 순서
	FPlane Planes[6];
};

class FViewContext;

class FFrustumCulling
{
public:
	//	ViewProj로 월드 공간 프러스텀 평면 6개를 생성
	static FFrustumPlanes BuildFrustumPlanes(const FMatrix& View, const FMatrix& Proj);
	
	//	AABB가 프러스텀과 교차하면 true (완전 외부면 false)
	static bool IntersectsAABB(const FFrustumPlanes& Frustum, const FBoundingBox& Box);

	/** 
	 * SIMD 가속 프러스텀 컬링 (4개 단위 배치 처리)
	 * @param Frustum 프러스텀 평면 정보
	 * @param MinX~MaxZ 4개 단위로 패딩된 AABB SoA 데이터 포인터
	 * @return 4개 객체의 외부 판정 비트마스크 (1이면 외부, 0이면 내부/교차)
	 */
	static uint32 TestAABB4(const FFrustumPlanes& Frustum, 
		const float* MinX, const float* MinY, const float* MinZ, 
		const float* MaxX, const float* MaxY, const float* MaxZ);
};
