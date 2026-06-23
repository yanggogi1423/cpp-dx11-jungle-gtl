#include "SphereComponent.h"
#include "Collision/CollisionQueryUtils.h"
#include "Object/Object.h"

void USphereComponent::PostDuplicate(UObject* Original)
{
	UShapeComponent::PostDuplicate(Original);

	USphereComponent* SphereComp = Cast<USphereComponent>(Original);
	SphereRadius = SphereComp->SphereRadius;
}


void USphereComponent::UpdateWorldAABB() const
{
	const FVector Center = GetWorldLocation();

	const float ScaledRadius = GetScaledSphereRadius();
	WorldAABB.Min = Center - FVector(ScaledRadius, ScaledRadius, ScaledRadius);
	WorldAABB.Max = Center + FVector(ScaledRadius, ScaledRadius, ScaledRadius);
}

bool USphereComponent::RaycastMesh(const FRay& Ray, FHitResult& OutHitResult)
{
	const bool bHit = FCollisionQueryUtils::RaycastSphere(
		GetWorldLocation(), GetScaledSphereRadius(), Ray, false, OutHitResult);
	if (bHit)
	{
		OutHitResult.HitComponent = this;
		OutHitResult.ImpactPoint = GetWorldLocation() + OutHitResult.Normal.GetSafeNormal() * GetScaledSphereRadius();
	}
	return bHit;
}

bool USphereComponent::SweepMesh(const FVector& Start, const FVector& End, const FQuat& ShapeWorldRotation,
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

	const bool bHit = FCollisionQueryUtils::RaycastSphere(
		GetWorldLocation(),
		GetScaledSphereRadius() + Shape.Radius,
		FRay(Start, Delta / SegmentLength),
		true,
		OutHitResult);
	if (bHit)
	{
		OutHitResult.HitComponent = this;
		OutHitResult.ImpactPoint = GetWorldLocation() + OutHitResult.Normal.GetSafeNormal() * GetScaledSphereRadius();
	}
	return bHit;
}

EPrimitiveType USphereComponent::GetPrimitiveType() const
{
	return EPrimitiveType::EPT_Sphere;
}
