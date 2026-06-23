#include "Render/Pipeline/FrustumCulling.h"
#include "Render/Pipeline/ViewContext.h"
#include "Render/Pipeline/PrimitiveProxy.h"
#include "Component/PrimitiveComponent.h"
#include <algorithm>
#include <cmath>
#include <intrin.h>

void FPlane::Normalize()
{
	//	거리 계산 안정성을 위해 평면 법선을 단위 벡터로 정규화
	const float LenSq = Normal.Dot(Normal);
	if (LenSq <= 1e-8f)
	{
		return;
	}

	const float InvLen = 1.0f / std::sqrt(LenSq);
	Normal *= InvLen;
	Distance *= InvLen;
}

float FPlane::SignedDistance(const FVector& Point) const
{
	return Normal.Dot(Point) + Distance;
}

FFrustumPlanes FFrustumCulling::BuildFrustumPlanes(const FMatrix& View, const FMatrix& Proj)
{
	//	현재 엔진의 행렬 방향에 맞춰 View * Proj 기준으로 평면 추출
	const FMatrix VP = View * Proj;
	FFrustumPlanes Frustum = {};

	Frustum.Planes[0].Normal = FVector(VP.M[0][3] + VP.M[0][0], VP.M[1][3] + VP.M[1][0], VP.M[2][3] + VP.M[2][0]);
	Frustum.Planes[0].Distance = VP.M[3][3] + VP.M[3][0];

	Frustum.Planes[1].Normal = FVector(VP.M[0][3] - VP.M[0][0], VP.M[1][3] - VP.M[1][0], VP.M[2][3] - VP.M[2][0]);
	Frustum.Planes[1].Distance = VP.M[3][3] - VP.M[3][0];

	Frustum.Planes[2].Normal = FVector(VP.M[0][3] + VP.M[0][1], VP.M[1][3] + VP.M[1][1], VP.M[2][3] + VP.M[2][1]);
	Frustum.Planes[2].Distance = VP.M[3][3] + VP.M[3][1];

	Frustum.Planes[3].Normal = FVector(VP.M[0][3] - VP.M[0][1], VP.M[1][3] - VP.M[1][1], VP.M[2][3] - VP.M[2][1]);
	Frustum.Planes[3].Distance = VP.M[3][3] - VP.M[3][1];

	Frustum.Planes[4].Normal = FVector(VP.M[0][2], VP.M[1][2], VP.M[2][2]);
	Frustum.Planes[4].Distance = VP.M[3][2];

	Frustum.Planes[5].Normal = FVector(VP.M[0][3] - VP.M[0][2], VP.M[1][3] - VP.M[1][2], VP.M[2][3] - VP.M[2][2]);
	Frustum.Planes[5].Distance = VP.M[3][3] - VP.M[3][2];

	// 평면의 inside 방향을 안정화: 카메라 전방의 한 점이 항상 inside(>=0)가 되도록 보정
	const FMatrix InvView = View.GetInverseFast();
	const FVector CameraPos = InvView.GetLocation();
	FVector CameraForward = InvView.TransformVector(FVector(0.0f, 0.0f, 1.0f));
	CameraForward.Normalize();

	// Near/Far 평면 거리 계산 (Reversed-Z 대응)
	// Standard: -M[3][2]/M[2][2] = Near, M[3][2]/(1-M[2][2]) = Far
	// Reversed: -M[3][2]/M[2][2] = Far,  M[3][2]/(1-M[2][2]) = Near
	float Z1 = (std::fabsf(Proj.M[2][2]) > 1e-6f) ? (-Proj.M[3][2] / Proj.M[2][2]) : 0.1f;
	float Z2 = (std::fabsf(1.0f - Proj.M[2][2]) > 1e-6f) ? (Proj.M[3][2] / (1.0f - Proj.M[2][2])) : 1000.0f;
	
	const float ActualNearZ = std::min(std::max(0.01f, std::min(Z1, Z2)), 10.0f);
	const float ActualFarZ = std::max(ActualNearZ + 1.0f, std::max(Z1, Z2));
	
	// 중간 지점은 항상 내부
	const FVector InsidePoint = CameraPos + CameraForward * (ActualNearZ + ActualFarZ) * 0.5f;

	for (FPlane& Plane : Frustum.Planes)
	{
		//	추출한 평면은 길이가 다를 수 있으므로 모두 정규화
		Plane.Normalize();
		if (Plane.SignedDistance(InsidePoint) < 0.0f)
		{
			Plane.Normal *= -1.0f;
			Plane.Distance *= -1.0f;
		}
	}

	return Frustum;
}

bool FFrustumCulling::IntersectsAABB(const FFrustumPlanes& Frustum, const FBoundingBox& Box)
{
	if (!Box.IsValid())
	{
		return false;
	}

	//	각 평면 기준 Positive Vertex 검사로 빠르게 outside 판정
	for (const FPlane& Plane : Frustum.Planes)
	{
		const FVector Positive(
			(Plane.Normal.X >= 0.0f) ? Box.Max.X : Box.Min.X,
			(Plane.Normal.Y >= 0.0f) ? Box.Max.Y : Box.Min.Y,
			(Plane.Normal.Z >= 0.0f) ? Box.Max.Z : Box.Min.Z);

		if (Plane.SignedDistance(Positive) < 0.0f)
		{
			return false;
		}
	}

	return true;
}

uint32 FFrustumCulling::TestAABB4(const FFrustumPlanes& Frustum,
	const float* MinX, const float* MinY, const float* MinZ,
	const float* MaxX, const float* MaxY, const float* MaxZ)
{
	// 평면 데이터를 128비트 레지스터로 로드 (미리 정규화된 평면들)
	__m128 PlaneX[6], PlaneY[6], PlaneZ[6], PlaneW[6];
	for (int p = 0; p < 6; ++p)
	{
		PlaneX[p] = _mm_set1_ps(Frustum.Planes[p].Normal.X);
		PlaneY[p] = _mm_set1_ps(Frustum.Planes[p].Normal.Y);
		PlaneZ[p] = _mm_set1_ps(Frustum.Planes[p].Normal.Z);
		PlaneW[p] = _mm_set1_ps(Frustum.Planes[p].Distance);
	}

	// SoA로부터 4개 AABB의 Min/Max 로드
	__m128 minX = _mm_loadu_ps(MinX);
	__m128 minY = _mm_loadu_ps(MinY);
	__m128 minZ = _mm_loadu_ps(MinZ);
	__m128 maxX = _mm_loadu_ps(MaxX);
	__m128 maxY = _mm_loadu_ps(MaxY);
	__m128 maxZ = _mm_loadu_ps(MaxZ);

	__m128 outMask = _mm_setzero_ps(); // bit set means OUTSIDE

	for (int p = 0; p < 6; ++p)
	{
		// p-vertex: 평면 노멀 방향에 따른 AABB의 가장 먼 점 추출 (SIMD blend 활용)
		// Normal이 양수면 Max, 음수면 Min 선택
		__m128 px = _mm_blendv_ps(minX, maxX, _mm_cmpgt_ps(PlaneX[p], _mm_setzero_ps()));
		__m128 py = _mm_blendv_ps(minY, maxY, _mm_cmpgt_ps(PlaneY[p], _mm_setzero_ps()));
		__m128 pz = _mm_blendv_ps(minZ, maxZ, _mm_cmpgt_ps(PlaneZ[p], _mm_setzero_ps()));

		// dist = Plane.Normal * P + Plane.Distance
		__m128 dist = _mm_add_ps(_mm_add_ps(_mm_mul_ps(px, PlaneX[p]), _mm_mul_ps(py, PlaneY[p])),
			_mm_add_ps(_mm_mul_ps(pz, PlaneZ[p]), PlaneW[p]));

		// dist < 0이면 평면 외부에 있는 것으로 간주
		outMask = _mm_or_ps(outMask, _mm_cmplt_ps(dist, _mm_setzero_ps()));
	}

	// 4비트 마스크 리턴 (각 비트가 1이면 해당 AABB는 외부)
	return (uint32)_mm_movemask_ps(outMask);
}
