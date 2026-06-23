#pragma once
#include "PrimitiveComponent.h"

UCLASS()
class UShapeComponent : public UPrimitiveComponent
{
	GENERATED_BODY(UShapeComponent, UPrimitiveComponent)

public:
    void PostDuplicate(UObject* Original) override;
    void Serialize(FArchive& Ar) override;

private:
	FColor ShapeColor;
    bool bDrawOnlyIfSelected;

    // UPrimitiveComponent을(를) 통해 상속됨
    void UpdateWorldAABB() const override;
    bool RaycastMesh(const FRay& Ray, FHitResult& OutHitResult) override;
    EPrimitiveType GetPrimitiveType() const override;
};