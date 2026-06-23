#pragma once

#include "Particle/ParticleEmitterInstance.h"
#include "Particle/ParticleRibbonTypes.h"

// Ribbon emitter용 instance (Cycle 12, 결정 6 옵션 A + 결정 8 옵션 A).
// base FParticleEmitterInstance 파생.
// override 4종:
//   SpawnParticles : base 호출 후 신규 SlotIndex의 payload 초기화 + linked list prepend + tangent 초기화.
//   KillParticle   : base swap-pop 전에 chain 재연결 + HeadIndices 갱신 (head 가 죽는 경우).
//   Tick           : base Tick 호출 후 chain 순회로 tangent/distance 갱신 + VertexBuffer rebuild.
//   GetRibbonVertexData : Builder가 RenderCommand 슬롯에 매핑하도록 VertexBuffer 노출.
struct FParticleRibbonEmitterInstance : public FParticleEmitterInstance
{
public:
    FParticleRibbonEmitterInstance() = default;
    ~FParticleRibbonEmitterInstance() override = default;

    void Tick(float DeltaTime, bool bAllowSpawning) override;
    void SpawnParticles(int32 Count, float StartTime, float Increment, const FVector& InitialLocation,
                        const FVector& InitialVelocity, struct FParticleEventInstancePayload* EventPayload = nullptr) override;
    void KillParticle(int32 Index) override;
    const FRibbonParticleVertex* GetRibbonVertexData(uint32& OutCount) const override;

private:
    // SlotIndex(physical) 기반 payload 포인터. swap-pop이 ParticleIndices만 swap하므로 SlotIndex 불변 → 안전.
    FRibbonParticlePayload* GetRibbonPayload(int32 SlotIndex);
    // SlotIndex(physical) 기반 base particle 포인터. ActiveIndex 우회 (chain 의 Next/Prev 가 SlotIndex 저장).
    FBaseParticle* GetParticleBySlot(int32 SlotIndex);

    // HeadIndices 재구성 — MaxTrailCount 변경 또는 첫 Tick 진입 시 호출.
    // Ribbon renderer properties를 .cpp에서 Cast 수행.
    void EnsureTrailState();

    // strip 정점 매 frame rebuild — silent bug λ 패턴 유지 (Mesh 와 동일).
    void BuildVertexBuffer();

private:
    // 각 trail 의 chain head SlotIndex (size = MaxTrailCount, init -1 = empty trail).
    // SpawnParticles에서 prepend, KillParticle에서 head death 시 NextIndex 로 갱신.
    TArray<int32> HeadIndices;

    // round-robin trail 분배 — SpawnParticles에서 ++NextTrailIndex % MaxTrailCount.
    int32 NextTrailIndex = 0;

    // chain head 마다 누적 tangent/distance 계산 후 strip 정점 채움 — slot 0 dynamic VB 의 source.
    TArray<FRibbonParticleVertex> VertexBuffer;
};
