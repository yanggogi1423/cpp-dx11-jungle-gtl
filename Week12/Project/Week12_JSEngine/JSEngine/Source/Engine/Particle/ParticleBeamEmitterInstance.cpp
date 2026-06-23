#include "Particle/ParticleBeamEmitterInstance.h"

#include <algorithm>
#include <cmath>
#include <random>

#include "Component/SceneComponent.h"
#include "Math/Utils.h"
#include "Particle/ParticleBeamTypes.h"
#include "Particle/ParticleDynamicData.h"
#include "Particle/ParticleModuleBeamNoise.h"
#include "Particle/ParticleModuleBeamSource.h"
#include "Particle/ParticleModuleBeamTarget.h"
#include "Particle/ParticleRendererProperties.h"
#include "Particle/ParticleSystem.h"
#include "Particle/ParticleSystemComponent.h"

// Cycle 15a Phase 5 정리:
//   - anonymous namespace 의 BeamSmallNumber / BeamInterpolationPointsMax / BeamAxisParallelDot / ComputePerpendicular /
//     ComputeBeamLocalAxes / FindFirstModule → ParticleDynamicData.cpp 로 이관 (FDynamicBeamEmitterData 가 사용).
//   - BuildVertexBuffer() → 삭제 (본문 FDynamicBeamEmitterData::BuildFromInstance 로 이관).
//   - GetBeamVertexData() override → 삭제 (D5).
//   - VertexBuffer 멤버 → 삭제 (D7).
//   - Tick() 의 BuildVertexBuffer 호출 제거.
// GenerateNoiseSamples 는 SpawnParticles 의 per-particle 영구 noise capture 에 사용 — Spawn 시점에 호출 필요하므로 본 .cpp 유지.

namespace
{
    template <typename T>
    T* FindFirstBeamModule(UParticleLODLevel* LOD)
    {
        if (!LOD)
        {
            return nullptr;
        }
        for (UParticleModule* Module : LOD->GetModules())
        {
            T* Casted = Cast<T>(Module);
            if (Casted)
            {
                return Casted;
            }
        }
        return nullptr;
    }

    // Cycle 13b 분기 1 B-2 + 분기 6 A: per-particle 영구 NoiseSamples 생성.
    // random source cascade: per-particle deterministic seed (ParticleId) 로 mt19937 local generator.
    // 위험 6 (Noise determinism) 방어: spawn 시 1회만 호출 → frame-rate 비종속.
    void GenerateNoiseSamples(FVector* OutSamples, int32 Frequency, uint32 Seed)
    {
        if (!OutSamples)
        {
            return;
        }
        for (int32 i = 0; i < BeamNoiseMaxFrequency; ++i)
        {
            OutSamples[i] = FVector::ZeroVector;
        }

        const int32 ClampedFreq = MathUtil::Clamp(Frequency, 0, BeamNoiseMaxFrequency);
        if (ClampedFreq <= 0)
        {
            return;
        }

        std::mt19937 Rng(Seed);
        std::uniform_real_distribution<float> Dist(-1.0f, 1.0f);
        for (int32 i = 0; i < ClampedFreq; ++i)
        {
            OutSamples[i] = FVector(Dist(Rng), Dist(Rng), Dist(Rng));
        }
    }
    UParticleLODLevel* ResolveBeamSourceLOD(FParticleBeamEmitterInstance* Instance)
    {
        if (!Instance)
        {
            return nullptr;
        }

        const FCompiledParticleLODData* CompiledLOD = Instance->GetCurrentCompiledLODData();
        return CompiledLOD && CompiledLOD->SourceLODLevel
            ? CompiledLOD->SourceLODLevel
            : Instance->GetCurrentLODLevel();
    }
}

// Function : Lookup beam payload by physical slot index
// 위험 1 방어: SlotIndex 음수 또는 MaxParticles 초과면 nullptr.
FParticleBeamPayload* FParticleBeamEmitterInstance::GetBeamPayload(int32 SlotIndex)
{
    if (!ParticleStorage.ParticleData || SlotIndex < 0 || SlotIndex >= GetMaxActiveParticleCount())
    {
        return nullptr;
    }
    uint8* ParticleBase = ParticleStorage.ParticleData + SlotIndex * ParticleStorage.GetStride();
    return reinterpret_cast<FParticleBeamPayload*>(ParticleBase + PayloadOffset);
}

// Function : Resize BeamStates to MaxBeamCount and reset NextBeamIndex
void FParticleBeamEmitterInstance::EnsureBeamState()
{
    int32 MaxBeams = 1;
    const FCompiledParticleLODData* CompiledLOD = GetCurrentCompiledLODData();
    if (const UParticleBeamRendererProperties* BeamRenderer =
        CompiledLOD ? Cast<UParticleBeamRendererProperties>(CompiledLOD->RendererProperties) : nullptr)
    {
        MaxBeams = std::max(BeamRenderer->GetMaxBeamCount(), 1);
    }

    if (static_cast<int32>(BeamStates.size()) != MaxBeams)
    {
        BeamStates.assign(MaxBeams, 0);
        NextBeamIndex = 0;
    }
}

// Function : Spawn beam particles — base spawn + BeamIndex round-robin + per-particle Noise capture
void FParticleBeamEmitterInstance::SpawnParticles(int32 Count, float StartTime, float Increment,
                                                  const FVector& InitialLocation, const FVector& InitialVelocity,
                                                  FParticleEventInstancePayload* EventPayload)
{
    EnsureBeamState();

    const int32 OldActiveCount = ActiveParticles;
    FParticleEmitterInstance::SpawnParticles(Count, StartTime, Increment, InitialLocation, InitialVelocity, EventPayload);

    const int32 MaxBeams = std::max(static_cast<int32>(BeamStates.size()), 1);

    // Cycle 13b: NoiseModule lookup (1회). 없으면 NoiseSamples zero-init 만 (BuildVertexBuffer 가 perturb 안 함).
    UParticleLODLevel* LOD = ResolveBeamSourceLOD(this);
    const UParticleModuleBeamNoise* NoiseModule = FindFirstBeamModule<UParticleModuleBeamNoise>(LOD);
    const int32 NoiseFrequency = NoiseModule ? NoiseModule->GetFrequency() : 0;

    for (int32 ActiveIdx = OldActiveCount; ActiveIdx < ActiveParticles; ++ActiveIdx)
    {
        const int32 SlotIndex = static_cast<int32>(ParticleStorage.ParticleIndices[ActiveIdx]);
        FParticleBeamPayload* Payload = GetBeamPayload(SlotIndex);
        if (!Payload)
        {
            continue;
        }

        Payload->BeamIndex = NextBeamIndex;
        NextBeamIndex = (NextBeamIndex + 1) % MaxBeams;

        const FBaseParticle* Particle = GetParticle(ActiveIdx);
        const uint32 Seed = Particle ? Particle->ParticleId : static_cast<uint32>(SlotIndex);
        GenerateNoiseSamples(Payload->NoiseSamples, NoiseFrequency, Seed);
    }
}

// Function : Tick beam emitter — base update + EnsureBeamState only
// Cycle 15a Phase 5: BuildVertexBuffer 호출 제거 — FDynamicBeamEmitterData::BuildFromInstance 가 vertex build 수행.
void FParticleBeamEmitterInstance::Tick(float DeltaTime, bool bAllowSpawning)
{
    FParticleEmitterInstance::Tick(DeltaTime, bAllowSpawning);
    EnsureBeamState();
}

// Function : Create Beam DynamicData (Cycle 15a Phase 3 + Phase 5)
// Phase 5 본문 이관 결과: FDynamicBeamEmitterData::BuildFromInstance 가 strip vertex 직접 build.
FDynamicEmitterDataBase* FParticleBeamEmitterInstance::CreateDynamicData()
{
    FDynamicBeamEmitterData* DynData = new FDynamicBeamEmitterData();
    DynData->EmitterIndex = GetEmitterIndex();

    FDynamicBeamEmitterReplayData& Replay = DynData->Source;
    Replay.ActiveParticleCount = GetActiveParticleCount();
    Replay.ParticleStride = GetParticleStride();
    Replay.ParticleSize = GetParticleSize();
    Replay.PayloadOffset = PayloadOffset;
    Replay.MaxActiveParticles = GetMaxActiveParticleCount();
    // TODO(multithread): switch to deep copy when render-thread separation lands
    Replay.ParticleData = ParticleStorage.ParticleData;
    // TODO(multithread): switch to deep copy when render-thread separation lands
    Replay.ParticleIndices = ParticleStorage.ParticleIndices;
    Replay.SortMode = ESortMode::None; // Beam Sort 는 D10 빈 구현 — SortMode 무관.

    const FCompiledParticleLODData* CompiledLOD = GetCurrentCompiledLODData();
    if (const UParticleBeamRendererProperties* BeamRenderer =
        CompiledLOD ? Cast<UParticleBeamRendererProperties>(CompiledLOD->RendererProperties) : nullptr)
    {
        Replay.InterpolationPoints = BeamRenderer->GetInterpolationPoints();
        Replay.Material = BeamRenderer->GetMaterial();
    }
    Replay.bHasNoise = (FindFirstBeamModule<UParticleModuleBeamNoise>(ResolveBeamSourceLOD(this)) != nullptr);
    Replay.ParticleTexture = nullptr; // Builder 가 Material.DiffuseMap 추출 후 채움.

    DynData->BuildFromInstance(*this);
    return DynData;
}
