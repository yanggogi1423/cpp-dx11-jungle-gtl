# Particle Logic Overview

> 목적: JSEngine 의 파티클 시스템이 **어떻게 동작하는지** 를 자산/런타임/렌더 세 축으로 설명한다.
> 기존 [particle_class_relation.md](particle_class_relation.md) 가 "누가 누구를 소유/참조하는가" 의 레퍼런스 맵이라면, 본 문서는 "한 프레임이 어떻게 흘러가는가" 의 설명서다.
> 작성: 2026-05-28 (Cycle 15a Phase 5 + RendererProperties + Beam interpolation fix 반영)

---

## 0. 한 줄 요약

`UParticleSystem` 자산(설계도) → `UParticleSystemComponent`(런타임 호스트) → `FParticleEmitterInstance`(파티클 CPU 시뮬레이터) → `FDynamicEmitterData`(매 프레임 만들어지는 렌더 스냅샷) → `ParticleRenderPass`(GPU 드로) 4단 파이프라인이다.

```
[Asset]                  [Runtime]                    [Render]
UParticleSystem    ─►    UParticleSystemComponent
  └ UParticleEmitter      └ FParticleEmitterInstance[]   매 frame   FDynamicEmitterDataBase*
      └ UParticleLODLevel      └ ParticleStorage      ──────────►   (Sprite/Mesh/Ribbon/Beam)
          ├ RequiredModule         (raw uint8 버퍼)                       │
          ├ Modules[]                                                     ▼
          ├ TypeDataModule                                          ParticleRenderPass
          └ RendererProperties                                       (switch dispatch)
```

---

## 1. 핵심 개념 — "자산 트리 vs 런타임 인스턴스" 분리

JSEngine 파티클은 **불변(immutable) 설계도**와 **mutable 시뮬레이션 상태**를 분리한다.

| 측면 | 자산 측 (UObject 트리) | 런타임 측 (non-UObject POD) |
|-----|------------------------|------------------------------|
| 소유 | `UObjectManager` (GC) | `UParticleSystemComponent` (raw new/delete) |
| 직렬화 | `.particlesystem` 파일 | 직렬화 안 됨 (Template 으로부터 재생성) |
| 인스턴스 수 | emitter 당 1개 | component × emitter 개수만큼 |
| 클래스 | `UParticleSystem`, `UParticleEmitter`, `UParticleLODLevel`, `UParticleModule` | `FParticleEmitterInstance` (+ Mesh/Ribbon/Beam derived) |

> 같은 `UParticleSystem` 자산을 100개의 액터가 공유해도 자산 트리는 1번만 메모리에 올라간다. 각 액터의 컴포넌트가 자기만의 `FParticleEmitterInstance` 를 들고 시뮬레이션한다.

### 1.1 왜 분리되어 있나

- **자산 측**은 에디터에서 편집되는 디자인 파라미터. `UPROPERTY` 기반 reflection 으로 detail panel/직렬화 자동화.
- **런타임 측**은 매 프레임 변하는 active particle 데이터. UObject 인 척하면 GC 오버헤드 + `new uint8[]` raw 버퍼 관리 복잡. POD struct 로 두는 게 자연스럽다.
- **연결고리**: `FParticleEmitterInstance` 가 자기를 만든 `UParticleEmitter*`(SpriteTemplate) 와 `UParticleSystemComponent*`(Component) 를 back-ref 로 들고, 매 Tick 마다 자산 트리를 읽어 자기 버퍼를 갱신한다.

---

## 2. 자산 트리 (Asset Side)

[ParticleSystem.h](../JSEngine/Source/Engine/Particle/ParticleSystem.h) 의 3-레벨 트리.

```
UParticleSystem                              (.particlesystem 자산 단위)
  └─ TArray<UParticleEmitter*>               (emitter 1개 = "한 종류의 파티클")
       └─ TArray<UParticleLODLevel*>         (거리별 LOD; LOD0 = 가장 가까운 시점)
            ├─ UParticleModuleRequired*       (필수 — Material, lifetime 기본 등)
            ├─ TArray<UParticleModule*>       (Spawn/Update 동작 모듈들)
            ├─ UParticleModuleTypeDataBase*   (deprecated, 정상화 단계에서만 사용)
            └─ UParticleRendererProperties*   (현재의 렌더 정책 권위 — Sprite/Mesh/Ribbon/Beam 결정)
```

### 2.1 LOD 선택

- `UParticleEmitter::SelectLODLevel(distance)` 가 거리에 따라 인덱스 결정.
- `FParticleEmitterInstance::Tick` 매 프레임 호출 → `Component->ComputeEmitterLODDistance()` (카메라 거리) → `SelectLODLevel`.
- 단일 LOD 만 있어도 동작 (기본 `DistanceThreshold = 100.0f`).

### 2.2 RendererProperties — Render Mode 의 권위

이전 사이클까지 사용하던 `TypeDataModule` (Cascade 스타일) 는 deprecated 상태로 남아있고, 현재는 [`UParticleRendererProperties`](../JSEngine/Source/Engine/Particle/ParticleRendererProperties.h) 가 다음을 결정한다:

| 멤버 | 역할 |
|-----|------|
| `RenderMode` (enum) | Sprite / Mesh / Ribbon / Beam — instance 타입 dispatch 키 |
| `BlendType` | Opaque / AlphaBlend / Additive / NoColor (Mesh 제외, 아래 §6.2) |
| `Opacity` | asset default (AlphaBlend 시만 의미) |
| `RequiredPayloadBytes()` | particle stride 에 더해질 type-별 payload 바이트 수 |
| `CreateInstance(Component, EmitterIndex)` | `FParticleEmitter*Instance` derived 를 dispatch 생성 |

derived 4종:
- `UParticleSpriteRendererProperties` — payload 0B, base instance 생성
- `UParticleMeshRendererProperties` — `sizeof(FMeshRotationPayload)=36B`, `FParticleMeshEmitterInstance`. `Mesh`/`OverrideMaterial`/`Alignment` 보유
- `UParticleRibbonRendererProperties` — `sizeof(FRibbonParticlePayload)=32B`, `FParticleRibbonEmitterInstance`. `MaxTrailCount`/`MaxParticleInTrailCount`/`SheetsPerTrail`/`TangentSpawningScalar`/`Material`
- `UParticleBeamRendererProperties` — `sizeof(FParticleBeamPayload)=100B`, `FParticleBeamEmitterInstance`. `MaxBeamCount`/`InterpolationPoints`/`FallbackDistance`/`TextureTile*`/`Material`

`UParticleLODLevel::GetEffectiveRenderMode()` 가 우선순위(RendererProperties → deprecated TypeDataModule → 기본 Sprite)로 정규화한다.

### 2.3 모듈 카테고리

각 `UParticleModule` 은 두 플래그로 자기 역할을 선언한다.

| 플래그 | 의미 | 호출 시점 |
|-------|------|----------|
| `bSpawnModule` | particle 생성 시 1회 초기화 | `SpawnParticles()` 안에서 |
| `bUpdateModule` | 매 frame active 전체 갱신 | `Tick()` 의 UpdateModules 루프 |

대표 모듈:

| 모듈 | Spawn | Update | 역할 |
|------|:----:|:----:|------|
| `Required` | ✓ | — | RelativeTime/Lifetime/Color/Size 초기화 (LOD 마다 정확히 1개) |
| `Spawn` | — | — | `ComputeSpawnCount(Rate, dt)` — Tick 직접 호출 |
| `Burst` | — | ✓ | BurstTime 도달 시 `Owner->SpawnParticles(N)` |
| `Lifetime` | ✓ | — | LifetimeMin/Max 범위 lifetime 결정 |
| `Location` / `LocationShape` | ✓ | — | 초기 위치 (점/구/박스/콘) |
| `Velocity` | ✓ | — | 초기 속도 |
| `Acceleration` | — | ✓ | `V += A * dt` |
| `Drag` | — | ✓ | `V *= exp(-k*dt)` |
| `RotationRate` | ✓ | ✓ | spawn 시 RotRate 결정, update 시 Rotation 누적 |
| `Color` | ✓ | ✓ | StartColor 설정 + Lerp(Start, End, RelativeTime) |
| `Size` | ✓ | ✓ | 동일 패턴 |
| `Collision` | — | ✓ | World->SweepSingle → Bounce/Stop/Kill + Event 큐잉 |
| `EventGenerator` | — | ✓ | 큐된 collision event 를 컴포넌트로 dispatch |
| `SubUV` | ✓ | ✓ | atlas frame 인덱스 갱신 |
| `Light` | — | — | helper — Builder/lightpass 가 `ShouldCreateLight`/`BuildLightInfo` 호출 |
| `MeshRotationRate` | ✓ | ✓ | Mesh 전용 — payload 의 Rotation 누적 |
| `BeamSource` / `BeamTarget` / `BeamNoise` | — | — | data-only — Beam Instance/DynamicData 가 lookup |

---

## 3. 컴포넌트 ↔ 인스턴스 라이프사이클

[ParticleSystemComponent.h](../JSEngine/Source/Engine/Particle/ParticleSystemComponent.h) / [ParticleSystemComponent.cpp](../JSEngine/Source/Engine/Particle/ParticleSystemComponent.cpp).

### 3.1 Template 할당 → 인스턴스 생성

```
SetTemplate(UParticleSystem*)
  ├─ TemplateAssetPath.SetPath(InTemplate->GetAssetPath())   // soft path 갱신
  ├─ Template = InTemplate                                    // runtime cache
  └─ RecreateEmitterInstances()
       ├─ ClearEmitterInstances() (기존 delete)
       ├─ Template->CacheEmitterModuleInfo() (모든 LOD 의 SpawnModules/UpdateModules 캐시 빌드)
       └─ for each emitter i in Template->Emitters:
            ├─ LOD0->GetEffectiveRendererProperties()->CreateInstance(this, i)
            │   (RendererProperties 부재 시 base FParticleEmitterInstance 폴백)
            ├─ EmitterInstances.push_back(Instance)
            └─ Instance->Init(Template->Emitters[i], this, i)
                 ├─ SelectLODLevel(0)
                 ├─ ParticleSize = sizeof(FBaseParticle) + RequiredPayloadBytes()
                 ├─ MaxActiveParticles = SpawnModule->Rate * MaxLifetime + buffer
                 └─ ParticleStorage.Allocate(Max, ParticleSize, align=16)
                     ├─ ParticleData : new uint8[Max * Stride]
                     └─ ParticleIndices : new uint16[Max] = {0,1,...,Max-1}
```

### 3.2 Asset Path 직렬화 패턴 (TSoftObjectPtr)

`UParticleSystemComponent::Template` 자체는 직렬화하지 않는다 — 대신 `TSoftObjectPtr<UParticleSystem> TemplateAssetPath` 를 직렬화 (StaticMeshComponent 와 동일 패턴).

```cpp
UPROPERTY(DisplayName = "Template")
TSoftObjectPtr<UParticleSystem> TemplateAssetPath;  // serialized

UParticleSystem* Template = nullptr;                // runtime cache (non-UPROPERTY)
```

이유는 [project-asset-ref-pattern](../). `UParticleSystem::AssetPath` 가 비어 있어도 component 가 path 를 직접 보유하므로 scene save/load 라운드트립이 깨지지 않는다.

`Serialize(FArchive&)` override 에서 load 시 `FResourceManager::LoadParticleSystem(TemplateAssetPath.GetPath())` 호출 후 `Template` cache 채우고 `RecreateEmitterInstances`.

---

## 4. 시뮬레이션 한 프레임 (Tick)

```
UEngine Tick
└─ UParticleSystemComponent::TickComponent(dt)
   └─ for each Instance in EmitterInstances:
        Instance->Tick(dt, bAllowSpawning=true)
        │
        ├─ [1] LOD 재선택
        │      distance = Component->ComputeEmitterLODDistance()
        │      SelectLODLevel(distance) → CurrentLODLevel 갱신
        │
        ├─ [2] Spawn
        │      SpawnCount = SpawnModule->ComputeSpawnCount(Rate, dt)
        │                    └ ConsumeSpawnCount(): SpawnFraction 누적 후 정수부 추출
        │      SpawnParticles(SpawnCount, ...):
        │        for i in [0..SpawnCount):
        │          slot = ParticleIndices[ActiveParticles++]
        │          Particle = ParticleStorage 의 slot 위치
        │          for each SpawnModule (Required 먼저, 그 다음 일반 SpawnModules):
        │            Module->Spawn(this, *Particle, SpawnTime)
        │          [Mesh]   GetMeshPayload(slot) 초기화
        │          [Ribbon] EnsureTrailState + linked list prepend + HeadIndices[trail]=slot
        │          [Beam]   EnsureBeamState + GenerateNoiseSamples(seed=Particle.ParticleId)
        │
        ├─ [3] Move + Kill
        │      for ActiveIndex in [0..ActiveParticles):
        │        Particle = GetParticle(ActiveIndex)
        │        Particle.RelativeTime += dt / Particle.Lifetime
        │        if RelativeTime >= 1.0:
        │          KillParticle(ActiveIndex)   // swap-pop with ParticleIndices[--ActiveParticles]
        │        else:
        │          Particle.OldLocation = Particle.Location
        │          Particle.Location += Particle.Velocity * dt
        │
        ├─ [4] Update modules
        │      for each UpdateModule in CurrentLODLevel->GetUpdateModules():
        │        Module->Update(this, dt)
        │          (Burst/Accel/Drag/RotRate/Color/Size/Collision/EventGen/SubUV/MeshRotRate 등)
        │
        └─ [5] type-specific post-process
               [Ribbon] chain 순회 → tangent/distance 갱신 + BuildVertexBuffer()
               [Beam]   EnsureBeamState (vertex build 은 렌더 단에서)
               [Mesh]   (추가 작업 없음 — payload 만 갱신)
```

### 4.1 ParticleStorage 메모리 레이아웃

```
ParticleStorage.ParticleData (단일 alloc, 16-byte aligned):
┌─────────────────────┬─────────────────────┬─────────────────────┐
│ [FBaseParticle | payload] │ [FBaseParticle | payload] │ ... (slot 단위 stride) │
└─────────────────────┴─────────────────────┴─────────────────────┘
   ▲                   ▲                   ▲
   slot 0              slot 1              slot 2

ParticleStorage.ParticleIndices (uint16, 활성 슬롯의 간접 테이블):
┌────┬────┬────┬────┬────┬─────┐
│ 4  │ 2  │ 7  │ 0  │ ?  │ ... │   ◄── ActiveParticles 만큼 유효
└────┴────┴────┴────┴────┴─────┘
```

- `KillParticle(ActiveIndex)` 는 `ParticleIndices[ActiveIndex] ↔ ParticleIndices[ActiveParticles-1]` 만 swap → ActiveParticles--.
- raw bytes 와 payload 의 슬롯 인덱스(physical) 는 절대 안 바뀐다 → Ribbon 의 linked list Prev/Next 가 안전.
- Ribbon `KillParticle` 만 base swap-pop **전에** chain 재연결 추가.

### 4.2 모듈 → 컴포넌트 역참조

모듈은 `Owner` (= `FParticleEmitterInstance*`) 만 보지만, `Owner->GetOwningComponent()` 로 컴포넌트를 거슬러 올라간다.

| 호출 | 용도 |
|-----|------|
| `Owner->GetComponentWorldLocation()` | Location/LocationShape spawn |
| `Owner->QueueCollisionEvent(Event)` | Collision module → component 의 PendingCollisionEvents 에 push |
| `Owner->DispatchQueuedParticleEvents()` | EventGenerator → `Component->OnParticleCollide.Broadcast(...)` |
| `Owner->KillParticle(i)` | Collision module 의 Kill response |
| `Component->ComputeEmitterLODDistance()` | Collision module 의 카메라 거리 |
| `Component->GetCachedCameraPosition()` | Mesh PSA_FacingCameraPosition (렌더 단) |

---

## 5. 렌더 파이프라인 (DynamicData / ReplayData)

[ParticleDynamicData.h](../JSEngine/Source/Engine/Particle/ParticleDynamicData.h) — Cycle 15a 에서 도입된 frame-scoped 렌더 스냅샷 계층.

> 주의: 디렉토리에 "Database"/"ReplayData" 라는 이름이 있지만 **SQL/디스크 영속화와 무관**하다. 한 frame 동안 살았다가 RenderPass 끝에 delete 되는 POD 다.

### 5.1 두 계층 책임 분리

| 계층 | 역할 | 소유 모델 |
|-----|-----|----------|
| `FDynamicEmitterReplayDataBase` | particle bytes 의 얕은 snapshot + 메타데이터 (POD) | Material/Texture 는 ResourceManager 소유의 raw ptr |
| `FDynamicEmitterDataBase` | virtual 정점 생성 / Sort / VertexFactoryType / FillVertexBuffer | ReplayData 를 value 멤버로 소유 + 자기 CPU 정점 버퍼 |

derived 4종 (Sprite/Mesh/Ribbon/Beam) 가 각각 `BuildFromInstance(Instance&)` 에서 ReplayData 채우고 자기 CPU 정점 버퍼를 build 한다.

### 5.2 한 프레임 렌더 측 흐름

```
PrimitiveDrawCommandBuilder (EPT_ParticleSystem case)
  │
  ├─ [A] Camera 캐싱
  │      Component->CacheCameraFromRenderBus(RenderBus)
  │        → Component.CachedCameraPosition/Forward/Up/Right + bCachedCameraValid=true
  │
  ├─ [B] DynamicData 수집
  │      All = Component->CollectDynamicData()
  │        for each Instance:
  │          DynData = Instance->CreateDynamicData()   ◄── new (매 frame)
  │            (a) new FDynamic{Sprite|Mesh|Ribbon|Beam}EmitterData
  │            (b) Replay.ParticleData/Indices  = 얕은 복사 (Instance 메모리 가리킴)
  │                Replay.Active/Stride/Size/PayloadOffset/Material/...
  │                Replay.BlendType = RendererProperties->BlendType (frame snapshot)
  │            (c) DynData->BuildFromInstance(*Instance)
  │                  Sprite : SpriteInstanceDataBuffer fill (Pos/Size/Color/Rot/SubUV)
  │                  Mesh   : SpinMat × AlignMat (PSA_Velocity 또는 PSA_FacingCameraPosition)
  │                           → MeshInstanceDataBuffer fill
  │                  Ribbon : GetRibbonVertexData() snapshot
  │                  Beam   : Source/Target/Noise lookup
  │                           → segment loop + noise perturbation
  │                           → degenerate seam vertex 추가
  │
  ├─ [C] type별 메타 보충
  │      Source.Material / ParticleTexture / SubUV grid 채움
  │      ActiveParticleCount==0 → delete DynData (skip)
  │
  ├─ [D] Sort
  │      DynData->Sort(RenderBus.GetCameraPosition())
  │        Sprite/Mesh : ViewProjDepth back-to-front (InstanceDataBuffer 재배치)
  │        Ribbon/Beam : 빈 구현 (정렬 안 함)
  │
  └─ [E] Command 발행
         Cmd.DynamicData = DynData   ◄── ownership 이전
         RenderBus.AddCommand(ERenderPass::Particle, Cmd)

ParticleRenderPass.cpp:216
  switch (VertexFactoryType):
    Sprite / Mesh / Ribbon / Beam 각각의 Render*() 호출
      → DynData->FillVertexBuffer(Device, Ctx, FInstanceBuffer&) → DrawInstanced
  frame 끝: delete Cmd.DynamicData
```

### 5.3 DynamicData 라이프사이클의 핵심

- **Instance 는 DynamicData 를 들고 있지 않는다.** `CreateDynamicData()` 는 매 frame `new` 해서 caller(Builder) 에게 즉시 ownership 이전.
- **ReplayData 의 `ParticleData`/`ParticleIndices` 는 얕은 복사** — Instance 의 `ParticleStorage` 메모리를 그대로 가리킨다. 한 frame 동안만 유효. (단일 스레드 + frame-scope 이라 safe. 멀티스레드 도입 시 deep copy 전환 필요 — `// TODO(multithread)` 주석 박혀 있음.)
- **`.particlesystem` 자산 직렬화에 DynamicData 는 들어가지 않는다** — 매 프레임 일회용.

### 5.4 렌더 패스 순서

```
Opaque → Light → Fog → FXAA → Font → SubUV → Translucent → Particle → SelectionMask → Grid
```

파티클은 Translucent 패스 **뒤에** 그려진다 — 일반 반투명 메시와 파티클이 섞이는 씬에서는 깊이가 일치하지 않을 수 있다.

---

## 6. 4가지 Emitter 타입 — 차이 요약

| 항목 | Sprite | Mesh | Ribbon | Beam |
|-----|:------:|:----:|:------:|:----:|
| Instance 클래스 | `FParticleEmitterInstance` | `FParticleMeshEmitterInstance` | `FParticleRibbonEmitterInstance` | `FParticleBeamEmitterInstance` |
| Payload bytes | 0 | 36 (`FMeshRotationPayload`) | 32 (`FRibbonParticlePayload`) | 100 (`FParticleBeamPayload`) |
| RendererProperties | `Sprite` | `Mesh` — Mesh + OverrideMaterial + Alignment | `Ribbon` — MaxTrail/Sheets/Material | `Beam` — MaxBeam/InterpolationPoints/Material |
| 추가 모듈 | — | `MeshRotationRate` | — | `BeamSource`/`BeamTarget`/`BeamNoise` |
| Spawn 특수 처리 | — | payload zero-init | linked list prepend | noise samples generate |
| Tick 특수 처리 | — | — | tangent 갱신 + VertexBuffer 재구축 | EnsureBeamState |
| 정점 build 위치 | Builder | DynamicData::BuildFromInstance | Instance::Tick (VertexBuffer 멤버) | DynamicData::BuildFromInstance |
| Sort 구현 | ViewProjDepth | ViewProjDepth | (빈 구현) | (빈 구현) |
| BlendType 출처 | RendererProperties | **Material** (Mesh 만 다름) | RendererProperties | RendererProperties |

### 6.1 Mesh 가 다른 점

- Material 자산이 권위 — emitter-level BlendType 콤보는 detail panel 에 노출되지 않음.
- `Alignment` (PSA_Velocity / PSA_FacingCameraPosition / PSA_None) 가 정점 build 시 spin × alignment 매트릭스 합성.
- `IsCachedCameraValid()` false 면 PSA_FacingCameraPosition 은 PSA_Velocity 로 자동 fallback.

### 6.2 BlendType 적용 정책 (2026-05-27 변경)

- **Sprite/Ribbon/Beam**: `RendererProperties->BlendType` → frame snapshot → ParticleRenderPass 가 그대로 D3D BlendState 적용. Detail panel 에 콤보 (Opaque/AlphaBlend/Additive) 노출.
- **Mesh**: Material 의 BlendType 사용 — RendererProperties 의 콤보 없음 (의도된 비대칭).
- `Opaque` 면 Default depth, 그 외 DepthReadOnly 자동 전환.

### 6.3 Opacity 곱셈

```
finalAlpha = Color.a × Component.OpacityMultiplier × RendererProperties.Opacity
                       └ runtime fade            └ asset default
```

- `Cmd.PerObjectConstants.Color.W` 에 한 번에 주입.
- **AlphaBlend 일 때만 곱한다** (Additive/Opaque/NoColor 는 1.0 고정).
- Mesh 는 Material.BlendType 으로 판정.

---

## 7. 이벤트 시스템

### 7.1 컴포넌트 단위 이벤트 (실사용 경로)

```
UParticleModuleCollision::Update
  └ World->SweepSingle/LineTraceSingle → Hit?
       └ Owner->QueueCollisionEvent(FParticleEventCollideData{...})
            └ Component->PendingCollisionEvents.push_back

UParticleModuleEventGenerator::Update
  └ Owner->DispatchQueuedParticleEvents()
       └ Component->OnParticleCollide.Broadcast(EventData)
            (외부 리스너 호출)
```

- 동일 LOD 에 `Collision` 과 `EventGenerator` 가 같이 있어야 외부로 broadcast 됨.
- `FParticleEventCollideData::HitComponent`/`HitActor` 슬롯이 채워진다 (2026-05-27 기준).

### 7.2 레벨 단위 이벤트 허브 (`AParticleEventManager`)

- 자기 자신의 `UParticleSystemComponent` (preview 용) + `UParticleSystem* PreviewParticleSystem` 보유.
- `PushCollisionEvent` 호출자가 코드베이스 전체에 **여전히 0건** — placeable preview 액터 용도로만 사용. 컴포넌트 큐와는 독립.

---

## 8. 흔한 함정과 주의사항

| 함정 | 원인 | 진단 포인트 |
|-----|------|------------|
| **Sprite 가 Material BlendType 무시** | Sprite/Ribbon/Beam 은 RendererProperties.BlendType 만 봄 | RendererProperties detail panel 의 Blend Mode 콤보 확인 |
| **Mesh 의 PSA_FacingCameraPosition 깨짐** | Builder 가 `CacheCameraFromRenderBus` 호출 안 함 → bCachedCameraValid=false | EditorMainPanelDebug 같은 외부 호출 경로 점검; 자동으로 PSA_Velocity fallback |
| **Ribbon 이 보이지 않음/잘못 그려짐** | KillParticle 시 chain 재연결이 base swap-pop **전에** 일어나야 함 | `FParticleRibbonEmitterInstance::KillParticle` 의 순서 확인 |
| **Beam noise 가 frame 마다 깜빡임** | spawn 시 1회 generate 보장이 깨짐 (`std::mt19937(ParticleId)`) | `BeamNoiseMaxFrequency=8` 컴파일 타임 고정, lifetime 동안 NoiseSamples 불변 |
| **Translucent 메시와 파티클의 깊이 어긋남** | 렌더 순서가 Translucent → Particle | 같은 패스에 넣을 방법 현재 없음 |
| **`TypeDataModule = nullptr` 인데 emitter 가 Sprite 로 보임** | `UPROPERTY` 누락 시 .particlesystem 라운드트립 후 silent fallback | `UParticleLODLevel::TypeDataModule` 의 `UPROPERTY` 마크 확인 |
| **scene 로드 후 Template = nullptr** | UParticleSystem* 직접 직렬화 시 path 누락 | `TemplateAssetPath`(TSoftObjectPtr) 패턴 따르는지 확인 |
| **컴포넌트 소멸 후 Instance 가 살아 있음** | `~UParticleSystemComponent` 가 `ClearEmitterInstances` 호출 누락 시 leak | base `virtual ~FParticleEmitterInstance` 로 derived 안전 delete |
| **멀티스레드 도입 시 ReplayData 가 dangling** | `ParticleData`/`Indices` 가 얕은 복사 raw ptr | `// TODO(multithread)` 주석 위치에서 deep copy 전환 필요 |

---

## 9. 디렉토리 맵 (참고)

```
JSEngine/Source/Engine/Particle/
├─ ParticleTypes.h                       enum/POD (EParticleEmitterRenderMode, FBaseParticle, ...)
├─ ParticleSystem.{h,cpp}                자산 트리 (System/Emitter/LODLevel)
├─ ParticleModule.{h,cpp}                모듈 베이스
├─ ParticleModules.{h,cpp}               Required/Spawn/Burst/Lifetime/Location/.../EventGenerator/SubUV
├─ ParticleModuleTypeData*.{h,cpp}       deprecated TypeData (Sprite/Mesh/Ribbon/Beam)
├─ ParticleModuleBeam*.{h,cpp}           BeamSource/BeamTarget/BeamNoise
├─ ParticleModuleMeshRotationRate.{h,cpp}
├─ ParticleRendererProperties.{h,cpp}    현재의 렌더 정책 권위 (Sprite/Mesh/Ribbon/Beam derived)
├─ ParticleEmitterInstance.{h,cpp}       base instance + Init/Tick/Spawn/Kill
├─ ParticleMeshEmitterInstance.{h,cpp}   Mesh derived
├─ ParticleRibbonEmitterInstance.{h,cpp} Ribbon derived
├─ ParticleBeamEmitterInstance.{h,cpp}   Beam derived
├─ ParticleSystemComponent.{h,cpp}       UComponent (씬 진입점)
├─ ParticleDynamicData.{h,cpp}           ReplayData + DynamicData 4종
├─ ParticleEvent.{h,cpp}                 AParticleEventManager (placeable)
└─ Particle{Mesh,Ribbon,Beam}Types.h     payload/vertex POD

JSEngine/Source/Engine/Render/Renderer/RenderFlow/
└─ ParticleRenderPass.{h,cpp}            switch dispatch → 4 type 별 Render() + DrawInstanced
```

---
