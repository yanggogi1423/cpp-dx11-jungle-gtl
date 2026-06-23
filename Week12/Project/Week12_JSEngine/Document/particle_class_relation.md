# Particle Class Ownership / Reference Map

`JSEngine/Source/Engine/Particle/` 내부 클래스들의 **소유(owns) / 참조(refs / weak)** 관계를 `UParticleSystemComponent`를 진입점으로 정리한다.

- **owns** : 해당 객체의 lifetime을 책임진다 (raw `new`/`delete`, value 멤버, `UPROPERTY`로 묶여 직렬화/GC 등 UObject 관리).
- **refs** : 다른 객체가 소유한 인스턴스를 단순히 가리킨다 (back-pointer, lookup 캐시, 행위 호출용).
- 화살표 방향은 **소유/참조하는 쪽 → 대상**.

> 파일 위치: 모든 클래스는 [JSEngine/Source/Engine/Particle/](JSEngine/Source/Engine/Particle/) 아래에 있다.

> ⚠️ 본 디렉토리에는 **SQL/key-value Database 시스템이 존재하지 않는다**. 본 문서에서 "DB"는 Cycle 15a에 도입된 **DynamicData / ReplayData 계층**(`FDynamicEmitter*DataBase`, base 클래스명 suffix가 "DataBase")을 의미한다 — git 로그의 "database done" / "emitter dynamic data base merge" 커밋 대상.

---

## 1. 파일별 클래스 인벤토리

| 파일 | 정의된 타입 | 종류 |
|------|------------|------|
| [ParticleTypes.h](JSEngine/Source/Engine/Particle/ParticleTypes.h) | `EParticleEmitterRenderMode`, `EParticleSortMode`, `FBaseParticle`, `FParticleDataContainer`, `FParticleEventCollideData` | enum / POD struct |
| [ParticleUpdateUtils.h](JSEngine/Source/Engine/Particle/ParticleUpdateUtils.h) | `PARTICLE_PTR` 매크로, `DECLARE_PARTICLE_PTR`, `BEGIN/END_UPDATE_LOOP`, `GetParticleDirect()` | 인라인 유틸 (현재 외부 참조 없음 — dead) |
| [ParticleModule.h/.cpp](JSEngine/Source/Engine/Particle/ParticleModule.h) | `UParticleModule`, `EParticleDistributionRuntimeKind`, `FParticleDistributionRuntimeData` | UObject (모듈 베이스) + 분포 런타임 POD |
| [ParticleModules.h/.cpp](JSEngine/Source/Engine/Particle/ParticleModules.h) | `UParticleModuleRequired`, `UParticleModuleSpawn`, `UParticleModuleBurst`, `UParticleModuleLifetime`, `UParticleModuleLocation`, `UParticleModuleLocationShape`(+`EProceduralParticleShape`), `UParticleModuleVelocity`, `UParticleModuleAcceleration`, `UParticleModuleDrag`, `UParticleModuleRotationRate`, `UParticleModuleColor`, `UParticleModuleLight`, `UParticleModuleSize`, `UParticleModuleCollision`(+`EParticleCollisionResponse`, `EParticleCollisionTraceMode`), `UParticleModuleEventGenerator`, `USubUVModule`(+`EParticleSubUVPlaybackMode`) | UObject (구체 모듈) |
| [ParticleModuleTypeData.h/.cpp](JSEngine/Source/Engine/Particle/ParticleModuleTypeData.h) | `UParticleModuleTypeDataBase`, `USpriteTypeData` | UObject (TypeData 베이스 + Sprite) |
| [ParticleModuleTypeDataMesh.h/.cpp](JSEngine/Source/Engine/Particle/ParticleModuleTypeDataMesh.h) | `UMeshTypeData` | UObject (Mesh TypeData) |
| [ParticleModuleTypeDataRibbon.h/.cpp](JSEngine/Source/Engine/Particle/ParticleModuleTypeDataRibbon.h) | `URibbonTypeData` | UObject (Ribbon TypeData) |
| [ParticleModuleTypeDataBeam.h/.cpp](JSEngine/Source/Engine/Particle/ParticleModuleTypeDataBeam.h) | `UBeamTypeData` | UObject (Beam TypeData) |
| [ParticleModuleBeamSource.h/.cpp](JSEngine/Source/Engine/Particle/ParticleModuleBeamSource.h) | `UParticleModuleBeamSource` | UObject (Beam source picker) |
| [ParticleModuleBeamTarget.h/.cpp](JSEngine/Source/Engine/Particle/ParticleModuleBeamTarget.h) | `UParticleModuleBeamTarget` | UObject (Beam target picker) |
| [ParticleModuleBeamNoise.h/.cpp](JSEngine/Source/Engine/Particle/ParticleModuleBeamNoise.h) | `UParticleModuleBeamNoise` | UObject (Beam noise 파라미터) |
| [ParticleModuleMeshRotationRate.h/.cpp](JSEngine/Source/Engine/Particle/ParticleModuleMeshRotationRate.h) | `UParticleModuleMeshRotationRate` | UObject (Mesh 회전 모듈) |
| [ParticleMeshTypes.h](JSEngine/Source/Engine/Particle/ParticleMeshTypes.h) | `EMeshAlignment`, `FMeshRotationPayload` (36B) | enum / payload POD |
| [ParticleRibbonTypes.h](JSEngine/Source/Engine/Particle/ParticleRibbonTypes.h) | `FRibbonParticlePayload` (32B), `FRibbonParticleVertex` (48B) | payload POD / vertex POD |
| [ParticleBeamTypes.h](JSEngine/Source/Engine/Particle/ParticleBeamTypes.h) | `BeamNoiseMaxFrequency` (constexpr=8), `FParticleBeamPayload` (100B), `FBeamParticleVertex` (48B) | constant / payload POD / vertex POD |
| [ParticleSystem.h/.cpp](JSEngine/Source/Engine/Particle/ParticleSystem.h) | `UParticleLODLevel`, `UParticleEmitter`, `UParticleSystem` | UObject (에셋 트리) |
| [ParticleEmitterInstance.h/.cpp](JSEngine/Source/Engine/Particle/ParticleEmitterInstance.h) | `FParticleEmitterInstance` (base, virtual dtor) | POD struct (런타임 인스턴스 베이스) |
| [ParticleMeshEmitterInstance.h/.cpp](JSEngine/Source/Engine/Particle/ParticleMeshEmitterInstance.h) | `FParticleMeshEmitterInstance` | POD struct (Mesh 런타임, base 파생) |
| [ParticleRibbonEmitterInstance.h/.cpp](JSEngine/Source/Engine/Particle/ParticleRibbonEmitterInstance.h) | `FParticleRibbonEmitterInstance` | POD struct (Ribbon 런타임, base 파생) |
| [ParticleBeamEmitterInstance.h/.cpp](JSEngine/Source/Engine/Particle/ParticleBeamEmitterInstance.h) | `FParticleBeamEmitterInstance` | POD struct (Beam 런타임, base 파생) |
| [ParticleDynamicData.h/.cpp](JSEngine/Source/Engine/Particle/ParticleDynamicData.h) | `EDynamicEmitterType`, `ESortMode`, `FDynamicEmitterReplayDataBase`, `FDynamicEmitterDataBase`, `FDynamicSpriteEmitterReplayData`/`...Data`, `FDynamicMeshEmitterReplayData`/`...Data`, `FDynamicBeamEmitterReplayData`/`...Data`, `FDynamicRibbonEmitterReplayData`/`...Data` | enum / POD (ReplayData) / 행위자 (DynamicData) |
| [ParticleSystemComponent.h/.cpp](JSEngine/Source/Engine/Particle/ParticleSystemComponent.h) | `UParticleSystemComponent`, `FOnParticleCollide` 델리게이트 | UComponent (씬 진입점) |
| [ParticleEvent.h/.cpp](JSEngine/Source/Engine/Particle/ParticleEvent.h) | `AParticleEventManager`, `FOnParticleEventCollide` 델리게이트 | AActor (씬 placeable 이벤트 허브) |

### 1.1 정의 없이 선언만 존재하는 타입 (stub)

| 타입 | 선언 위치 | 비고 |
|------|----------|------|
| `FParticleEventInstancePayload` | [ParticleEmitterInstance.h:28](JSEngine/Source/Engine/Particle/ParticleEmitterInstance.h:28) forward → `SpawnParticles(..., EventPayload)` 시그니처에만 등장 | 정의 없음. 호출부에서 `nullptr` 또는 default arg로만 전달 (이벤트 기반 스폰을 위한 미래용 슬롯). |
| `FRenderBus` | [ParticleSystemComponent.h:6](JSEngine/Source/Engine/Particle/ParticleSystemComponent.h:6) forward → `CacheCameraFromRenderBus(...)` 시그니처에만 등장 | 실제 정의는 `Render/Scene/RenderBus.h` (외부 헤더). |
| `FInstanceBuffer` | [ParticleDynamicData.h:8](JSEngine/Source/Engine/Particle/ParticleDynamicData.h:8) forward → `FillVertexBuffer(...)` 시그니처 | 실제 정의는 `Render/Resource/InstanceBuffer.h` (외부). |

> 기존 문서에 `UParticleModuleTypeDataBase` 가 stub으로 적혀 있었으나, **현재는 [ParticleModuleTypeData.h:13](JSEngine/Source/Engine/Particle/ParticleModuleTypeData.h:13)에 정의가 있고 UCLASS UObject로 완전 구현되어 있다** — stub 해제 (§11 변경/모순 사항 참조).

---

## 2. 상속 (Inheritance)

```
UObject
  ├── UParticleModule
  │     ├── UParticleModuleRequired         (bSpawnModule=true)
  │     ├── UParticleModuleSpawn            (Rate 기반 ComputeSpawnCount)
  │     ├── UParticleModuleBurst            (bUpdateModule=true; BurstTime/RepeatInterval)
  │     ├── UParticleModuleLifetime         (bSpawnModule=true)
  │     ├── UParticleModuleLocation         (bSpawnModule=true)
  │     ├── UParticleModuleLocationShape    (bSpawnModule=true; Sphere/Box/Cone)
  │     ├── UParticleModuleVelocity         (bSpawnModule=true)
  │     ├── UParticleModuleAcceleration     (bUpdateModule=true)
  │     ├── UParticleModuleDrag             (bUpdateModule=true; exp damping)
  │     ├── UParticleModuleRotationRate     (bSpawnModule=true; bUpdateModule=true; Particle.Rotation/RotationRate 누적)
  │     ├── UParticleModuleColor            (bSpawnModule=true; bUpdateModule=true)
  │     ├── UParticleModuleLight            (no Spawn/Update — ShouldCreateLight / BuildLightInfo helper만)
  │     ├── UParticleModuleSize             (bSpawnModule=true; bUpdateModule=true)
  │     ├── UParticleModuleCollision        (bUpdateModule=true; OnHit → Owner->QueueCollisionEvent)
  │     ├── UParticleModuleEventGenerator   (bUpdateModule=true; → Owner->DispatchQueuedParticleEvents)
  │     ├── USubUVModule                    (bSpawnModule=true; bUpdateModule=true; FName 기반 atlas lookup)
  │     ├── UParticleModuleMeshRotationRate (bSpawnModule=true; bUpdateModule=true; FMeshRotationPayload 누적)
  │     ├── UParticleModuleBeamSource       (data only — SourceComponent picker)
  │     ├── UParticleModuleBeamTarget       (data only — TargetComponent + bUseLocalTarget + TargetLocalVector)
  │     ├── UParticleModuleBeamNoise        (data only — Frequency/NoiseRange/bTargetNoise/bSmooth)
  │     └── UParticleModuleTypeDataBase     (TypeData 베이스; CreateInstance/RequiredPayloadBytes/GetRenderMode 가상)
  │           ├── USpriteTypeData           (RequiredPayloadBytes=0;     CreateInstance → FParticleEmitterInstance)
  │           ├── UMeshTypeData             (=sizeof(FMeshRotationPayload)=36; CreateInstance → FParticleMeshEmitterInstance; Mesh/Material/Alignment)
  │           ├── URibbonTypeData           (=sizeof(FRibbonParticlePayload)=32; CreateInstance → FParticleRibbonEmitterInstance; MaxTrail/InTrail/Sheets/TangentScalar/Material)
  │           └── UBeamTypeData             (=sizeof(FParticleBeamPayload)=100; CreateInstance → FParticleBeamEmitterInstance; MaxBeam/InterpPts/FallbackDist/Tile/Material)
  ├── UParticleLODLevel
  ├── UParticleEmitter
  └── UParticleSystem

UPrimitiveComponent
  └── UParticleSystemComponent

AActor
  └── AParticleEventManager

(POD, NOT UObject)
FParticleEmitterInstance  (virtual dtor; virtual Tick/SpawnParticles/KillParticle/CreateDynamicData/GetRibbonVertexData/GetRequiredPayloadBytes)
  ├── FParticleMeshEmitterInstance   (Cycle 11)
  ├── FParticleRibbonEmitterInstance (Cycle 12)
  └── FParticleBeamEmitterInstance   (Cycle 13a)

FDynamicEmitterReplayDataBase   (POD snapshot; virtual dtor)
  ├── FDynamicSpriteEmitterReplayData
  ├── FDynamicMeshEmitterReplayData
  ├── FDynamicBeamEmitterReplayData
  └── FDynamicRibbonEmitterReplayData

FDynamicEmitterDataBase         (행위자; virtual GetSource/GetVertexStride/GetVertexFactoryType/FillVertexBuffer/Sort)
  ├── FDynamicSpriteEmitterData  (owns FDynamicSpriteEmitterReplayData + TArray<FSpriteParticleInstanceData>)
  ├── FDynamicMeshEmitterData    (owns FDynamicMeshEmitterReplayData  + TArray<FMeshParticleInstanceData>)
  ├── FDynamicBeamEmitterData    (owns FDynamicBeamEmitterReplayData  + TArray<FBeamParticleVertex>)
  └── FDynamicRibbonEmitterData  (owns FDynamicRibbonEmitterReplayData + TArray<FRibbonParticleVertex>)
```

`UParticleModule`의 가상 메서드는 `Spawn(Owner, Particle, SpawnTime)` / `Update(Owner, DeltaTime)`. `UParticleModuleTypeDataBase`는 추가로 `RequiredPayloadBytes()` / `GetRenderMode()` / `CreateInstance(Component, EmitterIndex)`를 가상 노출한다.

---

## 3. 소유/참조 전체 그래프 (`UParticleSystemComponent` 진입)

```
┌──────────────────────────────────────────────────────────────────────────────────┐
│ UParticleSystemComponent  (extends UPrimitiveComponent)                          │
│                                                                                  │
│  owns ──► UParticleSystem*  Template               (UPROPERTY, 에셋 ref)         │
│  owns ──► TArray<FParticleEmitterInstance*> EmitterInstances                     │
│              (raw `new` (TypeData->CreateInstance dispatch) / `delete` 직접 관리)│
│  owns ──► TArray<FParticleEventCollideData> PendingCollisionEvents               │
│  owns ──► FOnParticleCollide OnParticleCollide                                   │
│  owns ──► value: FVector CachedCameraPosition/Forward/Up/Right + bool 5 멤버     │
└──────────────────────────────────────────────────────────────────────────────────┘
        │
        │ Template (UPROPERTY)
        ▼
┌──────────────────────────────────────────────────────────────┐
│ UParticleSystem  (extends UObject)                           │
│  owns ──► TArray<UParticleEmitter*> Emitters   (UPROPERTY)   │
│  values: AssetPath, UpdateTimeFPS, ThumbnailWarmup, LOD*     │
└──────────────────────────────────────────────────────────────┘
        │ Emitters[i]
        ▼
┌──────────────────────────────────────────────────────────────┐
│ UParticleEmitter  (extends UObject)                          │
│  owns ──► TArray<UParticleLODLevel*> LODLevels (UPROPERTY)   │
│  values: ParticleSize, MaxActiveParticles (캐시)             │
└──────────────────────────────────────────────────────────────┘
        │ LODLevels[i]
        ▼
┌──────────────────────────────────────────────────────────────────────────────┐
│ UParticleLODLevel  (extends UObject)                                         │
│  owns ──► UParticleModuleRequired*       RequiredModule    (UPROPERTY)       │
│  owns ──► TArray<UParticleModule*>       Modules           (UPROPERTY)       │
│  owns ──► UParticleModuleTypeDataBase*   TypeDataModule    (UPROPERTY)       │
│             (silent bug ι 회피 — UPROPERTY 필수, [ParticleSystem.h:67])      │
│                                                                              │
│  refs ──► UParticleModuleSpawn*    SpawnModule      (Modules의 캐스팅 캐시)  │
│  refs ──► TArray<UParticleModule*> SpawnModules     (Modules 부분집합)       │
│  refs ──► TArray<UParticleModule*> UpdateModules    (Modules 부분집합)       │
└──────────────────────────────────────────────────────────────────────────────┘
        ▲
        │ refs (Modules / RequiredModule / TypeDataModule)
        │
┌──────────────────────────────────────────────────────────────┐
│ UParticleModule  (베이스, extends UObject)                   │
│  values: bEnabled, bSpawnModule, bUpdateModule  (플래그)     │
│  owns ──► TMap<FString, FParticleDistributionRuntimeData>    │
│             DistributionRuntimeData                          │
│  virtual Spawn(Owner, Particle, SpawnTime)                   │
│  virtual Update(Owner, DeltaTime)                            │
└──────────────────────────────────────────────────────────────┘
        ▲
        │ extends
        │
   (Required / Spawn / Burst / Lifetime / Location / LocationShape / Velocity /
    Acceleration / Drag / RotationRate / Color / Light / Size / Collision /
    EventGenerator / SubUV / MeshRotationRate / BeamSource / BeamTarget / BeamNoise /
    TypeDataBase → Sprite/Mesh/Ribbon/Beam)
```

위 트리는 **에셋 측 트리** (`UParticleSystem` 루트, UObject로 구성).

### 3.1 런타임 인스턴스 측

`UParticleSystemComponent::EmitterInstances`가 가지는 `FParticleEmitterInstance*` (또는 파생)는 에셋 트리를 **참조**하면서 자신만의 CPU 버퍼를 **소유**한다. 인스턴스의 실체 타입은 emitter 에셋의 LOD0의 `TypeDataModule->CreateInstance()` 가 dispatch한다 ([ParticleSystemComponent.cpp:75](JSEngine/Source/Engine/Particle/ParticleSystemComponent.cpp:75)).

```
┌──────────────────────────────────────────────────────────────────────────────┐
│ FParticleEmitterInstance  (POD struct, virtual dtor, NOT a UObject)          │
│                                                                              │
│  refs ──► UParticleEmitter*           SpriteTemplate   (Init 시 set)         │
│  refs ──► UParticleSystemComponent*   Component        (소유주 역참조)         │
│  refs ──► UParticleLODLevel*          CurrentLODLevel  (SpriteTemplate에서   │
│                                                          lookup 캐시)        │
│                                                                              │
│  owns ──► FParticleDataContainer ParticleStorage                             │
│             (멤버 안에 new uint8[MemBlockSize] — 단일 블록 ParticleData        │
│              + ParticleIndices, AlignSize(stride,16) 적용)                   │
│                                                                              │
│  values: EmitterIndex, CurrentLODLevelIndex, PayloadOffset, ParticleSize,    │
│          ActiveParticles, ParticleCounter, MaxActiveParticles, SpawnFraction,│
│          EmitterTime, PreviousEmitterTime                                    │
└──────────────────────────────────────────────────────────────────────────────┘
                              ▲
                              │ (base, virtual dtor 통해 derived 안전 delete)
        ┌─────────────────────┼─────────────────────┐
        │                     │                     │
┌───────────────────┐ ┌───────────────────┐ ┌───────────────────────────────┐
│ FParticleMesh     │ │ FParticleRibbon   │ │ FParticleBeamEmitterInstance  │
│ EmitterInstance   │ │ EmitterInstance   │ │                               │
│                   │ │                   │ │  owns ──► TArray<int32>       │
│  (멤버 없음 —      │ │  owns ──► TArray  │ │           BeamStates           │
│   payload는       │ │     <int32>       │ │  values: NextBeamIndex        │
│   ParticleStorage │ │     HeadIndices   │ │  helper: GetBeamPayload()     │
│     안에 인터리브)  │ │  owns ──► TArray  │ │                               │
│  helper:          │ │     <FRibbon...   │ │  override: SpawnParticles,    │
│   GetMeshPayload, │ │     Vertex>       │ │            Tick,              │
│   GetMeshPayload  │ │     VertexBuffer  │ │            CreateDynamicData  │
│   At              │ │  values:          │ │                               │
│                   │ │   NextTrailIndex  │ │                               │
│  override:        │ │  override:        │ │                               │
│   SpawnParticles, │ │   Tick,           │ │                               │
│  CreateDynamicData│ │   SpawnParticles, │ │                               │
│                   │ │   KillParticle,   │ │                               │
│                   │ │   GetRibbonVertex │ │                               │
│                   │ │   Data            │ │                               │
└───────────────────┘ └───────────────────┘ └───────────────────────────────┘
```

- `ParticleStorage`는 `[FBaseParticle | payload bytes]` 인터리브된 `ParticleStride` (16-aligned) 간격으로 연속 배치한 raw 버퍼 + 별도 `ParticleIndices`(uint16) 단일 alloc.
- `ParticleIndices`는 `[0..Max)` 인덱스를 담은 **간접 테이블** — base `KillParticle`이 active 끝과 `swap`만 하고 `ActiveParticles`만 감소 → compact active 리스트 유지. Ribbon `KillParticle`은 base 호출 *전에* linked list (`Prev/Next/Head`)를 재연결한다.

### 3.2 이벤트 측

```
┌──────────────────────────────────────────────────────────────┐
│ FParticleEventCollideData  (POD struct in ParticleTypes.h)   │
│                                                              │
│  refs ──► UParticleSystemComponent*   Component              │
│  refs ──► FParticleEmitterInstance*   EmitterInstance        │
│  refs ──► UPrimitiveComponent*        HitComponent           │
│  refs ──► AActor*                     HitActor               │
│  values: EmitterIndex, ParticleId, Location, OldLocation,    │
│          Velocity, Normal, Time, FHitResult Hit              │
└──────────────────────────────────────────────────────────────┘
HitComponent / HitActor는 `UParticleModuleCollision::Update`가 `Hit.HitComponent / HitComponent->GetOwner()`로 채운다 ([ParticleModules.cpp:664](JSEngine/Source/Engine/Particle/ParticleModules.cpp:664)).
```

```
┌──────────────────────────────────────────────────────────────────┐
│ AParticleEventManager  (extends AActor)                          │
│  owns ──► TArray<FParticleEventCollideData> CollisionEvents      │
│  owns ──► FOnParticleEventCollide OnParticleCollide              │
│  owns ──► UParticleSystem* PreviewParticleSystem                 │
│             (InitDefaultComponents에서 CreateDefaultSpriteSystem │
│              + USubUVModule, 소멸자에서 DestroyObject)            │
└──────────────────────────────────────────────────────────────────┘
```

**현재 코드 상태:** `AParticleEventManager`는 자기 자신의 `UParticleSystemComponent`를 placeable preview용으로 1개 보유 ([ParticleEvent.cpp:42](JSEngine/Source/Engine/Particle/ParticleEvent.cpp:42)). 외부 emitter / 모듈이 manager로 push 하는 경로는 디렉토리 내에 **여전히 없음** — `PushCollisionEvent` 호출자 0건. 즉 컴포넌트 단위의 이벤트 큐(`UParticleSystemComponent::PendingCollisionEvents`)와는 **별개의 병렬 허브**.

### 3.3 DynamicData 측 (Cycle 15a 신규)

```
┌──────────────────────────────────────────────────────────────────────────────────┐
│ FDynamicEmitterDataBase  (POD 행위자, virtual dtor, ownership: caller-owned)      │
│                                                                                  │
│  values: int32 EmitterIndex                                                      │
│  virtual GetSource() / GetVertexStride() / GetVertexFactoryType() /              │
│          FillVertexBuffer(Device,Ctx,FInstanceBuffer&) / Sort(CameraPos)         │
└──────────────────────────────────────────────────────────────────────────────────┘
                              ▲
                              │ extends (각 type별 owned ReplayData + CPU 버퍼)
        ┌─────────────────────┼──────────────────────┬─────────────────────────┐
        │                     │                      │                         │
┌────────────────────┐ ┌────────────────────┐ ┌────────────────────┐ ┌────────────────────┐
│ FDynamicSprite     │ │ FDynamicMesh       │ │ FDynamicBeam       │ │ FDynamicRibbon     │
│ EmitterData        │ │ EmitterData        │ │ EmitterData        │ │ EmitterData        │
│                    │ │                    │ │                    │ │                    │
│ owns ─► Source     │ │ owns ─► Source     │ │ owns ─► Source     │ │ owns ─► Source     │
│  (Sprite Replay)   │ │  (Mesh Replay)     │ │  (Beam Replay)     │ │  (Ribbon Replay)   │
│ owns ─► TArray<    │ │ owns ─► TArray<    │ │ owns ─► TArray<    │ │ owns ─► TArray<    │
│   FSpriteParticle  │ │   FMeshParticle    │ │   FBeamParticle    │ │   FRibbonParticle  │
│   InstanceData>    │ │   InstanceData>    │ │   Vertex>          │ │   Vertex>          │
│ Sort: ViewProj     │ │ Sort: ViewProj     │ │ Sort: 빈 (D10)     │ │ Sort: 빈            │
│ Depth back-to-front│ │ Depth back-to-front│ │                    │ │                    │
└────────────────────┘ └────────────────────┘ └────────────────────┘ └────────────────────┘

ReplayData 베이스 (POD snapshot):
┌────────────────────────────────────────────────────────────────────────────────┐
│ FDynamicEmitterReplayDataBase                                                  │
│  values: eEmitterType, ActiveParticleCount, ParticleStride, ParticleSize,      │
│          PayloadOffset, MaxActiveParticles, SortMode                           │
│  refs (얕은 복사):                                                              │
│   ─► const uint8*  ParticleData     (Instance.ParticleStorage 의 raw ptr)      │
│   ─► const uint16* ParticleIndices  (Instance.ParticleStorage 의 raw ptr)      │
│   ─► UMaterialInterface* Material                                              │
│   ─► UTexture* ParticleTexture                                                 │
│  TODO(multithread): switch to deep copy when render-thread separation lands    │
└────────────────────────────────────────────────────────────────────────────────┘
```

**ownership 흐름:**
1. `UParticleSystemComponent::CollectDynamicData()` → 각 `Instance->CreateDynamicData()` 호출 → `new FDynamic*EmitterData` 반환 ([ParticleSystemComponent.cpp:237](JSEngine/Source/Engine/Particle/ParticleSystemComponent.cpp:237)).
2. `Builder` (외부 `PrimitiveDrawCommandBuilder.cpp:653`)가 array를 받아 `Cmd.DynamicData = DynData` 로 ownership 이전.
3. `RenderPass`가 frame 끝에 `delete DynData` — frame-scope life-cycle, 단일 스레드 안전.

---

## 4. 관계 표 (요약)

### 4.1 소유 관계

| 소유자 | 소유 대상 | 멤버명 | 메커니즘 |
|--------|-----------|--------|----------|
| `UParticleSystemComponent` | `UParticleSystem` (참조) | `Template` | `UPROPERTY` 포인터 (에셋 ref) |
| `UParticleSystemComponent` | `FParticleEmitterInstance` (또는 Mesh/Ribbon/Beam derived) | `EmitterInstances` | `TypeData->CreateInstance()` (raw `new`) / `delete` |
| `UParticleSystemComponent` | `FParticleEventCollideData` | `PendingCollisionEvents` | value `TArray` |
| `UParticleSystemComponent` | `FOnParticleCollide` | `OnParticleCollide` | value 델리게이트 |
| `UParticleSystemComponent` | camera 캐시 5종 | `CachedCameraPosition/Forward/Up/Right/bCachedCameraValid` | value (POD) |
| `UParticleSystem` | `UParticleEmitter` | `Emitters` | `UPROPERTY` `TArray` |
| `UParticleEmitter` | `UParticleLODLevel` | `LODLevels` | `UPROPERTY` `TArray` |
| `UParticleLODLevel` | `UParticleModuleRequired` | `RequiredModule` | `UPROPERTY` |
| `UParticleLODLevel` | `UParticleModule` 들 | `Modules` | `UPROPERTY` `TArray` |
| `UParticleLODLevel` | `UParticleModuleTypeDataBase` | `TypeDataModule` | `UPROPERTY` (silent bug ι 회피, [ParticleSystem.h:67](JSEngine/Source/Engine/Particle/ParticleSystem.h:67)) |
| `UParticleModule` | `FParticleDistributionRuntimeData` | `DistributionRuntimeData` | value `TMap` |
| `FParticleEmitterInstance` | particle raw 메모리 | `ParticleStorage.ParticleData` (포함 `ParticleIndices`) | 단일 `new uint8[]`, container가 관리 |
| `FParticleMeshEmitterInstance` | (추가 멤버 없음) | — | payload는 ParticleStorage 내 인터리브 |
| `FParticleRibbonEmitterInstance` | trail head 배열 | `HeadIndices` | value `TArray<int32>` |
| `FParticleRibbonEmitterInstance` | strip 정점 버퍼 | `VertexBuffer` | value `TArray<FRibbonParticleVertex>` |
| `FParticleBeamEmitterInstance` | beam state 배열 | `BeamStates` | value `TArray<int32>` |
| `FDynamicSpriteEmitterData` | 메타 + Sprite 인스턴스 버퍼 | `Source`, `SpriteInstanceDataBuffer` | value 멤버 |
| `FDynamicMeshEmitterData` | 메타 + Mesh 인스턴스 버퍼 | `Source`, `MeshInstanceDataBuffer` | value 멤버 |
| `FDynamicBeamEmitterData` | 메타 + Beam 정점 버퍼 | `Source`, `BeamVertexBuffer` | value 멤버 |
| `FDynamicRibbonEmitterData` | 메타 + Ribbon 정점 버퍼 | `Source`, `RibbonVertexBuffer` | value 멤버 |
| `AParticleEventManager` | `FParticleEventCollideData` | `CollisionEvents` | value `TArray` |
| `AParticleEventManager` | `FOnParticleEventCollide` | `OnParticleCollide` | value 델리게이트 |
| `AParticleEventManager` | `UParticleSystem` (preview) | `PreviewParticleSystem` | `UObjectManager` create + 소멸자 `DestroyObject` |

### 4.2 비소유 참조 관계 (back-pointer / 캐시 / 행위 호출)

| 참조 보유자 | 참조 대상 | 멤버명 | 용도 |
|-------------|-----------|--------|------|
| `UParticleLODLevel` | `UParticleModuleSpawn` | `SpawnModule` | `Modules` 안에서 캐스팅 캐시 |
| `UParticleLODLevel` | `UParticleModule` (subset) | `SpawnModules` | `Modules` 중 `IsSpawnModule()` 필터 캐시 (+ `RequiredModule` head) |
| `UParticleLODLevel` | `UParticleModule` (subset) | `UpdateModules` | `Modules` 중 `IsUpdateModule()` 필터 캐시 |
| `FParticleEmitterInstance` | `UParticleEmitter` | `SpriteTemplate` | 에셋 lookup |
| `FParticleEmitterInstance` | `UParticleSystemComponent` | `Component` | 소유자 역참조 (World Location / 이벤트 큐 / camera 캐시) |
| `FParticleEmitterInstance` | `UParticleLODLevel` | `CurrentLODLevel` | LOD 캐시, `SpriteTemplate->GetLODLevel(...)` 결과 |
| `FDynamicEmitterReplayDataBase` | particle bytes | `ParticleData`, `ParticleIndices` | 얕은 복사 raw ptr (Instance 메모리 가리키기만) |
| `FDynamicEmitterReplayDataBase` | material/texture | `Material`, `ParticleTexture` | asset ref (ResourceManager / .particlesystem) |
| `FDynamicMeshEmitterReplayData` | mesh asset | `MeshAsset` | Builder가 MeshBuffer lookup에 사용 |
| `UParticleModuleBeamSource` | `USceneComponent` | `SourceComponent` | `TObjectPtr<USceneComponent>` picker |
| `UParticleModuleBeamTarget` | `USceneComponent` | `TargetComponent` | `TObjectPtr<USceneComponent>` picker |
| `UMeshTypeData` | `UStaticMesh` | `Mesh` | `UPROPERTY` (`ReferenceKind = Asset`) |
| `UMeshTypeData` | `UMaterialInterface` | `OverrideMaterial` | `UPROPERTY` (`ReferenceKind = Asset`) |
| `URibbonTypeData` | `UMaterialInterface` | `Material` | `UPROPERTY` (`ReferenceKind = Asset`) |
| `UBeamTypeData` | `UMaterialInterface` | `Material` | `UPROPERTY` (`ReferenceKind = Asset`) |
| `UParticleModuleRequired` | `UMaterialInterface` | `Material` | `UPROPERTY` (`ReferenceKind = Asset`) |
| `USubUVModule` | `FTextureAtlasResource` | `CachedSubUV` | ResourceManager 소유, 참조만 |
| `FParticleEventCollideData` | Component/Instance/HitComponent/HitActor | 각 필드 | 이벤트 컨텍스트 |

---

## 5. 행위적(behavioral) 호출 관계

소유/참조와 별개로, **누가 누구의 메서드를 호출하는가**:

| 호출자 | 피호출자 / 멤버 | 위치 |
|--------|-----------------|------|
| `UParticleSystemComponent::SetTemplate` | `RecreateEmitterInstances` | [ParticleSystemComponent.cpp:34](JSEngine/Source/Engine/Particle/ParticleSystemComponent.cpp:34) |
| `UParticleSystemComponent::RecreateEmitterInstances` | `EmitterAsset->CacheEmitterModuleInfo()`, `LOD0->GetTypeDataModule()->CreateInstance(this, i)` (또는 base `new`), `Instance->Init(...)` | [ParticleSystemComponent.cpp:52](JSEngine/Source/Engine/Particle/ParticleSystemComponent.cpp:52) |
| `UParticleSystemComponent::CollectDynamicData` | 각 `Instance->CreateDynamicData()` | [ParticleSystemComponent.cpp:237](JSEngine/Source/Engine/Particle/ParticleSystemComponent.cpp:237) |
| `UParticleSystemComponent::CacheCameraFromRenderBus` | `InRenderBus.GetCameraPosition/Forward/Up/Right` → 캐시 멤버 set + `bCachedCameraValid = true` | [ParticleSystemComponent.cpp:264](JSEngine/Source/Engine/Particle/ParticleSystemComponent.cpp:264) |
| `UParticleSystemComponent::TickComponent` | `TickPreview(dt,true)` → `Instance->Tick(dt, bAllow)` 루프 + `NotifySpatialIndexDirty` | [ParticleSystemComponent.cpp:212](JSEngine/Source/Engine/Particle/ParticleSystemComponent.cpp:212) |
| `FParticleEmitterInstance::Init` | `SpriteTemplate->CacheEmitterModuleInfo()`, `SelectLODLevel(0.0f)`, `GetLODLevel(...)`, `CurrentLODLevel->GetTypeDataModule()->RequiredPayloadBytes()`, `ParticleStorage.Allocate(Max, ParticleSize+Payload, 16)` | [ParticleEmitterInstance.cpp:21](JSEngine/Source/Engine/Particle/ParticleEmitterInstance.cpp:21) |
| `UParticleEmitter::CacheEmitterModuleInfo` | `UParticleLODLevel::CacheModuleLists` (모든 LOD) | [ParticleSystem.cpp:335](JSEngine/Source/Engine/Particle/ParticleSystem.cpp:335) |
| `FParticleEmitterInstance::Tick` | `Component->ComputeEmitterLODDistance()`, `SelectLODLevel`, `CurrentLODLevel->GetSpawnModule()->ComputeSpawnCount`, `SpawnParticles`, `KillParticle`, 각 `UpdateModule->Update(this, dt)` | [ParticleEmitterInstance.cpp:91](JSEngine/Source/Engine/Particle/ParticleEmitterInstance.cpp:91) |
| `FParticleEmitterInstance::SpawnParticles` | 각 `SpawnModule->Spawn(this, *Particle, SpawnTime)` (Required + Spawn module 리스트) | [ParticleEmitterInstance.cpp:173](JSEngine/Source/Engine/Particle/ParticleEmitterInstance.cpp:173) |
| `FParticleEmitterInstance::CreateDynamicData` | `CurrentLODLevel->GetEffectiveRenderMode()` → Sprite/Ribbon 분기 → `new FDynamicSpriteEmitterData` 또는 `new FDynamicRibbonEmitterData` + `BuildFromInstance(*this)` | [ParticleEmitterInstance.cpp:329](JSEngine/Source/Engine/Particle/ParticleEmitterInstance.cpp:329) |
| `FParticleMeshEmitterInstance::SpawnParticles` | base `SpawnParticles` + `GetMeshPayload` 초기화 | [ParticleMeshEmitterInstance.cpp:43](JSEngine/Source/Engine/Particle/ParticleMeshEmitterInstance.cpp:43) |
| `FParticleMeshEmitterInstance::CreateDynamicData` | `new FDynamicMeshEmitterData` + ReplayData 채움 + `BuildFromInstance(*this)` | [ParticleMeshEmitterInstance.cpp:65](JSEngine/Source/Engine/Particle/ParticleMeshEmitterInstance.cpp:65) |
| `FParticleRibbonEmitterInstance::SpawnParticles` | base `SpawnParticles` + `EnsureTrailState` + linked list prepend + `HeadIndices` 갱신 | [ParticleRibbonEmitterInstance.cpp:92](JSEngine/Source/Engine/Particle/ParticleRibbonEmitterInstance.cpp:92) |
| `FParticleRibbonEmitterInstance::KillParticle` | chain 재연결 (Prev/Next/Head 갱신) → base `KillParticle` swap-pop | [ParticleRibbonEmitterInstance.cpp:157](JSEngine/Source/Engine/Particle/ParticleRibbonEmitterInstance.cpp:157) |
| `FParticleRibbonEmitterInstance::Tick` | base `Tick` + `EnsureTrailState` + chain 순회 tangent/distance 갱신 + `BuildVertexBuffer()` | [ParticleRibbonEmitterInstance.cpp:208](JSEngine/Source/Engine/Particle/ParticleRibbonEmitterInstance.cpp:208) |
| `FParticleBeamEmitterInstance::SpawnParticles` | base `SpawnParticles` + `EnsureBeamState` + `FindFirstBeamModule<UParticleModuleBeamNoise>` + `GenerateNoiseSamples(payload, freq, ParticleId)` | [ParticleBeamEmitterInstance.cpp:108](JSEngine/Source/Engine/Particle/ParticleBeamEmitterInstance.cpp:108) |
| `FParticleBeamEmitterInstance::Tick` | base `Tick` + `EnsureBeamState` (vertex build은 안 함 — DynamicData::BuildFromInstance가 수행) | [ParticleBeamEmitterInstance.cpp:143](JSEngine/Source/Engine/Particle/ParticleBeamEmitterInstance.cpp:143) |
| `FParticleBeamEmitterInstance::CreateDynamicData` | `new FDynamicBeamEmitterData` + ReplayData 채움 (`InterpolationPoints`/`Material`/`bHasNoise`) + `BuildFromInstance(*this)` | [ParticleBeamEmitterInstance.cpp:151](JSEngine/Source/Engine/Particle/ParticleBeamEmitterInstance.cpp:151) |
| `FDynamicSpriteEmitterData::BuildFromInstance` | `Instance.GetParticle(i)` 루프 → `SpriteInstanceDataBuffer` 채움 | [ParticleDynamicData.cpp:151](JSEngine/Source/Engine/Particle/ParticleDynamicData.cpp:151) |
| `FDynamicMeshEmitterData::BuildFromInstance` | `Instance.GetParticle(i)` + `NonConstInstance.GetMeshPayload(SlotIndex)` → `SpinMatrix * AlignmentMatrix` → `MeshInstanceDataBuffer` 채움. `Instance.GetOwningComponent()->GetCachedCameraPosition()` 사용 (PSA_FacingCameraPosition). `bCachedCameraValid=false` 면 `PSA_Velocity` fallback. | [ParticleDynamicData.cpp:247](JSEngine/Source/Engine/Particle/ParticleDynamicData.cpp:247) |
| `FDynamicBeamEmitterData::BuildFromInstance` | `Instance.GetComponentWorldLocation()`, `Instance.GetOwningComponent()->GetForwardVector/RightVector/UpVector`, `FindFirstModule<UParticleModuleBeamSource/Target/Noise>`, `NonConstInstance.GetBeamPayload(SlotIndex)` → strip 정점 build (segment + noise perturbation + degenerate seam) | [ParticleDynamicData.cpp:379](JSEngine/Source/Engine/Particle/ParticleDynamicData.cpp:379) |
| `FDynamicRibbonEmitterData::BuildFromInstance` | `Instance.GetRibbonVertexData(count)` → `RibbonVertexBuffer.assign(...)` | [ParticleDynamicData.cpp:544](JSEngine/Source/Engine/Particle/ParticleDynamicData.cpp:544) |
| `UParticleModuleRequired::Spawn` | particle 의 RelativeTime/Lifetime/Size/Color 초기화 | [ParticleModules.cpp:153](JSEngine/Source/Engine/Particle/ParticleModules.cpp:153) |
| `UParticleModuleSpawn::ComputeSpawnCount` | `Owner->ConsumeSpawnCount(Rate, dt)` | [ParticleModules.cpp:179](JSEngine/Source/Engine/Particle/ParticleModules.cpp:179) |
| `UParticleModuleBurst::Update` | `Owner->SpawnParticles(BurstCount*TriggerCount, ...)` (BurstTime + RepeatInterval 트리거) | [ParticleModules.cpp:270](JSEngine/Source/Engine/Particle/ParticleModules.cpp:270) |
| `UParticleModuleLocation::Spawn` | `Owner->GetComponentWorldLocation()` | [ParticleModules.cpp:218](JSEngine/Source/Engine/Particle/ParticleModules.cpp:218) |
| `UParticleModuleLocationShape::Spawn` | `Owner->GetComponentWorldLocation()` + `RandomPointInShape(...)` | [ParticleModules.cpp:232](JSEngine/Source/Engine/Particle/ParticleModules.cpp:232) |
| `UParticleModuleCollision::Update` | `Owner->GetOwningComponent()`, `Component->ComputeEmitterLODDistance()`, `World->SweepSingle / LineTraceSingle`, `Owner->QueueCollisionEvent(Event)` (with `Hit.HitComponent`/`HitActor`), `Owner->KillParticle(i)` | [ParticleModules.cpp:513](JSEngine/Source/Engine/Particle/ParticleModules.cpp:513) |
| `UParticleModuleEventGenerator::Update` | `Owner->GetOwningComponent()->GetPendingCollisionEvents()` (overflow trim) + `Owner->DispatchQueuedParticleEvents()` | [ParticleModules.cpp:691](JSEngine/Source/Engine/Particle/ParticleModules.cpp:691) |
| `UParticleModuleMeshRotationRate::Spawn` | `dynamic_cast<FParticleMeshEmitterInstance*>(Owner)` → `MeshInstance->GetMeshPayloadAt(ActiveCount-1)->RotRate = RandomRangeVector(...)` | [ParticleModuleMeshRotationRate.cpp:40](JSEngine/Source/Engine/Particle/ParticleModuleMeshRotationRate.cpp:40) |
| `UParticleModuleMeshRotationRate::Update` | active 루프 → `Payload->Rotation += RotRate * dt` | [ParticleModuleMeshRotationRate.cpp:70](JSEngine/Source/Engine/Particle/ParticleModuleMeshRotationRate.cpp:70) |
| `UParticleSystemComponent::DispatchQueuedParticleEvents` | `OnParticleCollide.Broadcast(EventData)` 후 큐 clear | [ParticleSystemComponent.cpp:123](JSEngine/Source/Engine/Particle/ParticleSystemComponent.cpp:123) |
| `UParticleSystemComponent::ComputeEmitterLODDistance` | `GetOwner()->GetFocusedWorld()->GetActiveCamera()->GetLocation()` | [ParticleSystemComponent.cpp:99](JSEngine/Source/Engine/Particle/ParticleSystemComponent.cpp:99) |
| `UParticleSystemComponent::UpdateWorldAABB` | 각 `Instance->GetActiveParticleCount()` + `Instance->GetParticle(i)->Location`로 AABB 확장 | [ParticleSystemComponent.cpp:135](JSEngine/Source/Engine/Particle/ParticleSystemComponent.cpp:135) |
| `AParticleEventManager::DispatchEvents` | `OnParticleCollide.Broadcast` 후 큐 clear | [ParticleEvent.cpp:26](JSEngine/Source/Engine/Particle/ParticleEvent.cpp:26) |
| `AParticleEventManager::InitDefaultComponents` | `AddComponent<UParticleSystemComponent>()`, `UParticleSystem::CreateDefaultSpriteSystem()`, `USubUVModule` 추가 후 `SetTemplate` | [ParticleEvent.cpp:35](JSEngine/Source/Engine/Particle/ParticleEvent.cpp:35) |

### 5.1 데이터 흐름 (한 프레임)

```
UEngine Tick
  └─ UParticleSystemComponent::TickComponent(dt)
       └─ TickPreview(dt, true):
            for each FParticleEmitterInstance* (실체: base / Mesh / Ribbon / Beam):
              Instance->Tick(dt, true)
                ├─ [base] Component->ComputeEmitterLODDistance()
                ├─ [base] SelectLODLevel(distance)            → CurrentLODLevel 갱신
                ├─ [base] SpawnModule->ComputeSpawnCount      → SpawnCount
                ├─ [base] SpawnParticles(SpawnCount, ...)
                │    └─ for each SpawnModule in CurrentLODLevel->GetSpawnModules():
                │         Module->Spawn(this, particle, t)
                │           Required (RelativeTime=0, Lifetime, Size=1, Color=White)
                │           Lifetime (LifetimeMin~Max)
                │           Location / LocationShape (Component world + offset/shape)
                │           Velocity (Velocity range)
                │           RotationRate (initial Particle.RotationRate)
                │           Color (StartColor)
                │           Size  (StartSize range)
                │           SubUV (Particle.SubUVIndex initial frame)
                │    [Mesh derived] 신규 active range → GetMeshPayload()
                │                                       Payload {InitialOrientation,
                │                                                Rotation, RotRate} = Zero
                │                                       (이후 MeshRotationRate Spawn 가 RotRate set)
                │    [Ribbon derived] EnsureTrailState + linked list prepend (head=신규)
                │                     + HeadIndices[trail] = newSlot
                │                     + Payload {Prev=-1, Next=oldHead, Tangent=Velocity/Speed
                │                                Distance=0, SpawnedTangentStrength}
                │    [Beam derived] EnsureBeamState + per-particle BeamIndex round-robin
                │                   + GenerateNoiseSamples(Payload->NoiseSamples,
                │                                          NoiseModule->GetFrequency(),
                │                                          Particle.ParticleId)
                ├─ [base] active particle 루프:
                │    Particle.RelativeTime += dt / Lifetime
                │    if RelativeTime≥1 → KillParticle
                │    Particle.OldLocation = Particle.Location
                │    Particle.Location += Velocity * dt
                ├─ [base] for each UpdateModule:
                │    Module->Update(this, dt)
                │      Burst        → Owner->SpawnParticles(BurstCount*N) (BurstTime hit)
                │      Acceleration → Velocity += Acceleration * dt
                │      Drag         → Velocity *= exp(-DragCoef * dt)
                │      RotationRate → Particle.Rotation += Particle.RotationRate * dt
                │      Color        → Lerp(StartColor, EndColor, RelativeTime)
                │      Size         → Lerp(currentSize, EndSizeRange, RelativeTime)
                │      Collision    → SweepSingle/LineTraceSingle → Hit?
                │                       Bounce/Stop/Kill/Ignore + Particle.CollisionCount++
                │                       Component->QueueCollisionEvent(event)
                │                       (필요 시 KillParticle)
                │      EventGenerator → PendingEvents overflow trim
                │                       Owner->DispatchQueuedParticleEvents()
                │                       → Component->OnParticleCollide.Broadcast(...)
                │      SubUV        → Particle.SubUVIndex = (Life/FPS-based frame)
                │      MeshRotationRate
                │                   → dynamic_cast<FParticleMeshEmitterInstance>(Owner)
                │                     for each active idx → Payload->Rotation += RotRate * dt
                └─ [Ribbon] EnsureTrailState + chain 순회 tangent/distance 갱신
                            + BuildVertexBuffer() → VertexBuffer 재구축
                  [Beam]   EnsureBeamState (vertex build 안 함)
                  [Mesh]   (추가 작업 없음)
       └─ NotifySpatialIndexDirty()

# Render 사이드 (Builder → Component → Instance):
PrimitiveDrawCommandBuilder (Render/Scene/PrimitiveDrawCommandBuilder.cpp)
  └─ EPT_ParticleSystem case:
       ├─ Component->CacheCameraFromRenderBus(RenderBus)         (Cycle 14)
       ├─ CollectParticleLights(Component, RenderBus)            (light 통합)
       ├─ TArray<FDynamicEmitterDataBase*> All = Component->CollectDynamicData()
       │    └─ for each Instance: Instance->CreateDynamicData() → new FDynamic*EmitterData
       │         ├─ [Sprite/Ribbon] base FParticleEmitterInstance::CreateDynamicData 가
       │         │                  RenderMode 분기 (Sprite default + Ribbon placeholder)
       │         ├─ [Mesh]   FParticleMeshEmitterInstance::CreateDynamicData override
       │         ├─ [Beam]   FParticleBeamEmitterInstance::CreateDynamicData override
       │         └─ 각 DynData->BuildFromInstance(*this):
       │              Sprite: SpriteInstanceDataBuffer fill (Position/Size/Color/Rot/SubUV)
       │              Mesh  : Spin x Alignment 매트릭스 합성 → InstanceData fill
       │              Beam  : Source/Target/Noise modules + segment loop + degenerate seam
       │              Ribbon: instance.GetRibbonVertexData() snapshot
       ├─ for each DynData:
       │    type 분기 (Source.eEmitterType) — Material/ParticleTexture/SubUV grid 채움
       │    ActiveParticleCount==0 또는 Mesh path 의 MeshBuffer 미존재 → delete DynData, skip
       │    DynData->Sort(RenderBus.GetCameraPosition()) — Sprite/Mesh ViewProjDepth 적용
       │    Cmd.DynamicData = DynData       (ownership 이전)
       │    RenderBus.AddCommand(ERenderPass::Particle, Cmd)
       └─ (RenderPass가 frame 끝에 delete Cmd.DynamicData)
```

---

## 6. 외부 의존성 (Particle/ 디렉토리 밖)

| 외부 타입 | 사용 위치 | 관계 종류 |
|-----------|----------|----------|
| `UPrimitiveComponent` | `UParticleSystemComponent`의 부모 | 상속 |
| `UObject` | `UParticleSystem`/`Emitter`/`LODLevel`/`Module` 베이스 | 상속 |
| `AActor` | `AParticleEventManager`의 부모, `FParticleEventCollideData::HitActor` 슬롯 | 상속 / 참조 |
| `USceneComponent` | `UParticleModuleBeamSource::SourceComponent`, `UParticleModuleBeamTarget::TargetComponent`, beam DynamicData의 `GetWorldLocation()` 호출 | `TObjectPtr<>` |
| `UStaticMesh` | `UMeshTypeData::Mesh`, `FDynamicMeshEmitterReplayData::MeshAsset`, `GetEffectiveMaterial()`의 `GetSections()/GetMaterialSlots()` | `UPROPERTY` asset ref |
| `UMaterialInterface`, `UMaterial` | `UMeshTypeData`/`URibbonTypeData`/`UBeamTypeData`/`UParticleModuleRequired` 의 Material | `UPROPERTY` asset ref |
| `UTexture`, `FTextureAtlasResource` | `FDynamicEmitterReplayDataBase::ParticleTexture`, `USubUVModule::CachedSubUV` | 참조 (ResourceManager 소유) |
| `FResourceManager` | `FResourceManager::Get().LoadStaticMesh/DeserializeMaterial/GetMaterial/FindSubUVExact` | 정적 의존 |
| `UWorld`, `FViewportCamera` | `ComputeEmitterLODDistance`, `UParticleModuleCollision::Update`의 `World->SweepSingle/LineTraceSingle` | 일시 참조 |
| `FRenderBus` | `UParticleSystemComponent::CacheCameraFromRenderBus` 인자 | 외부 헤더 (forward only in particle headers) |
| `FInstanceBuffer` | `FDynamicEmitterDataBase::FillVertexBuffer` 인자 | 외부 헤더 (forward only) |
| `ID3D11Device`, `ID3D11DeviceContext` | `FillVertexBuffer` 인자 | D3D11 raw 핸들 |
| `FEngineRandom` | `ParticleModules.cpp` / `ParticleModuleMeshRotationRate.cpp` 의 `RandomRange` 헬퍼 | 정적 의존 |
| `std::mt19937`, `std::uniform_real_distribution` | `FParticleBeamEmitterInstance.cpp`의 `GenerateNoiseSamples` (ParticleId seed) | 정적 (deterministic noise) |
| `FHitResult`, `FCollisionShape`, `FCollisionQueryParams`, `CollisionTypes` | `FParticleEventCollideData::Hit`, Collision module의 sweep 인자 | value 멤버 / 인자 |
| `FName` | `UParticleModuleRequired`/`USubUVModule::SubUVName` | value 멤버 |
| `FVector`, `FColor`, `FBoundingBox`, `FRay`, `FMatrix`, `FQuat`, `FVector2` | 전반 (Math/Core) | value/인자 |
| `EVertexFactoryType`, `FSpriteParticleInstanceData`, `FMeshParticleInstanceData` | `ParticleDynamicData.h`/`.cpp` | enum/POD (Render/Resource/VertexTypes.h) |
| `MathUtil::Clamp/Abs` | Beam noise / alignment 계산 | 정적 헬퍼 |
| `UObjectManager` | `CreateObject<T>` / `DestroyObject` | UObject lifetime |
| `FArchive`, `UPROPERTY`/`UCLASS`/`UENUM` 매크로 | 직렬화/reflection 통합 | 매크로 |
| `DECLARE_DELEGATE` 매크로 | `FOnParticleCollide`, `FOnParticleEventCollide` | 델리게이트 정의 |

---

## 7. 주의/관찰 사항

1. **이중 이벤트 허브** — `UParticleSystemComponent::OnParticleCollide`(컴포넌트 단위)와 `AParticleEventManager::OnParticleCollide`(레벨 단위)가 별개로 존재. `AParticleEventManager::PushCollisionEvent` 호출자는 디렉토리 내·외 여전히 0건 — manager는 placeable preview 액터로만 기능 (`InitDefaultComponents`에서 자기 자신의 SpriteSystem을 띄움).
2. **`ParticleUpdateUtils.h` 미사용** — `PARTICLE_PTR` / `GetParticleDirect` 매크로/인라인은 디렉토리 내·외 어디서도 include 되지 않는다 (dead). `FParticleEmitterInstance::GetParticle`이 동일 계산을 직접 수행.
3. **TypeData ↔ Instance 라우팅** — `RecreateEmitterInstances`가 LOD0의 `TypeDataModule->CreateInstance(this, i)`를 호출. `USpriteTypeData::CreateInstance` = base `new FParticleEmitterInstance` 이므로 Sprite 회귀 0. Mesh/Ribbon/Beam은 derived 인스턴스 생성. TypeData가 nullptr인 emitter는 base 인스턴스로 폴백 ([ParticleSystemComponent.cpp:75](JSEngine/Source/Engine/Particle/ParticleSystemComponent.cpp:75)).
4. **`FParticleEmitterInstance::~FParticleEmitterInstance` 가상** — base 포인터 (`EmitterInstances[i]`)로 `delete` 시 derived (Mesh/Ribbon/Beam) 의 소멸자 안전 호출.
5. **`UParticleLODLevel::TypeDataModule` UPROPERTY 필수** — UPROPERTY가 빠지면 .particlesystem 저장-로드 후 nullptr → 모든 emitter가 Sprite로 silent fallback (silent bug ι). [ParticleSystem.h:67](JSEngine/Source/Engine/Particle/ParticleSystem.h:67) 의 코멘트가 이를 명시.
6. **TypeData 캐싱 분리** — `UParticleLODLevel::CacheModuleLists()`는 `Modules` 순회 중 `Cast<UParticleModuleTypeDataBase>`된 것을 **TypeDataModule 슬롯**에만 저장하고 Spawn/Update 리스트엔 안 넣는다 — TypeData는 실행 모듈이 아니라 emitter type/render policy 슬롯. [ParticleSystem.cpp:173](JSEngine/Source/Engine/Particle/ParticleSystem.cpp:173).
7. **`FParticleEmitterInstance`는 UObject가 아니다** — `UParticleSystemComponent`가 `new`/`delete`로 직접 lifetime 관리. UObject GC 경로 밖.
8. **`UParticleLODLevel`의 캐시 멤버** — `SpawnModule`, `SpawnModules`, `UpdateModules`, `TypeDataModule`은 `Modules` (+`RequiredModule`)의 부분 뷰. `CacheModuleLists()` 호출 전에는 비어 있다. `UParticleEmitter::CacheEmitterModuleInfo`가 모든 LOD에 대해 일괄 호출 (FParticleEmitterInstance::Init 시점 + RecreateEmitterInstances 시점).
9. **컴포넌트 ↔ 인스턴스 양방향 참조** — `UParticleSystemComponent → EmitterInstances*` (소유), `FParticleEmitterInstance → Component` (back-ref). 컴포넌트 소멸 시 `ClearEmitterInstances`에서 인스턴스가 모두 delete → dangling 없음.
10. **DynamicData ownership 모델** — `Instance->CreateDynamicData()`가 매 frame `new` (D2). `Builder`가 `Cmd.DynamicData = DynData` 로 ownership 이전 → `RenderPass`가 frame 끝에 `delete`. ReplayData 내 `ParticleData`/`ParticleIndices`는 instance 소유 메모리를 가리키는 raw ptr (얕은 복사 — D3). 단일 스레드 + frame-scope이라 안전, 멀티스레드 도입 시 deep copy 전환 필요.
11. **Mesh `PSA_FacingCameraPosition` fallback** — `FDynamicMeshEmitterData::BuildFromInstance`는 `OwningComp->IsCachedCameraValid()`가 false면 자동으로 `PSA_Velocity`로 fallback (위험 12 방어). camera 캐싱은 Builder가 `Component->CacheCameraFromRenderBus(...)` 호출 직후 frame 동안만 valid.
12. **Beam Noise 결정성** — `FParticleBeamEmitterInstance::SpawnParticles`가 spawn 시점에만 `std::mt19937` (Seed = `Particle.ParticleId`)로 `NoiseSamples[Frequency]`를 1회 채움. lifetime 동안 고정 → frame-rate 비종속 (위험 6 방어). `BeamNoiseMaxFrequency = 8`은 컴파일 타임 고정 — payload sizeof 정적 결정.
13. **Ribbon linked list 무결성** — `KillParticle`이 base swap-pop 전에 `Prev/Next/Head`를 재연결. base swap-pop은 `ParticleIndices`만 swap하므로 `SlotIndex` 불변 → chain 안전. Prev/Next는 반드시 **SlotIndex (physical)** 저장, ActiveIndex 아님.
14. **Beam vertex build 위치 변경** — 기존 `FParticleBeamEmitterInstance::BuildVertexBuffer()` / `VertexBuffer` 멤버 삭제됨. 이제 `FDynamicBeamEmitterData::BuildFromInstance`가 매 frame strip vertex를 직접 build (Phase 5 이관). Mesh도 동일 — alignment+spin 계산이 `FDynamicMeshEmitterData::BuildFromInstance`로 이관.
15. **`InstanceData` 멤버 삭제됨** — `FParticleEmitterInstance::InstanceData` / `InstancePayloadSize` / `SpriteInstanceDataBuffer` 모두 Cycle 15a Phase 5에서 삭제. payload는 `ParticleStorage`에 인터리브, Sprite 버퍼는 `FDynamicSpriteEmitterData`가 소유.
16. **`FParticleEmitterRuntimeView` 삭제됨** — Cycle 15a Phase 5 (D11) — `FDynamicEmitterReplayDataBase`가 대체. [ParticleTypes.h:134](JSEngine/Source/Engine/Particle/ParticleTypes.h:134) 의 코멘트가 명시.

---

## 8. 한 줄 요약

> `UParticleSystemComponent`가 **에셋 트리(`UParticleSystem → UParticleEmitter → UParticleLODLevel → UParticleModule[+TypeDataModule]`)** 를 `Template`으로 참조하면서, 각 `UParticleEmitter`마다 **런타임 `FParticleEmitterInstance`** (또는 `TypeData->CreateInstance()`가 dispatch한 Mesh/Ribbon/Beam derived) 를 `new`로 소유한다. 인스턴스는 에셋 트리(SpriteTemplate, CurrentLODLevel)와 컴포넌트(Component)를 back-ref로 들고, 인터리브 payload를 포함한 `ParticleStorage` raw 버퍼만 소유한다. 모듈은 인스턴스(`Owner`)를 통해 컴포넌트로 들어가 충돌/이벤트를 큐잉하고, 컴포넌트가 `OnParticleCollide` 델리게이트로 외부에 broadcast 한다. 매 frame 렌더 사이드에서는 `Component::CollectDynamicData()` → `Instance::CreateDynamicData()` 가 `FDynamic*EmitterData` (ReplayData 메타 + CPU 정점 버퍼) 를 `new`로 만들어 Builder에 ownership 이전하고, RenderPass가 frame 끝에 delete 한다.

---

## 9. DynamicData / ReplayData 계층 (Cycle 15a)

> 본 디렉토리에는 SQL/key-value Database가 없다. 본 섹션은 "DB" 키워드를 **`FDynamicEmitter*DataBase` (base 클래스명 suffix가 DataBase) 인프라**로 해석한 것이다 — git 로그의 "database done" / "emitter dynamic data base merge" 커밋이 이 인프라의 도입.

### 9.1 DynamicData ↔ Particle 소유/참조 방향

| 보유자 | 보유 대상 | 멤버 | 메커니즘 | 방향 |
|-------|----------|------|---------|------|
| caller (Builder) | `FDynamicEmitterDataBase*` (4종 derived) | `Cmd.DynamicData` | raw `new` (Instance가 매 frame 생성) / `delete` (RenderPass) | Builder가 ownership 보유 |
| `FDynamicSpriteEmitterData` | `FDynamicSpriteEmitterReplayData` + `TArray<FSpriteParticleInstanceData>` | `Source`, `SpriteInstanceDataBuffer` | value 멤버 | DynamicData → Source (owns) |
| `FDynamicMeshEmitterData` | `FDynamicMeshEmitterReplayData` + `TArray<FMeshParticleInstanceData>` | `Source`, `MeshInstanceDataBuffer` | value 멤버 | DynamicData → Source (owns) |
| `FDynamicBeamEmitterData` | `FDynamicBeamEmitterReplayData` + `TArray<FBeamParticleVertex>` | `Source`, `BeamVertexBuffer` | value 멤버 | DynamicData → Source (owns) |
| `FDynamicRibbonEmitterData` | `FDynamicRibbonEmitterReplayData` + `TArray<FRibbonParticleVertex>` | `Source`, `RibbonVertexBuffer` | value 멤버 | DynamicData → Source (owns) |
| `FDynamicEmitterReplayDataBase` | particle raw bytes (instance 소유) | `ParticleData`, `ParticleIndices` | raw `const uint8*` / `const uint16*` — **얕은 복사** | ReplayData → Instance memory (refs only, frame-scope) |
| `FDynamicEmitterReplayDataBase` | material/texture asset | `Material`, `ParticleTexture` | raw 포인터 (asset 소유) | ReplayData → Asset (refs) |
| `FDynamicMeshEmitterReplayData` | mesh asset | `MeshAsset` | raw 포인터 | ReplayData → UStaticMesh (refs) |
| `FParticleEmitterInstance` | DynamicData (생성 시점) | (locals만 — `return new ...`) | 즉시 caller에 ownership 이전 | Instance가 **소유하지 않음** |

**핵심:** Instance는 DynamicData를 들고 있지 않는다 — `CreateDynamicData()`가 매 frame 새 객체를 만들어 caller에게 넘기고 끝. Instance 측은 raw particle bytes만 owns, DynamicData는 그 bytes를 frame 동안만 가리키는 weak snapshot.

### 9.2 호출 시점 (lifetime 흐름)

| 시점 | 호출 | 위치 |
|-----|------|------|
| 1. 에디터/씬에서 `SetTemplate(UParticleSystem*)` | `UParticleSystemComponent::SetTemplate` → `RecreateEmitterInstances` → `TypeData->CreateInstance()` + `Instance->Init(...)` (1회) | [ParticleSystemComponent.cpp:26](JSEngine/Source/Engine/Particle/ParticleSystemComponent.cpp:26) |
| 2. 매 frame 시뮬레이션 | `TickComponent(dt)` → `Instance->Tick(dt, true)` (per emitter) | [ParticleSystemComponent.cpp:212](JSEngine/Source/Engine/Particle/ParticleSystemComponent.cpp:212) |
| 3. 매 frame 렌더 전 — camera 캐싱 | `Builder.CacheCameraFromRenderBus(RenderBus)` → `Component`에 5 필드 set | [ParticleSystemComponent.cpp:264](JSEngine/Source/Engine/Particle/ParticleSystemComponent.cpp:264) |
| 4. 매 frame 렌더 — DynamicData 생성 | `Builder` → `Component->CollectDynamicData()` → 각 `Instance->CreateDynamicData()` (per emitter `new`) | [ParticleSystemComponent.cpp:237](JSEngine/Source/Engine/Particle/ParticleSystemComponent.cpp:237) |
| 5. Builder가 type별 메타 보충 | Material/ParticleTexture/SubUV grid set into `Source` | `Render/Scene/PrimitiveDrawCommandBuilder.cpp:670` |
| 6. Sort hook (D8) | `DynData->Sort(RenderBus.GetCameraPosition())` — Sprite/Mesh `ViewProjDepth` back-to-front | [ParticleDynamicData.cpp:201](JSEngine/Source/Engine/Particle/ParticleDynamicData.cpp:201), [338](JSEngine/Source/Engine/Particle/ParticleDynamicData.cpp:338) |
| 7. RenderPass | `DynData->FillVertexBuffer(Device, Ctx, FInstanceBuffer&)` → GPU upload | [ParticleDynamicData.cpp:181](JSEngine/Source/Engine/Particle/ParticleDynamicData.cpp:181) 외 |
| 8. Frame 끝 | RenderPass가 `delete Cmd.DynamicData` | (외부 RenderPass) |

DynamicData는 **에셋 로드/저장과 무관** — `.particlesystem` 직렬화에 들어가지 않음 (asset에는 들어가지 않는 frame-scope POD).

### 9.3 데이터 흐름 다이어그램 (DynamicData)

```
┌────────────────────────────────────────────────────────────────────────────┐
│                    한 frame (Tick → CollectDynamicData → Render)           │
└────────────────────────────────────────────────────────────────────────────┘

[Simulation phase — Instance가 raw particle bytes 갱신]
┌─────────────────────────────────────────┐
│ FParticleEmitterInstance::Tick(dt)      │
│   → ParticleStorage.ParticleData[ ... ] │   ← raw uint8 buffer (in-place)
│      [BaseParticle | payload bytes]     │
│   → ParticleStorage.ParticleIndices[]   │   ← active list compaction
└─────────────────────────────────────────┘
                    │
                    │ Instance state 그대로
                    ▼
[Builder phase — CollectDynamicData]
┌──────────────────────────────────────────────────────────────────────────┐
│ Builder: Component->CacheCameraFromRenderBus(RenderBus)                  │
│   → Component.CachedCamera* 5 멤버 set                                   │
│                                                                          │
│ Builder: array = Component->CollectDynamicData()                         │
│   for each instance:                                                     │
│     DynData = Instance->CreateDynamicData()  ← Sprite/Mesh/Ribbon/Beam   │
│       (a) new FDynamic*EmitterData                                       │
│       (b) Replay = DynData->Source (value);                              │
│           Replay.ParticleData    = ParticleStorage.ParticleData   ← 얕은 │
│           Replay.ParticleIndices = ParticleStorage.ParticleIndices ← 복사│
│           Replay.ActiveParticleCount/Stride/Size/PayloadOffset 등 메타   │
│           [Mesh] Replay.MeshAsset = MeshTD->GetMesh()                    │
│                  Replay.Material  = MeshTD->GetEffectiveMaterial()       │
│           [Beam] Replay.Material = BeamTD->GetMaterial()                 │
│                  Replay.bHasNoise = (BeamNoise module 존재)              │
│       (c) DynData->BuildFromInstance(*this)                              │
│             Sprite: GetParticle(i) loop → SpriteInstanceDataBuffer fill  │
│             Mesh  : GetMeshPayload(slot)->Rotation + camera/velocity →   │
│                     SpinMat × AlignMat → MeshInstanceDataBuffer fill     │
│             Beam  : Source/Target/Noise lookup → segments + perturbation │
│                     + degenerate seam → BeamVertexBuffer fill            │
│             Ribbon: GetRibbonVertexData() snapshot → RibbonVertexBuffer  │
└──────────────────────────────────────────────────────────────────────────┘
                    │
                    │ ownership transferred
                    ▼
[Builder phase — Cmd 발행]
┌──────────────────────────────────────────────────────────────────────────┐
│ Builder: type 분기 (Source.eEmitterType)                                  │
│   Sprite: Required + USubUV + Atlas → Source.Material/Texture/SubUVCols  │
│   Mesh  : MeshBufferManager.GetStaticMeshBuffer + Mat.DiffuseMap         │
│   Ribbon: URibbonTypeData::GetMaterial + Mat.DiffuseMap                  │
│   Beam  : (Material 이미 set) + Mat.DiffuseMap                           │
│                                                                          │
│ Builder: DynData->Sort(RenderBus.GetCameraPosition())                    │
│   Sprite: ViewProjDepth back-to-front (SpriteInstanceDataBuffer 직접)    │
│   Mesh  : ViewProjDepth back-to-front (MeshInstanceDataBuffer 직접)      │
│   Beam  : empty (additive)                                               │
│   Ribbon: empty (placeholder)                                            │
│                                                                          │
│ Builder: Cmd.DynamicData = DynData (ownership 이전)                       │
│         RenderBus.AddCommand(Particle, Cmd)                              │
└──────────────────────────────────────────────────────────────────────────┘
                    │
                    │ Cmd queue
                    ▼
[RenderPass phase]
┌──────────────────────────────────────────────────────────────────────────┐
│ DynData->FillVertexBuffer(Device, DeviceContext, FInstanceBuffer&)       │
│   → InstanceBuffer.Update(Device, Ctx, ptr, count)  (GPU upload)         │
│ ... DrawInstanced ...                                                    │
│                                                                          │
│ frame 끝: delete Cmd.DynamicData                                          │
└──────────────────────────────────────────────────────────────────────────┘
```

---

## 10. 신규 Emitter / Module (기존 md 이후 추가)

### 10.1 신규 모듈 / TypeData 목록

| 모듈 / TypeData | 종류 | bSpawnModule | bUpdateModule | override 메서드 | 주요 멤버 |
|----------------|------|:-----:|:-----:|-----------------|----------|
| `UParticleModuleTypeDataBase` | TypeData base | (n/a) | (n/a) | `RequiredPayloadBytes`, `GetRenderMode`, `CreateInstance` | (없음) |
| `USpriteTypeData` | TypeData Sprite | (n/a) | (n/a) | 위 3종 override (전부 base 회귀) | (없음) |
| `UMeshTypeData` | TypeData Mesh | (n/a) | (n/a) | 위 3종 + `GetEffectiveMaterial` | `Mesh`, `bOverrideMaterial`, `OverrideMaterial`, `Alignment` |
| `URibbonTypeData` | TypeData Ribbon | (n/a) | (n/a) | 위 3종 | `MaxTrailCount`, `MaxParticleInTrailCount`, `SheetsPerTrail`, `TangentSpawningScalar`, `Material` |
| `UBeamTypeData` | TypeData Beam | (n/a) | (n/a) | 위 3종 | `MaxBeamCount`, `InterpolationPoints`, `FallbackDistance`, `TextureTile`, `TextureTileDistance`, `Material` |
| `UParticleModuleBeamSource` | Data module | ✗ | ✗ | (없음) | `SourceComponent` (TObjectPtr) |
| `UParticleModuleBeamTarget` | Data module | ✗ | ✗ | (없음) | `TargetComponent`, `bUseLocalTarget`, `TargetLocalVector` |
| `UParticleModuleBeamNoise` | Data module | ✗ | ✗ | (없음) | `Frequency`(1~8), `NoiseRange`, `bTargetNoise`, `bSmooth` |
| `UParticleModuleMeshRotationRate` | 동작 모듈 | ✓ | ✓ | `Spawn`, `Update` | `RotRateMin/Max` |
| `UParticleModuleBurst` | 동작 모듈 | ✗ | ✓ | `Update` | `BurstCount`, `BurstTime`, `bRepeat`, `RepeatInterval` |
| `UParticleModuleLocationShape` | Spawn 모듈 | ✓ | ✗ | `Spawn` | `Shape` (Sphere/Box/Cone), `bSurfaceOnly`, `SphereRadius`, `BoxExtents`, `ConeHeight`, `ConeHalfAngle` |
| `UParticleModuleAcceleration` | Update 모듈 | ✗ | ✓ | `Update` | `Acceleration` |
| `UParticleModuleDrag` | Update 모듈 | ✗ | ✓ | `Update` | `DragCoefficient` |
| `UParticleModuleRotationRate` | Spawn+Update | ✓ | ✓ | `Spawn`, `Update` | `StartRotationRateMin/Max` |
| `UParticleModuleLight` | helper (no Spawn/Update) | ✗ | ✗ | — | `bLightEnabled`, `LightColor`, `Brightness`, `Radius`, `Falloff`, `MaxLightsPerEmitter` 등 |
| `USubUVModule` | 동작 모듈 | ✓ | ✓ | `Spawn`, `Update`, `Serialize`, `PostEditProperty` | `SubUVName`, `StartFrameIndex`, `EndFrameIndex`, `PlaybackMode`, `FrameRate`, `bLoop`, `bRandomStartFrame`, `CachedSubUV` |

### 10.2 신규 emitter instance 파생

| 인스턴스 타입 | base | override | 추가 owns | 호출 진입점 |
|--------------|------|----------|----------|-------------|
| `FParticleEmitterInstance` | (base) | `~`, `Tick`, `SpawnParticles`, `KillParticle`, `GetRequiredPayloadBytes`, `CreateDynamicData`, `GetRibbonVertexData` (default impls) | `ParticleStorage` | `USpriteTypeData::CreateInstance` (또는 TypeData 부재 시 fallback) |
| `FParticleMeshEmitterInstance` | base | `SpawnParticles`, `CreateDynamicData` | (없음 — payload는 ParticleStorage 인터리브) | `UMeshTypeData::CreateInstance` |
| `FParticleRibbonEmitterInstance` | base | `Tick`, `SpawnParticles`, `KillParticle`, `GetRibbonVertexData` | `HeadIndices`, `VertexBuffer`, `NextTrailIndex` | `URibbonTypeData::CreateInstance` |
| `FParticleBeamEmitterInstance` | base | `Tick`, `SpawnParticles`, `CreateDynamicData` | `BeamStates`, `NextBeamIndex` | `UBeamTypeData::CreateInstance` |

### 10.3 신규 모듈의 data flow 위치

| 모듈 | Spawn 단계 (`SpawnParticles`) | Update 단계 (`Tick`의 UpdateModules) | Event 단계 |
|-----|------------------------------|--------------------------------------|-----------|
| `UParticleModuleBurst` | (n/a) | `Owner->SpawnParticles(BurstCount*TriggerCount, ...)` 호출 (즉, Update 안에서 base SpawnParticles 우회 호출) | — |
| `UParticleModuleLocationShape` | `Particle.Location = Owner->GetComponentWorldLocation() + RandomPointInShape(...)` | — | — |
| `UParticleModuleAcceleration` | — | `Particle.Velocity += Acceleration * dt` (active 루프) | — |
| `UParticleModuleDrag` | — | `Particle.Velocity *= exp(-DragCoef * dt)` | — |
| `UParticleModuleRotationRate` | `Particle.RotationRate = RandomRange(Min, Max)` | `Particle.Rotation += Particle.RotationRate * dt` | — |
| `UParticleModuleLight` | — | (UpdateModule 아님 — Builder/RenderPass의 light pass가 `ShouldCreateLight` + `BuildLightInfo` 호출) | — |
| `USubUVModule` | `Particle.SubUVIndex = RangeStart + RandomOffset` | active 루프에서 RelativeTime/FPS 기반 frame 갱신 | — |
| `UParticleModuleMeshRotationRate` | `dynamic_cast<FParticleMeshEmitterInstance*>(Owner)` 후 `GetMeshPayloadAt(ActiveCount-1)->RotRate = RandomRangeVector(...)` | `dynamic_cast` 후 active 루프 → `Payload->Rotation += Payload->RotRate * dt` | — |
| `UParticleModuleBeamSource/Target/Noise` | (Spawn override 없음 — Beam instance 의 SpawnParticles 가 `FindFirstBeamModule<Noise>` 로 lookup 후 noise samples generate) | (Update override 없음 — DynamicData::BuildFromInstance 가 lookup 후 strip 정점 build에 사용) | — |
| `UMeshTypeData::Alignment` (멤버) | — | — (Update 단계 아님) | `FDynamicMeshEmitterData::BuildFromInstance`에서 frame 1회 lookup 후 `PSA_Velocity / PSA_FacingCameraPosition` 분기 |
| `UBeamTypeData::InterpolationPoints/FallbackDistance/TextureTile/TextureTileDistance` (멤버) | — | — | `FDynamicBeamEmitterData::BuildFromInstance` 가 read |
| `URibbonTypeData::TangentSpawningScalar` (멤버) | `FParticleRibbonEmitterInstance::SpawnParticles`가 read → `Payload->SpawnedTangentStrength = TangentScalar * Speed` | — | `FDynamicRibbonEmitterData::BuildFromInstance` (placeholder) |

### 10.4 컴포넌트 역참조 호출 (`Owner->Component->XXX()` 또는 `Instance->GetOwningComponent()`)

| 호출자 | 호출 | 용도 |
|--------|------|------|
| `UParticleModuleCollision::Update` | `Owner->GetOwningComponent()`, `Component->ComputeEmitterLODDistance()`, `Owner->QueueCollisionEvent(Event)`, `Owner->KillParticle(i)` | World/Actor lookup + 이벤트 큐 push (기존 패턴) |
| `UParticleModuleEventGenerator::Update` | `Owner->GetOwningComponent()->GetPendingCollisionEvents()`, `Owner->DispatchQueuedParticleEvents()` | 이벤트 dispatch + overflow trim |
| `UParticleModuleBurst::Update` | `Owner->SpawnParticles(BurstCount*N, 0, 0, Owner->GetComponentWorldLocation(), ZeroVector)` | 강제 spawn |
| `UParticleModuleLocation::Spawn` / `UParticleModuleLocationShape::Spawn` | `Owner->GetComponentWorldLocation()` | base location |
| `FDynamicMeshEmitterData::BuildFromInstance` | `Instance.GetOwningComponent()->IsCachedCameraValid()` / `GetCachedCameraPosition()` | PSA_FacingCameraPosition alignment |
| `FDynamicBeamEmitterData::BuildFromInstance` | `Instance.GetComponentWorldLocation()`, `Instance.GetOwningComponent()->GetForwardVector()/GetRightVector()/GetUpVector()` | source fallback, EmitterForward/Right/Up axes (bUseLocalTarget 계산) |
| `UParticleSystemComponent::CacheCameraFromRenderBus` (외부 Builder 호출) | (Component 측 진입점 — 자기 자신 멤버 set) | derived BuildFromInstance가 read할 cache |

---

## 11. 변경/모순 사항 (기존 md 대비)

기존 [particle_class_relation.md](Document/particle_class_relation.md) (이번 갱신 전 상태) 의 서술과 현재 코드가 어긋나는 항목:

1. **`UParticleModuleTypeDataBase`는 더 이상 stub이 아님.** 기존 md 1.1: "정의 파일이 디렉토리 내 없음. 비소유 포인터로만 들고 있음." → 현재 [ParticleModuleTypeData.h:13](JSEngine/Source/Engine/Particle/ParticleModuleTypeData.h:13) 에 UCLASS 정의가 있고 3종 가상 메서드 + 4종 derived(Sprite/Mesh/Ribbon/Beam)가 완전 구현. `UParticleLODLevel::TypeDataModule`은 **UPROPERTY 소유 멤버**(`owns`)로 격상되었다.
2. **`UParticleLODLevel::TypeDataModule`은 비소유가 아님.** 기존 md 3, 4.2: "stub, 비소유". → 현재는 `UPROPERTY` 로 마크되어 직렬화/소유 관리. (silent bug ι 회피 — 본문 7.5 참조)
3. **`FParticleEmitterInstance`는 더 이상 final 클래스가 아님 (가상 dtor + 파생 인스턴스 3종).** 기존 md는 단일 POD struct로만 표기. → Mesh/Ribbon/Beam derived가 추가되었고 base에 `virtual ~`, `virtual Tick`, `virtual SpawnParticles`, `virtual KillParticle`, `virtual CreateDynamicData`, `virtual GetRibbonVertexData`, `virtual GetRequiredPayloadBytes` 도입.
4. **`FParticleEmitterInstance`의 `ParticleData`/`ParticleIndices`/`InstanceData`/`ParticleStride`/`InstancePayloadSize` 멤버 삭제됨.** 기존 md 3.1: `owns ──► uint8* ParticleData (new uint8[ParticleStride * Max])` 등으로 기재. → 현재는 `FParticleDataContainer ParticleStorage` 단일 멤버가 단일 alloc(`new uint8[MemBlockSize]`)로 ParticleData + ParticleIndices 통합 보유. 16-byte align 적용. `InstanceData`/`InstancePayloadSize`/`SpriteInstanceDataBuffer` 전부 Cycle 15a Phase 5에서 삭제 ([ParticleEmitterInstance.h:96](JSEngine/Source/Engine/Particle/ParticleEmitterInstance.h:96), [ParticleEmitterInstance.h:106](JSEngine/Source/Engine/Particle/ParticleEmitterInstance.h:106) 코멘트).
5. **`FParticleEventCollideData::HitComponent`/`HitActor` 슬롯이 이제 채워진다.** 기존 md 7.7: "현재 collision 모듈은 이 두 필드를 채우지 않는다 (단순 평면 충돌)." → 현재 [ParticleModules.cpp:664](JSEngine/Source/Engine/Particle/ParticleModules.cpp:664) 에서 `Event.HitComponent = Hit.HitComponent; Event.HitActor = Hit.HitComponent->GetOwner();` 로 set. Sweep/LineTrace 기반 일반 World 충돌과 통합됨.
6. **`UParticleSystemComponent`가 camera 캐시 5종을 멤버로 보유.** 기존 md에는 없음. → Cycle 14 (`CachedCameraPosition/Forward/Up/Right` + `bCachedCameraValid`) 추가, `CacheCameraFromRenderBus`/getter API 추가 ([ParticleSystemComponent.h:46-54, 89-93](JSEngine/Source/Engine/Particle/ParticleSystemComponent.h:46)).
7. **`UParticleSystemComponent::CollectDynamicData()` 추가, `BuildInstanceData()` 삭제됨.** 기존 md는 BuildInstanceData 흐름이 없었지만, Cycle 15a Phase 5 (D5) 에서 기존 BuildInstanceData() / GetSpriteInstanceData / GetMeshInstanceData / GetBeamVertexData 가 모두 삭제되고 `CollectDynamicData()` 가 단일 진입점으로 대체. 본문 §9.2 참조.
8. **모듈 목록이 크게 확장됨.** 기존 md 1: Required/Spawn/Lifetime/Location/Velocity/Color/Size/Collision/EventGenerator 9종만 기재. → 현재는 추가로 `Burst`/`LocationShape`/`Acceleration`/`Drag`/`RotationRate`/`Light`/`SubUV`/`MeshRotationRate`/`BeamSource`/`BeamTarget`/`BeamNoise` 11종 신규 + TypeData 5종(`TypeDataBase`/`Sprite`/`Mesh`/`Ribbon`/`Beam`).
9. **`AParticleEventManager`가 이제 자기 자신의 `UParticleSystemComponent` 와 `PreviewParticleSystem` 을 보유 + 소유.** 기존 md 7.1: "독립 액터 — Particle/ 외부 검색 시 .vcxproj 외에는 나오지 않는다." → 현재 [ParticleEvent.cpp:35-57](JSEngine/Source/Engine/Particle/ParticleEvent.cpp:35) 의 `InitDefaultComponents`가 `AddComponent<UParticleSystemComponent>` + `CreateDefaultSpriteSystem` + `USubUVModule` 추가 + `SetTickInEditor(true)` 로 placeable preview 액터로 동작. **단** 외부 `PushCollisionEvent` 호출자는 여전히 0건 — 컴포넌트 큐와는 별개 허브 상태는 유지.
10. **`UParticleSystem`에 type별 기본 emitter system 팩토리 3종 추가.** `CreateDefaultMeshSystem` ([ParticleSystem.cpp:549](JSEngine/Source/Engine/Particle/ParticleSystem.cpp:549)), `CreateDefaultRibbonSystem` ([ParticleSystem.cpp:612](JSEngine/Source/Engine/Particle/ParticleSystem.cpp:612)), `CreateDefaultBeamSystem` ([ParticleSystem.cpp:661](JSEngine/Source/Engine/Particle/ParticleSystem.cpp:661)). 기존엔 `CreateDefaultSpriteSystem` 만 있었음.
11. **`UParticleLODLevel::GetEffectiveRenderMode()` 추가.** TypeDataModule 우선 → RequiredModule 폴백 → Sprite 기본 ([ParticleSystem.cpp:199](JSEngine/Source/Engine/Particle/ParticleSystem.cpp:199)). `FParticleEmitterInstance::CreateDynamicData`의 base 분기 (Sprite vs Ribbon placeholder) 가 이 메서드를 사용.
12. **`FParticleEmitterRuntimeView` 삭제됨.** 기존 md에 명시적 stub 표 항목으로는 없었으나 (Cycle 15a Phase 5 이전엔 존재했을 가능성) — 현재는 [ParticleTypes.h:134](JSEngine/Source/Engine/Particle/ParticleTypes.h:134) 코멘트에 "삭제됨, `FDynamicEmitterReplayDataBase` 가 대체" 명시.
13. **`ParticleUpdateUtils.h` 는 dead 상태 유지.** 기존 md 7.2 의 관찰과 동일 — 변경 없음 (참고: 본 문서 7.2).
