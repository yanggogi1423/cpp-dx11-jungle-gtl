#pragma once
#include "PrimitiveComponent.h"

UCLASS()
class UHeightFogComponent : public UPrimitiveComponent
{
	GENERATED_BODY(UHeightFogComponent, UPrimitiveComponent)
public:
    UHeightFogComponent();
    ~UHeightFogComponent() override = default;

	virtual void Serialize(FArchive& Ar) override;

    EPrimitiveType GetPrimitiveType() const override { return EPrimitiveType::EPT_FOG; }

    void SetFogDensity(float InFogDensity) { FogDensity = InFogDensity; }
    float GetFogDensity() const { return FogDensity; }

    void SetHeightFalloff(float InHeightFalloff) { HeightFalloff = InHeightFalloff; }
    float GetHeightFalloff() const { return HeightFalloff; }

    void SetFogInscatteringColor(const FVector4& InColor) { FogInscatteringColor = FColor(InColor.X, InColor.Y, InColor.Z, InColor.W); }
    FVector4 GetFogInscatteringColor() const { return FogInscatteringColor.ToVector4(); }

    void SetFogHeight(float InFogHeight) { FogHeight = InFogHeight; }
    float GetFogHeight() const { return FogHeight; }

    void SetFogStartDistance(float InFogStartDistance) { FogStartDistance = InFogStartDistance; }
    float GetFogStartDistance() const { return FogStartDistance; }

    void SetFogCutoffDistance(float InCutoffDistance) { FogCutoffDistance = InCutoffDistance; }
    float GetFogCutoffDistance() const { return FogCutoffDistance; }

    void SetFogMaxOpacity(float InFogMaxOpacity) { FogMaxOpacity = InFogMaxOpacity; }
    float GetFogMaxOpacity() const { return FogMaxOpacity; }

    // --- Property / Serialization ---
    void GetEditableProperties(TArray<FPropertyDescriptor>& OutProps) override;
    void PostEditProperty(const char* PropertyName) override;

private:
	UPROPERTY(EditAnywhere, Category = "Fog", DisplayName = "Inscattering Color")
    FColor FogInscatteringColor;

	UPROPERTY(EditAnywhere, Category = "Fog", DisplayName = "Density")
    float FogDensity = 0;
	UPROPERTY(EditAnywhere, Category = "Fog", DisplayName = "Height Falloff")
    float HeightFalloff = 0;
	UPROPERTY(EditAnywhere, Category = "Fog", DisplayName = "Fog Height")
    float FogHeight = 0;
	UPROPERTY(EditAnywhere, Category = "Fog", DisplayName = "Start Distance")
    float FogStartDistance = 0;
	UPROPERTY(EditAnywhere, Category = "Fog", DisplayName = "Cutoff Distance")
    float FogCutoffDistance = 1000;
	UPROPERTY(EditAnywhere, Category = "Fog", DisplayName = "Max Opacity")
    float FogMaxOpacity = 1.f;

    // UPrimitiveComponent을(를) 통해 상속됨
    void UpdateWorldAABB() const override;
    bool RaycastMesh(const FRay& Ray, FHitResult& OutHitResult) override;
};