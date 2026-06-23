#include "BoxComponent.h" 
#include "Collision/CollisionQueryUtils.h"
#include "Object/Object.h"

bool UBoxComponent::RaycastMesh(const FRay& Ray, FHitResult& OutHitResult)
{
	const bool bHit = FCollisionQueryUtils::RaycastOBB(GetWorldOBB(), Ray, false, OutHitResult);
	if (bHit)
	{
		OutHitResult.HitComponent = this;
	}
	return bHit;
}

bool UBoxComponent::SweepMesh(const FVector& Start, const FVector& End, const FQuat& ShapeWorldRotation,
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

	FOBB InflatedBox = GetWorldOBB();
	InflatedBox.Extents += FVector(Shape.Radius, Shape.Radius, Shape.Radius);

	const bool bHit = FCollisionQueryUtils::RaycastOBB(
		InflatedBox, FRay(Start, Delta / SegmentLength), true, OutHitResult);
	if (bHit)
	{
		OutHitResult.HitComponent = this;
		OutHitResult.ImpactPoint = OutHitResult.Location - OutHitResult.Normal.GetSafeNormal() * Shape.Radius;
	}
	return bHit;
}

EPrimitiveType UBoxComponent::GetPrimitiveType() const
{
	return EPrimitiveType::EPT_Box;
}
