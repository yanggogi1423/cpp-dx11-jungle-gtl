# Particle Control Flow Diagnosis (Phase B 진입 전)

작성일: 2026-05-24
대상 브랜치: `feature/ParticleRender`
선행 문서: `Cascade_Porting_Status.md`, `VertexFactory_Cascade_Investigation.md`, `RenderDataFlow.md`, `particle_class_relation.md`

---

## 1. 진단 범위 및 기준 명제

사용자가 의심하는 명제:
> "Render infra(GPU 그리기 경로)는 갖춰졌으나, asset과 instance별로 particle effect를 control할 수 있는 infra가 부족하다."

이 명제를 전체 control flow의 각 hop에서 실측 검증한다.
산출 4축: **Has / Missing / Hardcoded / Coupling**.

---

## 2. 전체 Control Flow 그림

```
[Asset 객체 그래프]
UParticleSystem ── owns ──► UParticleEmitter ── owns ──► UParticleLODLevel
                                                            │
                                                            ├─ owns ──► UParticleModuleRequired (Atlas, MaxParticles, Lifetime ...)
                                                            ├─ owns ──► UParticleModuleSpawn (Rate)
                                                            └─ owns ──► UParticleModule[]
        │ SetTemplate
        ▼
[Component 측]
UParticleSystemComponent.Template (UPROPERTY)
        │ RecreateEmitterInstances → new FParticleEmitterInstance(N)
        ▼
[런타임 인스턴스]
FParticleEmitterInstance.ParticleData/ParticleIndices/ActiveParticles
        │ TickComponent → Instance.Tick(dt)
        │   ├─ SpawnModule.ComputeSpawnCount → SpawnCount
        │   ├─ SpawnParticles → SpawnModules.Spawn(particle)
        │   ├─ active loop : RelativeTime, Location 업데이트, KillParticle
        │   └─ UpdateModules.Update(this, dt)
        ▼
[Command 브릿지]
PrimitiveDrawCommandBuilder.case EPT_ParticleSystem
        │ ParticleSystemComponent.BuildSpriteInstanceData() → EmitterInstanceData[][]
        │ for each emitter:
        │   FRenderCommand Cmd
        │   ├─ ParticleInstances     = InstanceData.data()
        │   ├─ ParticleInstanceCount = InstanceData.size()
        │   ├─ ParticleTexture       = nullptr        ★ HARDCODED
        │   ├─ ParticleSubUVColumns  = 1              ★ HARDCODED
        │   └─ ParticleSubUVRows     = 1              ★ HARDCODED
        │ RenderBus.AddCommand(ERenderPass::Particle, Cmd)
        ▼
[GPU 측]
FParticleRenderPass.DrawCommand
        │ for each Cmd in GetCommands(Particle):
        │   InstanceBuffer.Update(Cmd.ParticleInstances, Count)
        │   SpriteParticleCB(b8) ← (SubUVColumns/Rows)
        │   PSSetShaderResources(0, Cmd.ParticleTexture->GetSRV())   ★ nullptr → 검은 sample
        │   IASetVertexBuffers(0, 2, {Quad, InstanceBuffer})
        │   DrawIndexedInstanced(6, InstanceCount, 0, 0, 0)
        ▼
[Shader]
SpriteParticleVS  : view billboard + SubUV grid → SV_POSITION + TexCoord + Color
SpriteParticlePS  : SpriteAtlas.Sample(TexCoord) * Color, alpha<0.01 → discard
```

---

## 3. Hop별 실측 결과

### 3.1 Asset 측 — UObject 객체 그래프

#### Has
- [x] `UParticleSystem` UCLASS + `Emitters: TArray<UParticleEmitter*>` UPROPERTY — [ParticleSystem.h:72-86](JSEngine/Source/Engine/Particle/ParticleSystem.h:72)
- [x] `UParticleEmitter` UCLASS + `LODLevels: TArray<UParticleLODLevel*>` UPROPERTY — [ParticleSystem.h:48-69](JSEngine/Source/Engine/Particle/ParticleSystem.h:48)
- [x] `UParticleLODLevel` UCLASS + 5개 UPROPERTY (`Level`, `bEnabled`, `DistanceThreshold`, `RequiredModule`, `Modules`) — [ParticleSystem.h:8-46](JSEngine/Source/Engine/Particle/ParticleSystem.h:8)
- [x] `UParticleModule` 베이스 UCLASS + `bEnabled`/`bSpawnModule`/`bUpdateModule` UPROPERTY — [ParticleModule.h:8-42](JSEngine/Source/Engine/Particle/ParticleModule.h:8)
- [x] 9개 module 파생 UCLASS — [ParticleModules.h](JSEngine/Source/Engine/Particle/ParticleModules.h): Required / Spawn / Lifetime / Location / Velocity / Color / Size / Collision / EventGenerator
- [x] Factory: `UObjectManager::Get().CreateObject<T>()` (`NewObject<T>()`도 alias로 동작) — [Object.h:128-147](JSEngine/Source/Engine/Object/Object.h:128)
- [x] Reflection: `FReflectionRegistry::Get().FindClass(ClassName)` 으로 string→Class 매핑
- [x] 직렬화: `UObject::Serialize(FArchive&)` + `SerializeProperties` UPROPERTY 기반 자동 직렬화 — [Object.h:98-99](JSEngine/Source/Engine/Object/Object.h:98)
- [x] `.particlesystem` 확장자 + json 기반 직렬화 포맷 — [ResourceManager.cpp:59-65](JSEngine/Source/Engine/Core/ResourceManager.cpp:59) (IsParticleSystemAssetPath)
- [x] Multi-object 그래프 직렬화: `FParticleSystemObjectGraphResolver` + `CollectParticleSystemObjectGraph` + reflection-based ref traversal — [ResourceManager.cpp:85-187](JSEngine/Source/Engine/Core/ResourceManager.cpp:85)
- [x] `UParticleSystem::CreateDefaultSpriteSystem()` reference impl 존재 (Required + Spawn + Lifetime + Location + Velocity + Color + Size 7-module 셋업) — [ParticleSystem.cpp:183-215](JSEngine/Source/Engine/Particle/ParticleSystem.cpp:183)
- [x] `AddEmitter` / `RemoveEmitter` / `ClearEmitters` / `CacheEmitterModuleInfo` / `Validate` API — [ParticleSystem.cpp:108-182](JSEngine/Source/Engine/Particle/ParticleSystem.cpp:108)
- [x] `EditorParticleSystemWidget` — 본격 에디터 UI 존재 (DrawMainLayout / EmittersPanel / DetailsPanel / CurveEditor) — [EditorParticleSystemWidget.h:12-77](JSEngine/Source/Editor/UI/EditorParticleSystemWidget.h:12)
- [x] `ParticleSystemViewportClient` — 별도 preview viewport 존재
- [x] `EParticleEmitterRenderMode` enum (`Sprite`/`Mesh`/`Beam`/`Ribbon`) 정의 — [ParticleTypes.h:11-17](JSEngine/Source/Engine/Particle/ParticleTypes.h:11)

#### Missing
- [ ] **`USubUVModule` / `UParticleModuleSubUV` 클래스가 없다** — grep 결과 0건. ParticleSystemComponent.cpp:222 TODO 주석만 존재
- [ ] **`UParticleModuleRequired`에 SubUV atlas texture 포인터 필드가 없다** — `SubUVName: FName` 1개만 있음, `UTexture*` 없음 — [ParticleModules.h:35-36](JSEngine/Source/Engine/Particle/ParticleModules.h:35)
- [ ] **`UParticleModuleRequired`에 SubUV columns/rows UPROPERTY가 없다**
- [ ] `UParticleModuleTypeDataBase` (Mesh/Beam/Ribbon RenderMode 분기용) — forward 선언만, 정의 없음 — [ParticleSystem.h:6](JSEngine/Source/Engine/Particle/ParticleSystem.h:6), [particle_class_relation.md:30](Document/particle_class_relation.md)
- [ ] `FParticleEventInstancePayload` — forward 선언만, 정의 없음
- [ ] AParticleEventManager가 컴포넌트 이벤트와 연결되어 있지 않음 (병렬 허브 상태, 별 개 작업)

#### Hardcoded
- `UParticleEmitter::ParticleSize = sizeof(FBaseParticle)` 고정 — `CacheEmitterModuleInfo`에서 항상 재설정 ([ParticleSystem.cpp:48](JSEngine/Source/Engine/Particle/ParticleSystem.cpp:48))
- `UParticleEmitter::MaxActiveParticles = 128` default, RequiredModule.MaxParticles로 max() 갱신
- `UParticleModuleRequired::RenderMode = Sprite` 고정 (UPROPERTY 아님 — private 멤버) — [ParticleModules.h:38](JSEngine/Source/Engine/Particle/ParticleModules.h:38)
- `UParticleModuleSpawn::Rate = 10.0f` default
- `UParticleLODLevel::DistanceThreshold = 100000.0f` default
- `UParticleModuleRequired::MaxParticles = 128`, `EmitterDuration = 1.0f`, `bLooping = true`

#### Coupling
- `UParticleSystem.Emitters` — `UPROPERTY`로 노출, json 직렬화/Inspector 양쪽 자동 처리
- `UParticleLODLevel.RequiredModule` + `Modules` — 둘 다 UPROPERTY, 자동 직렬화/노출
- `UParticleLODLevel.SpawnModule` / `SpawnModules` / `UpdateModules` — **non-UPROPERTY 캐시**. `CacheModuleLists()` 호출 안 하면 비어 있어 Tick에서 Spawn/Update 동작 안 함 ([ParticleSystem.cpp:8-41](JSEngine/Source/Engine/Particle/ParticleSystem.cpp:8))
- `UParticleEmitter.ParticleSize` / `MaxActiveParticles` — non-UPROPERTY 캐시, `CacheEmitterModuleInfo()` 호출 필요
- `UParticleModuleRequired::RenderMode` private + non-UPROPERTY → asset 편집기에서 변경 불가, 코드에서만 변경 가능

---

### 3.2 Component 측 — Template → EmitterInstances

#### Has
- [x] `UParticleSystemComponent` UCLASS(SpawnableComponent) — [ParticleSystemComponent.h:9-10](JSEngine/Source/Engine/Particle/ParticleSystemComponent.h:9)
- [x] `Template: UParticleSystem*` UPROPERTY → reflection inspector로 할당 가능 — [ParticleSystemComponent.h:58-59](JSEngine/Source/Engine/Particle/ParticleSystemComponent.h:58)
- [x] `SetTemplate(UParticleSystem*)` 본문: same-pointer early return → `Template = InTemplate` → `RecreateEmitterInstances()` — [ParticleSystemComponent.cpp:16-25](JSEngine/Source/Engine/Particle/ParticleSystemComponent.cpp:16)
- [x] `RecreateEmitterInstances()`: `ClearEmitterInstances()` → Template nullptr 가드 → `Template->GetEmitters()` 순회 → `new FParticleEmitterInstance()` → `Instance->Init(...)` — [ParticleSystemComponent.cpp:30-46](JSEngine/Source/Engine/Particle/ParticleSystemComponent.cpp:30)
- [x] `ClearEmitterInstances()`: 모든 instance `delete`, `EmitterInstances.clear()`, `PendingCollisionEvents.clear()` — [ParticleSystemComponent.cpp:51-59](JSEngine/Source/Engine/Particle/ParticleSystemComponent.cpp:51)
- [x] `~UParticleSystemComponent()` → `ClearEmitterInstances()` (dangling 방지) — [ParticleSystemComponent.cpp:7-10](JSEngine/Source/Engine/Particle/ParticleSystemComponent.cpp:7)
- [x] `FParticleEmitterInstance::Init` 본문: `SpriteTemplate->CacheEmitterModuleInfo()` → ParticleSize/MaxActiveParticles 읽음 → `SelectLODLevel(0)` → `GetLODLevel(...)` → ParticleData/ParticleIndices `new[]` — [ParticleEmitterInstance.cpp:18-49](JSEngine/Source/Engine/Particle/ParticleEmitterInstance.cpp:18)
- [x] Component ↔ Instance 양방향 참조 (Instance.Component back-ref) — [ParticleEmitterInstance.h:50](JSEngine/Source/Engine/Particle/ParticleEmitterInstance.h:50)
- [x] `GetEmitterInstances() const` 외부 노출 — [ParticleSystemComponent.h:20](JSEngine/Source/Engine/Particle/ParticleSystemComponent.h:20)
- [x] `GetTotalActiveParticleCount`, `GetEmitterInstance(Index)` API — [ParticleSystemComponent.cpp:137-171](JSEngine/Source/Engine/Particle/ParticleSystemComponent.cpp:137)

#### Missing
- [ ] Template 변경 시 spatial index dirty notify 명시적 호출 없음 (TickComponent에서만 `NotifySpatialIndexDirty` 호출) — minor

#### Hardcoded
- `ParticleSize = sizeof(FBaseParticle)`, `ParticleStride = ParticleSize` (FBaseParticle 외 다른 layout 불가)
- `MaxActiveParticles = max(SpriteTemplate->GetMaxActiveParticleCount(), 1)` — Template 없으면 1로 fallback

#### Coupling
- `Template` 변경 → `RecreateEmitterInstances()`가 동기적으로 모든 instance를 새로 할당 (frame 중간에 호출되면 비싸지만 정확함)
- `Instance->Init` 안에서 `SpriteTemplate->CacheEmitterModuleInfo()` 호출 — Template를 외부에서 따로 cache할 필요 없음 (자동)
- `FParticleEmitterInstance`는 **UObject가 아님** — raw `new`/`delete`, UObject GC 밖

---

### 3.3 Tick 측 — Spawn/Update Module → ActiveParticles

#### Has
- [x] `TickComponent(dt)`: EmitterInstances 순회 → `Instance->Tick(dt)` + `NotifySpatialIndexDirty()` — [ParticleSystemComponent.cpp:177-187](JSEngine/Source/Engine/Particle/ParticleSystemComponent.cpp:177)
- [x] `Instance::Tick(dt)` 전체 시퀀스 — [ParticleEmitterInstance.cpp:76-120](JSEngine/Source/Engine/Particle/ParticleEmitterInstance.cpp:76):
  - 1. 가드 (Template/Component/Data/dt)
  - 2. `SelectLODLevel(Component->ComputeEmitterLODDistance())`
  - 3. `SpawnModule->ComputeSpawnCount(this, dt)` → SpawnCount
  - 4. `SpawnParticles(SpawnCount, 0, dt/N, ComponentWorldLocation, ZeroVector)`
  - 5. active particle 루프: `RelativeTime += dt/Lifetime`, `Location += Velocity * dt`, `RelativeTime >= 1` → `KillParticle`
  - 6. `UpdateModules` 순회 → `Module->Update(this, dt)`
- [x] `SpawnParticles`: `GetSpawnModules()` 순회 → `Module->Spawn(this, *Particle, SpawnTime)` — [ParticleEmitterInstance.cpp:152-186](JSEngine/Source/Engine/Particle/ParticleEmitterInstance.cpp:152)
- [x] `KillParticle`: swap-to-end 방식 — [ParticleEmitterInstance.cpp:192-202](JSEngine/Source/Engine/Particle/ParticleEmitterInstance.cpp:192)
- [x] `ConsumeSpawnCount(Rate, dt)`: `Rate*dt + SpawnFraction` 누적 → floor → 잔여 fraction 저장 — [ParticleEmitterInstance.cpp:270-281](JSEngine/Source/Engine/Particle/ParticleEmitterInstance.cpp:270)

#### Module 구현 상태 (껍데기/일부/완전)

| Module | Spawn | Update | 상태 | 위치 |
|--------|-------|--------|------|------|
| `UParticleModuleRequired` | RelativeTime/Lifetime/Size/Color 초기화 | (없음) | **완전** | [ParticleModules.cpp:34-53](JSEngine/Source/Engine/Particle/ParticleModules.cpp:34) |
| `UParticleModuleSpawn` | (없음) | `ComputeSpawnCount` (Rate 기반) | **완전** | [ParticleModules.cpp:55-73](JSEngine/Source/Engine/Particle/ParticleModules.cpp:55) |
| `UParticleModuleLifetime` | `Lifetime = RandomRange(Min, Max)` | (없음) | **완전** | [ParticleModules.cpp:75-91](JSEngine/Source/Engine/Particle/ParticleModules.cpp:75) |
| `UParticleModuleLocation` | `Location = ComponentWorldLocation + RandomRangeVector(Min, Max)` | (없음) | **완전** | [ParticleModules.cpp:93-111](JSEngine/Source/Engine/Particle/ParticleModules.cpp:93) |
| `UParticleModuleVelocity` | `Velocity = RandomRangeVector(Min, Max)` | (없음) | **완전** | [ParticleModules.cpp:113-130](JSEngine/Source/Engine/Particle/ParticleModules.cpp:113) |
| `UParticleModuleColor` | `Color = StartColor` | `Color = Lerp(Start, End, RelativeTime)` | **완전** | [ParticleModules.cpp:132-164](JSEngine/Source/Engine/Particle/ParticleModules.cpp:132) |
| `UParticleModuleSize` | `Size = StartSize` | `Size = Lerp(Start, End, RelativeTime)` | **완전** | [ParticleModules.cpp:166-198](JSEngine/Source/Engine/Particle/ParticleModules.cpp:166) |
| `UParticleModuleCollision` | (없음) | Z 평면 충돌 + 이벤트 큐 | **완전** (간이) | [ParticleModules.cpp:200-258](JSEngine/Source/Engine/Particle/ParticleModules.cpp:200) |
| `UParticleModuleEventGenerator` | (없음) | `Component->DispatchQueuedParticleEvents()` | **완전** | [ParticleModules.cpp:260-277](JSEngine/Source/Engine/Particle/ParticleModules.cpp:260) |

#### Missing
- [ ] **SubUV-related module 자체가 없음** — RelativeTime → SubUVIndex 매핑할 방법 없음
- [ ] Rotation/RotationRate 관련 module 없음 (Spawn 시 `*Particle = FBaseParticle()` → Rotation=0)
- [ ] Acceleration / Force module 없음 (중력 등)
- [ ] Mesh/Beam/Ribbon RenderMode용 module 없음 (RenderMode enum만 존재)

#### Hardcoded
- `Particle.Rotation = 0` 고정 (Spawn 시 default ctor로 0, update 안 함)
- `Particle.RotationRate = 0` 고정
- `SpawnIncrement = dt / SpawnCount` (Count==0이면 0) — 균등 시간 분배 가정
- `RelativeTime += dt / max(Lifetime, 0.01f)` — Lifetime이 0.01보다 작으면 clamp
- `InitialVelocity = ZeroVector` (Tick에서 SpawnParticles 호출 시 hardcode) — Velocity module이 덮어쓰지만 없으면 0

#### Coupling
- `UParticleModuleSpawn::UParticleModuleSpawn()` 에서 `bSpawnModule = false` — `LODLevel.SpawnModules` 캐시에는 안 들어감. 별도 `SpawnModule` 캐시 슬롯에만 들어감 ([ParticleSystem.cpp:26-30](JSEngine/Source/Engine/Particle/ParticleSystem.cpp:26)). **Tick은 `GetSpawnModule()`로만 ComputeSpawnCount 호출하므로 의도된 분리.**
- `CacheModuleLists()`가 안 불리면 `SpawnModules` / `UpdateModules`가 비어 Spawn/Update 모두 no-op. `Init` 시 자동 호출되므로 보통은 안전 — 단, **이미 인스턴스화된 emitter의 Module 배열을 외부에서 수정**하면 캐시가 stale.
- `Component->ComputeEmitterLODDistance()` → `GetOwner()->GetFocusedWorld()->GetActiveCamera()` 의존 (World/Camera 없으면 0.0f, LOD 0 fallback)

---

### 3.4 Command 브릿지 측 — instance data + Cmd 채우기

#### Has
- [x] `BuildSpriteInstanceData()` 본문 — emitter 루프, ActiveParticles 만큼 `FSpriteParticleInstanceData` 채움 — [ParticleSystemComponent.cpp:189-226](JSEngine/Source/Engine/Particle/ParticleSystemComponent.cpp:189):
  - `Data.Position = Particle->Location`
  - `Data.Size = FVector2(Particle->Size.X, .Y)`
  - `Data.Color = Particle->Color`
  - `Data.Rotation = Particle->Rotation`
- [x] `GetEmitterInstanceData(int32) const` getter (out-of-range → static empty) — [ParticleSystemComponent.cpp:228-236](JSEngine/Source/Engine/Particle/ParticleSystemComponent.cpp:228)
- [x] `FRenderCommand`에 신규 4 멤버 + 인스턴스 카운트 추가 (struct 멤버, union 외부) — [RenderCommand.h:482-488](JSEngine/Source/Engine/Render/Scene/RenderCommand.h:482):
  - `const FSpriteParticleInstanceData* ParticleInstances`
  - `uint32 ParticleInstanceCount`
  - `UTexture* ParticleTexture`
  - `uint32 ParticleSubUVColumns`
  - `uint32 ParticleSubUVRows`
- [x] `PrimitiveDrawCommandBuilder::EPT_ParticleSystem` case 본문 + `return true` (fall-through 버그 해결) — [PrimitiveDrawCommandBuilder.cpp:535-573](JSEngine/Source/Engine/Render/Scene/PrimitiveDrawCommandBuilder.cpp:535):
  - `Cast<UParticleSystemComponent>` + nullptr 체크 + `BuildSpriteInstanceData()` 호출
  - emitter 루프 → `GetEmitterInstanceData` empty면 skip
  - `Cmd.PerObjectConstants = (Identity, white)`
  - `Cmd.VertexFactoryType = SpriteParticle`
  - `Cmd.WorldAABB = ComponentWorldAABB`
  - `Cmd.ParticleInstances = InstanceData.data()`
  - `Cmd.ParticleInstanceCount = InstanceData.size()`
  - `RenderBus.AddCommand(ERenderPass::Particle, Cmd)`

#### Missing
- [ ] **`Data.SubUVIndex = 0` 고정** (per-particle) — `// TODO: USubUVModule 포팅 후 페이로드에서 추출` 주석 박혀 있음 — [ParticleSystemComponent.cpp:222](JSEngine/Source/Engine/Particle/ParticleSystemComponent.cpp:222)
- [ ] 거리순 정렬 없음 — alpha blending 정확도 영향 (낮은 우선순위)
- [ ] Material 결합 없음 — `Cmd.Material = nullptr`

#### Hardcoded (= "Control이 끊긴 지점")
| Cmd 필드 | 현재 값 | 원본이어야 할 위치 |
|---------|--------|------------------|
| **`Cmd.ParticleTexture`** | **`nullptr`** | RequiredModule(or SubUVModule) — TODO 주석 박힘 — [PrimitiveDrawCommandBuilder.cpp:566](JSEngine/Source/Engine/Render/Scene/PrimitiveDrawCommandBuilder.cpp:566) |
| **`Cmd.ParticleSubUVColumns`** | **`1`** | RequiredModule(or SubUVModule) — [PrimitiveDrawCommandBuilder.cpp:567](JSEngine/Source/Engine/Render/Scene/PrimitiveDrawCommandBuilder.cpp:567) |
| **`Cmd.ParticleSubUVRows`** | **`1`** | RequiredModule(or SubUVModule) — [PrimitiveDrawCommandBuilder.cpp:568](JSEngine/Source/Engine/Render/Scene/PrimitiveDrawCommandBuilder.cpp:568) |
| `Cmd.PerObjectConstants.Model` | `Identity` | Component WorldMatrix (Position은 instance마다 따로 들고 있으므로 OK인 설계) |
| `Cmd.PerObjectConstants.Color` | `(1,1,1,1)` | (per-particle Color로 충분, OK) |
| `Cmd.Material` | `nullptr` (init) | Material 시스템 결합 필요 (후순위) |
| `Data.SubUVIndex` | `0` (per-particle) | RelativeTime × FrameCount 같은 산출 |

#### Coupling
- `Cmd.ParticleInstances`는 **Component 소유 메모리 포인터** — 한 프레임 안에서만 유효. `PrepareBatchers` 거치지 않고 `RenderPass.DrawCommand`가 직접 `GetCommands(Particle)` 소비.
- `BuildSpriteInstanceData()`는 builder가 매번 호출 (캐시 무효 신호 없음). 같은 프레임에 여러 viewport가 같은 component를 보면 중복 계산.
- `ShowFlags.bPrimitives` 가드 — Particle은 EPT_ParticleSystem case에서 체크 ([PrimitiveDrawCommandBuilder.cpp:537-538](JSEngine/Source/Engine/Render/Scene/PrimitiveDrawCommandBuilder.cpp:537))

#### 끊긴 path 목록 (정량적)
1. **Asset (RequiredModule) → Cmd.ParticleTexture**: asset에 atlas texture pointer 필드가 없음 + builder에서 nullptr 고정
2. **Asset (RequiredModule) → Cmd.ParticleSubUVColumns/Rows**: asset에 columns/rows 필드가 없음 + builder에서 1 고정
3. **Particle.RelativeTime → Data.SubUVIndex**: per-particle SubUVIndex 산출 로직 부재 (`Data.SubUVIndex = 0`)
4. **Particle.Rotation → 동적 회전**: Spawn 시 0으로 초기화되고 갱신 안 됨 (Rotation module 부재)
5. **Asset Material → Cmd.Material**: Material 결합 미구현 (후순위)

---

### 3.5 GPU 측 — Pass / cbuffer / Shader

#### Has
- [x] `ERenderPass::Particle` enum 등록 (Translucent와 SelectionMask 사이) — [RenderTypes.h:48-67](JSEngine/Source/Engine/Render/Common/RenderTypes.h:48)
- [x] `FParticleRenderPass` 클래스 (`FBaseRenderPass` 상속) — [ParticleRenderPass.h:6-24](JSEngine/Source/Engine/Render/Renderer/RenderFlow/ParticleRenderPass.h:6)
- [x] `RenderPipeline.cpp:91,145,238` ParticleRenderPass 생성 / `RenderPasses.push_back` / `Release`/`reset` 완전 결선
- [x] `EnsureGPUResources(Device)`: 정적 quad VB(`FSpriteParticleVertex[4]`) + IB(`uint32[6]`) + `InstanceBuffer(stride=44, cap=256)` + `SpriteParticleCB(16)` 1회 생성 — [ParticleRenderPass.cpp:55-83](JSEngine/Source/Engine/Render/Renderer/RenderFlow/ParticleRenderPass.cpp:55)
- [x] `Begin`: RTV=PrevPassRTV, DSV=Context, Topology=TriList, OutRTV/SRV chain — [ParticleRenderPass.cpp:85-95](JSEngine/Source/Engine/Render/Renderer/RenderFlow/ParticleRenderPass.cpp:85)
- [x] `DrawCommand` 본문 — [ParticleRenderPass.cpp:97-193](JSEngine/Source/Engine/Render/Renderer/RenderFlow/ParticleRenderPass.cpp:97):
  - Commands 비면 early return
  - Program(VS+PS) GetOrCreate + Bind
  - State: AlphaBlend / DepthReadOnly / SolidNoCull / Linear sampler
  - 각 Cmd마다: `InstanceBuffer.Update(ParticleInstances, Count)` → `SpriteParticleCB.Update(SubUVColumns/Rows)` → `VSSetConstantBuffers(8, b8)` → `PerObjectConstantBuffer.Update + VS/PS b1` → `PSSetShaderResources(0, ParticleTexture->GetSRV())` → `IASetVertexBuffers(0, 2, {Quad, Instance})` → `DrawIndexedInstanced(6, InstanceCount, 0, 0, 0)`
  - 종료 시 slot 1 명시적 nullptr unbind (다음 패스 보호)
- [x] `SpriteParticleCB` (16B): `SubUVColumns, SubUVRows, Padding[2]` — VS만 b8에 binding — [ParticleRenderPass.cpp:14-19](JSEngine/Source/Engine/Render/Renderer/RenderFlow/ParticleRenderPass.cpp:14)
- [x] Shader `SpriteParticleVS`: view-space billboard + RotatedXY + SubUV cell offset/scale 계산 — [SpriteParticle.hlsl:35-68](JSEngine/Shaders/Particle/SpriteParticle.hlsl:35)
- [x] Shader `SpriteParticlePS`: `SpriteAtlas.Sample(SpriteSampler, TexCoord) * Color`, alpha<0.01 discard — [SpriteParticle.hlsl:70-79](JSEngine/Shaders/Particle/SpriteParticle.hlsl:70)
- [x] `SpriteParticleLayout` 7 elements (Slot 0 PER_VERTEX×2, Slot 1 PER_INSTANCE×5) — [VertexFactoryTypes.h:110-121](JSEngine/Source/Engine/Render/Resource/VertexFactoryTypes.h:110)
- [x] `Registry::Get(SpriteParticle)` 명시 case — [VertexFactoryTypes.h:248-249](JSEngine/Source/Engine/Render/Resource/VertexFactoryTypes.h:248)

#### Missing
- [ ] `Cmd.ParticleTexture == nullptr` 케이스에 대한 fallback texture 없음 — `PSSetShaderResources(0, nullptr)` → `SpriteAtlas.Sample(...)` → 검은 sample → `Sample.a * Color.a == 0` → **discard** → **화면에 안 보임**

#### Hardcoded
- Quad geometry: XY ∈ [-0.5, 0.5], UV ∈ [0,1] 표준 (조정 없음, 의도된 unit quad) — [ParticleRenderPass.cpp:62-68](JSEngine/Source/Engine/Render/Renderer/RenderFlow/ParticleRenderPass.cpp:62)
- `InstanceBuffer` 초기 capacity 256, grow-by-2x ([ParticleRenderPass.cpp:75](JSEngine/Source/Engine/Render/Renderer/RenderFlow/ParticleRenderPass.cpp:75))
- VS의 `register(b8)` — PS에는 SpriteParticleCB 안 묶임 (현재 PS는 SubUVColumns/Rows 안 씀)
- `t0` slot 1개 (SpriteAtlas만)

#### Coupling
- VS에서 `View`/`Projection` matrix는 `Common.hlsli` 등에서 끌어와 사용 (frame constants).
- VS billboard 계산은 `View._11/.21/.31` (CameraRight) + `View._12/.22/.32` (CameraUp) 사용 — View matrix가 정확해야 정상 빌보드.
- `SpriteParticleCB(b8)`는 SubUV 메타데이터만, per-emitter 또는 per-cmd 단위 (현재 매 Cmd마다 update).
- ID pick: `PickPasses[] = {Opaque, Translucent, SubUV}` — Particle 미포함 (silent bug 함정 §7-3 회피)
- Outline: `ParticleSystemComponent::SupportsOutline() = false` → SelectionMask 패스 자동 제외

---

### 3.6 Use Case별 갭

#### Use Case (2) "코드/스크립트로 런타임 spawn·제어"

| 항목 | 상태 | 비고 |
|------|------|------|
| 객체 그래프 코드 생성 | ✅ 가능 | `UObjectManager::Get().CreateObject<T>()` / `AddEmitter()` / `Modules.push_back()` |
| Reference impl 존재 | ✅ 있음 | `UParticleSystem::CreateDefaultSpriteSystem()` ([ParticleSystem.cpp:183-215](JSEngine/Source/Engine/Particle/ParticleSystem.cpp:183)) |
| `SetTemplate(PS)` → 자동 init | ✅ 동작 | `RecreateEmitterInstances` → `Init` → `CacheEmitterModuleInfo` 자동 chain |
| Spawn rate 변경 즉시 반영 | ⚠️ 부분 | `SpawnModule->Rate` private + UPROPERTY → reflection setter 필요, 직접 접근 없음 |
| Color/Size 변경 즉시 반영 | ⚠️ 부분 | 동일 — private UPROPERTY (편집기/reflection 경로) |
| Texture 런타임 교체 | ❌ 불가 | RequiredModule에 texture 필드 없음 |
| SubUV columns/rows 변경 | ❌ 불가 | RequiredModule에 필드 없음 |
| Per-particle SubUVIndex | ❌ 0 고정 | TODO 주석 |

**갭의 핵심**: 객체 그래프 생성·Tick·instance 발생까지 path가 살아있다. **texture/SubUV 없는 단순 sprite**는 atlas nullptr 때문에 PS에서 discard → 화면에 안 보임 → debug 어려움. 코드에서 effect를 control하려면 최소한 atlas texture를 RequiredModule 또는 신규 SubUVModule에 추가하고 builder에서 Cmd로 흘려야 한다.

#### Use Case (3) ".uasset 로드" (실제 포맷: `.particlesystem` json)

| 항목 | 상태 | 비고 |
|------|------|------|
| 확장자/포맷 정의 | ✅ `.particlesystem` json | [ResourceManager.cpp:59-65](JSEngine/Source/Engine/Core/ResourceManager.cpp:59) |
| `LoadParticleSystem(Path)` 본문 | ✅ 완전 | [ResourceManager.cpp:1747-1855](JSEngine/Source/Engine/Core/ResourceManager.cpp:1747) — multi-object 그래프 reconstruction |
| `SaveParticleSystem(Asset, Path)` 본문 | ✅ 완전 | [ResourceManager.cpp:1857-1928](JSEngine/Source/Engine/Core/ResourceManager.cpp:1857) |
| Object polymorphism | ✅ 지원 | Class name → `FReflectionRegistry::FindClass` → `NewObject(Class)` |
| Multi-object 참조 | ✅ 지원 | `FParticleSystemObjectGraphResolver` + `Property->VisitReferences` |
| UPROPERTY transient 마킹 | ✅ 지원 | `Property->IsTransient()` 체크 ([ResourceManager.cpp:171](JSEngine/Source/Engine/Core/ResourceManager.cpp:171)) |
| `CacheEmitterModuleInfo` 자동 rebuild | ✅ | `RebuildParticleSystemCaches` ([ResourceManager.cpp:189-203](JSEngine/Source/Engine/Core/ResourceManager.cpp:189)) |
| Editor UI (개별 module 편집) | ✅ 골조 있음 | `FEditorParticleSystemWidget` — [EditorParticleSystemWidget.h:12-77](JSEngine/Source/Editor/UI/EditorParticleSystemWidget.h:12) |
| Preview viewport | ✅ 있음 | `FParticleSystemViewportClient` |
| Texture 직렬화 | ❌ 필드 자체 부재 | RequiredModule에 `UTexture*` 필드가 없으므로 직렬화할 게 없음 |
| SubUV columns/rows 직렬화 | ❌ 필드 부재 | 동일 |

**갭의 핵심**: 직렬화 인프라는 다 있다(`Object->Serialize(FArchive&)` + `SerializeProperties` UPROPERTY-기반 + multi-object resolver). 단지 **직렬화할 UPROPERTY 필드 자체가 RequiredModule에 없는 것**이 문제. 필드를 추가하면 별도 직렬화 코드 추가 없이 자동으로 json 직렬화 경로에 포함된다.

---

## 4. 통합 갭 매트릭스

| Hop | Has | Missing | Hardcoded | Coupling |
|-----|-----|---------|-----------|----------|
| **3.1 Asset** | UCLASS/UPROPERTY 트리, factory, reflection, json 직렬화, Editor widget | USubUVModule, RequiredModule의 Texture/Columns/Rows 필드, TypeDataBase | RenderMode=Sprite | 캐시 멤버 (`SpawnModule`/`SpawnModules`/`UpdateModules`) — `CacheModuleLists` 필요 |
| **3.2 Component** | `Template` UPROPERTY, `SetTemplate`/`RecreateEmitterInstances`/`Clear`, Init chain | (없음) | ParticleSize/Stride = sizeof(FBaseParticle) | Template 변경 시 즉시 동기 재할당 |
| **3.3 Tick** | TickComponent → Instance.Tick → Spawn/Update modules 전부 동작 | SubUV/Rotation/Force module | Rotation=0, RotationRate=0 | SpawnModule 캐시는 별도 슬롯 (bSpawnModule=false) |
| **3.4 Command 브릿지** | BuildSpriteInstanceData, GetEmitterInstanceData, FRenderCommand 신규 필드, EPT_ParticleSystem case + return true | (모듈 부재로 인한 파급) | **`ParticleTexture=nullptr`, `SubUVColumns/Rows=1`, `SubUVIndex=0`** | Cmd가 component 소유 메모리 직접 참조 |
| **3.5 GPU** | Pass enum, Pipeline 등록, EnsureGPUResources, DrawIndexedInstanced, VS/PS, Layout, Registry case | atlas nullptr 시 fallback texture | quad geometry, t0 slot 1개 | b8 (VS only) — PS는 미사용 |

---

## 5. "Control이 끊긴 지점" 우선순위 목록

다음 cycle 후보 산정의 근거. 정렬 기준: (a) 그 path가 막혀서 뒤쪽 hop이 통째로 검증 불가한가, (b) 변경 영역이 좁은가, (c) silent bug 함정과 충돌 없는가, (d) use case (2)+(3) 양쪽 공통으로 요구되는가.

| 순위 | 끊긴 path | 상류 hop | 변경 영역 추정 | use case 양쪽 공통? | 비고 |
|-----|----------|---------|--------------|-------------------|------|
| **A** | **Asset(RequiredModule)에 atlas Texture + SubUVColumns/Rows UPROPERTY 추가 → builder가 Cmd로 전달** | §3.1 + §3.4 | **좁음** (3 파일: ParticleModules.h/.cpp, PrimitiveDrawCommandBuilder.cpp) | **✅ 양쪽 필수** | 가장 상류. 이게 풀려야 atlas sample → discard 사이클을 끊고 화면 검증 가능. SubUV cell 매핑(셰이더)·instance pipeline은 이미 동작 중. |
| B | `BuildSpriteInstanceData::SubUVIndex = 0` → RelativeTime × FrameCount 기반 산출 | §3.3 + §3.4 | 좁음 (1 파일) | (2)만 직접 요구, (3)은 무방 | A에 의존 (Columns/Rows 있어야 의미). A 다음 cycle. |
| C | 별도 `USubUVModule` 클래스 추가 (RequiredModule 대신 분리) | §3.1 | 중간 (신규 .h/.cpp, vcxproj 위험) | 양쪽 가능하지만 over-engineering | A를 RequiredModule에 얹는 게 더 침습성 낮음. C는 후순위. |
| D | RotationRate / Rotation module | §3.1 + §3.3 | 좁음 | (2)에 더 유용 | A보다 검증 가치 낮음 (정적 sprite로도 visual). |
| E | Material 결합 (`Cmd.Material`) | §3.4 | 큼 (Material 시스템 연동) | 후순위 | Status.md §6에서도 "중간" |
| F | 거리순 정렬 | §3.4 | 중간 | 후순위 | alpha 정확성, 시각 차이 작음 |

**1순위 결정: A** — Asset의 RequiredModule에 `SubUVTexture: UTexture*` + `SubUVColumns: int32` + `SubUVRows: int32` UPROPERTY 추가하고, `PrimitiveDrawCommandBuilder`의 `EPT_ParticleSystem` case에서 Cmd로 흘림.

**선정 근거 (prompt §1 4규칙 매칭)**:
1. **상류 끊김**: 이 path가 막혀서 atlas sample이 nullptr → 검은 픽셀 → discard → 화면 빈 결과로 모든 검증이 막힘 ✅ 강하게 부합
2. **변경 영역 좁음**: 3 파일, 추정 30 라인 미만 ✅
3. **silent bug 함정 비충돌**: §7의 함정 7개 중 충돌 없음 (vcxproj 4번도 신규 파일 없으므로 회피) ✅
4. **양 use case 공통**: 코드 spawn에서도 texture 지정 필요, asset 로드에서도 직렬화 자동 포함 ✅

---

## 6. Inference / 가정 (사실과 분리)

> 이 섹션은 코드 직접 확인이 아닌 추론. 구현 전 사용자 확인 권장.

- **추론 1**: `RequiredModule`에 SubUV 필드를 추가하는 것이 별도 `USubUVModule`을 만드는 것보다 침습성이 낮다고 판단. 이유: (a) RequiredModule은 LODLevel당 1개만 존재하는 의무 module이라 SubUV 메타데이터를 hold할 자연스러운 위치이고, (b) UE Cascade에서도 SubUV는 RequiredModule의 "Sub UV" 카테고리 또는 별도 module로 표현되는데, 우리 코드베이스의 단순화 경향과 LODLevel 캐시 구조를 보면 RequiredModule 안에 두는 게 더 일관됨. 단, 정통 Cascade 호환성을 우선하면 별도 module이 맞을 수 있음.
- **추론 2**: `UTexture*` UPROPERTY 직렬화는 다른 곳에서 이미 패턴이 있을 것으로 추정. ResourceManager 그래프 직렬화에서 `Property->VisitReferences`가 nested UObject 참조를 따라가는 만큼 UTexture asset 자체는 별도 경로에서 로드되고 path 참조만 기록될 것. **이 부분은 구현 시 다른 component(예: `UBillboardComponent::Texture`)의 직렬화 패턴을 grep으로 확인 필요**.
- **추론 3**: PS 셰이더가 `SpriteAtlas.Sample(SpriteSampler, TexCoord) * Color`인 상태에서 SRV가 nullptr이면 일반적으로 D3D11이 zero 텍스처를 반환하지만 driver/일부 환경에선 undefined일 수도. 따라서 nullptr fallback (예: white 1×1 texture)을 추가하면 ParticleTexture 없는 경우에도 Color 단색이 보임 — 이는 A cycle의 sub-task 또는 별도 cycle로 다룰 수 있음.
- **추론 4**: EditorParticleSystemWidget이 RequiredModule에 신규 UPROPERTY 추가 시 자동으로 Details 패널에 나타날 가능성이 높음. 이유: widget이 reflection 기반으로 UPROPERTY를 순회한다면 추가만으로 노출됨. 단, widget 본문을 다 안 봤으므로 미확인.

---

## 7. 진단 중 발견한 silent bug 후보

선행 문서 §7의 7개 함정 외 신규 후보:

| 후보 | 위치 | 설명 |
|------|------|------|
| α | [ParticleSystemComponent.cpp:189-226](JSEngine/Source/Engine/Particle/ParticleSystemComponent.cpp:189) `BuildSpriteInstanceData` | builder가 매번 호출 → 같은 프레임에 multi-viewport(Editor 4-viewport)에서는 동일 component에 대해 N번 reconstruction. 현재는 결과가 idempotent라 functional bug 없음, 성능 함정. |
| β | [ParticleRenderPass.cpp:168-173](JSEngine/Source/Engine/Render/Renderer/RenderFlow/ParticleRenderPass.cpp:168) `PSSetShaderResources(0, &TextureSRV)` | `ParticleTexture` nullptr이면 `TextureSRV = nullptr`로 SRV 슬롯 t0 unbind. PS shader는 `SpriteAtlas.Sample(...)` 호출 → D3D 사양상 zero 반환 + warning. 알파 0이라 discard → **화면에 안 보임**. 다음 cycle에서 atlas 연결되면 자연 해소되나, **fallback white 1×1 텍스처**가 있으면 검증 편의성이 크게 높아짐. |
| γ | [ParticleModules.cpp:55-58](JSEngine/Source/Engine/Particle/ParticleModules.cpp:55) `UParticleModuleSpawn::UParticleModuleSpawn() { bSpawnModule = false; }` | 의도된 설계 (`SpawnModule`은 LODLevel.SpawnModule 별도 슬롯에 캐시되므로 SpawnModules 배열에는 들어가면 안 됨). 그러나 모르고 `bSpawnModule = true`로 바꾸면 Spawn이 2번 호출되어 invariant 깨짐. comment 추가 권장. |
| δ | [ParticleSystem.h:38](JSEngine/Source/Engine/Particle/ParticleSystem.h:38) `UParticleLODLevel::Modules` UPROPERTY (TArray) | Editor inspector에서 `Modules`에 신규 element 추가 시 어떤 UClass를 instantiate할지 선택 UI가 필요. EditorParticleSystemWidget이 `AddDefaultEmitter` / `DrawEmitterModuleRow` 등을 가지므로 이미 처리 가능성 있음 — 단 실측 안 함. |
| ε | [ParticleSystemComponent.h:38](JSEngine/Source/Engine/Particle/ParticleSystemComponent.h:38) `SupportsOutline() = false` | 의도된 설계 (SelectionMask 패스 제외) — 단, particle을 outline으로 보이게 하려면 `true`로 바꿔야 하고 그러면 `SelectionMaskRenderPass`에서 SkeletalMesh 같은 default Primitive layout으로 처리되어 잘못 그려질 위험. 함정 §7-3과 동일한 부류. |

---

## 8. 결론 (한 줄)

> Render infra와 simulation infra, 직렬화 infra 모두 **path가 살아 있다**. 막힌 곳은 **(asset의 RequiredModule에 atlas texture/SubUV grid 메타데이터 UPROPERTY가 부재)** → **(builder가 Cmd에 nullptr/1 고정)** → **(PS sample이 zero → discard → 화면 빈 결과)** 의 단일 path 1개. 다음 cycle은 이 path 1개를 잇는 데 집중해야 한다.
