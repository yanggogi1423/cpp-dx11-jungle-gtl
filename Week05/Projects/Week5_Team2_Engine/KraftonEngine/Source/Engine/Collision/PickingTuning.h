#pragma once

#include "Core/CoreTypes.h"
#include "Core/RayTypes.h"
#include "Core/EngineTypes.h"

#include <cfloat>
#include <cmath>
#include <algorithm>
#include <immintrin.h>

struct FPickingTuning
{
	static float& RayParallelEpsilon()
	{
		static float Value = 1e-8f;
		return Value;
	}

	static float& NearTEpsilon()
	{
		static float Value = 1e-4f;
		return Value;
	}

	static float& TriangleDetEpsilon()
	{
		static float Value = 1e-4f;
		return Value;
	}

	static bool& UseBackFaceCull()
	{
		static bool Value = true;
		return Value;
	}

	static bool& UseWorldBVHSAH()
	{
		static bool Value = true;
		return Value;
	}

	static bool& UseStaticMeshBVHSAH()
	{
		static bool Value = true;
		return Value;
	}

	static uint32& BroadLinearVisibleThreshold()
	{
		static uint32 Value = 128u;
		return Value;
	}

	static uint32& WorldBVHLeafCountSmall()
	{
		static uint32 Value = 6u;
		return Value;
	}

	static uint32& WorldBVHLeafCountLarge()
	{
		static uint32 Value = 8u;
		return Value;
	}

	static uint32& WorldBVHLargeObjectCutoff()
	{
		static uint32 Value = 2048u;
		return Value;
	}

	static uint32& StaticMeshBVHLeafCountSmall()
	{
		static uint32 Value = 12u;
		return Value;
	}

	static uint32& StaticMeshBVHLeafCountLarge()
	{
		static uint32 Value = 16u;
		return Value;
	}

	static uint32& StaticMeshBVHLargeObjectCutoff()
	{
		static uint32 Value = 4096u;
		return Value;
	}

	static bool& EnableOpenMP()
	{
		static bool Value = false;
		return Value;
	}

	static uint32& OpenMPMinWorkItems()
	{
		static uint32 Value = 1024u;
		return Value;
	}

	static bool& EnableRayAABBPacketSIMD()
	{
		static bool Value = true;
		return Value;
	}

	static uint32& RayAABBPacketMinCount()
	{
		static uint32 Value = 12u;
		return Value;
	}

	static bool& UseRayOcclusionGate()
	{
		static bool Value = true;
		return Value;
	}
};

struct alignas(16) FRayAABBKernel
{
	FVector Origin = FVector(0, 0, 0);
	FVector InvDir = FVector(0, 0, 0);
	bool bParallelAxis[3] = { true, true, true };
	bool bCanUsePacketSIMD = false;
	__m128 OriginX4 = _mm_setzero_ps();
	__m128 OriginY4 = _mm_setzero_ps();
	__m128 OriginZ4 = _mm_setzero_ps();
	__m128 InvDirX4 = _mm_setzero_ps();
	__m128 InvDirY4 = _mm_setzero_ps();
	__m128 InvDirZ4 = _mm_setzero_ps();

	static FRayAABBKernel Build(const FRay& Ray)
	{
		FRayAABBKernel Kernel;
		const float Eps = FPickingTuning::RayParallelEpsilon();

		Kernel.bParallelAxis[0] = std::abs(Ray.Direction.X) <= Eps;
		Kernel.bParallelAxis[1] = std::abs(Ray.Direction.Y) <= Eps;
		Kernel.bParallelAxis[2] = std::abs(Ray.Direction.Z) <= Eps;
		Kernel.Origin = Ray.Origin;
		Kernel.InvDir.X = Kernel.bParallelAxis[0] ? 0.0f : (1.0f / Ray.Direction.X);
		Kernel.InvDir.Y = Kernel.bParallelAxis[1] ? 0.0f : (1.0f / Ray.Direction.Y);
		Kernel.InvDir.Z = Kernel.bParallelAxis[2] ? 0.0f : (1.0f / Ray.Direction.Z);
		Kernel.bCanUsePacketSIMD = !Kernel.bParallelAxis[0] && !Kernel.bParallelAxis[1] && !Kernel.bParallelAxis[2];
		Kernel.OriginX4 = _mm_set1_ps(Ray.Origin.X);
		Kernel.OriginY4 = _mm_set1_ps(Ray.Origin.Y);
		Kernel.OriginZ4 = _mm_set1_ps(Ray.Origin.Z);
		Kernel.InvDirX4 = _mm_set1_ps(Kernel.InvDir.X);
		Kernel.InvDirY4 = _mm_set1_ps(Kernel.InvDir.Y);
		Kernel.InvDirZ4 = _mm_set1_ps(Kernel.InvDir.Z);
		return Kernel;
	}
};

inline bool IntersectRayAABBNearT(const FRay& /*Ray*/, const FRayAABBKernel& Kernel, const FBoundingBox& Box, float& OutNearT, float& OutFarT)
{
	float TMin = -FLT_MAX;
	float TMax = FLT_MAX;
	const float Ox = Kernel.Origin.X;
	const float Oy = Kernel.Origin.Y;
	const float Oz = Kernel.Origin.Z;

	if (Kernel.bParallelAxis[0])
	{
		if (Ox < Box.Min.X || Ox > Box.Max.X) return false;
	}
	else
	{
		float T1 = (Box.Min.X - Ox) * Kernel.InvDir.X;
		float T2 = (Box.Max.X - Ox) * Kernel.InvDir.X;
		if (T1 > T2) std::swap(T1, T2);
		TMin = (std::max)(TMin, T1);
		TMax = (std::min)(TMax, T2);
		if (TMin > TMax) return false;
	}

	if (Kernel.bParallelAxis[1])
	{
		if (Oy < Box.Min.Y || Oy > Box.Max.Y) return false;
	}
	else
	{
		float T1 = (Box.Min.Y - Oy) * Kernel.InvDir.Y;
		float T2 = (Box.Max.Y - Oy) * Kernel.InvDir.Y;
		if (T1 > T2) std::swap(T1, T2);
		TMin = (std::max)(TMin, T1);
		TMax = (std::min)(TMax, T2);
		if (TMin > TMax) return false;
	}

	if (Kernel.bParallelAxis[2])
	{
		if (Oz < Box.Min.Z || Oz > Box.Max.Z) return false;
	}
	else
	{
		float T1 = (Box.Min.Z - Oz) * Kernel.InvDir.Z;
		float T2 = (Box.Max.Z - Oz) * Kernel.InvDir.Z;
		if (T1 > T2) std::swap(T1, T2);
		TMin = (std::max)(TMin, T1);
		TMax = (std::min)(TMax, T2);
		if (TMin > TMax) return false;
	}

	if (TMax < 0.0f)
	{
		return false;
	}

	OutNearT = (std::max)(0.0f, TMin);
	OutFarT = TMax;
	return true;
}

inline bool IntersectRayAABBNearTMinMax(
	const FRay& /*Ray*/,
	const FRayAABBKernel& Kernel,
	float MinX, float MinY, float MinZ,
	float MaxX, float MaxY, float MaxZ,
	float& OutNearT,
	float& OutFarT)
{
	float TMin = -FLT_MAX;
	float TMax = FLT_MAX;
	const float Ox = Kernel.Origin.X;
	const float Oy = Kernel.Origin.Y;
	const float Oz = Kernel.Origin.Z;

	if (Kernel.bParallelAxis[0])
	{
		if (Ox < MinX || Ox > MaxX) return false;
	}
	else
	{
		float T1 = (MinX - Ox) * Kernel.InvDir.X;
		float T2 = (MaxX - Ox) * Kernel.InvDir.X;
		if (T1 > T2) std::swap(T1, T2);
		TMin = (std::max)(TMin, T1);
		TMax = (std::min)(TMax, T2);
		if (TMin > TMax) return false;
	}

	if (Kernel.bParallelAxis[1])
	{
		if (Oy < MinY || Oy > MaxY) return false;
	}
	else
	{
		float T1 = (MinY - Oy) * Kernel.InvDir.Y;
		float T2 = (MaxY - Oy) * Kernel.InvDir.Y;
		if (T1 > T2) std::swap(T1, T2);
		TMin = (std::max)(TMin, T1);
		TMax = (std::min)(TMax, T2);
		if (TMin > TMax) return false;
	}

	if (Kernel.bParallelAxis[2])
	{
		if (Oz < MinZ || Oz > MaxZ) return false;
	}
	else
	{
		float T1 = (MinZ - Oz) * Kernel.InvDir.Z;
		float T2 = (MaxZ - Oz) * Kernel.InvDir.Z;
		if (T1 > T2) std::swap(T1, T2);
		TMin = (std::max)(TMin, T1);
		TMax = (std::min)(TMax, T2);
		if (TMin > TMax) return false;
	}

	if (TMax < 0.0f)
	{
		return false;
	}

	OutNearT = (std::max)(0.0f, TMin);
	OutFarT = TMax;
	return true;
}

inline uint32 IntersectRayAABBNearTMinMax4(
	const FRay& Ray,
	const FRayAABBKernel& Kernel,
	const float* MinX, const float* MinY, const float* MinZ,
	const float* MaxX, const float* MaxY, const float* MaxZ,
	float MaxNearT,
	float OutNearT4[4])
{
	if (!Kernel.bCanUsePacketSIMD)
	{
		uint32 HitMask = 0u;
		for (uint32 Lane = 0; Lane < 4u; ++Lane)
		{
			float NearT = 0.0f;
			float FarT = 0.0f;
			if (IntersectRayAABBNearTMinMax(
				Ray, Kernel,
				MinX[Lane], MinY[Lane], MinZ[Lane],
				MaxX[Lane], MaxY[Lane], MaxZ[Lane],
				NearT, FarT) && NearT <= MaxNearT)
			{
				OutNearT4[Lane] = NearT;
				HitMask |= (1u << Lane);
			}
		}
		return HitMask;
	}

	const __m128 MinXv = _mm_loadu_ps(MinX);
	const __m128 MinYv = _mm_loadu_ps(MinY);
	const __m128 MinZv = _mm_loadu_ps(MinZ);
	const __m128 MaxXv = _mm_loadu_ps(MaxX);
	const __m128 MaxYv = _mm_loadu_ps(MaxY);
	const __m128 MaxZv = _mm_loadu_ps(MaxZ);

	const __m128 T1X = _mm_mul_ps(_mm_sub_ps(MinXv, Kernel.OriginX4), Kernel.InvDirX4);
	const __m128 T2X = _mm_mul_ps(_mm_sub_ps(MaxXv, Kernel.OriginX4), Kernel.InvDirX4);
	const __m128 T1Y = _mm_mul_ps(_mm_sub_ps(MinYv, Kernel.OriginY4), Kernel.InvDirY4);
	const __m128 T2Y = _mm_mul_ps(_mm_sub_ps(MaxYv, Kernel.OriginY4), Kernel.InvDirY4);
	const __m128 T1Z = _mm_mul_ps(_mm_sub_ps(MinZv, Kernel.OriginZ4), Kernel.InvDirZ4);
	const __m128 T2Z = _mm_mul_ps(_mm_sub_ps(MaxZv, Kernel.OriginZ4), Kernel.InvDirZ4);

	const __m128 TxMin = _mm_min_ps(T1X, T2X);
	const __m128 TxMax = _mm_max_ps(T1X, T2X);
	const __m128 TyMin = _mm_min_ps(T1Y, T2Y);
	const __m128 TyMax = _mm_max_ps(T1Y, T2Y);
	const __m128 TzMin = _mm_min_ps(T1Z, T2Z);
	const __m128 TzMax = _mm_max_ps(T1Z, T2Z);

	__m128 TMin = _mm_max_ps(TxMin, TyMin);
	TMin = _mm_max_ps(TMin, TzMin);
	TMin = _mm_max_ps(TMin, _mm_set1_ps(0.0f));

	__m128 TMax = _mm_min_ps(TxMax, TyMax);
	TMax = _mm_min_ps(TMax, TzMax);

	const __m128 C0 = _mm_cmpge_ps(TMax, TMin);
	const __m128 C1 = _mm_cmpge_ps(TMax, _mm_set1_ps(0.0f));
	const __m128 C2 = _mm_cmple_ps(TMin, _mm_set1_ps(MaxNearT));
	const __m128 Hit = _mm_and_ps(_mm_and_ps(C0, C1), C2);

	_mm_storeu_ps(OutNearT4, TMin);
	return static_cast<uint32>(_mm_movemask_ps(Hit));
}
