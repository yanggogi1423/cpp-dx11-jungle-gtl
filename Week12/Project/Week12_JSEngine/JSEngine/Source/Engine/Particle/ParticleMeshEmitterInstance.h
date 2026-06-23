#pragma once

#include "Particle/ParticleEmitterInstance.h"
#include "Render/Resource/VertexTypes.h"

struct FMeshRotationPayload;

// Mesh emitter용 instance (Cycle 11, 옵션 B).
// base FParticleEmitterInstance 파생 — Tick/KillParticle은 base 그대로 사용 (Mesh는 swap-pop 안전).
// override 2종 (Cycle 15a Phase 5 정리 후):
//   SpawnParticles : base 호출 후 신규 SlotIndex의 payload(FMeshRotationPayload) 초기화.
//   CreateDynamicData : Mesh DynamicData 생성 + alignment + spin 합성 (DynamicData::BuildFromInstance 에 본문 이관됨).
//
// Cycle 15a Phase 5 (D5/D7): BuildInstanceData/GetMeshInstanceData/MeshInstanceDataBuffer 삭제됨.
// 본문 (alignment 행렬 + spin 누적)은 FDynamicMeshEmitterData::BuildFromInstance 로 이관.
struct FParticleMeshEmitterInstance : public FParticleEmitterInstance
{
public:
    FParticleMeshEmitterInstance() = default;
    ~FParticleMeshEmitterInstance() override = default;

    void SpawnParticles(int32 Count, float StartTime, float Increment, const FVector& InitialLocation,
                        const FVector& InitialVelocity, struct FParticleEventInstancePayload* EventPayload = nullptr) override;

    // Cycle 15a Phase 3 (ReplayData/DynamicData, D7): Mesh DynamicData 생성 override.
    FDynamicEmitterDataBase* CreateDynamicData() override;

    // Cycle 14 (M2, 결정 20 옵션 A): payload access path public 화.
    // UParticleModuleMeshRotationRate::Spawn / Update 에서 Cast<FParticleMeshEmitterInstance>(Owner) 후 호출.
    // Cycle 15a Phase 5: FDynamicMeshEmitterData::BuildFromInstance 가 payload 의 Rotation read 위해 호출.
    // SlotIndex(physical) 기반 — swap-pop 안전.
    FMeshRotationPayload* GetMeshPayload(int32 SlotIndex);

    // Cycle 14 (M2): ActiveIdx (compact list) → SlotIndex 변환 + payload 회수 편의 helper.
    // Update 루프에서 `for (int32 i = 0; i < ActiveCount; ++i)` 형태일 때 사용.
    FMeshRotationPayload* GetMeshPayloadAt(int32 ActiveIdx);
};
