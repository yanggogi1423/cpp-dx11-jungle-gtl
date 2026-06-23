#include "Particle/ParticleModuleMeshRotationRate.h"

#include "Particle/ParticleMeshEmitterInstance.h"
#include "Particle/ParticleMeshTypes.h"

#include <algorithm>

namespace
{
    float GetEmitterSpawnDistributionTime(FParticleEmitterInstance* Owner, float SpawnTime)
    {
        const float ClampedSpawnOffset = std::max(SpawnTime, 0.0f);
        return Owner ? std::max(Owner->GetPreviousEmitterTime() + ClampedSpawnOffset, 0.0f) : ClampedSpawnOffset;
    }
}

UParticleModuleMeshRotationRate::UParticleModuleMeshRotationRate()
{
    // Color/Size 패턴 — Spawn 시 초기값 셋팅 + Update 시 누적.
    bSpawnModule = true;
    bUpdateModule = true;
}

// Function : Initialize per-particle RotRate at spawn time
// input : Owner, Particle, SpawnTime
// Owner : emitter instance that owns the particle (must be Mesh-derived for payload access)
// Particle : particle being initialized (FBaseParticle — payload write 는 Owner cast 후)
// SpawnTime : relative spawn time (unused — RotRate 는 SpawnTime 비의존)
// output : payload.RotRate set to RandomRange([Min, Max]) per axis when Owner is Mesh-derived; otherwise no-op
//
// 위험 13 방어: Cast 실패 (Mesh 가 아닌 emitter 에 잘못 추가) → early return. Particle.RotRate 자체는 변경 없음.
// 기본값 (Min == Max == Zero) 시 모든 particle 의 RotRate = Zero → Cycle 11 옵션 B 동작 그대로 (회귀 안전).
void UParticleModuleMeshRotationRate::Spawn(FParticleEmitterInstance* Owner, FBaseParticle& Particle, float SpawnTime)
{
    (void)Particle;
    FParticleMeshEmitterInstance* MeshInstance = dynamic_cast<FParticleMeshEmitterInstance*>(Owner);
    if (!MeshInstance)
    {
        return; // 위험 13 방어
    }

    // base SpawnParticles 가 호출하는 시점: 신규 particle 이 ActiveIdx = (ActiveParticles - 1) 위치에 있음.
    // ParticleIndices[ActiveParticles - 1] = 신규 SlotIndex.
    // 단 본 module 의 Spawn 은 새 particle 1개씩 호출되므로 GetMeshPayloadAt(ActiveParticles - 1) 로 회수.
    const int32 NewActiveIdx = MeshInstance->GetActiveParticleCount() - 1;
    if (FMeshRotationPayload* Payload = MeshInstance->GetMeshPayloadAt(NewActiveIdx))
    {
        const float DistributionTime = GetEmitterSpawnDistributionTime(Owner, SpawnTime);
        Payload->RotRate = EvaluateVectorDistribution(
            "RotRateMin",
            RotRateMin,
            RotRateMax,
            DistributionTime);
    }
}

// Function : Accumulate per-particle Rotation by RotRate * DeltaTime each update
// input : Owner, DeltaTime
// Owner : emitter instance that owns active particles (must be Mesh-derived)
// DeltaTime : elapsed time for this simulation step
// output : Each active particle's payload.Rotation gains RotRate * DeltaTime
//
// 위험 13 방어: Cast 실패 시 early return. payload nullptr 검사도 명시 (storage 미준비 시 skip).
// 누적 시점: base Tick 의 Update module 루프 ([ParticleEmitterInstance.cpp:129-135]) 에서 호출.
//   → particle position update 후 / module Spawn 보다 뒤 → BuildInstanceData 보다 앞.
//   같은 frame 의 SpawnTime=0 신규 particle 도 본 update 에서 한 번 누적 (RotRate * dt).
void UParticleModuleMeshRotationRate::Update(FParticleEmitterInstance* Owner, float DeltaTime)
{
    if (DeltaTime <= 0.0f)
    {
        return;
    }
    FParticleMeshEmitterInstance* MeshInstance = dynamic_cast<FParticleMeshEmitterInstance*>(Owner);
    if (!MeshInstance)
    {
        return; // 위험 13 방어
    }

    const int32 ActiveCount = MeshInstance->GetActiveParticleCount();
    for (int32 ActiveIdx = 0; ActiveIdx < ActiveCount; ++ActiveIdx)
    {
        FMeshRotationPayload* Payload = MeshInstance->GetMeshPayloadAt(ActiveIdx);
        if (!Payload)
        {
            continue; // storage 미준비 또는 invalid SlotIndex → skip (silent rendering 회피)
        }
        Payload->Rotation = Payload->Rotation + Payload->RotRate * DeltaTime;
    }
}
