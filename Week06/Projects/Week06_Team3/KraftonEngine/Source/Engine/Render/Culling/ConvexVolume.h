#pragma once

#include "Engine/Math/Vector.h"
#include "Engine/Math/Matrix.h"

struct FBoundingBox;

enum class EAABBFrustumClassify : int
{
	Outside,
	Intersects,
	Contains,
};

struct FConvexVolume
{
public:
	void UpdateFromMatrix(const FMatrix& InViewProjectionMatrix);
	bool IntersectAABB(const FBoundingBox& Box) const;
	// Returns true if the AABB is completely inside all 6 frustum planes
	bool ContainsAABB(const FBoundingBox& Box) const;

	// OBBWorldTransform: 단위 큐브[-0.5, 0.5]를 월드 공간으로 변환하는 행렬
	// SAT: 8개 OBB 코너를 6개 절두체 평면에 댛래 테스트 - 어떤 평면에서 모든 코너가 외부면 false
	// @@@@@@ 이거 frustum culling 좀 더 최적화 있음 (Week5 확인해보셈)
	// @@@@@@ 이거 8개 점 전부 검사 말고 2점만 검사하는 방법 있음.
	bool IntersectOBB(const FMatrix& OBBWorldTransform) const;
	
	EAABBFrustumClassify ClassifyAABB(const FBoundingBox& Box) const;
private:
	FVector4 Planes[6];
};
