#pragma once
#include "PrimitiveComponent.h"
#include "Core/ResourceTypes.h"

UCLASS()
class UFireballComponent : public UPrimitiveComponent {
	GENERATED_BODY(UFireballComponent, UPrimitiveComponent)
public:
	UFireballComponent();
	~UFireballComponent() override = default;

	//virtual UFireballComponent* Duplicate() override;
	//virtual UFireballComponent* DuplicateSubObjects() override { return this; }

	void PostDuplicate(UObject* original) override; 

	virtual void Serialize(FArchive& Ar) override;

	void GetEditableProperties(TArray<FPropertyDescriptor>& OutProps) override;
    void PostEditProperty(const char* PropertyName) override;

	EPrimitiveType GetPrimitiveType() const override { return EPrimitiveType::EPT_Fireball; }

    bool SupportsOutline() const override { return true; }

	void UpdateWorldAABB() const override;
    bool RaycastMesh(const FRay& Ray, FHitResult& OutHitResult) override;


	// Accessors (Probably redundant, remove later)
	float GetIntensity()	 const { return Intensity; }
	float GetRadius()		 const { return Radius; }
	float GetRadiusFallOff() const { return RadiusFallOff; }
	FColor& GetLinearColor() { return Color; }

	void SetIntensity(float InIntensity) { Intensity = InIntensity; }
	void SetRadius(float InRadius) { if (InRadius) Radius = InRadius; }
	void SetRadiusFallOff(float InFallOff) { if (InFallOff) RadiusFallOff = InFallOff; }

private:
	UPROPERTY(EditAnywhere, Category = "Fireball", DisplayName = "Intensity")
	float  Intensity		= 1.f;
	UPROPERTY(EditAnywhere, Category = "Fireball", DisplayName = "Radius")
	float  Radius			= 15.f;
	UPROPERTY(EditAnywhere, Category = "Fireball", DisplayName = "Radius Falloff")
	float  RadiusFallOff	= 1.f;
	UPROPERTY(EditAnywhere, Category = "Fireball", DisplayName = "Color")
	FColor Color			= FColor(1.0f, 0.8f, 0.04f, 1.f);
};