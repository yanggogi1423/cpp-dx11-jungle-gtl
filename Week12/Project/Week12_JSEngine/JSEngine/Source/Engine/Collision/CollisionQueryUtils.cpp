#include "CollisionQueryUtils.h"

#include <algorithm>
#include <cfloat>
#include <cmath>

static float ComputeOBBDepenetration(const FOBB& Box, const FVector Axes[3], const float Extents[3],
	const FVector& Point, FVector& OutNormal)
{
	const FVector CenterToPoint = Point - Box.Center;
	float Local[3] = {
		FVector::DotProduct(Axes[0], CenterToPoint),
		FVector::DotProduct(Axes[1], CenterToPoint),
		FVector::DotProduct(Axes[2], CenterToPoint)
	};

	float BestDistance = Local[0] + Extents[0];
	FVector BestNormal = -Axes[0];

	for (int32 AxisIndex = 0; AxisIndex < 3; ++AxisIndex)
	{
		const float NegativeFaceDistance = Local[AxisIndex] + Extents[AxisIndex];
		if (NegativeFaceDistance < BestDistance)
		{
			BestDistance = NegativeFaceDistance;
			BestNormal = -Axes[AxisIndex];
		}

		const float PositiveFaceDistance = Extents[AxisIndex] - Local[AxisIndex];
		if (PositiveFaceDistance < BestDistance)
		{
			BestDistance = PositiveFaceDistance;
			BestNormal = Axes[AxisIndex];
		}
	}

	OutNormal = BestNormal.GetSafeNormal();
	return std::max(BestDistance, 0.0f);
}

bool FCollisionQueryUtils::RaycastOBB(const FOBB& Box, const FRay& Ray, bool bInitialOverlapAsHit,
	FHitResult& OutHitResult)
{
	if (!Box.IsValid())
	{
		return false;
	}

	FVector Axes[3];
	Box.GetAxes(Axes[0], Axes[1], Axes[2]);
	const float Extents[3] = { Box.Extents.X, Box.Extents.Y, Box.Extents.Z };
	const FVector RayToCenter = Box.Center - Ray.Origin;

	float TMin = -FLT_MAX;
	float TMax = FLT_MAX;
	FVector EnterNormal = FVector::ZeroVector;
	FVector ExitNormal = FVector::ZeroVector;

	for (int32 AxisIndex = 0; AxisIndex < 3; ++AxisIndex)
	{
		const FVector Axis = Axes[AxisIndex].GetSafeNormal();
		const float ProjectedCenter = FVector::DotProduct(Axis, RayToCenter);
		const float ProjectedDirection = FVector::DotProduct(Axis, Ray.Direction);
		const float Extent = Extents[AxisIndex];

		if (std::fabs(ProjectedDirection) <= 1.0e-6f)
		{
			if (-ProjectedCenter - Extent > 0.0f || -ProjectedCenter + Extent < 0.0f)
			{
				return false;
			}
			continue;
		}

		float NearT = (ProjectedCenter - Extent) / ProjectedDirection;
		float FarT = (ProjectedCenter + Extent) / ProjectedDirection;
		FVector NearNormal = -Axis;
		FVector FarNormal = Axis;

		if (NearT > FarT)
		{
			std::swap(NearT, FarT);
			std::swap(NearNormal, FarNormal);
		}

		if (NearT > TMin)
		{
			TMin = NearT;
			EnterNormal = NearNormal;
		}
		if (FarT < TMax)
		{
			TMax = FarT;
			ExitNormal = FarNormal;
		}
		if (TMin > TMax)
		{
			return false;
		}
	}

	if (TMax < 0.0f)
	{
		return false;
	}

	const bool bStartsInside = TMin < 0.0f;
	if (bStartsInside && bInitialOverlapAsHit)
	{
		FVector HitNormal = FVector::ZeroVector;
		const float PushOutDistance = ComputeOBBDepenetration(Box, Axes, Extents, Ray.Origin, HitNormal);
		OutHitResult.bHit = true;
		OutHitResult.Distance = 0.0f;
		OutHitResult.Location = Ray.Origin + HitNormal * PushOutDistance;
		OutHitResult.ImpactPoint = OutHitResult.Location;
		OutHitResult.Normal = HitNormal;
		OutHitResult.FaceIndex = -1;
		return true;
	}

	const float HitT = bStartsInside ? TMax : TMin;
	const FVector HitNormal = bStartsInside ? ExitNormal : EnterNormal;

	OutHitResult.bHit = true;
	OutHitResult.Distance = HitT;
	OutHitResult.Location = Ray.Origin + Ray.Direction * HitT;
	OutHitResult.ImpactPoint = OutHitResult.Location;
	OutHitResult.Normal = HitNormal.GetSafeNormal();
	OutHitResult.FaceIndex = -1;
	return true;
}

bool FCollisionQueryUtils::RaycastSphere(const FVector& Center, float Radius, const FRay& Ray,
	bool bInitialOverlapAsHit, FHitResult& OutHitResult)
{
	if (Radius <= 0.0f)
	{
		return false;
	}

	const FVector OriginToCenter = Ray.Origin - Center;
	const float A = FVector::DotProduct(Ray.Direction, Ray.Direction);
	const float B = 2.0f * FVector::DotProduct(OriginToCenter, Ray.Direction);
	const float C = FVector::DotProduct(OriginToCenter, OriginToCenter) - Radius * Radius;
	const float Discriminant = B * B - 4.0f * A * C;
	if (Discriminant < 0.0f || A <= 1.0e-6f)
	{
		return false;
	}

	const float SqrtDiscriminant = std::sqrt(Discriminant);
	const float InvDenominator = 1.0f / (2.0f * A);
	const float T0 = (-B - SqrtDiscriminant) * InvDenominator;
	const float T1 = (-B + SqrtDiscriminant) * InvDenominator;

	if (T0 < 0.0f && T1 >= 0.0f && bInitialOverlapAsHit)
	{
		FVector HitNormal = (Ray.Origin - Center).GetSafeNormal();
		if (HitNormal.IsNearlyZero())
		{
			HitNormal = -Ray.Direction.GetSafeNormal();
		}

		OutHitResult.bHit = true;
		OutHitResult.Distance = 0.0f;
		OutHitResult.Location = Center + HitNormal * Radius;
		OutHitResult.ImpactPoint = OutHitResult.Location;
		OutHitResult.Normal = HitNormal;
		OutHitResult.FaceIndex = -1;
		return true;
	}

	float HitT = T0;
	if (HitT < 0.0f)
	{
		HitT = T1;
	}
	if (HitT < 0.0f)
	{
		return false;
	}

	const FVector HitLocation = Ray.Origin + Ray.Direction * HitT;

	OutHitResult.bHit = true;
	OutHitResult.Distance = HitT;
	OutHitResult.Location = HitLocation;
	OutHitResult.ImpactPoint = HitLocation;
	OutHitResult.Normal = (HitLocation - Center).GetSafeNormal();
	OutHitResult.FaceIndex = -1;
	return true;
}

bool FCollisionQueryUtils::RaycastCapsule(const FVector& A, const FVector& B, float Radius, const FRay& Ray,
	bool bInitialOverlapAsHit, FHitResult& OutHitResult)
{
	if (Radius <= 0.0f)
	{
		return false;
	}

	const FVector Segment = B - A;
	const float SegmentLengthSq = FVector::DotProduct(Segment, Segment);
	if (SegmentLengthSq <= 1.0e-6f)
	{
		return RaycastSphere(A, Radius, Ray, bInitialOverlapAsHit, OutHitResult);
	}

	auto SubmitHit = [](const FHitResult& Candidate, bool& bHasBest, FHitResult& BestHit)
	{
		if (!Candidate.bHit)
		{
			return;
		}
		if (!bHasBest || Candidate.Distance < BestHit.Distance)
		{
			BestHit = Candidate;
			bHasBest = true;
		}
	};

	const FVector OriginToA = Ray.Origin - A;
	const float StartSegmentT = std::clamp(
		FVector::DotProduct(OriginToA, Segment) / SegmentLengthSq,
		0.0f,
		1.0f);
	const FVector ClosestAtStart = A + Segment * StartSegmentT;
	const FVector StartToSegment = Ray.Origin - ClosestAtStart;
	if (bInitialOverlapAsHit && StartToSegment.SizeSquared() <= Radius * Radius)
	{
		FVector HitNormal = StartToSegment.GetSafeNormal();
		if (HitNormal.IsNearlyZero())
		{
			HitNormal = -Ray.Direction.GetSafeNormal();
		}

		OutHitResult.bHit = true;
		OutHitResult.Distance = 0.0f;
		OutHitResult.Location = ClosestAtStart + HitNormal * Radius;
		OutHitResult.ImpactPoint = OutHitResult.Location;
		OutHitResult.Normal = HitNormal;
		OutHitResult.FaceIndex = -1;
		return true;
	}

	bool bHasBest = false;
	FHitResult BestHit;

	const float DirectionSegment = FVector::DotProduct(Ray.Direction, Segment) / SegmentLengthSq;
	const float OriginSegment = FVector::DotProduct(OriginToA, Segment) / SegmentLengthSq;
	const FVector PerpDirection = Ray.Direction - Segment * DirectionSegment;
	const FVector PerpOrigin = OriginToA - Segment * OriginSegment;

	const float ACoef = FVector::DotProduct(PerpDirection, PerpDirection);
	const float BCoef = 2.0f * FVector::DotProduct(PerpOrigin, PerpDirection);
	const float CCoef = FVector::DotProduct(PerpOrigin, PerpOrigin) - Radius * Radius;
	if (ACoef > 1.0e-6f)
	{
		const float Discriminant = BCoef * BCoef - 4.0f * ACoef * CCoef;
		if (Discriminant >= 0.0f)
		{
			const float SqrtDiscriminant = std::sqrt(Discriminant);
			const float InvDenominator = 1.0f / (2.0f * ACoef);
			const float CandidateDistances[2] = {
				(-BCoef - SqrtDiscriminant) * InvDenominator,
				(-BCoef + SqrtDiscriminant) * InvDenominator
			};

			for (const float Distance : CandidateDistances)
			{
				if (Distance < 0.0f)
				{
					continue;
				}

				const float SegmentT = OriginSegment + DirectionSegment * Distance;
				if (SegmentT < -1.0e-4f || SegmentT > 1.0001f)
				{
					continue;
				}

				const FVector HitLocation = Ray.Origin + Ray.Direction * Distance;
				const FVector ClosestPoint = A + Segment * std::clamp(SegmentT, 0.0f, 1.0f);
				FVector HitNormal = (HitLocation - ClosestPoint).GetSafeNormal();
				if (HitNormal.IsNearlyZero())
				{
					HitNormal = -Ray.Direction.GetSafeNormal();
				}

				FHitResult CandidateHit;
				CandidateHit.bHit = true;
				CandidateHit.Distance = Distance;
				CandidateHit.Location = HitLocation;
				CandidateHit.ImpactPoint = HitLocation;
				CandidateHit.Normal = HitNormal;
				CandidateHit.FaceIndex = -1;
				SubmitHit(CandidateHit, bHasBest, BestHit);
			}
		}
	}

	FHitResult CapHit;
	if (RaycastSphere(A, Radius, Ray, bInitialOverlapAsHit, CapHit))
	{
		SubmitHit(CapHit, bHasBest, BestHit);
	}
	if (RaycastSphere(B, Radius, Ray, bInitialOverlapAsHit, CapHit))
	{
		SubmitHit(CapHit, bHasBest, BestHit);
	}

	if (!bHasBest)
	{
		return false;
	}

	OutHitResult = BestHit;
	return true;
}

bool FCollisionQueryUtils::IsPointInTriangle(const FVector& Point, const FVector& A, const FVector& B, const FVector& C)
{
	const FVector V0 = C - A;
	const FVector V1 = B - A;
	const FVector V2 = Point - A;

	const float Dot00 = FVector::DotProduct(V0, V0);
	const float Dot01 = FVector::DotProduct(V0, V1);
	const float Dot02 = FVector::DotProduct(V0, V2);
	const float Dot11 = FVector::DotProduct(V1, V1);
	const float Dot12 = FVector::DotProduct(V1, V2);
	const float Denominator = Dot00 * Dot11 - Dot01 * Dot01;
	if (std::fabs(Denominator) <= 1.0e-6f)
	{
		return false;
	}

	const float InvDenominator = 1.0f / Denominator;
	const float U = (Dot11 * Dot02 - Dot01 * Dot12) * InvDenominator;
	const float V = (Dot00 * Dot12 - Dot01 * Dot02) * InvDenominator;
	return U >= -1.0e-4f && V >= -1.0e-4f && (U + V) <= 1.0001f;
}

FVector FCollisionQueryUtils::ClosestPointOnTriangle(const FVector& Point, const FVector& A, const FVector& B,
	const FVector& C)
{
	const FVector AB = B - A;
	const FVector AC = C - A;
	const FVector AP = Point - A;

	const float D1 = FVector::DotProduct(AB, AP);
	const float D2 = FVector::DotProduct(AC, AP);
	if (D1 <= 0.0f && D2 <= 0.0f)
	{
		return A;
	}

	const FVector BP = Point - B;
	const float D3 = FVector::DotProduct(AB, BP);
	const float D4 = FVector::DotProduct(AC, BP);
	if (D3 >= 0.0f && D4 <= D3)
	{
		return B;
	}

	const float VC = D1 * D4 - D3 * D2;
	if (VC <= 0.0f && D1 >= 0.0f && D3 <= 0.0f)
	{
		const float V = D1 / (D1 - D3);
		return A + AB * V;
	}

	const FVector CP = Point - C;
	const float D5 = FVector::DotProduct(AB, CP);
	const float D6 = FVector::DotProduct(AC, CP);
	if (D6 >= 0.0f && D5 <= D6)
	{
		return C;
	}

	const float VB = D5 * D2 - D1 * D6;
	if (VB <= 0.0f && D2 >= 0.0f && D6 <= 0.0f)
	{
		const float W = D2 / (D2 - D6);
		return A + AC * W;
	}

	const float VA = D3 * D6 - D5 * D4;
	if (VA <= 0.0f && (D4 - D3) >= 0.0f && (D5 - D6) >= 0.0f)
	{
		const float W = (D4 - D3) / ((D4 - D3) + (D5 - D6));
		return B + (C - B) * W;
	}

	const float InvDenominator = 1.0f / (VA + VB + VC);
	const float V = VB * InvDenominator;
	const float W = VC * InvDenominator;
	return A + AB * V + AC * W;
}

FVector FCollisionQueryUtils::ChooseNormalOpposingDirection(const FVector& Normal, const FVector& Direction)
{
	return FVector::DotProduct(Direction, Normal) <= 0.0f ? Normal : -Normal;
}

bool FCollisionQueryUtils::RaySphereSweep(const FVector& Start, const FVector& Direction, float SegmentLength,
	const FVector& Center, float Radius, float& OutDistance, FVector& OutNormal)
{
	const FVector M = Start - Center;
	const float B = FVector::DotProduct(M, Direction);
	const float C = FVector::DotProduct(M, M) - Radius * Radius;
	constexpr float InitialOverlapToleranceSq = 1.0e-6f;
	if (C < -InitialOverlapToleranceSq)
	{
		OutDistance = 0.0f;
		OutNormal = M.GetSafeNormal();
		if (OutNormal.IsNearlyZero())
		{
			OutNormal = -Direction;
		}
		return true;
	}

	if (B > 0.0f)
	{
		return false;
	}

	const float Discriminant = B * B - C;
	if (Discriminant < 0.0f)
	{
		return false;
	}

	const float Distance = -B - std::sqrt(Discriminant);
	if (Distance < 0.0f || Distance > SegmentLength)
	{
		return false;
	}

	OutDistance = Distance;
	OutNormal = (Start + Direction * Distance - Center).GetSafeNormal();
	return !OutNormal.IsNearlyZero();
}

bool FCollisionQueryUtils::RayCapsuleEdgeSweep(const FVector& Start, const FVector& Direction, float SegmentLength,
	const FVector& A, const FVector& B, float Radius, float& OutDistance, FVector& OutNormal,
	FVector& OutClosestPoint)
{
	const FVector Edge = B - A;
	const float EdgeLenSq = FVector::DotProduct(Edge, Edge);
	if (EdgeLenSq <= 1.0e-6f)
	{
		return false;
	}

	const FVector M = Start - A;
	const float DirEdge = FVector::DotProduct(Direction, Edge) / EdgeLenSq;
	const float MEdge = FVector::DotProduct(M, Edge) / EdgeLenSq;
	const FVector N = Direction - Edge * DirEdge;
	const FVector Q = M - Edge * MEdge;

	const float ACoef = FVector::DotProduct(N, N);
	const float BCoef = 2.0f * FVector::DotProduct(Q, N);
	const float CCoef = FVector::DotProduct(Q, Q) - Radius * Radius;
	constexpr float InitialOverlapToleranceSq = 1.0e-6f;
	if (CCoef < -InitialOverlapToleranceSq && MEdge >= -1.0e-4f && MEdge <= 1.0001f)
	{
		OutDistance = 0.0f;
		OutClosestPoint = A + Edge * std::clamp(MEdge, 0.0f, 1.0f);
		OutNormal = (Start - OutClosestPoint).GetSafeNormal();
		if (OutNormal.IsNearlyZero())
		{
			OutNormal = -Direction;
		}
		return true;
	}

	if (ACoef <= 1.0e-6f)
	{
		return false;
	}

	const float Discriminant = BCoef * BCoef - 4.0f * ACoef * CCoef;
	if (Discriminant < 0.0f)
	{
		return false;
	}

	const float SqrtDiscriminant = std::sqrt(Discriminant);
	const float InvDenominator = 1.0f / (2.0f * ACoef);
	const float Candidates[2] = {
		(-BCoef - SqrtDiscriminant) * InvDenominator,
		(-BCoef + SqrtDiscriminant) * InvDenominator
	};

	for (float Distance : Candidates)
	{
		if (Distance < 0.0f || Distance > SegmentLength)
		{
			continue;
		}

		const float EdgeT = MEdge + DirEdge * Distance;
		if (EdgeT < -1.0e-4f || EdgeT > 1.0001f)
		{
			continue;
		}

		OutDistance = Distance;
		OutClosestPoint = A + Edge * std::clamp(EdgeT, 0.0f, 1.0f);
		OutNormal = (Start + Direction * Distance - OutClosestPoint).GetSafeNormal();
		return !OutNormal.IsNearlyZero();
	}

	return false;
}

bool FCollisionQueryUtils::IsBetterSweepHit(const FHitResult& Candidate, const FHitResult& CurrentBest,
	bool bHasBest, const FVector& Direction)
{
	if (!bHasBest)
	{
		return true;
	}
	if (Candidate.Distance < CurrentBest.Distance - 1.0e-4f)
	{
		return true;
	}
	if (std::fabs(Candidate.Distance - CurrentBest.Distance) <= 1.0e-4f)
	{
		return FVector::DotProduct(Direction, Candidate.Normal) < FVector::DotProduct(Direction, CurrentBest.Normal);
	}
	return false;
}
