#pragma once

#include "Engine/Asset/StaticMesh.h"
#include "Particle/ParticleMeshTypes.h"
#include "Particle/ParticleModuleTypeData.h"
#include "Render/Resource/Material.h"

// Legacy mesh TypeData. New assets should use UParticleMeshRendererProperties.
// RequiredPayloadBytes()가 sizeof(FMeshRotationPayload)를 반환 — container Stride에 자동 가산 (Cycle 10d 의 ξ 해소 실측).
// CreateInstance()가 FParticleMeshEmitterInstance를 반환해 BuildInstanceData/SpawnParticles override가 작동한다.
UCLASS()
class UMeshTypeData : public UParticleModuleTypeDataBase
{
public:
    GENERATED_BODY(UMeshTypeData, UParticleModuleTypeDataBase)

    int32 RequiredPayloadBytes() const override { return sizeof(FMeshRotationPayload); }
    EParticleEmitterRenderMode GetRenderMode() const override { return EParticleEmitterRenderMode::Mesh; }
    FParticleEmitterInstance* CreateInstance(UParticleSystemComponent* Component, int32 EmitterIndex) const override;

    UStaticMesh* GetMesh() const { return Mesh; }
    void SetMesh(UStaticMesh* InMesh)
    {
        Mesh = InMesh;
        SetOverrideMaterial(false, nullptr);
    }

    void SetOverrideMaterial(bool bEnable, UMaterialInterface* InMaterial)
    {
        bOverrideMaterial = bEnable;
        OverrideMaterial = InMaterial;
    }

    // bOverrideMaterial이 true면 OverrideMaterial을 반환, 아니면 Section[0]의 MaterialSlotIndex가 가리키는 slot의 material 반환.
    // 둘 다 없으면 첫 non-null slot fallback. 모두 실패 시 nullptr — RenderPass에서 default white SRV로 fallback.
    // Section[0].MaterialSlotIndex 사용 이유: 항상 slot 0이 아니라 실제 section이 참조하는 slot을 정확히 추출.
    UMaterialInterface* GetEffectiveMaterial() const;

    // Cycle 14 (M1): alignment 모드 getter/setter. BuildInstanceData 에서 PSA_Velocity / PSA_FacingCameraPosition 분기.
    EMeshAlignment GetAlignment() const { return Alignment; }
    void SetAlignment(EMeshAlignment InAlignment) { Alignment = InAlignment; }

private:
    UPROPERTY(DisplayName = "Static Mesh", Category = "Mesh", ReferenceKind = Asset)
    UStaticMesh* Mesh = nullptr;

    UPROPERTY(DisplayName = "Override Material", Category = "Mesh")
    bool bOverrideMaterial = false;

    UPROPERTY(DisplayName = "Material Override", Category = "Mesh", ReferenceKind = Asset)
    UMaterialInterface* OverrideMaterial = nullptr;

    // Cycle 14 (M1, 결정 16 A + 결정 17 B): mesh 의 forward axis 정렬 모드.
    // default = PSA_Velocity (velocity 비-zero 면 자동 align, zero 면 identity — 회귀 안전).
    UPROPERTY(DisplayName = "Alignment", Category = "Mesh")
    EMeshAlignment Alignment = EMeshAlignment::PSA_Velocity;
};
