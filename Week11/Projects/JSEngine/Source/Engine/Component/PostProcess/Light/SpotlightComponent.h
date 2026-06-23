#pragma once
#include "PointLightComponent.h"

UCLASS()
class USpotlightComponent : public UPointLightComponent
{
	GENERATED_BODY(USpotlightComponent, UPointLightComponent)
public:
    void PostDuplicate(UObject* Origiunal) override;
    void GetEditableProperties(TArray<FPropertyDescriptor>& OutProps) override;

	void Serialize(FArchive& Ar) override;

protected:
	FMatrix ComputeCascadeShadowMatrix(const FMatrix& CamView, const FMatrix& CamProj,
		float SplitNearT, float SplitFarT) const override;
	FMatrix ComputePerspectiveShadowMatrix(const FMatrix& CamView, const FMatrix& CamProj,
		const TArray<FBoundingBox>* VisibleObjectsBounds) const override;

public:
	UPROPERTY(EditAnywhere, Category = "Light", DisplayName = "Inner Cone Angle")
    float InnerConeAngle = 10.f;
	UPROPERTY(EditAnywhere, Category = "Light", DisplayName = "Outer Cone Angle")
    float OuterConeAngle = 15.f;
};
