#include "CapsuleComponent.h"
#include "Collision/CollisionQueryUtils.h"
#include "Object/Object.h"


void UCapsuleComponent::UpdateWorldAABB() const
{
	FTransform T = GetWorldTransform();

	FVector Center = T.GetLocation();
	FVector Axis = T.GetUnitAxis(EAxis::Z);

	float HalfHeight = GetScaledCapsuleHalfHeight();
	float Radius = GetScaledCapsuleRadius();

	FVector A = Center + Axis * HalfHeight;
	FVector B = Center - Axis * HalfHeight;

	FVector Min(
		std::min(A.X, B.X),
		std::min(A.Y, B.Y),
		std::min(A.Z, B.Z));

	FVector Max(
		std::max(A.X, B.X),
		std::max(A.Y, B.Y),
		std::max(A.Z, B.Z));

	Min -= FVector(Radius, Radius, Radius);
	Max += FVector(Radius, Radius, Radius);

	WorldAABB.Min = Min;
	WorldAABB.Max = Max;
}

bool UCapsuleComponent::RaycastMesh(const FRay& Ray, FHitResult& OutHitResult)
{
	const FTransform& Transform = GetWorldTransform();
	const FVector Center = Transform.GetLocation();
	const FVector Axis = Transform.GetUnitAxis(EAxis::Z);
	const float HalfHeight = GetScaledCapsuleHalfHeight();
	const float Radius = GetScaledCapsuleRadius();
	const FVector A = Center - Axis * HalfHeight;
	const FVector B = Center + Axis * HalfHeight;

	const bool bHit = FCollisionQueryUtils::RaycastCapsule(A, B, Radius, Ray, false, OutHitResult);
	if (bHit)
	{
		OutHitResult.HitComponent = this;
	}
	return bHit;
}

bool UCapsuleComponent::SweepMesh(const FVector& Start, const FVector& End, const FQuat& ShapeWorldRotation,
	const FCollisionShape& Shape, FHitResult& OutHitResult)
{
	(void)ShapeWorldRotation;

	if (!Shape.IsSphere() || Shape.Radius < 0.0f)
	{
		return false;
	}

	const FVector Delta = End - Start;
	const float SegmentLength = Delta.Size();
	if (SegmentLength <= 1.0e-6f)
	{
		return false;
	}

	const FTransform& Transform = GetWorldTransform();
	const FVector Center = Transform.GetLocation();
	const FVector Axis = Transform.GetUnitAxis(EAxis::Z);
	const float HalfHeight = GetScaledCapsuleHalfHeight();
	const float InflatedRadius = GetScaledCapsuleRadius() + Shape.Radius;
	const FVector A = Center - Axis * HalfHeight;
	const FVector B = Center + Axis * HalfHeight;

	const bool bHit = FCollisionQueryUtils::RaycastCapsule(
		A,
		B,
		InflatedRadius,
		FRay(Start, Delta / SegmentLength),
		true,
		OutHitResult);
	if (bHit)
	{
		OutHitResult.HitComponent = this;
		OutHitResult.ImpactPoint = OutHitResult.Location - OutHitResult.Normal.GetSafeNormal() * Shape.Radius;
	}
	return bHit;
}

EPrimitiveType UCapsuleComponent::GetPrimitiveType() const
{
	return EPrimitiveType::EPT_Capsule;
}
