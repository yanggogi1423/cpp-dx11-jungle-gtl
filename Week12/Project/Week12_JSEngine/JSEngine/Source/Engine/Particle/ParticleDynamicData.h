#pragma once

#include "Core/CoreMinimal.h"
#include "Render/Resource/RenderResources.h"
#include "Render/Resource/VertexFactoryTypes.h"
#include "Render/Resource/VertexTypes.h"
#include "Particle/ParticleRibbonTypes.h"

class FInstanceBuffer;
class UMaterialInterface;
class UTexture;
struct FParticleEmitterInstance;
struct ID3D11Device;
struct ID3D11DeviceContext;

// ReplayData / DynamicData 두 계층 인프라 (Cycle 15a, 결정 D1-D12).
//
// 동기 (D1): 멀티스레드 사전 인프라 + 도입 의의. 본 엔진은 현재 단일 스레드이므로
//            race-free 소유권 이전은 미적용. 단 향후 멀티스레드 분리 시 base 골격은 유지.
//
// 책임 분리:
//   FDynamicEmitterReplayDataBase = 한 frame raw particle bytes + 메타데이터 묶음 POD (스냅샷)
//   FDynamicEmitterDataBase       = 행위자 — ReplayData를 owned member로 보유 +
//                                    virtual 정점 생성/Sort/VertexFactoryType 노출
//
// Type별 derived는 동일 헤더에 정의 (Sprite/Mesh/Beam). Ribbon은 본 cycle 대상 외 (D6).

// Function : Identify emitter type for ReplayData / DynamicData carriers
// 본 enum은 ReplayData가 자기 emitter type을 식별하는 데 사용.
// 기존 EParticleEmitterRenderMode와 별개 — DynamicData 계층 전용 식별자.
enum class EDynamicEmitterType : uint8
{
    Sprite,
    Mesh,
    Beam,
    Ribbon, // 본 cycle 대상 외 (D6) — enum 자리만 확보, 사용 없음
    None,
};

// Function : Sort mode for DynamicData::Sort() virtual hook
// D9: 구조는 전부 enum/switch, 구현은 ViewProjDepth 1개.
// 나머지 3종 (DistanceToView / Age_OldestFirst / Age_NewestFirst)은 switch 분기만 자리 잡고 본문 TODO.
// 기존 EParticleSortMode (ParticleTypes.h:21-26)는 사용처 0건의 dead code — 본 enum과 별개로 신설.
enum class ESortMode : uint8
{
    None,
    ViewProjDepth,
    DistanceToView,
    Age_OldestFirst,
    Age_NewestFirst,
};

// Function : Common particle replay data snapshot — base for type-specific derivations
// 한 frame raw particle bytes + 메타데이터 묶음 POD.
// Instance가 CreateDynamicData() 시점에 얕은 복사로 채움 (D3).
//
// 얕은 복사 (raw 포인터): ParticleData / ParticleIndices는 Instance 소유 메모리를 참조만 함.
// 한 frame 동안만 유효 — Instance가 메모리 reallocate 또는 destroy 시 dangling.
// 현재는 단일 스레드 + frame-scope이므로 안전.
struct FDynamicEmitterReplayDataBase
{
    EDynamicEmitterType eEmitterType = EDynamicEmitterType::None;

    // 메타데이터 (D3: 5 필드)
    int32 ActiveParticleCount = 0;
    int32 ParticleStride = 0;
    int32 ParticleSize = 0;
    int32 PayloadOffset = 0;
    int32 MaxActiveParticles = 0;

    // TODO(multithread): switch to deep copy when render-thread separation lands
    const uint8* ParticleData = nullptr;
    // TODO(multithread): switch to deep copy when render-thread separation lands
    const uint16* ParticleIndices = nullptr;

    // 정렬 모드 (D8/D9). DynamicData::Sort()에서 분기.
    ESortMode SortMode = ESortMode::None;

    // 머티리얼/텍스처 리소스 핸들 — Builder가 Cmd에 매핑할 때 사용.
    // raw pointer owned by the asset/resource manager.
    UMaterialInterface* Material = nullptr;
    UTexture* ParticleTexture = nullptr;

    // Per-emitter blend mode (RendererProperties->BlendType 의 frame snapshot).
    // Sprite/Ribbon/Beam render path 에서 D3D BlendState 결정에 사용.
    EBlendType BlendType = EBlendType::AlphaBlend;

    virtual ~FDynamicEmitterReplayDataBase() = default;
};

// Function : Behavior carrier — owns ReplayData and exposes virtual hooks for render-side
// Builder/RenderPass는 DynamicData* 한 포인터로 모든 type을 dispatch.
// 매 frame new (D2): Component->CollectDynamicData()에서 생성, RenderPass가 frame 끝에 delete.
struct FDynamicEmitterDataBase
{
public:
    // 어느 emitter에서 만들어졌는지 식별 (디버그/멀티 emitter 추적용).
    int32 EmitterIndex = -1;

    virtual ~FDynamicEmitterDataBase() = default;

    // ReplayData 참조 노출 — Builder가 메타데이터 read 시 사용.
    virtual const FDynamicEmitterReplayDataBase& GetSource() const = 0;

    // 정점 stride (per-instance 또는 per-vertex stride) — InstanceBuffer.Create에 사용.
    virtual int32 GetVertexStride() const = 0;

    // VertexFactoryType — Builder가 Cmd.VertexFactoryType에 set + RenderPass의 dispatch switch에 사용.
    virtual EVertexFactoryType GetVertexFactoryType() const = 0;

    // CPU-side 정점 데이터를 GPU InstanceBuffer로 upload. RenderPass helper가 호출.
    // helper는 D3D state setup + DrawInstanced만 담당 (데이터 fetch + Map/Unmap 은 본 hook).
    // Device/DeviceContext 인자는 FInstanceBuffer::Update 가 grow-by-2x 시 ID3D11Device::CreateBuffer 필요해서.
    virtual void FillVertexBuffer(ID3D11Device* Device, ID3D11DeviceContext* DeviceContext, FInstanceBuffer& InOutBuffer) = 0;

    // 정렬 hook (D8). default empty — Beam은 빈 구현, Sprite/Mesh는 override.
    // ParticleIndices를 재배치 (raw data는 불변 — 얕은 복사 무결성 보존).
    virtual void Sort(const FVector& /*CameraPos*/) {}
};

// ──────────────────────────────────────────────────────────────────────────────
// Sprite derived (Phase 2)
// ──────────────────────────────────────────────────────────────────────────────

// Function : Sprite-specific replay snapshot — base가 이미 모든 필드 보유, derived 고유 필드 없음
// Sprite는 ParticleData + Texture/Material 외 type-specific 메타 없음.
struct FDynamicSpriteEmitterReplayData : public FDynamicEmitterReplayDataBase
{
    FDynamicSpriteEmitterReplayData()
    {
        eEmitterType = EDynamicEmitterType::Sprite;
    }

    // SubUV grid (Sprite 전용 — Cmd.ParticleSubUVColumns/Rows에 매핑).
    uint32 SubUVColumns = 1;
    uint32 SubUVRows = 1;
};

// Function : Sprite DynamicData — owns Sprite InstanceData CPU buffer + virtual hooks
// Instance의 SpriteInstanceDataBuffer를 본 클래스로 이관 (D7).
// FillVertexBuffer는 RenderPass의 InstanceBuffer (slot 1 stream)에 push.
struct FDynamicSpriteEmitterData : public FDynamicEmitterDataBase
{
public:
    FDynamicSpriteEmitterReplayData Source;

    // CPU-side Sprite instance data 배열. CreateDynamicData가 채우고 FillVertexBuffer가 GPU로 push.
    TArray<FSpriteParticleInstanceData> SpriteInstanceDataBuffer;

    ~FDynamicSpriteEmitterData() override = default;

    const FDynamicEmitterReplayDataBase& GetSource() const override { return Source; }
    int32 GetVertexStride() const override { return static_cast<int32>(sizeof(FSpriteParticleInstanceData)); }
    EVertexFactoryType GetVertexFactoryType() const override { return EVertexFactoryType::SpriteParticle; }

    // Instance측 시뮬레이션 상태를 읽어 SpriteInstanceDataBuffer를 채움.
    // 본래 FParticleEmitterInstance::BuildInstanceData()에 있던 Sprite path 본문 이관.
    // Instance는 raw particle data 소유자 — 본 메서드는 instance를 통해 GetParticle 호출.
    void BuildFromInstance(const FParticleEmitterInstance& Instance);

    void FillVertexBuffer(ID3D11Device* Device, ID3D11DeviceContext* DeviceContext, FInstanceBuffer& InOutBuffer) override;

    // ViewProjDepth 정렬 (D9, D10):
    //   1. CameraPos 받음 (Component 캐시 또는 RenderBus에서 외부 전달)
    //   2. 각 active particle의 view-projection depth 산출
    //   3. ParticleIndices를 back-to-front로 재배치 (raw data는 안 건드림 — 얕은 복사 무결성 보존)
    // ParticleIndices의 mutable 접근 필요 — Source의 const uint16* 는 const_cast 사용.
    // (단일 스레드 가정 — Instance가 같은 ParticleIndices를 동시 접근하지 않음.)
    void Sort(const FVector& CameraPos) override;
};

// ──────────────────────────────────────────────────────────────────────────────
// Mesh derived (Phase 3)
// ──────────────────────────────────────────────────────────────────────────────

// Function : Mesh-specific replay snapshot — base 필드 + Mesh 고유 메타
struct FDynamicMeshEmitterReplayData : public FDynamicEmitterReplayDataBase
{
    FDynamicMeshEmitterReplayData()
    {
        eEmitterType = EDynamicEmitterType::Mesh;
    }

    // Mesh asset 핸들은 ReplayData가 들고 다님 — Builder가 Cmd.MeshBuffer에 매핑할 때 사용.
    // raw pointer (asset 소유 — ResourceManager).
    class UStaticMesh* MeshAsset = nullptr;
};

// Function : Mesh DynamicData — owns Mesh InstanceData CPU buffer + virtual hooks
// Instance의 MeshInstanceDataBuffer를 본 클래스로 이관 (D7) — Phase 5에서 Instance 멤버 삭제.
struct FDynamicMeshEmitterData : public FDynamicEmitterDataBase
{
public:
    FDynamicMeshEmitterReplayData Source;

    // CPU-side Mesh instance data 배열. CreateDynamicData가 채우고 FillVertexBuffer가 GPU로 push.
    TArray<FMeshParticleInstanceData> MeshInstanceDataBuffer;

    ~FDynamicMeshEmitterData() override = default;

    const FDynamicEmitterReplayDataBase& GetSource() const override { return Source; }
    int32 GetVertexStride() const override { return static_cast<int32>(sizeof(FMeshParticleInstanceData)); }
    EVertexFactoryType GetVertexFactoryType() const override { return EVertexFactoryType::MeshParticle; }

    // Phase 3: instance의 기존 GetMeshInstanceData(count)를 snapshot.
    // Phase 5에서 본문이 이관되어 직접 build 하도록 진화 예정.
    void BuildFromInstance(const FParticleEmitterInstance& Instance);

    void FillVertexBuffer(ID3D11Device* Device, ID3D11DeviceContext* DeviceContext, FInstanceBuffer& InOutBuffer) override;

    // Mesh ViewProjDepth 정렬 — Sprite 와 동일 방식 (instance buffer 직접 정렬).
    void Sort(const FVector& CameraPos) override;
};

// ──────────────────────────────────────────────────────────────────────────────
// Beam derived (Phase 3)
// ──────────────────────────────────────────────────────────────────────────────

// Function : Beam-specific replay snapshot — base 필드 + Beam 고유 메타
struct FDynamicBeamEmitterReplayData : public FDynamicEmitterReplayDataBase
{
    FDynamicBeamEmitterReplayData()
    {
        eEmitterType = EDynamicEmitterType::Beam;
    }

    // Beam 고유 메타 — 디버그/툴 hint. RendererProperties 에서 추출 후 set.
    int32 InterpolationPoints = 0;
    bool bHasNoise = false;
};

// Function : Beam DynamicData — owns Beam VertexBuffer (per-vertex CPU array) + virtual hooks
// Instance의 VertexBuffer를 본 클래스로 이관 (D7) — Phase 5에서 Instance 멤버 삭제.
struct FDynamicBeamEmitterData : public FDynamicEmitterDataBase
{
public:
    FDynamicBeamEmitterReplayData Source;

    // CPU-side Beam strip vertex 배열 (per-vertex, NOT per-instance — slot 0 indexless strip).
    TArray<FBeamParticleVertex> BeamVertexBuffer;

    ~FDynamicBeamEmitterData() override = default;

    const FDynamicEmitterReplayDataBase& GetSource() const override { return Source; }
    int32 GetVertexStride() const override { return static_cast<int32>(sizeof(FBeamParticleVertex)); }
    EVertexFactoryType GetVertexFactoryType() const override { return EVertexFactoryType::BeamParticle; }

    // Phase 3: instance 의 기존 GetBeamVertexData(count) snapshot.
    // Phase 5 에서 BuildVertexBuffer 본문이 이관되어 직접 build 하도록 진화.
    void BuildFromInstance(const FParticleEmitterInstance& Instance);

    void FillVertexBuffer(ID3D11Device* Device, ID3D11DeviceContext* DeviceContext, FInstanceBuffer& InOutBuffer) override;

    // Beam Sort: D10 — 빈 구현. Additive blending 통상 + strip vertex 순서 의존.
    // Beam typically uses additive blending; sort is no-op by design
    void Sort(const FVector& /*CameraPos*/) override {}
};

// ──────────────────────────────────────────────────────────────────────────────
// Ribbon placeholder (Phase 4, 옵션 C)
// ──────────────────────────────────────────────────────────────────────────────
// Ribbon은 본 cycle 대상 외 (D6) — ParticleRibbonEmitterInstance.h/.cpp 무수정.
// 단 RenderCommand 슬롯 통합 (D4) 영향으로 Ribbon도 DynamicData* 슬롯으로 데이터 전달 필요.
// 본 placeholder는 RibbonVertexBuffer snapshot만 함 — Ribbon 시뮬레이션은 instance가 그대로 수행.
// 본 cycle 종료 후 별도 cycle에서 Ribbon-specific Sort/메타 확장 가능.

struct FDynamicRibbonEmitterReplayData : public FDynamicEmitterReplayDataBase
{
    FDynamicRibbonEmitterReplayData()
    {
        eEmitterType = EDynamicEmitterType::Ribbon;
    }
};

struct FDynamicRibbonEmitterData : public FDynamicEmitterDataBase
{
public:
    FDynamicRibbonEmitterReplayData Source;

    // CPU-side Ribbon strip vertex 배열 — instance의 RibbonVertexBuffer 를 snapshot.
    TArray<FRibbonParticleVertex> RibbonVertexBuffer;

    ~FDynamicRibbonEmitterData() override = default;

    const FDynamicEmitterReplayDataBase& GetSource() const override { return Source; }
    int32 GetVertexStride() const override { return static_cast<int32>(sizeof(FRibbonParticleVertex)); }
    EVertexFactoryType GetVertexFactoryType() const override { return EVertexFactoryType::RibbonParticle; }

    // Phase 4 placeholder: instance 의 기존 GetRibbonVertexData(count) snapshot.
    void BuildFromInstance(const FParticleEmitterInstance& Instance);

    void FillVertexBuffer(ID3D11Device* Device, ID3D11DeviceContext* DeviceContext, FInstanceBuffer& InOutBuffer) override;

    // Ribbon Sort: 본 cycle placeholder — 빈 구현.
    void Sort(const FVector& /*CameraPos*/) override {}
};
