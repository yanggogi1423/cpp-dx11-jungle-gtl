#include "Particle/ParticleMeshEmitterInstance.h"

#include "Math/Vector.h"
#include "Particle/ParticleDynamicData.h"
#include "Particle/ParticleMeshTypes.h"
#include "Particle/ParticleRendererProperties.h"
#include "Particle/ParticleSystem.h"
#include "Particle/ParticleSystemComponent.h"

// Cycle 15a Phase 5 정리:
//   - anonymous namespace 의 MakeShaderEulerRotation / ExtractShaderEuler / MakeAlignmentMatrix
//     → ParticleDynamicData.cpp 로 이관 (FDynamicMeshEmitterData::BuildFromInstance 가 사용).
//   - BuildInstanceData() override → 삭제 (D5).
//   - GetMeshInstanceData() override → 삭제 (D5).
//   - MeshInstanceDataBuffer 멤버 → 삭제 (D7) — DynamicData 가 소유.

// Function : Lookup mesh rotation payload by physical slot index
// input : SlotIndex (physical slot in ParticleStorage.ParticleData)
// output : pointer to interleaved FMeshRotationPayload, or nullptr when storage not ready
FMeshRotationPayload* FParticleMeshEmitterInstance::GetMeshPayload(int32 SlotIndex)
{
    if (!ParticleStorage.ParticleData || SlotIndex < 0)
    {
        return nullptr;
    }
    uint8* ParticleBase = ParticleStorage.ParticleData + SlotIndex * ParticleStorage.GetStride();
    return reinterpret_cast<FMeshRotationPayload*>(ParticleBase + PayloadOffset);
}

// Function : Lookup mesh rotation payload by active index (compact list)
FMeshRotationPayload* FParticleMeshEmitterInstance::GetMeshPayloadAt(int32 ActiveIdx)
{
    if (!ParticleStorage.ParticleData || !ParticleStorage.ParticleIndices ||
        ActiveIdx < 0 || ActiveIdx >= ActiveParticles)
    {
        return nullptr;
    }
    const int32 SlotIndex = static_cast<int32>(ParticleStorage.ParticleIndices[ActiveIdx]);
    return GetMeshPayload(SlotIndex);
}

// Function : Spawn particles via base and initialize mesh rotation payload for new slots
void FParticleMeshEmitterInstance::SpawnParticles(int32 Count, float StartTime, float Increment,
                                                  const FVector& InitialLocation, const FVector& InitialVelocity,
                                                  FParticleEventInstancePayload* EventPayload)
{
    const int32 OldActiveCount = ActiveParticles;
    FParticleEmitterInstance::SpawnParticles(Count, StartTime, Increment, InitialLocation, InitialVelocity, EventPayload);

    for (int32 ActiveIdx = OldActiveCount; ActiveIdx < ActiveParticles; ++ActiveIdx)
    {
        const int32 SlotIndex = static_cast<int32>(ParticleStorage.ParticleIndices[ActiveIdx]);
        if (FMeshRotationPayload* Payload = GetMeshPayload(SlotIndex))
        {
            Payload->InitialOrientation = FVector::ZeroVector;
            Payload->Rotation = FVector::ZeroVector;
            Payload->RotRate = FVector::ZeroVector;
        }
    }
}

// Function : Create Mesh DynamicData (Cycle 15a Phase 3 + Phase 5)
// Phase 5 본문 이관 결과: FDynamicMeshEmitterData::BuildFromInstance 가 alignment+spin 직접 수행.
// 본 함수는 ReplayData 메타 채움 + BuildFromInstance 호출.
FDynamicEmitterDataBase* FParticleMeshEmitterInstance::CreateDynamicData()
{
    FDynamicMeshEmitterData* DynData = new FDynamicMeshEmitterData();
    DynData->EmitterIndex = GetEmitterIndex();

    FDynamicMeshEmitterReplayData& Replay = DynData->Source;
    Replay.ActiveParticleCount = GetActiveParticleCount();
    Replay.ParticleStride = GetParticleStride();
    Replay.ParticleSize = GetParticleSize();
    Replay.PayloadOffset = PayloadOffset;
    Replay.MaxActiveParticles = GetMaxActiveParticleCount();
    // TODO(multithread): switch to deep copy when render-thread separation lands
    Replay.ParticleData = ParticleStorage.ParticleData;
    // TODO(multithread): switch to deep copy when render-thread separation lands
    Replay.ParticleIndices = ParticleStorage.ParticleIndices;
    Replay.SortMode = ESortMode::None;

    const FCompiledParticleLODData* CompiledLOD = GetCurrentCompiledLODData();
    if (const UParticleMeshRendererProperties* MeshRenderer =
        CompiledLOD ? Cast<UParticleMeshRendererProperties>(CompiledLOD->RendererProperties) : nullptr)
    {
        Replay.MeshAsset = MeshRenderer->GetMesh();
        Replay.Material = MeshRenderer->GetEffectiveMaterial();
    }
    Replay.ParticleTexture = nullptr; // Builder 가 Material.DiffuseMap 추출 후 채움.

    DynData->BuildFromInstance(*this);
    return DynData;
}
