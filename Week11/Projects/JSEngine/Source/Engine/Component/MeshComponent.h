#pragma once
#include "PrimitiveComponent.h"

class UMaterialInterface;

UCLASS(Abstract)
class UMeshComponent : public UPrimitiveComponent
{
	GENERATED_BODY(UMeshComponent, UPrimitiveComponent)
public:
	virtual void Serialize(FArchive& Ar) override;

	virtual void SetMaterial(int32 SlotIndex, UMaterialInterface* InMaterial) override;
	virtual UMaterialInterface* GetMaterial(int32 SlotIndex) const override;

	const TArray<UMaterialInterface*>& GetOverrideMaterial() const;
	const FVector2& GetScroll() const { return ScrollUV; };

	virtual int32 GetNumMaterials() const override;
	void GetEditableProperties(TArray<FPropertyDescriptor>& OutProps) override;
	void PostEditProperty(const char * PropertyName) override;
	
	virtual void TickComponent(float DeltaTime) override;

protected:
	UPROPERTY(EditAnywhere, Category = "Rendering", DisplayName = "Materials")
	TArray<UMaterialInterface*> Materials;

	UPROPERTY(EditAnywhere, Category = "Rendering", DisplayName = "Scroll UV")
	FVector2 ScrollUV;
};
