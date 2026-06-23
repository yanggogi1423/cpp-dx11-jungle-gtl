# Cycle 13 (Beam Emitter) ReDiagnose Report

**작성일**: 2026-05-26
**대상 브랜치**: `feature/ParticleRender`
**모드**: diagnose (read-only — 코드 변경 0건)
**선행 문서**:
- [Cycle12_ImplementReport.md](Cycle12_ImplementReport.md) — Ribbon 구현 완료 결과
- [Cycle12_ReDiagnose.md](Cycle12_ReDiagnose.md) — Cycle 12 진입 진단
- [ParticleEmitter_InfraCheck.md](ParticleEmitter_InfraCheck.md) — Beam 사전 식별 (§3.3 / §6 추측 3·7)
- [Cycle11_ImplementPlan.md](Cycle11_ImplementPlan.md) — derived instance 패턴 참고

**진단 범위 (lock-in)**: (1) Cycle 10/11/12 가 Beam 관점에서 무사함을 확인 (§1, 간략화), (2) Beam wiring 현재 상태 점검 (§2), (3) Beam 고유 신규 영역 — TypeData / Source-Target 모듈 / Noise / payload / instance / render (§3, **집중**), (4) 사용자 결정 6건 (10/11/12/13/14/15) lock-in 후보 + Claude 의견 (§4, **집중**), (5) Beam 고유 silent bug 후보 6건 (위험 5-10) (§5).

**Source/Target 추상화 기준선**: 사용자 prompt 는 actor reference (UE Cascade 충실) 기준선. 그러나 본 진단 결과 코드베이스에 **`AActor*` UPROPERTY 패턴 0건** (REFLECTION_GUIDE.md §2.2 의 `ReferenceKind` 옵션은 `RuntimeObject` / `ActorComponent` / `Asset` 3종 — Actor enum 값 없음). 대안 권고는 §4 결정 10에 명시.

---

## §1 Cycle 10/11/12 회귀 점검 (Beam 관점 한정)

### 1.1 container Stride 자동 가산 (Cycle 10d) — [OK]

- **Init 의 PayloadBytes 분기**: [ParticleEmitterInstance.cpp:44-46](../JSEngine/Source/Engine/Particle/ParticleEmitterInstance.cpp:44) — `CurrentLODLevel->GetTypeDataModule()->RequiredPayloadBytes()` nullptr-safe.
- **Allocate 호출**: [ParticleEmitterInstance.cpp:53](../JSEngine/Source/Engine/Particle/ParticleEmitterInstance.cpp:53) — `ParticleStorage.Allocate(MaxActiveParticles, ParticleSize + PayloadBytes)` 그대로 (Cycle 10d 의 ξ 해소 정착).
- **Beam 영향**: `UBeamTypeData::RequiredPayloadBytes() = sizeof(FParticleBeamPayload)` 만 반환하면 동일 자동 가산 메커니즘 동작. Init / Allocate 변경 0건. Sprite (0B) → Mesh (36B) → Ribbon (32B) 에 이어 Beam 의 **세 번째 실측 검증** 예정.

### 1.2 derived instance 패턴 (Cycle 11/12) — [FACT]

| 항목 | Mesh ([ParticleMeshEmitterInstance.h:14-32](../JSEngine/Source/Engine/Particle/ParticleMeshEmitterInstance.h:14)) | Ribbon ([ParticleRibbonEmitterInstance.h:13-48](../JSEngine/Source/Engine/Particle/ParticleRibbonEmitterInstance.h:13)) |
| --- | --- | --- |
| `SpawnParticles` override | ✅ — payload init | ✅ — payload init + chain prepend + tangent init |
| `KillParticle` override | ❌ (base swap-pop 안전) | ✅ — chain 재연결 + head 갱신 |
| `Tick` override | ❌ (base 사용) | ✅ — chain 순회 + VertexBuffer rebuild |
| `BuildInstanceData` override | ✅ — Mesh InstanceDataBuffer 채움 | ❌ (slot 0 only — Builder가 직접 호출 안 함) |
| 추가 데이터 getter | `GetMeshInstanceData` | `GetRibbonVertexData` |
| helper 함수 | `GetMeshPayload(SlotIndex)` | `GetRibbonPayload(SlotIndex)` + `GetParticleBySlot` + `EnsureTrailState` + `BuildVertexBuffer` |

**Beam 사전 판단**: Beam derived 는 **Ribbon 쪽에 가까움**. 근거:
- Beam 의 strip 정점 생성 (Source→Target 보간) 은 Ribbon 의 strip 생성 (chain 순회) 와 동일 카테고리. slot 0 dynamic VB + indexless `Draw`.
- Spawn 시 Source/Target 캡처 + Tick 시 Source/Target 변동 추적 → SpawnParticles override + Tick override.
- multi-beam (결정 13) 채택 시 multi-trail 과 동일하게 chain 또는 BeamIndex 식별 — 패턴 답습.
- 단 Beam 은 **linked list 불필요** (beam 1개 = particle 1개 또는 interpolation point 수, chain 의존 없음). KillParticle override 의 필요성은 spawn 캡처 모드면 0 (base swap-pop 안전), Tick 추적 모드도 actor pointer 만 instance 보유면 0.

### 1.3 Ribbon 의 `EnsureTrailState()` lazy init 패턴 (Cycle 12 결정사항) — [FACT]

- **본문**: [ParticleRibbonEmitterInstance.cpp:61-77](../JSEngine/Source/Engine/Particle/ParticleRibbonEmitterInstance.cpp:61) — `HeadIndices.assign(MaxTrails, -1)` + `NextTrailIndex = 0`.
- **호출처**: `Tick()` 진입 시 [cpp:206](../JSEngine/Source/Engine/Particle/ParticleRibbonEmitterInstance.cpp:206) + `SpawnParticles()` 진입 시 [cpp:90](../JSEngine/Source/Engine/Particle/ParticleRibbonEmitterInstance.cpp:90).
- **회피 이유**: base `Init` 이 non-virtual ([ParticleEmitterInstance.h:24](../JSEngine/Source/Engine/Particle/ParticleEmitterInstance.h:24)) — override 불가. 따라서 derived 가 안전하게 LOD/TypeData 접근하려면 첫 Tick/Spawn 진입 시 lazy 초기화.

**Beam 영향**: 다음 조건 둘 다 만족 시 동일 lazy 패턴 필요 —
- (a) Beam 의 `BeamStates` 또는 `SourceActors` 가 frame 단위 안정성 필요 (Cycle 12 결정 8 — multi-trail 옵션 A 사례).
- (b) 본 구조의 size 가 TypeData 의 `MaxBeamCount` 에 의존.

결정 13 의 옵션 결정에 따라 결정:
- 결정 13 옵션 A (multi-beam) → EnsureBeamState 필요.
- 결정 13 옵션 B (single beam) → 불필요 (instance 멤버 자체가 0개).
- 결정 13 옵션 C (stub) → 불필요 (single 만 구현 → 별도 cycle 에서 추가).

### 1.4 RenderRibbonEmitter 의 topology 복원 패턴 (Cycle 12) — [FACT]

- **helper 시작 시 STRIP 세트**: [ParticleRenderPass.cpp:481](../JSEngine/Source/Engine/Render/Renderer/RenderFlow/ParticleRenderPass.cpp:481) — `IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP)`.
- **helper 끝에서 LIST 복원**: [ParticleRenderPass.cpp:487](../JSEngine/Source/Engine/Render/Renderer/RenderFlow/ParticleRenderPass.cpp:487) — `IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST)`.
- **이유**: `Begin()` 이 LIST 로 세팅 ([cpp:144](../JSEngine/Source/Engine/Render/Renderer/RenderFlow/ParticleRenderPass.cpp:144)). 다음 helper (Sprite/Mesh) 가 LIST 가정. helper 끝에서 복원하지 않으면 dispatch 순서에 따라 STRIP 로 그려져 silent rendering 결함.

**Beam 영향**: Beam 도 strip 사용 시 동일 책임. `RenderBeamEmitter` 도 helper 끝에서 LIST 복원 필수.

---

## §2 Beam wiring 현재 상태

### 2.1 `EParticleEmitterRenderMode::Beam` enum — [OK]

- **정의**: [ParticleTypes.h:16](../JSEngine/Source/Engine/Particle/ParticleTypes.h:16) — `Sprite, Mesh, Beam, Ribbon` 4 값 정의 완료.
- **Editor label**: [EditorParticleSystemWidget.cpp:551-552](../JSEngine/Source/Editor/UI/EditorParticleSystemWidget.cpp:551), [cpp:686-687](../JSEngine/Source/Editor/UI/EditorParticleSystemWidget.cpp:686), [cpp:2021](../JSEngine/Source/Editor/UI/EditorParticleSystemWidget.cpp:2021) — `"Beam"` / `"New Beam Data"` 라벨 노출 완료.
- **Cycle 13 추가 작업**: 0건.

### 2.2 `EVertexFactoryType::BeamParticle` switch case — [GAP]

- **enum**: [VertexFactoryTypes.h:38](../JSEngine/Source/Engine/Render/Resource/VertexFactoryTypes.h:38) — `BeamParticle` 정의 완료.
- **현재 switch 본문**: [VertexFactoryTypes.h:325-326](../JSEngine/Source/Engine/Render/Resource/VertexFactoryTypes.h:325)
  ```cpp
  case EVertexFactoryType::BeamParticle:
      return EmptyParticleDesc;
  ```
- **Cycle 13 작업**: `BeamParticleDesc` 추가 정의 + 위 case 본문을 `return BeamParticleDesc;` 로 교체. (Ribbon 의 Cycle 12 작업과 동일 패턴 — [VertexFactoryTypes.h:266-278](../JSEngine/Source/Engine/Render/Resource/VertexFactoryTypes.h:266) 참고.)

### 2.3 `PrimitiveDrawCommandBuilder` 의 Beam case — [OK / 부분]

- **본문 wired**: [PrimitiveDrawCommandBuilder.cpp:652-657](../JSEngine/Source/Engine/Render/Scene/PrimitiveDrawCommandBuilder.cpp:652)
  ```cpp
  case EParticleEmitterRenderMode::Beam:
      Cmd.BeamVertices = Instance->GetBeamVertexData(Count);
      Cmd.BeamVertexCount = Count;
      Cmd.VertexFactoryType = EVertexFactoryType::BeamParticle;
      bHasData = (Cmd.BeamVertices != nullptr && Count > 0);
      break;
  ```
- **GAP — Material 분기 누락**: [cpp:696-709](../JSEngine/Source/Engine/Render/Scene/PrimitiveDrawCommandBuilder.cpp:696) 의 ParticleTexture 추출 분기는 `Mesh || Ribbon` 만 — Beam 도 동일 분기에 포함 필요 (TypeData 의 GetMaterial 결과로 DiffuseMap 추출). Beam case 자체에 `Cmd.Material = BeamTD->GetMaterial()` 라인 추가 + `Mesh || Ribbon || Beam` 로 확장.
- **Cycle 13 작업**: Builder 의 Beam case 본문에 `Cmd.Material` 라인 1줄 추가 + Material/Texture 분기 조건 확장 1줄.

### 2.4 `FRenderCommand` 의 Beam 슬롯 — [OK]

- **선언**: [RenderCommand.h:504-505](../JSEngine/Source/Engine/Render/Scene/RenderCommand.h:504)
  ```cpp
  const FBeamParticleVertex* BeamVertices = nullptr;
  uint32 BeamVertexCount = 0;
  ```
- **forward declaration**: [RenderCommand.h:30](../JSEngine/Source/Engine/Render/Scene/RenderCommand.h:30) — `struct FBeamParticleVertex;` 완료.
- **sizeof baseline**: 본 cycle 종료 시 `static_assert(sizeof(FRenderCommand) == 464)` ([ParticleRenderPass.cpp:15](../JSEngine/Source/Engine/Render/Renderer/RenderFlow/ParticleRenderPass.cpp:15)) — Beam 슬롯이 이미 포함된 baseline 이므로 Cycle 13 의 `FBeamParticleVertex` 도입이 sizeof 영향 0.
- **Cycle 13 추가 작업**: 0건.

### 2.5 `ParticleRenderPass::RenderBeamEmitter` stub — [OK]

- **선언**: [ParticleRenderPass.h:27](../JSEngine/Source/Engine/Render/Renderer/RenderFlow/ParticleRenderPass.h:27).
- **body stub**: [ParticleRenderPass.cpp:493-498](../JSEngine/Source/Engine/Render/Renderer/RenderFlow/ParticleRenderPass.cpp:493) — `(void)Cmd; (void)Context;` NOP.
- **dispatch 측 wiring**: [ParticleRenderPass.cpp:189-191](../JSEngine/Source/Engine/Render/Renderer/RenderFlow/ParticleRenderPass.cpp:189) — switch case 호출 완료.
- **Cycle 13 작업**: body 채우기 (shader bind + state + slot 0 dynamic VB + indexless `Draw` + topology STRIP + LIST 복원). Ribbon 의 `RenderRibbonEmitter` 본문 ([cpp:410-488](../JSEngine/Source/Engine/Render/Renderer/RenderFlow/ParticleRenderPass.cpp:410)) 복제 + Beam 특화 (blend type — §3.6.4 참조).

### 2.6 base instance 의 `GetBeamVertexData()` 기본 getter — [OK]

- **선언**: [ParticleEmitterInstance.h:48](../JSEngine/Source/Engine/Particle/ParticleEmitterInstance.h:48) — `virtual const FBeamParticleVertex* GetBeamVertexData(uint32& OutCount) const`.
- **본문**: [ParticleEmitterInstance.cpp:377-381](../JSEngine/Source/Engine/Particle/ParticleEmitterInstance.cpp:377) — `OutCount = 0; return nullptr;` 기본값.
- **Cycle 13 작업**: 0건 (derived `FParticleBeamEmitterInstance` 가 override 만 추가).

### 2.7 `ShaderPaths.h` 의 `ParticleBeam` — [GAP]

- **현재 정의**: [ShaderPaths.h:34-36](../JSEngine/Source/Engine/Render/Resource/ShaderPaths.h:34) — `ParticleSprite`, `ParticleMesh`, `ParticleRibbon` 만 존재.
- **`ParticleBeam` 항목 누락**.
- **Cycle 13 작업**: 한 줄 추가:
  ```cpp
  inline constexpr const char* ParticleBeam = "Shaders/Particle/BeamParticle.hlsl";
  ```
  + `Shaders/Particle/BeamParticle.hlsl` 신규 작성 (VS `BeamParticleVS` + PS `BeamParticlePS`).

### 2.8 wiring 종합

| 항목 | 상태 | Cycle 13 추가 작업 |
| --- | --- | --- |
| `EParticleEmitterRenderMode::Beam` | ✅ | 0건 |
| Editor label / RenderMode 메뉴 | ✅ | 0건 |
| `EVertexFactoryType::BeamParticle` enum | ✅ | 0건 |
| `EVertexFactoryRegistry::Get` Beam case | ⚠️ stub | `BeamParticleDesc` 정의 + case 본문 교체 |
| `FRenderCommand::BeamVertices / BeamVertexCount` | ✅ | 0건 |
| `PrimitiveDrawCommandBuilder` Beam dispatch | ✅ 부분 | Material 라인 1줄 + Texture 분기 조건 확장 |
| `RenderBeamEmitter` 선언 + dispatch | ✅ stub | body 채우기 |
| base `GetBeamVertexData` | ✅ | 0건 (derived override 만) |
| `ShaderPaths.h::ParticleBeam` | ❌ | 1줄 추가 + `BeamParticle.hlsl` 신규 |

**비교**: Cycle 12 진입 시점 wiring 상태와 거의 동일 수준. **Beam wiring 은 거의 완성 — Cycle 13 의 작업 대부분은 신규 모듈 / 데이터 구조 / render body 채우기에 집중됨**.

---

## §3 Beam 고유 신규 영역 ⭐ (집중 영역)

### 3.1 `UParticleModuleTypeDataBeam` (TypeData)

**Mesh/Ribbon 와 동일 UCLASS 패턴** ([ParticleModuleTypeDataMesh.h:11-44](../JSEngine/Source/Engine/Particle/ParticleModuleTypeDataMesh.h:11), [ParticleModuleTypeDataRibbon.h:12-50](../JSEngine/Source/Engine/Particle/ParticleModuleTypeDataRibbon.h:12)) 적용 가능. 멤버 후보:

| 멤버 | 타입 | 출처 | 비고 |
| --- | --- | --- | --- |
| `BeamMethod` | `EBeam2Method` enum (`PEB2M_Distance` / `PEB2M_Target`) | UE Cascade | **결정 15** 에 따라 stub 또는 단일 값 |
| `MaxBeamCount` | `int32` | UE Cascade | **결정 13** 에 따라 도입 여부 |
| `InterpolationPoints` | `int32` | UE Cascade | beam 곡률점 수 (0 = 직선) |
| `Speed` | `float` | UE Cascade | 본 cycle 미적용 권고 (별도 cycle) |
| `Sheets` | `int32` | UE Cascade | strip 두께 분할 — 본 cycle 미적용 (1 고정) |
| `TextureTile / TextureTileDistance` | `float` | UE Cascade | UV 반복 (Ribbon 의 `Distance` 와 유사) |
| `Material` | `UMaterialInterface*` UPROPERTY (Asset) | Ribbon 패턴 ([ParticleModuleTypeDataRibbon.h:48](../JSEngine/Source/Engine/Particle/ParticleModuleTypeDataRibbon.h:48)) | **필수** — RenderPass 의 ParticleTexture 추출에 사용 |

**필수 override**:
- `RequiredPayloadBytes() → sizeof(FParticleBeamPayload)` ([ParticleModuleTypeDataMesh.h:17](../JSEngine/Source/Engine/Particle/ParticleModuleTypeDataMesh.h:17) 패턴)
- `GetRenderMode() → EParticleEmitterRenderMode::Beam`
- `CreateInstance(Component, EmitterIndex) → new FParticleBeamEmitterInstance()`

**MaxTrailCount 대응**: 결정 13 옵션 A 채택 시 Ribbon 의 `MaxTrailCount` ([ParticleModuleTypeDataRibbon.h:36-37](../JSEngine/Source/Engine/Particle/ParticleModuleTypeDataRibbon.h:36)) 와 같은 자리에 `MaxBeamCount` 추가. instance 의 `BeamStates[MaxBeamCount]` 또는 `HeadIndices` 와 같은 cap.

### 3.2 Source / Target 모듈

#### 3.2.1 UE Cascade `EBeam2SourceTargetMethod` enum

| 값 | 의미 | 본 엔진 적용 가능성 |
| --- | --- | --- |
| `PEB2STM_Default` | emitter 위치 | ✅ — `Component->GetWorldLocation()` 호출 ([ParticleEmitterInstance.cpp:269-270](../JSEngine/Source/Engine/Particle/ParticleEmitterInstance.cpp:269)) |
| `PEB2STM_UserSet` | 사용자 set FVector | ✅ — 단순 UPROPERTY FVector |
| `PEB2STM_Emitter` | 다른 emitter | ⚠️ — emitter index reference + per-frame lookup, 본 cycle 외 |
| `PEB2STM_Particle` | 다른 emitter 의 particle | ⚠️ — 동일 |
| `PEB2STM_Actor` | **actor reference** ⭐ | ❌ — **본 엔진 reflection 에 `Actor` 타입 reference 패턴 0건** (§3.2.3 참조) |

#### 3.2.2 Source/Target 모듈의 fallback

- **Source 미설정**: `Component->GetWorldLocation()` (emitter 위치).
- **Target 미설정**: `Source + Forward * Distance` (Distance 또는 SourceTangent 방향). `Forward` 는 `Component->GetForwardVector()` 가능 (단, 본 엔진 SceneComponent 에 `GetForwardVector` 가 있는지 확인 필요 — 본 진단 범위 외).

#### 3.2.3 본 엔진의 Actor reference 모델 — **[GAP]**

**REFLECTION_GUIDE.md §2.2** 의 `ReferenceKind` 옵션:
```txt
ReferenceKind = (RuntimeObject / ActorComponent / Asset)
```
→ **`Actor` 값 없음**.

**grep 결과**: `ReferenceType=Actor` / `ReferenceKind=Actor` / `UPROPERTY ... AActor*` 패턴 **0건**.

**대안 패턴 (코드 사실)**:
- `TObjectPtr<USceneComponent>` — [MovementComponent.h:62-63](../JSEngine/Source/Engine/Component/Movement/MovementComponent.h:62) (UpdatedComponent). UPROPERTY + NoEdit + Transient + LuaReadOnly.
- `TObjectPtr<T>` with `ReferenceKind` 명시 — REFLECTION_GUIDE.md §4 의 권장 패턴.

**Beam 적용 가능 패턴**:
- **옵션 A (component reference)**: `UPROPERTY TObjectPtr<USceneComponent> SourceComponent` → `SourceComponent->GetWorldLocation()`. UE Cascade 의 `PEB2STM_Actor` 와 비교해 1 단계 추가 (사용자가 actor 의 root component 를 picker 로 골라야 함).
- **옵션 B (단순 FVector)**: `UPROPERTY FVector SourceLocation` (UserSet 만). actor 추적 포기.
- **옵션 C (actor 직접)**: `UPROPERTY TObjectPtr<AActor> SourceActor` + `ReferenceKind = RuntimeObject`. **본 엔진에 검증 사례 0건** — picker UI 가 actor 를 표시할지 미확정.

**Claude 권고 (§4 결정 10)**: **옵션 A (component reference)**. 본 엔진의 검증된 `TObjectPtr<USceneComponent>` 패턴 사용. AActor 의 root scene component 를 picker 로 선택 → `GetWorldLocation()` 으로 위치 추적.

#### 3.2.4 Component world location 대안 — [OK]

- `Component->GetWorldLocation()` 는 base instance 의 `GetComponentWorldLocation()` ([ParticleEmitterInstance.cpp:267-273](../JSEngine/Source/Engine/Particle/ParticleEmitterInstance.cpp:267)) 에서 검증된 패턴.
- `USceneComponent*` 의 `GetWorldLocation()` 메서드는 기존 패턴 — 호출 비용 측정 본 진단 범위 외이나 component transform cache 시스템 의존.

#### 3.2.5 갱신 시점 비교

| 모드 | payload | instance 멤버 | Tick override 책임 | actor lifetime 위험 |
| --- | --- | --- | --- | --- |
| **Spawn 캡처** (정적 beam) | `FVector SourceLoc + TargetLoc` (~28B+) | (없음) | 0 — 매 frame 위치 변동 없음 | 0 |
| **Tick 추적** (actor lock-on) | `int32 BeamIndex` 만 (~4B) | `TObjectPtr<USceneComponent> SourceComp / TargetComp` (TypeData 측 또는 instance 측) | 매 frame `GetWorldLocation()` 2회 호출 + payload 갱신 (또는 매 frame strip 정점 재계산) | **위험 5 (dangling pointer)** |
| **하이브리드** | Spawn 시 캡처하되 Tick 마다 actor 가 valid 면 update | medium | medium | 분기 복잡 |

#### 3.2.6 Module 등록 — UCLASS reflection

- Mesh / Ribbon TypeData 와 동일 UCLASS + GENERATED_BODY 패턴 → `URibbonTypeData.gen.cpp` ([JSEngine.vcxproj:464](../JSEngine/JSEngine.vcxproj:464)) 등록 패턴 그대로 적용 가능.
- Cycle 12 의 빌드 진행 중 발견된 이슈 ([Cycle12_ImplementReport.md §7](Cycle12_ImplementReport.md)): `GenerateReflection.py` 실행 후 신규 `.gen.cpp` 를 vcxproj 에 수동 등록 필수. Beam 도 동일 절차.

### 3.3 `UParticleModuleBeamNoise` (Noise)

#### 3.3.1 UE Cascade 의 Noise 데이터 모델

| 멤버 | 타입 | 의미 |
| --- | --- | --- |
| `Frequency` | `int32` | noise sample 수 (interpolation point 마다 perturbation) |
| `NoiseRange` | `FVector` | XYZ 노이즈 범위 (각 축의 amplitude) |
| `NoiseSpeed` | `float` | noise 갱신 속도 |
| `bSmooth` | `bool` | smooth interpolation |
| `bNoiseLock` | `bool` | frame 간 noise 고정 (per-particle 영구) |
| `bTargetNoise` | `bool` | target 쪽도 noise 적용 |

#### 3.3.2 per-frame vs per-particle 영구

| 모드 | payload 영향 | 시각 효과 | 결정성 |
| --- | --- | --- | --- |
| **per-frame** | 0 (매 frame `RandomFloat`) | 떨림 (전기 효과) | 비결정적 — frame rate / seed 분기 |
| **per-particle 영구 (NoiseLock)** | `FVector NoiseSamples[Frequency]` (예: Frequency=8 → 96B) → payload 124B 4x 증가 | 정적 (lightning bolt) | 결정적 — spawn 시점 고정 |

#### 3.3.3 본 엔진의 random source — [FACT]

- `FEngineRandom` singleton: [EngineRandom.h:8-24](../JSEngine/Source/Engine/Core/Random/EngineRandom.h:8) — `std::mt19937` Generator + `SetSeed` + `RandomFloat01/RandomFloat/RandomInt/RandomBool`.
- 사용 사례: [ParticleModules.cpp:22-24](../JSEngine/Source/Engine/Particle/ParticleModules.cpp:22) `RandomRange(Min, Max)` (Lifetime / Location / Velocity / Color / Size 모두 사용).
- **결정성 보장**: `SetSeed(uint32)` 가능 → per-particle NoiseLock 시 seed 를 ParticleId 기반으로 도출하면 결정적 noise 가능.

#### 3.3.4 LineBatcher 와의 통합

- `LineBatcher::CreateDynamicBuffer` ([LineBatcher.cpp:145-160](../JSEngine/Source/Engine/Render/LineBatcher.cpp:145)) 가 dynamic VB 생성 패턴 — Noise 시각 검증 (각 sample 점 line draw) 가능.
- **권고**: Noise 디버그 시각화는 Cycle 12 의 디버그 플래그 (결정 9 옵션 B) 와 동일 — **별도 cycle (13c) 로 분리**. (결정 12 옵션 B 권고 일관성.)

### 3.4 `FParticleBeamPayload` (payload)

#### 3.4.1 Spawn 캡처 모드 payload (결정 11 옵션 A 채택 시)

| 멤버 | 크기 | offset |
| --- | --- | --- |
| `FVector SourceLocation` | 12 | 0 |
| `FVector TargetLocation` | 12 | 12 |
| `int32 BeamIndex` (결정 13 옵션 A) | 4 | 24 |
| `float Padding` (16B align) | 4 | 28 |
| **subtotal** | **32B** | — |
| `FVector NoiseSamples[N]` (결정 12 옵션 A — Noise 포함) | 12 × N | 32 |
| **N=8 시 total** | **128B (align)** | — |

**Noise 제외 시**: 32B (Mesh 36B / Ribbon 32B 와 동일 수준).
**Noise 포함 시**: 128B (Mesh/Ribbon 4x).

#### 3.4.2 Tick 추적 모드 payload (결정 11 옵션 B 채택 시)

| 멤버 | 크기 |
| --- | --- |
| `int32 BeamIndex` | 4 |
| `float Padding` | 4 |
| **subtotal (Noise 제외)** | **8B → align 16B** |
| `FVector NoiseSamples[N]` | 12 × N |
| **N=8 시 total** | **112B (align)** |

→ instance 측 `TObjectPtr<USceneComponent> SourceComp/TargetComp` 가 actor 위치 보유. payload 의 SourceLocation/TargetLocation 멤버 불필요.

#### 3.4.3 결정: Spawn 캡처 vs Tick 추적 (결정 11)

**Claude 권고 (§4 결정 11)**: **옵션 A (Spawn 캡처)** — payload 가 ~32B 로 Mesh/Ribbon 수준 유지. Tick override 단순. actor lifetime 위험 0.

#### 3.4.4 `static_assert(sizeof == ?)`

Mesh 의 36B ([ParticleMeshTypes.h:22](../JSEngine/Source/Engine/Particle/ParticleMeshTypes.h:22)) / Ribbon 의 32B ([ParticleRibbonTypes.h:24-25](../JSEngine/Source/Engine/Particle/ParticleRibbonTypes.h:24)) 패턴 따라 컴파일 타임 검증 필수.

### 3.5 `FParticleBeamEmitterInstance`

#### 3.5.1 override 함수 후보

| 함수 | Spawn 캡처 모드 (결정 11 A) | Tick 추적 모드 (결정 11 B) | 비고 |
| --- | --- | --- | --- |
| `SpawnParticles` | ✅ — base 호출 후 SourceLoc/TargetLoc 캡처 | ✅ — BeamIndex 만 초기화 | Mesh 패턴 ([ParticleMeshEmitterInstance.cpp:25-44](../JSEngine/Source/Engine/Particle/ParticleMeshEmitterInstance.cpp:25)) |
| `Tick` | ❌ (base 사용 — 위치 변동 없음) | ✅ — actor 위치 추적 + payload 갱신 또는 VertexBuffer 직접 재계산 | Ribbon 의 BuildVertexBuffer 시점 패턴 |
| `KillParticle` | ❌ (chain 없음 — base swap-pop 안전) | ❌ (동일) | Mesh 와 동일 |
| `BuildInstanceData` | ❌ (slot 0 only — Builder 가 호출 안 함) | ❌ | Ribbon 패턴 |
| `GetBeamVertexData` | ✅ — VertexBuffer 노출 | ✅ | Ribbon 의 GetRibbonVertexData 와 1:1 대응 |

#### 3.5.2 멤버 후보

```cpp
struct FParticleBeamEmitterInstance : public FParticleEmitterInstance
{
    // Spawn 캡처 모드 (결정 11 옵션 A 채택 시):
    // 모듈 hook 으로 SourceLoc/TargetLoc 을 payload 에 캡처. instance 멤버는 VertexBuffer 만.

    // 결정 13 옵션 A (multi-beam) 채택 시:
    TArray<int32> BeamStates;       // size = MaxBeamCount (구체 의미는 별도 결정)
    int32 NextBeamIndex = 0;        // round-robin 분배

    // Ribbon 의 VertexBuffer 패턴 그대로:
    TArray<FBeamParticleVertex> VertexBuffer;
};
```

#### 3.5.3 lazy init 필요성

- 결정 13 옵션 A (multi-beam) → Ribbon 의 `EnsureTrailState` 와 동일 `EnsureBeamState` 패턴 필요.
- 결정 13 옵션 B (single beam) → 불필요.
- 결정 13 옵션 C (stub) → 불필요 (single 구현 → 후속 cycle 에서 EnsureBeamState 도입).

### 3.6 Render 어댑터

#### 3.6.1 `FBeamParticleVertex` 구조

Ribbon 의 `FRibbonParticleVertex` ([ParticleRibbonTypes.h:30-40](../JSEngine/Source/Engine/Particle/ParticleRibbonTypes.h:30), 48B, 5 입력) **동일 layout 채택 권고**:

| 멤버 | 크기 | offset | 의미 |
| --- | --- | --- | --- |
| `FVector Position` | 12 | 0 | strip 정점 world position |
| `FVector Tangent` | 12 | 12 | beam 방향 (Source→Target normalize) |
| `FColor Color` | 16 | 24 | per-vertex color |
| `float TexCoordU` | 4 | 40 | UV 좌표 (Distance / TextureTile) |
| `float Size` | 4 | 44 | strip 폭 |
| **total** | **48B** | — | — |

→ **재사용 가능**: `FRibbonParticleVertex` 그대로 alias 또는 `using FBeamParticleVertex = FRibbonParticleVertex;` (단 RenderCommand forward declaration 충돌 주의).

#### 3.6.2 Topology — STRIP + degenerate seam

Ribbon 패턴 그대로 ([ParticleRibbonEmitterInstance.cpp:301-306](../JSEngine/Source/Engine/Particle/ParticleRibbonEmitterInstance.cpp:301)). Beam 1개 = strip 1개. multi-beam (결정 13 옵션 A) 시 degenerate vertex 1개로 seam.

#### 3.6.3 Beam-specific shader

- Ribbon 의 unlit (`Sample * Color`) — Ribbon 의 `RibbonParticle.hlsl` 패턴 그대로 적용 가능.
- 또는 Beam glow (Color.rgb 가산) — additive blend 와 짝.

#### 3.6.4 D3D state — [GAP]

| 옵션 | BlendType 검증 | 비고 |
| --- | --- | --- |
| **AlphaBlend** (Ribbon 과 동일) | ✅ — [RenderResources.h:69](../JSEngine/Source/Engine/Render/Resource/RenderResources.h:69) 정의됨 | 가장 단순 — Ribbon 의 state 세팅 그대로 복제 |
| **Additive** (UE Cascade beam 통상) | ❌ — **`EBlendType` 에 `Additive` 값 없음** ([RenderResources.h:66-71](../JSEngine/Source/Engine/Render/Resource/RenderResources.h:66): `Opaque`, `AlphaBlend`, `NoColor` 만) | enum 값 추가 + `GetOrCreateBlendState` 분기 추가 필요 — **본 cycle 범위 외** |

**Claude 권고**: **AlphaBlend** + DepthReadOnly + SolidNoCull (Ribbon 의 [ParticleRenderPass.cpp:428-434](../JSEngine/Source/Engine/Render/Renderer/RenderFlow/ParticleRenderPass.cpp:428) 세팅 복제). Additive 도입은 Material 측 BlendType 시스템 별도 cycle 로 분리.

#### 3.6.5 `RenderBeamEmitter` body

Cycle 12 의 `RenderRibbonEmitter` ([ParticleRenderPass.cpp:410-488](../JSEngine/Source/Engine/Render/Renderer/RenderFlow/ParticleRenderPass.cpp:410)) 와 **거의 동일** —
- slot 0 only (`IASetVertexBuffers(0, 1, VBs, ...)`)
- topology STRIP + 끝에서 LIST 복원
- indexless `Draw(VertexCount, 0)`
- BlendAlpha + DepthReadOnly + SolidNoCull
- shader: `GetBeamParticleProgram()` (Ribbon 의 [cpp:65-81](../JSEngine/Source/Engine/Render/Renderer/RenderFlow/ParticleRenderPass.cpp:65) 패턴 복제 + ParticleRibbon → ParticleBeam 교체)

추가 멤버: `FInstanceBuffer BeamVertexBuffer` ([ParticleRenderPass.h:40-41](../JSEngine/Source/Engine/Render/Renderer/RenderFlow/ParticleRenderPass.h:40) RibbonVertexBuffer 패턴).

---

## §4 사용자 결정 필요 항목 ⭐ (집중 영역, Claude 의견 첨부)

### 결정 10 — Source/Target 추상화 수준

| 옵션 | 내용 | 영향 |
| --- | --- | --- |
| **A** | `TObjectPtr<USceneComponent> SourceComponent / TargetComponent` (검증된 패턴) | Movement Component 의 `UpdatedComponent` 패턴 ([MovementComponent.h:62-63](../JSEngine/Source/Engine/Component/Movement/MovementComponent.h:62)) 답습. Picker UI 자동 (component picker 검증됨). `GetWorldLocation()` 호출로 위치 추적. |
| **B** | `FVector SourceLocation / TargetLocation` (UserSet 만) | 단순 — picker 불필요. 그러나 actor 추적 불가 (정적 lightning bolt 등 한정). |
| **C** | `TObjectPtr<AActor> SourceActor / TargetActor` + `ReferenceKind = RuntimeObject` | UE Cascade `PEB2STM_Actor` 와 가장 유사. **본 엔진 검증 사례 0건** — picker UI 동작 미확정. silent regression 위험. |
| **D** | enum 기반 polymorphism (`EBeam2SourceTargetMethod` 5종 전부) | 가장 호환적이나 코드 분기 복잡도 5x. critical path 길어짐. |

**Claude 권고**: **옵션 A (Component reference)**. 근거:
1. (§3.2.3) `TObjectPtr<USceneComponent>` 패턴 검증 완료 ([MovementComponent.h:62](../JSEngine/Source/Engine/Component/Movement/MovementComponent.h:62)). picker UI / 직렬화 자동.
2. AActor 의 root scene component 를 picker 로 선택 → `GetWorldLocation()` 호출로 actor 위치 추적 가능 (한 단계 추가만).
3. 옵션 C 의 `TObjectPtr<AActor>` 는 본 엔진 reflection 에 검증 0건 — silent UI/직렬화 버그 위험.
4. UE Cascade 의 5종 method 중 `PEB2STM_Default` (emitter 위치) 와 `PEB2STM_UserSet` 은 `SourceComponent = nullptr` 시 fallback 으로 자연 표현 가능. `PEB2STM_Emitter / Particle` 은 본 cycle 외.
5. 옵션 B 는 옵션 A 의 specialization (Component nullptr 시 FVector fallback) 으로 자연 표현 가능 — 옵션 A 채택해도 옵션 B 의 use case 흡수.

### 결정 11 — Source/Target 갱신 시점

| 옵션 | 내용 | 영향 |
| --- | --- | --- |
| **A** | Spawn 시 1회 캡처 (정적 beam) — payload 에 `FVector SourceLoc/TargetLoc` 저장 | payload 28B+ (Noise 제외). Tick override 단순. actor lifetime 위험 0. |
| **B** | Tick 매 frame 추적 — instance 에 SourceComp pointer 만, 매 frame `GetWorldLocation()` 호출 | payload 8B+. **위험 5 (dangling pointer)**. 매 frame component lookup 비용. |
| **C** | 하이브리드 — Component 가 set 되면 Tick 추적, 아니면 Spawn 캡처 | 분기 복잡 — silent bug 표면적 증가. |

**Claude 권고**: **옵션 A (Spawn 캡처)**. 근거:
1. payload ~32B 가 Mesh (36B) / Ribbon (32B) 와 동일 수준 — container Stride 자동 가산 패턴 (Cycle 10d) 의 **세 번째 실측 검증** 자연스러움.
2. Tick override 단순 — Mesh 와 동일 수준 (override 0). Ribbon 의 chain 순회 책임 없음 — critical path 짧음.
3. 위험 5 (dangling pointer) 자동 회피 — actor destroy 와 무관.
4. 동적 actor 추적 (옵션 B) 는 별도 cycle 로 분리 가능 — TypeData 에 `bTrackSourceTarget` 플래그 추가하고 Tick override 본문 분기 추가하는 식.
5. Cycle 12 의 단일 issue 원칙 (결정 9 옵션 B 채택 일관성 — 디버깅 표면적 최소).

### 결정 12 — Noise 포함 여부

| 옵션 | 내용 | 영향 |
| --- | --- | --- |
| **A** | Cycle 13 에 Noise 포함 (Beam 완성) | payload 128B (4x 증가). NoiseModule + per-sample 보간 로직 + LineBatcher 디버그 통합. critical path 길어짐. silent bug 후보 위험 6 (determinism) 도입. |
| **B** | Cycle 13 에서 Noise 제외, **별도 cycle (13c) 분리** | payload ~32B 유지. NoiseModule UCLASS 0건. Cycle 13 critical path 짧음. |
| **C** | Cycle 13 에서 Noise stub 만 (TypeData 멤버 정의, 본문 NOP) + Cycle 13c 에서 본문 | TypeData 멤버 추가만. payload 영향 0. UI 노출은 본 cycle 부터. |

**Claude 권고**: **옵션 B (별도 cycle)**. 근거:
1. Cycle 12 의 결정 9 옵션 B (디버그 플래그 별도 cycle) 채택 일관성 — 동일 단일 issue 원칙.
2. Noise payload 4x 증가 (32B → 128B) 가 결정 11 옵션 A (Spawn 캡처) 의 단순함을 훼손.
3. Noise random 결정성 (frame-rate independence) 은 위험 6 으로 식별됨 — 본 cycle 의 silent bug 표면적 추가.
4. LineBatcher 와의 통합 (각 sample 점 line draw) 은 Cycle 12 의 디버그 시각화 분리와 동일 후속 작업.
5. Beam emitter "두 점 사이 strip" 기본 동작 검증 후 noise 추가가 디버깅 효율적.

### 결정 13 — Multi-beam 지원 (multi-trail 대응)

| 옵션 | 내용 | 영향 |
| --- | --- | --- |
| **A** | `MaxBeamCount` TypeData 멤버 + `BeamIndex` payload + instance 의 `BeamStates[MaxBeamCount]` | Ribbon 의 결정 8 옵션 A 답습. EnsureBeamState lazy init 필요. payload 4B 추가. |
| **B** | emitter 당 single beam 만 | payload 4B 절약. instance 멤버 0개. multi-beam 필요 시 emitter 여러 개. |
| **C** | stub (`MaxBeamCount` 멤버 정의, instance 본문은 single 만) | TypeData 멤버 추가만. 본문은 옵션 B. |

**Claude 권고**: **옵션 A (multi-beam)**. 근거:
1. Cycle 12 의 결정 8 옵션 A (multi-trail) 채택 일관성 — 동일 패턴.
2. Ribbon 의 `MaxTrailCount` / `HeadIndices` / `NextTrailIndex` 패턴 ([ParticleRibbonEmitterInstance.h:41-44](../JSEngine/Source/Engine/Particle/ParticleRibbonEmitterInstance.h:41)) 그대로 적용 가능.
3. `BeamIndex` payload 4B 는 Mesh/Ribbon 의 payload 와 동등 — Stride 영향 미미.
4. Cycle 12 의 multi-trail 빌드 검증 통과 ([Cycle12_ImplementReport.md §7](Cycle12_ImplementReport.md)) — 패턴 risk 0.

단, 옵션 B/C 도 합리적 — Beam 은 Ribbon 보다 multi-instance 의미가 약함 (multi-trail 은 motion trail 자연, multi-beam 은 동시 multiple lightning bolt 등 한정).

### 결정 14 — Cycle 13a / 13b 분할 여부

| 옵션 | 내용 | 영향 |
| --- | --- | --- |
| **A** | Cycle 13 통합 (Cycle 12 처럼 한 번에) | Mesh/Ribbon 패턴 정착됨. wiring 거의 완성 (§2). critical path: TypeData + Source/Target 모듈 + Instance + Vertex + Render body. |
| **B** | Cycle 13a (payload + 모듈 + Spawn/Tick) + Cycle 13b (dynamic VB + render) — 원래 plan | sub-cycle 별 verify 가능. 그러나 13a 종료 시점에 화면 표시 0건 — verify 의미 약함. |
| **C** | Cycle 13a (payload + 모듈 + Spawn/Tick + Render stub) + Cycle 13b (Noise + 디버그 시각화) | 결정 12 옵션 B 와 통합 — 13a 가 본 cycle, 13b 가 Noise/디버그. |

**Claude 권고**: **옵션 A (통합) — 단, 결정 12 옵션 B (Noise 제외) 와 결합**. 근거:
1. Cycle 12 의 Ribbon 통합 성공 사례 ([Cycle12_ImplementReport.md §0](Cycle12_ImplementReport.md)) — 11 파일 변경 (신규 6 + 수정 5 + vcxproj), 빌드 통과. Beam 도 동일 규모 예상.
2. Beam wiring 의 §2 결과: stub 완성 — Cycle 13 작업 대부분이 신규 모듈 본문 + render body.
3. silent bug 후보 §5 의 위험 5-10 중 위험 6 (Noise determinism) 은 결정 12 옵션 B 채택 시 본 cycle 외 — 위험 5/7/8/9/10 만 본 cycle 방어 (5건, Cycle 12 의 위험 1-4 와 동등 수준).
4. 옵션 B 의 13a-only 종료는 화면 표시 0건 → verify 어려움.

### 결정 15 — BeamMethod 첫 도입 범위

| 옵션 | 내용 | 영향 |
| --- | --- | --- |
| **A** | `PEB2M_Distance` 만 (Source + Forward × Distance) — Target 모듈 불필요 | 가장 단순. Source 모듈만 도입. 직선 beam 만. |
| **B** | `PEB2M_Target` 만 (Source ↔ Target) — Source/Target 모듈 완전 활용 | "beam = 두 점 사이 strip" 의 직관성. Source + Target 모듈 도입. UE Cascade 의 main use case. |
| **C** | 둘 다 (TypeData 의 `BeamMethod` enum 으로 switch) | 코드 분기 2x. UI 노출 필요. |
| **D** | UE 5종 전부 | 분기 5x — 본 cycle 외. |

**Claude 권고**: **옵션 B (`PEB2M_Target` 만)**. 근거:
1. Source/Target 모듈 둘 다 도입하면 결정 10 옵션 A (Component reference) 의 일관 적용 — Source/Target 둘 다 `TObjectPtr<USceneComponent>`.
2. 직관성 — "beam = Source 에서 Target 까지 strip" 이 사용자가 이해하기 가장 자연.
3. Target nullptr fallback: `Source + Forward * Distance` (옵션 A 자동 흡수). TypeData 에 `FallbackDistance` 멤버 1개 추가하면 옵션 A 도 동시 표현 가능.
4. BeamMethod enum 분기 (옵션 C) 는 사용자 lock-in 후 별도 cycle 로 확장 자연.

---

## §5 silent bug 후보 (Beam 고유)

Cycle 12 의 위험 1-4 답습 형식. 결정 11 옵션 A (Spawn 캡처) + 결정 12 옵션 B (Noise 제외) 가정.

### 위험 5 (actor lifetime / dangling pointer) — [영향: 결정 11 옵션 A 채택 시 0]

- **시나리오**: Tick 추적 모드 (결정 11 옵션 B) 시 `SourceComponent` / `TargetComponent` pointer 가 dangling — actor destroy 후 `GetWorldLocation()` 호출 시 segfault.
- **방어 코드 위치 후보**:
  - `SpawnParticles` 시작: `if (!SourceComponent->IsValid()) return;` (component 의 valid 검사 — IsValid 메서드 존재 여부 본 진단 범위 외).
  - `Tick` 의 각 frame 진입: 동일 검사.
- **Spawn 캡처 모드 (결정 11 옵션 A) 채택 시**: payload 의 FVector 만 보유 → component pointer 없음 → 위험 0.

### 위험 6 (Noise determinism) — [영향: 결정 12 옵션 B 채택 시 0]

- **시나리오**: per-frame noise 가 frame-rate 의존이면 다른 머신에서 다른 결과 (network sync / replay 깨짐).
- **방어 코드 위치 후보**:
  - `FEngineRandom::SetSeed(ParticleId)` 호출 + spawn 시점에 모든 noise sample 캡처 (per-particle 영구).
  - 또는 시간 누적 (`AccumulatedTime`) 기반 deterministic noise 함수 (Perlin 등).
- **Noise 제외 (결정 12 옵션 B) 채택 시**: 본 cycle 외.

### 위험 7 (zero-length beam) — [방어 필수]

- **시나리오**: Source == Target 또는 Distance == 0 시 `(Target - Source).Normalize()` 가 0/0 → NaN tangent → strip 정점에 NaN → GPU 가 invalid geometry 그리지 않거나 검은 patches.
- **방어 코드 위치 후보**:
  - `BuildVertexBuffer` (또는 strip 정점 생성 시점): `if ((Target - Source).SizeSquared() < epsilon) continue;` — Ribbon 의 `RibbonSmallNumber` 패턴 ([ParticleRibbonEmitterInstance.cpp:11](../JSEngine/Source/Engine/Particle/ParticleRibbonEmitterInstance.cpp:11)) 답습.

### 위험 8 (interp point overflow) — [방어 필수]

- **시나리오**: `InterpolationPoints` 값이 음수 또는 과도 (예: 10000) → VertexBuffer 폭증 + GPU memory 압박 + per-frame allocation cost.
- **방어 코드 위치 후보**:
  - `BuildVertexBuffer` 시작: `const int32 InterpCount = std::clamp(InterpolationPoints, 0, 64);` (또는 TypeData UPROPERTY 의 Min/Max 적용 — UPROPERTY 매크로의 `Min=0, Max=64` 옵션 사용 가능 [REFLECTION_GUIDE.md §2.2](../REFLECTION_GUIDE.md)).

### 위험 9 (MaxBeamCount 초과 spawn) — [방어 필수, 결정 13 옵션 A 채택 시]

- **시나리오**: multi-beam 모드에서 spawn 이 `MaxBeamCount` 초과 시 BeamStates 배열 overflow.
- **방어 코드 위치 후보**:
  - `SpawnParticles` 의 base 호출은 자동 안전 (`ActiveParticles < MaxActiveParticles` 검사 [ParticleEmitterInstance.cpp:178](../JSEngine/Source/Engine/Particle/ParticleEmitterInstance.cpp:178)) — payload 단위.
  - 그러나 BeamIndex 분배 시 `NextBeamIndex = (NextBeamIndex + 1) % MaxBeamCount` 명시 (Ribbon 의 `NextTrailIndex` 패턴 [ParticleRibbonEmitterInstance.cpp:116](../JSEngine/Source/Engine/Particle/ParticleRibbonEmitterInstance.cpp:116)).

### 위험 10 (Source 갱신 race) — [영향: 결정 11 옵션 A 채택 시 0]

- **시나리오**: Spawn 캡처 모드에서 Spawn 시점과 actor location 갱신 시점 사이 race. 다른 thread 가 actor 를 동시에 이동 중이면 부정확한 SourceLocation 캡처.
- **방어 코드 위치 후보**:
  - 본 엔진의 component tick / spawn 호출이 단일 thread (main game thread) 인지 본 진단 범위 외 — 현재 코드베이스가 멀티스레드 actor tick 지원하지 않으면 위험 0.
  - Spawn 시점에 즉시 `Component->GetWorldLocation()` 호출 — 동일 thread 내 sequential 보장.

### 위험 종합

| 위험 | 본 cycle 영향 | 결정 의존성 | 방어 위치 |
| --- | --- | --- | --- |
| 5 (dangling pointer) | 0 | 결정 11 A | — |
| 6 (Noise determinism) | 0 | 결정 12 B | — |
| 7 (zero-length beam) | **있음** | — | `BuildVertexBuffer` (epsilon 검사) |
| 8 (interp overflow) | **있음** | — | `BuildVertexBuffer` (clamp) |
| 9 (MaxBeamCount 초과) | **있음** | 결정 13 A | `SpawnParticles` (round-robin) |
| 10 (Source 갱신 race) | 0 | 단일 thread 가정 | — |

**Claude 권고 결정 (11/12) 조합 채택 시**: 본 cycle 의 silent bug 방어 코드 위치 **3건** (위험 7/8/9). Cycle 12 의 위험 1-4 (4건) 와 비슷한 표면적.

---

## §6 결론 한 줄

> **Cycle 13 진입 가능 — 단 사용자 결정 6건 (10/11/12/13/14/15) lock-in 후 plan 작성**. Beam wiring 은 Cycle 10a 시점에 대부분 완성 (enum / RenderCommand 슬롯 / Builder dispatch / RenderPass stub / base GetBeamVertexData) — Cycle 13 의 critical path 는 **신규 모듈 (TypeData + Source + Target) + 신규 instance / payload / vertex + Render body** 5 영역에 집중. wiring 잔여 작업은 `BeamParticleDesc` 본문 (`EmptyParticleDesc` 교체) + `ShaderPaths::ParticleBeam` 1줄 + Builder Material 라인 1줄 (3건). **Source/Target 추상화는 actor reference (UE Cascade 충실) 대신 `TObjectPtr<USceneComponent>` 권고** — 본 엔진 reflection 에 `AActor*` UPROPERTY 패턴 0건, `ReferenceKind` enum 에 `Actor` 값 없음 (REFLECTION_GUIDE.md §2.2 확인). 신규 silent bug 후보 6건 (위험 5-10) 중 결정 11 옵션 A (Spawn 캡처) + 결정 12 옵션 B (Noise 제외) 채택 시 본 cycle 영향 3건 (위험 7/8/9) — Cycle 12 의 위험 1-4 와 동등 수준. Mesh (144B) / Ribbon (144B) 에 이어 Beam 의 container 자동 가산 패턴 **세 번째 실측 검증** 예정.

**다음 단계**: 사용자가 결정 10-15 검토 → lock-in → `Cycle13_ImplementPlan.md` 작성 진입.
