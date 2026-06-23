#pragma once

#include "Engine/Asset/StaticMesh.h"
#include "Particle/ParticleBeamTypes.h"
#include "Object/Object.h"
#include "Particle/ParticleMeshTypes.h"
#include "Particle/ParticleRibbonTypes.h"
#include "Particle/ParticleTypes.h"
#include "Render/Resource/Material.h"

#include <algorithm>

struct FParticleEmitterInstance;
class UParticleSystemComponent;

UCLASS()
class UParticleRendererProperties : public UObject
{
public:
    GENERATED_BODY(UParticleRendererProperties, UObject)

    virtual EParticleEmitterRenderMode GetRenderMode() const { return RenderMode; }
    void SetRenderMode(EParticleEmitterRenderMode InRenderMode) { RenderMode = InRenderMode; }

    EBlendType GetBlendType() const { return BlendType; }
    void SetBlendType(EBlendType InBlendType) { BlendType = InBlendType; }

    // Emitter-level opacity (asset default). AlphaBlend 모드일 때만 의미가 있으며 Component 의 OpacityMultiplier 와 곱해짐.
    float GetOpacity() const { return Opacity; }
    void SetOpacity(float InValue) { Opacity = InValue; }

    virtual int32 RequiredPayloadBytes() const { return 0; }
    virtual FParticleEmitterInstance* CreateInstance(UParticleSystemComponent* Component, int32 EmitterIndex) const;

private:
    UPROPERTY(DisplayName = "Render Mode", NoEdit)
    EParticleEmitterRenderMode RenderMode = EParticleEmitterRenderMode::Sprite;

    UPROPERTY(DisplayName = "Blend Mode", Category = "Rendering")
    EBlendType BlendType = EBlendType::AlphaBlend;

    UPROPERTY(DisplayName = "Opacity", Category = "Rendering", Min = 0.0f, Max = 1.0f)
    float Opacity = 1.0f;
};

UCLASS()
class UParticleSpriteRendererProperties : public UParticleRendererProperties
{
public:
    GENERATED_BODY(UParticleSpriteRendererProperties, UParticleRendererProperties)

    EParticleEmitterRenderMode GetRenderMode() const override { return EParticleEmitterRenderMode::Sprite; }
    int32 RequiredPayloadBytes() const override { return 0; }
    FParticleEmitterInstance* CreateInstance(UParticleSystemComponent* Component, int32 EmitterIndex) const override;
};

UCLASS()
class UParticleMeshRendererProperties : public UParticleRendererProperties
{
public:
    GENERATED_BODY(UParticleMeshRendererProperties, UParticleRendererProperties)

    EParticleEmitterRenderMode GetRenderMode() const override { return EParticleEmitterRenderMode::Mesh; }
    int32 RequiredPayloadBytes() const override { return sizeof(FMeshRotationPayload); }
    FParticleEmitterInstance* CreateInstance(UParticleSystemComponent* Component, int32 EmitterIndex) const override;

    UStaticMesh* GetMesh() const { return Mesh; }
    void SetMesh(UStaticMesh* InMesh) { Mesh = InMesh; }

    void SetOverrideMaterial(bool bEnable, UMaterialInterface* InMaterial)
    {
        bOverrideMaterial = bEnable;
        OverrideMaterial = InMaterial;
    }

    UMaterialInterface* GetEffectiveMaterial() const;

    EMeshAlignment GetAlignment() const { return Alignment; }
    void SetAlignment(EMeshAlignment InAlignment) { Alignment = InAlignment; }

private:
    UPROPERTY(DisplayName = "Static Mesh", Category = "Mesh", ReferenceKind = Asset)
    UStaticMesh* Mesh = nullptr;

    UPROPERTY(DisplayName = "Override Material", Category = "Mesh")
    bool bOverrideMaterial = false;

    UPROPERTY(DisplayName = "Material Override", Category = "Mesh", ReferenceKind = Asset)
    UMaterialInterface* OverrideMaterial = nullptr;

    UPROPERTY(DisplayName = "Alignment", Category = "Mesh")
    EMeshAlignment Alignment = EMeshAlignment::PSA_Velocity;
};

UCLASS()
class UParticleRibbonRendererProperties : public UParticleRendererProperties
{
public:
    GENERATED_BODY(UParticleRibbonRendererProperties, UParticleRendererProperties)

    EParticleEmitterRenderMode GetRenderMode() const override { return EParticleEmitterRenderMode::Ribbon; }
    int32 RequiredPayloadBytes() const override { return sizeof(FRibbonParticlePayload); }
    FParticleEmitterInstance* CreateInstance(UParticleSystemComponent* Component, int32 EmitterIndex) const override;

    int32 GetMaxTrailCount() const { return MaxTrailCount; }
    int32 GetMaxParticleInTrailCount() const { return MaxParticleInTrailCount; }
    float GetSheetsPerTrail() const { return SheetsPerTrail; }
    float GetTangentSpawningScalar() const { return TangentSpawningScalar; }
    UMaterialInterface* GetMaterial() const { return Material; }

    void SetMaterial(UMaterialInterface* InMaterial) { Material = InMaterial; }
    void SetMaxTrailCount(int32 InCount) { MaxTrailCount = std::max(InCount, 1); }
    void SetMaxParticleInTrailCount(int32 InCount) { MaxParticleInTrailCount = std::max(InCount, 1); }
    void SetSheetsPerTrail(float InValue) { SheetsPerTrail = std::max(InValue, 1.0f); }
    void SetTangentSpawningScalar(float InValue) { TangentSpawningScalar = std::max(InValue, 0.0f); }

private:
    UPROPERTY(DisplayName = "Max Trail Count", Category = "Ribbon")
    int32 MaxTrailCount = 1;

    UPROPERTY(DisplayName = "Max Particle In Trail", Category = "Ribbon")
    int32 MaxParticleInTrailCount = 64;

    UPROPERTY(DisplayName = "Sheets Per Trail", Category = "Ribbon")
    float SheetsPerTrail = 1.0f;

    UPROPERTY(DisplayName = "Tangent Spawning Scalar", Category = "Ribbon")
    float TangentSpawningScalar = 0.0f;

    UPROPERTY(DisplayName = "Material", Category = "Ribbon", ReferenceKind = Asset)
    UMaterialInterface* Material = nullptr;
};

UCLASS()
class UParticleBeamRendererProperties : public UParticleRendererProperties
{
public:
    GENERATED_BODY(UParticleBeamRendererProperties, UParticleRendererProperties)

    EParticleEmitterRenderMode GetRenderMode() const override { return EParticleEmitterRenderMode::Beam; }
    int32 RequiredPayloadBytes() const override { return sizeof(FParticleBeamPayload); }
    FParticleEmitterInstance* CreateInstance(UParticleSystemComponent* Component, int32 EmitterIndex) const override;

    int32 GetMaxBeamCount() const { return std::max(MaxBeamCount, 1); }
    int32 GetInterpolationPoints() const { return std::clamp(InterpolationPoints, 0, 64); }
    float GetFallbackDistance() const { return std::max(FallbackDistance, 0.0f); }
    float GetTextureTile() const { return std::max(TextureTile, 0.0f); }
    float GetTextureTileDistance() const { return std::max(TextureTileDistance, 0.0f); }
    UMaterialInterface* GetMaterial() const { return Material; }

    void SetMaterial(UMaterialInterface* InMaterial) { Material = InMaterial; }
    void SetMaxBeamCount(int32 InCount) { MaxBeamCount = std::max(InCount, 1); }
    void SetInterpolationPoints(int32 InCount) { InterpolationPoints = std::clamp(InCount, 0, 64); }
    void SetFallbackDistance(float InValue) { FallbackDistance = std::max(InValue, 0.0f); }
    void SetTextureTile(float InValue) { TextureTile = std::max(InValue, 0.0f); }
    void SetTextureTileDistance(float InValue) { TextureTileDistance = std::max(InValue, 0.0f); }

private:
    UPROPERTY(DisplayName = "Max Beam Count", Category = "Beam", Min = 1)
    int32 MaxBeamCount = 1;

    UPROPERTY(DisplayName = "Interpolation Points", Category = "Beam", Min = 0, Max = 64)
    int32 InterpolationPoints = 0;

    UPROPERTY(DisplayName = "Fallback Distance", Category = "Beam", Min = 0.0f)
    float FallbackDistance = 100.0f;

    UPROPERTY(DisplayName = "Texture Tile", Category = "Beam", Min = 0.0f)
    float TextureTile = 1.0f;

    UPROPERTY(DisplayName = "Texture Tile Distance", Category = "Beam", Min = 0.0f)
    float TextureTileDistance = 0.0f;

    UPROPERTY(DisplayName = "Material", Category = "Beam", ReferenceKind = Asset)
    UMaterialInterface* Material = nullptr;
};
