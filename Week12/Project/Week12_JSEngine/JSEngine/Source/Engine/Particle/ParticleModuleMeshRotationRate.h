#pragma once

#include "Math/Vector.h"
#include "Particle/ParticleModule.h"

// Mesh emitter 의 per-particle 회전 속도 module (Cycle 14, M2).
// 결정 19 옵션 A: Spawn + Update 두 hook 모두 활성화 (Color/Size 검증 패턴 답습).
//   Spawn  : FMeshRotationPayload.RotRate 를 [Min, Max] 범위 random 으로 초기화.
//   Update : 매 frame chain (active) 순회 + payload.Rotation += RotRate * dt 누적.
//
// 본 module 은 **Mesh emitter 전용** — base Update 루프에서 호출되지만, 안에서 Cast 가 실패하면 early return (위험 13 방어).
// Mesh 가 아닌 emitter (Sprite/Ribbon/Beam) 에 mistakenly 추가되면 Update 진입에서 nullptr 검사로 안전하게 no-op.
//
// payload access path: FParticleMeshEmitterInstance::GetMeshPayload / GetMeshPayloadAt public helper (결정 20 옵션 A).
//   → base FParticleEmitterInstance 변경 0건 보장.
//
// Cycle 11 옵션 B 의 FMeshRotationPayload (36B, InitialOrientation/Rotation/RotRate 3 FVector) 슬롯 이미 존재 →
//   payload struct 변경 0건, sizeof 변경 0건, Stride 변경 0건.
UCLASS()
class UParticleModuleMeshRotationRate : public UParticleModule
{
public:
    GENERATED_BODY(UParticleModuleMeshRotationRate, UParticleModule)

    UParticleModuleMeshRotationRate();

    // Spawn hook — payload.RotRate 초기화 (RandomRange per axis).
    void Spawn(FParticleEmitterInstance* Owner, FBaseParticle& Particle, float SpawnTime) override;

    // Update hook — 모든 active particle 의 payload.Rotation 에 RotRate * dt 누적.
    // 위험 13 방어: Cast<FParticleMeshEmitterInstance>(Owner) 실패 시 early return.
    void Update(FParticleEmitterInstance* Owner, float DeltaTime) override;

    // Cycle 14 inspection: detail panel 의 inline edit 용 getter/setter.
    // Beam Noise / Source / Target 의 동일 패턴 답습 ([ParticleModuleBeamNoise.h]).
    const FVector& GetRotRateMin() const { return RotRateMin; }
    const FVector& GetRotRateMax() const { return RotRateMax; }
    void SetRotRateMin(const FVector& InMin) { RotRateMin = InMin; }
    void SetRotRateMax(const FVector& InMax) { RotRateMax = InMax; }

private:
    // Min/Max FVector per axis — Velocity / Location 모듈 패턴 답습 ([ParticleModules.h:103-124]).
    // default = ZeroVector → Cycle 11 옵션 B 동작 그대로 (회귀 안전: module 추가하기만 하고 값을 그대로 두면 효과 0).
    UPROPERTY(DisplayName = "Rotation Rate Min")
    FVector RotRateMin = FVector::ZeroVector;

    UPROPERTY(DisplayName = "Rotation Rate Max")
    FVector RotRateMax = FVector::ZeroVector;
};
