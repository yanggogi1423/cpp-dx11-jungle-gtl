#pragma once
#include "LightComponent.h"

UCLASS()
class UPointLightComponent : public ULightComponent
{
	GENERATED_BODY(UPointLightComponent, ULightComponent)
public:
    virtual void PostDuplicate(UObject* Original) override;
    virtual void GetEditableProperties(TArray<FPropertyDescriptor>& OutProps) override;

	virtual void Serialize(FArchive& Ar) override;

protected:
	virtual FMatrix ComputePerspectiveShadowMatrix(const FMatrix& CamView, const FMatrix& CamProj,
		const TArray<FBoundingBox>* VisibleObjectsBounds) const override;

	//virtual void PrintShadowMapDebugInfo(TArray<FPropertyDescriptor>& OutProps) const override;

public:
	UPROPERTY(EditAnywhere, Category = "Light", DisplayName = "Attenuation Radius")
    float AttenuationRadius		= 10.f;
	UPROPERTY(EditAnywhere, Category = "Light", DisplayName = "Light Falloff Exponent")
    float LightFalloffExponent	= 1.f;
};