#pragma once

#include "Particle/ParticleBeamTypes.h"
#include "Particle/ParticleEmitterInstance.h"

// Beam emitter용 instance (Cycle 13a, 결정 11 옵션 B + 결정 13 옵션 A + 결정 15 옵션 B).
// base FParticleEmitterInstance 파생.
// Cycle 15a Phase 5 정리 후:
//   override 2종:
//     SpawnParticles : base 호출 후 신규 SlotIndex 의 payload 에 BeamIndex round-robin 분배 + Noise capture.
//     Tick           : base Tick 호출 후 EnsureBeamState 만 (vertex build 는 DynamicData::BuildFromInstance 가 수행).
//   payload public helper 1:
//     GetBeamPayload : FDynamicBeamEmitterData::BuildFromInstance 가 NoiseSamples read 위해 호출.
//
// 삭제됨 (D5/D7):
//   GetBeamVertexData / BuildVertexBuffer / VertexBuffer 멤버.
struct FParticleBeamEmitterInstance : public FParticleEmitterInstance
{
public:
    FParticleBeamEmitterInstance() = default;
    ~FParticleBeamEmitterInstance() override = default;

    void Tick(float DeltaTime, bool bAllowSpawning) override;
    void SpawnParticles(int32 Count, float StartTime, float Increment, const FVector& InitialLocation,
                        const FVector& InitialVelocity, struct FParticleEventInstancePayload* EventPayload = nullptr) override;

    // Cycle 15a Phase 3 (ReplayData/DynamicData, D7): Beam DynamicData 생성 override.
    // Phase 5 본문 이관 결과: FDynamicBeamEmitterData::BuildFromInstance 가 strip vertex 직접 build.
    FDynamicEmitterDataBase* CreateDynamicData() override;

    // Cycle 15a Phase 5: payload access path private → public 승격 (Mesh 의 GetMeshPayload 패턴 답습).
    // FDynamicBeamEmitterData::BuildFromInstance 가 payload 의 NoiseSamples read 위해 호출.
    FParticleBeamPayload* GetBeamPayload(int32 SlotIndex);

private:
    // BeamStates 재구성 — MaxBeamCount 변경 또는 첫 Tick 진입 시 호출 (Ribbon 의 EnsureTrailState 패턴 답습).
    void EnsureBeamState();

private:
    // 결정 13 옵션 A: BeamStates size = MaxBeamCount.
    TArray<int32> BeamStates;

    // round-robin BeamIndex 분배.
    int32 NextBeamIndex = 0;
};
