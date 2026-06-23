#pragma once
#include "Component/SceneComponent.h"

UCLASS()
class ULightComponentBase : public USceneComponent {
	GENERATED_BODY(ULightComponentBase, USceneComponent)
public:
	ULightComponentBase() = default;
    virtual void PostDuplicate(UObject* Original) override;
    virtual void GetEditableProperties(TArray<FPropertyDescriptor>& OutProps) override;

	virtual void Serialize(FArchive& Ar) override;

protected:
	~ULightComponentBase() = default;

public:
	UPROPERTY(EditAnywhere, Category = "Light", DisplayName = "Light Color")
    FColor LightColor = FColor::White();
	UPROPERTY(EditAnywhere, Category = "Light", DisplayName = "Intensity")
	float Intensity = 1.0f;

	UPROPERTY(EditAnywhere, Category = "Light", DisplayName = "Cast Shadows")
	bool bCastShadows = true;
};