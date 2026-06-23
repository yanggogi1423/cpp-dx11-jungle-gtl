#pragma once

#include "Particle/ParticleModuleTypeData.h"
#include "Particle/ParticleRibbonTypes.h"
#include "Render/Resource/Material.h"

#include <algorithm>

// Legacy ribbon TypeData. New assets should use UParticleRibbonRendererProperties.
// RequiredPayloadBytes()가 sizeof(FRibbonParticlePayload)를 반환 — container Stride에 자동 가산.
// CreateInstance()가 FParticleRibbonEmitterInstance를 반환해 SpawnParticles/KillParticle/Tick override가 작동한다.
//
// 결정 9 옵션 B: bRenderGeometry/SpawnPoints/Tangents 디버그 플래그는 본 cycle 제외 — 후속 cycle (12c) 에서 추가.
UCLASS()
class URibbonTypeData : public UParticleModuleTypeDataBase
{
public:
    GENERATED_BODY(URibbonTypeData, UParticleModuleTypeDataBase)

    int32 RequiredPayloadBytes() const override { return sizeof(FRibbonParticlePayload); }
    EParticleEmitterRenderMode GetRenderMode() const override { return EParticleEmitterRenderMode::Ribbon; }
    FParticleEmitterInstance* CreateInstance(UParticleSystemComponent* Component, int32 EmitterIndex) const override;

    int32 GetMaxTrailCount() const { return std::max(MaxTrailCount, 1); }
    int32 GetMaxParticleInTrailCount() const { return std::max(MaxParticleInTrailCount, 1); }
    float GetSheetsPerTrail() const { return std::max(SheetsPerTrail, 1.0f); }
    float GetTangentSpawningScalar() const { return std::max(TangentSpawningScalar, 0.0f); }
    UMaterialInterface* GetMaterial() const { return Material; }

    // Detail panel 의 picker 가 호출 — Material 만 변경. 다른 멤버는 reflection 으로 자동 노출됨.
    void SetMaterial(UMaterialInterface* InMaterial) { Material = InMaterial; }
    void SetMaxTrailCount(int32 InCount) { MaxTrailCount = std::max(InCount, 1); }
    void SetMaxParticleInTrailCount(int32 InCount) { MaxParticleInTrailCount = std::max(InCount, 1); }
    void SetSheetsPerTrail(float InValue) { SheetsPerTrail = std::max(InValue, 1.0f); }
    void SetTangentSpawningScalar(float InValue) { TangentSpawningScalar = std::max(InValue, 0.0f); }

private:
    UPROPERTY(DisplayName = "Max Trail Count", Category = "Ribbon", Min = 1)
    int32 MaxTrailCount = 1;

    UPROPERTY(DisplayName = "Max Particle In Trail", Category = "Ribbon", Min = 1)
    int32 MaxParticleInTrailCount = 64;

    UPROPERTY(DisplayName = "Sheets Per Trail", Category = "Ribbon", Min = 1.0f)
    float SheetsPerTrail = 1.0f;

    UPROPERTY(DisplayName = "Tangent Spawning Scalar", Category = "Ribbon", Min = 0.0f)
    float TangentSpawningScalar = 0.0f;

    UPROPERTY(DisplayName = "Material", Category = "Ribbon", ReferenceKind = Asset)
    UMaterialInterface* Material = nullptr;
};
