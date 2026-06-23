#pragma once
#include "ShapeComponent.h"

#include <algorithm>
#include <cmath>

UCLASS(SpawnableComponent, DisplayName = "Sphere Component", Category = "Collision")
class USphereComponent : public UShapeComponent
{
public:
	GENERATED_BODY(USphereComponent, UShapeComponent)
	float GetSphereRadius() const { return SphereRadius; }
	float GetScaledSphereRadius() const
	{
		const FVector Scale = GetWorldTransform().GetScale3D();
		return SphereRadius * std::max({ std::fabs(Scale.X), std::fabs(Scale.Y), std::fabs(Scale.Z) });
	}

	void PostDuplicate(UObject* Original) override;

private:
	UPROPERTY(DisplayName = "Sphere Radius", LuaReadOnly, LuaName = SphereRadius)
	float SphereRadius = 0.5f;

	// UShapeComponent을(를) 통해 상속됨
	void UpdateWorldAABB() const override;
	bool RaycastMesh(const FRay& Ray, FHitResult& OutHitResult) override;
	bool SweepMesh(const FVector& Start, const FVector& End, const FQuat& ShapeWorldRotation,
		const FCollisionShape& Shape, FHitResult& OutHitResult) override;
	EPrimitiveType GetPrimitiveType() const override;
};
