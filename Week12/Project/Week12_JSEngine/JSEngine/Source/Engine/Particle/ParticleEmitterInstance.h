#pragma once

#include "Particle/ParticleSystem.h"
#include "Render/Resource/VertexTypes.h"

class UParticleSystemComponent;

// Ribbon path 의 getter (D6: Ribbon 시뮬레이션 코드 무수정 — 본 virtual 유지).
struct FRibbonParticleVertex;

// Cycle 15a (ReplayData/DynamicData 인프라): base가 자기 type의 DynamicData를 만들어 반환.
// Sprite/Ribbon 은 base default 가 분기 처리, Mesh/Beam derived 가 override.
struct FDynamicEmitterDataBase;

struct FParticleEmitterInstance
{
public:
    FParticleEmitterInstance() = default;
    // 가상 소멸자 — Mesh/Ribbon/Beam 파생 instance가 base 포인터로 delete 될 때 derived 소멸자 호출 보장.
    // 누락 시 Cycle 11+에서 leak 발현하므로 base 단독 cycle에서 미리 도입.
    virtual ~FParticleEmitterInstance();

    void Init(UParticleEmitter* InTemplate, UParticleSystemComponent* InComponent, int32 InEmitterIndex);
    void Reset();
    virtual void Tick(float DeltaTime, bool bAllowSpawning);
    void SelectLODLevel(float Distance);
    virtual void SpawnParticles(int32 Count, float StartTime, float Increment, const FVector& InitialLocation,
                                const FVector& InitialVelocity, struct FParticleEventInstancePayload* EventPayload = nullptr);
    virtual void KillParticle(int32 Index);

    // RendererProperties payload byte 수 조회 helper. RendererProperties 부재 시 0.
    // Init에서 ParticleStride 계산에 사용. 향후 Ribbon/Beam이 override 가능성 있으나 본 cycle은 default.
    virtual int32 GetRequiredPayloadBytes() const;

    // Cycle 15a (ReplayData/DynamicData, D2): 매 frame new — 호출자가 ownership 가져감.
    // 호출자(Component::CollectDynamicData)가 RenderCommand에 매핑 후
    // frame 끝에 RenderPass가 delete (frame-scope life-cycle, 단일 스레드 안전).
    //
    // base 구현 = RenderMode 분기 (Sprite default + Ribbon placeholder).
    // Mesh/Beam derived 가 override 해 자기 type DynamicData 반환.
    virtual FDynamicEmitterDataBase* CreateDynamicData();

    // Ribbon getter — D6 (Ribbon 시뮬레이션 무수정) 보장 위해 유지.
    // base default = nullptr 반환. Ribbon derived 가 override.
    // 본 메서드는 FDynamicRibbonEmitterData::BuildFromInstance 가 snapshot 위해 호출.
    virtual const FRibbonParticleVertex* GetRibbonVertexData(uint32& OutCount) const;

    // Getter
    int32 GetActiveParticleCount() const { return ActiveParticles; }
    int32 GetMaxActiveParticleCount() const { return MaxActiveParticles; }
    int32 GetParticleStride() const { return ParticleStorage.GetStride(); } // Cycle 10d: container로 위임
    int32 GetParticleSize() const { return ParticleSize; }
    int32 GetParticleMemoryBytes() const { return ParticleStorage.GetMemoryBytes(); }

    const uint8* GetParticleData() const { return ParticleStorage.ParticleData; }
    const uint16* GetParticleIndices() const { return ParticleStorage.ParticleIndices; }

    UParticleEmitter* GetTemplate() const { return SpriteTemplate; }
    UParticleLODLevel* GetCurrentLODLevel() const { return CurrentLODLevel; }
    const FCompiledParticleLODData* GetCurrentCompiledLODData() const { return CurrentCompiledLOD; }
    int32 GetCurrentLODLevelIndex() const { return CurrentLODLevelIndex; }
    int32 GetEmitterIndex() const { return EmitterIndex; }
    uint32 GetParticleCounter() const { return ParticleCounter; }
    float GetEmitterTime() const { return EmitterTime; }
    float GetPreviousEmitterTime() const { return PreviousEmitterTime; }

    FBaseParticle* GetParticle(int32 ActiveIndex);
    const FBaseParticle* GetParticle(int32 ActiveIndex) const;

    UParticleSystemComponent* GetComponent() const { return Component; }
    FVector GetComponentWorldLocation() const;
    UParticleSystemComponent* GetOwningComponent() const { return Component; }
    bool UsesLocalSpace() const;
    FVector ResolveParticleLocationForRender(const FVector& ParticleLocation) const;
    FVector ResolveParticleVectorForRender(const FVector& ParticleVector) const;

    void QueueCollisionEvent(const FParticleEventCollideData& EventData);
    void DispatchQueuedParticleEvents();
    int32 ConsumeSpawnCount(float Rate, float DeltaTime);

	bool CanRebindCompiledLOD(const FCompiledParticleLODData* NewLOD) const;
    void RebindCompiledLOD(float Distance);

protected:
    // Cycle 11: derived (Mesh/Ribbon/Beam) instance가 payload 영역에 접근하기 위해 protected로 노출.
    // ParticleStorage는 container 자체가 public 멤버 (ParticleData/Indices/Stride)를 제공.
    // PayloadOffset/ActiveParticles는 derived의 Spawn override + BuildInstanceData에 필요.
    // 외부(component/builder)는 여전히 public getter만 사용.
    FParticleDataContainer ParticleStorage;
    int32 PayloadOffset = 0;
    int32 ActiveParticles = 0;

private:
    UParticleEmitter* SpriteTemplate = nullptr;
    UParticleSystemComponent* Component = nullptr;
    int32 EmitterIndex = -1;

    int32 CurrentLODLevelIndex = 0;
    UParticleLODLevel* CurrentLODLevel = nullptr;
    const FCompiledParticleLODData* CurrentCompiledLOD = nullptr;
    // 실제 데이터들, memory pool and live data — ParticleStorage/PayloadOffset/ActiveParticles는 위 protected로 이동.
    // Cycle 15a Phase 5: InstanceData / InstancePayloadSize 멤버 삭제됨 (dead state — 할당 path 부재 + read 0건).
    int32 ParticleSize = sizeof(FBaseParticle);
    // Cycle 10d: ParticleStride 멤버 삭제 — source-of-truth가 FParticleDataContainer로 이전.
    // 외부 read는 GetParticleStride() (container.GetStride() 위임) 또는 ParticleStorage.GetStride() 직접.
    uint32 ParticleCounter = 0;
    int32 MaxActiveParticles = 0;
    float SpawnFraction = 0.0f;
    float EmitterTime = 0.0f;
    float PreviousEmitterTime = 0.0f;

	uint32 ObservedCompiledRevision = 0; // Cycle 10e: CompiledRevision 관찰용 (LOD 변경 감지) — Init에서 초기화, Tick에서 비교 후 필요 시 LOD 재선택.
    int32 ObservedPayloadSize = 0;
    int32 ObservedParticleStride = 0;
    EParticleEmitterRenderMode ObservedRenderMode = EParticleEmitterRenderMode::Sprite;
    // Cycle 15a Phase 5 (D7): SpriteInstanceDataBuffer 멤버 삭제 — FDynamicSpriteEmitterData 가 소유.
};
