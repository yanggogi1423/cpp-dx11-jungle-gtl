#pragma once

#include "Math/Vector.h"
#include "Particle/ParticleBeamTypes.h"
#include "Particle/ParticleModule.h"

// Beam emitter 의 Noise 옵션 모듈 (Cycle 13b — 분기 1 B-2 + 분기 2 A + 분기 3 B + 분기 4 A).
// 본 모듈은 단순 데이터 컨테이너 — Spawn/Update 동작 없음. Beam instance 가:
//   - SpawnParticles 에서 LOD->Modules 순회로 본 모듈을 찾고 GetFrequency() 호출 → NoiseSamples 생성
//   - BuildVertexBuffer 에서 동일 lookup → GetNoiseRange/IsTargetNoise/IsSmooth 로 perturbation 분기
//
// 분기 1 B-2: per-particle 영구 sample 캡처 — bNoiseLock=true 의미 (UPROPERTY 미도입 — 본 모드 고정).
//             NoiseSpeed UPROPERTY 도 미도입 (per-frame 모드용 — 본 cycle 외).
// 분기 3 B: Beam-local 좌표 — NoiseRange 의 X/Y/Z 채널은 Tangent/Perp1/Perp2 축 의미.
UCLASS()
class UParticleModuleBeamNoise : public UParticleModule
{
public:
    GENERATED_BODY(UParticleModuleBeamNoise, UParticleModule)

    int32   GetFrequency() const     { return Frequency; }
    FVector GetNoiseRange() const    { return NoiseRange; }
    bool    IsTargetNoise() const    { return bTargetNoise; }
    bool    IsSmooth() const         { return bSmooth; }

    void SetFrequency(int32 InValue)        { Frequency = InValue; }
    void SetNoiseRange(const FVector& In)   { NoiseRange = In; }
    void SetTargetNoise(bool bIn)           { bTargetNoise = bIn; }
    void SetSmooth(bool bIn)                { bSmooth = bIn; }

private:
    // 분기 2 A: Default=4, Min=1, Max=BeamNoiseMaxFrequency=8 (REFLECTION_GUIDE.md §2.2 의 Min/Max 옵션).
    // payload 의 NoiseSamples[BeamNoiseMaxFrequency] 가 컴파일 타임 8 고정 → Max 가 그 이상이면 buffer overrun.
    UPROPERTY(DisplayName = "Frequency", Category = "Beam Noise", Min = 1, Max = 8)
    int32 Frequency = 4;

    // 분기 3 B: Beam-local 좌표 — X=Tangent 축 amplitude, Y=Perp1 amplitude, Z=Perp2 amplitude.
    // Default (0, 30, 30): tangent 축 흔들림 없음 (beam 길이 유지), perp 방향으로 30 단위 흔들림 (lightning bolt).
    UPROPERTY(DisplayName = "Noise Range", Category = "Beam Noise")
    FVector NoiseRange = FVector(0.0f, 30.0f, 30.0f);

    // 분기 4 A: false 면 Target 끝점은 정확히 TargetComponent 위치 (lightning 끝이 target 에 명확히 도달).
    // true 면 Target 끝점도 noise perturb (불꽃 끝이 분산됨).
    UPROPERTY(DisplayName = "Target Noise", Category = "Beam Noise")
    bool bTargetNoise = false;

    // false 면 nearest sample (꺽인 직선), true 면 linear interp (부드러운 곡선).
    UPROPERTY(DisplayName = "Smooth", Category = "Beam Noise")
    bool bSmooth = false;
};
