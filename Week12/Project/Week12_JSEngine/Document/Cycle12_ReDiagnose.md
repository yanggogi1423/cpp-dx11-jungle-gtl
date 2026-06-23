# Cycle 12 (Ribbon Emitter) ReDiagnose Report

**작성일**: 2026-05-26
**대상 브랜치**: `feature/ParticleRender`
**모드**: diagnose (read-only — 코드 변경 0건)
**선행 문서**:
- [Cycle11_ImplementPlan.md](Cycle11_ImplementPlan.md) — Ribbon outline §B
- [Cycle11_ReDiagnose.md](Cycle11_ReDiagnose.md) — Cycle 11 진입 진단

**진단 범위**: (1) Cycle 10d/11 가정의 코드 정착 상태, (2) Ribbon 고유 구조 (linked list, dynamic VB, multi-trail) 의 현재 codebase 지원 여부, (3) 사용자 결정 분기점 4건 (결정 6/7/8/9) 식별 + Claude 의견.

---

## §1 Cycle 10d / 11 회귀 점검

### 1.1 container payload-aware Stride 검증 — [FACT]

- **Init 의 PayloadBytes 산출**: [ParticleEmitterInstance.cpp:44-46](JSEngine/Source/Engine/Particle/ParticleEmitterInstance.cpp:44) — `CurrentLODLevel->GetTypeDataModule()->RequiredPayloadBytes()` 가 nullptr-safe 로 호출.
- **Allocate 호출**: [ParticleEmitterInstance.cpp:53](JSEngine/Source/Engine/Particle/ParticleEmitterInstance.cpp:53) — `ParticleStorage.Allocate(MaxActiveParticles, ParticleSize + PayloadBytes)` (Cycle 10d 의 ξ 해소 패턴 정착).
- **container 측 alignment**: [ParticleTypes.h:71-80](JSEngine/Source/Engine/Particle/ParticleTypes.h:71) — `Allocate(MaxParticles, ParticleStride, Alignment=16)` 가 `AlignSize(ParticleStride, 16)` 호출 후 멤버에 저장 → `GetStride()` 가 single source.

**측정 (sizeof 추정)**:
- `sizeof(FBaseParticle)` ≈ 108B (Location/OldLocation/Velocity/BaseVelocity 4×FVector(12) = 48 + RelativeTime/Lifetime 2×float = 8 + Size FVector = 12 + Color FColor(16) = 16 + Rotation/RotationRate 2×float = 8 + ParticleId/Flags/CollisionCount/SubUVIndex 4×uint32 = 16). [ParticleTypes.h:28-44](JSEngine/Source/Engine/Particle/ParticleTypes.h:28) + [Vector.h:9](JSEngine/Source/Engine/Math/Vector.h:9) + [Color.h:8](JSEngine/Source/Engine/Math/Color.h:8).
- `USpriteTypeData::RequiredPayloadBytes() = 0` ([ParticleModuleTypeData.h:38](JSEngine/Source/Engine/Particle/ParticleModuleTypeData.h:38)).
- `UMeshTypeData::RequiredPayloadBytes() = sizeof(FMeshRotationPayload) = 36` ([ParticleModuleTypeDataMesh.h:17](JSEngine/Source/Engine/Particle/ParticleModuleTypeDataMesh.h:17), [ParticleMeshTypes.h:22](JSEngine/Source/Engine/Particle/ParticleMeshTypes.h:22) `static_assert(sizeof == 36)`).
- **Sprite Stride** = AlignSize(108 + 0, 16) = **112B**.
- **Mesh Stride** = AlignSize(108 + 36, 16) = AlignSize(144, 16) = **144B**.
- **차이** = 32B (36B payload + 16B 단위 padding 흡수).

**Ribbon 영향**: Ribbon payload 도입 시 `URibbonTypeData::RequiredPayloadBytes() = sizeof(FRibbonParticlePayload)` 만 반환하면 같은 자동 가산 메커니즘이 동작. Init 경로 / Allocate 경로 변경 0건.

### 1.2 MeshInstanceBuffer 패턴 재사용 가능성 — [FACT]

- `FInstanceBuffer` 정의: [InstanceBuffer.h:13-38](JSEngine/Source/Engine/Render/Resource/InstanceBuffer.h:13).
- 내부 구현: `FVertexBuffer` 1개 래핑 + `D3D11_USAGE_DYNAMIC` + `D3D11_BIND_VERTEX_BUFFER` + `D3D11_CPU_ACCESS_WRITE` + `D3D11_MAP_WRITE_DISCARD` ([Buffer.cpp:111-167](JSEngine/Source/Engine/Render/Resource/Buffer.cpp:111)).
- **결정적**: 이 클래스는 slot/instancing 비종속 — 일반 dynamic VB 그 자체. "Instance" 이름은 호출자 의도일 뿐, 실제 binding 슬롯은 `IASetVertexBuffers(slot, ...)` 호출자가 결정.
  - Sprite path: `IASetVertexBuffers(0, 2, {Quad, InstanceBuffer}, ...)` ([ParticleRenderPass.cpp:272](JSEngine/Source/Engine/Render/Renderer/RenderFlow/ParticleRenderPass.cpp:272)) — slot 1 사용.
  - Mesh path: `IASetVertexBuffers(0, 2, {MeshVB, MeshInstanceBuffer}, ...)` ([ParticleRenderPass.cpp:364](JSEngine/Source/Engine/Render/Renderer/RenderFlow/ParticleRenderPass.cpp:364)) — slot 1 사용.
- **grow-by-2x**: `Update()` 에서 capacity 초과 시 2배 재할당 자동 ([InstanceBuffer.cpp:31-46](JSEngine/Source/Engine/Render/Resource/InstanceBuffer.cpp:31)) — Ribbon 의 per-frame 변동 vertex 수에 적합.

**다른 dynamic VB 사례 (3.4 참조)**: `LineBatcher::CreateDynamicBuffer` ([LineBatcher.cpp:145-160](JSEngine/Source/Engine/Render/LineBatcher.cpp:145)) / `SubUVBatcher` ([SubUVBatcher.cpp:72-84](JSEngine/Source/Engine/Render/SubUVBatcher.cpp:72)) 는 raw D3D11 buffer + 고정 max 크기 + 수동 Map/Unmap (자동 grow 없음).

**Ribbon 재사용 판단**: `FInstanceBuffer` 그대로 사용 가능. slot 0 binding + `D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP` 만 변경하면 됨. 신규 클래스 (`FDynamicVertexBuffer`) 도입 불필요. 단, 이름이 의미상 오해 소지 — 사용 시 주석 명시 권장.

### 1.3 silent bug μ (MeshBuffer cache UUID 충돌) — [OK]

- **현재 코드는 UUID 기반이 아니라 pointer 기반**: [MeshBufferManager.h:29](JSEngine/Source/Engine/Render/Resource/MeshBufferManager.h:29) — `TMap<const UStaticMesh*, FMeshBuffer> StaticMeshBufferMap[MAX_LOD]`.
- `GetStaticMeshBuffer(const UStaticMesh*, LOD)` ([MeshBufferManager.cpp:130-173](JSEngine/Source/Engine/Render/Resource/MeshBufferManager.cpp:130)) — 같은 asset pointer 면 cache hit.
- Cycle 11 plan 의 `MeshBufferManager.GetMesh(UUID)` 표기는 plan 작성 시점 추측. 실제 구현은 pointer key 로 정착됨 → UUID 충돌 자체가 발생할 여지 없음 (서로 다른 asset 은 서로 다른 pointer).
- 같은 StaticMesh asset 을 Static actor 와 Mesh particle 이 동시 사용 시: 동일 FMeshBuffer 공유 (read-only 자원이므로 안전).
- **race condition (한쪽 release, 다른 쪽 in-use)**: 명시적 ref-count 없음. 그러나 cache 자체가 program lifetime 동안 보유 (`Release()` 호출 시 일괄 해제) → 부분 release 가 없음.

**Ribbon 영향**: Ribbon 은 mesh asset 사용 안 함 (strip 정점 직접 생성). μ 영향 = 0. plan §B-d 가정 그대로.

### 1.4 silent bug λ (BuildInstanceData 매 frame rebuild) — [OK]

- **base Sprite path**: [ParticleEmitterInstance.cpp:316-341](JSEngine/Source/Engine/Particle/ParticleEmitterInstance.cpp:316) — `SpriteInstanceDataBuffer.clear()` → `reserve(ActiveParticles)` → `push_back` 루프.
- **Mesh derived**: [ParticleMeshEmitterInstance.cpp:52-78](JSEngine/Source/Engine/Particle/ParticleMeshEmitterInstance.cpp:52) — `MeshInstanceDataBuffer.clear()` → `reserve(ActiveParticles)` → `push_back`. 동일 패턴.
- 캐시 도입 안 됨 — 결정 8 (캐시 시점) 의 보류 상태 그대로.

**Ribbon 영향**: Ribbon 의 strip 정점은 매 frame 변함 (particle motion 으로 tangent/distance 재계산) → 캐시 의미 자체가 없음. λ 패턴 그대로 유지 적절.

---

## §2 Ribbon outline (plan §B) 코드 대조

### 2.1 `EParticleEmitterRenderMode::Ribbon` — [FACT]

- enum 정의: [ParticleTypes.h:12-18](JSEngine/Source/Engine/Particle/ParticleTypes.h:12) — `Sprite, Mesh, Beam, Ribbon` 4 값 모두 정의 완료.
- 별도 추가 작업 불요.

### 2.2 `EVertexFactoryType::RibbonParticle` switch case — [FACT]

- enum: [VertexFactoryTypes.h:36](JSEngine/Source/Engine/Render/Resource/VertexFactoryTypes.h:36) — `RibbonParticle` 정의 완료.
- 현재 switch 본문: [VertexFactoryTypes.h:296-298](JSEngine/Source/Engine/Render/Resource/VertexFactoryTypes.h:296)
  ```
  case EVertexFactoryType::RibbonParticle:
  case EVertexFactoryType::BeamParticle:
      return EmptyParticleDesc;
  ```
- Cycle 12 작업: `RibbonParticle` case 를 별도로 분리해 `return RibbonParticleDesc;` 로 교체. Beam case 는 `EmptyParticleDesc` 유지.

### 2.3 PrimitiveDrawCommandBuilder Ribbon dispatch — [FACT]

- Builder 측 dispatch: [PrimitiveDrawCommandBuilder.cpp:641-646](JSEngine/Source/Engine/Render/Scene/PrimitiveDrawCommandBuilder.cpp:641) — Ribbon case 본문 이미 wired (Cycle 10a 단계):
  ```
  case EParticleEmitterRenderMode::Ribbon:
      Cmd.RibbonVertices = Instance->GetRibbonVertexData(Count);
      Cmd.RibbonVertexCount = Count;
      Cmd.VertexFactoryType = EVertexFactoryType::RibbonParticle;
      bHasData = (Cmd.RibbonVertices != nullptr && Count > 0);
      break;
  ```
- FRenderCommand 슬롯: [RenderCommand.h:502-503](JSEngine/Source/Engine/Render/Scene/RenderCommand.h:502)
  ```
  const FRibbonParticleVertex* RibbonVertices = nullptr;
  uint32 RibbonVertexCount = 0;
  ```
- base instance default getter: [ParticleEmitterInstance.cpp:365-369](JSEngine/Source/Engine/Particle/ParticleEmitterInstance.cpp:365) — `GetRibbonVertexData() = nullptr/0` 반환.
- **결론**: Builder 측 추가 작업은 Material 결정 (Ribbon 전용 trail material 처리) 만. Mesh 의 `MeshBuffer` / `MeshTypeData` 같은 asset 의존 코드는 없음 (strip 정점 직접 생성).

### 2.4 `ParticleRenderPass::RenderRibbonEmitter` — [FACT]

- 메서드 존재: [ParticleRenderPass.h:26](JSEngine/Source/Engine/Render/Renderer/RenderFlow/ParticleRenderPass.h:26).
- body stub: [ParticleRenderPass.cpp:379-384](JSEngine/Source/Engine/Render/Renderer/RenderFlow/ParticleRenderPass.cpp:379) — `(void)Cmd; (void)Context;` NOP.
- dispatch 측 wiring 완료: [ParticleRenderPass.cpp:162-164](JSEngine/Source/Engine/Render/Renderer/RenderFlow/ParticleRenderPass.cpp:162) — switch case 에서 호출.
- Cycle 12 작업: body 채우기 (shader bind + state + slot 0 dynamic VB + DrawIndexed 또는 Draw + topology STRIP).

### 2.5 ShaderPaths.h 의 RibbonParticle — [GAP]

- 현재 정의: [ShaderPaths.h:34-35](JSEngine/Source/Engine/Render/Resource/ShaderPaths.h:34) — `ParticleSprite`, `ParticleMesh` 만 존재.
- `ParticleRibbon` 항목 누락.
- Cycle 12 작업: 한 줄 추가:
  ```
  inline constexpr const char* ParticleRibbon = "Shaders/Particle/RibbonParticle.hlsl";
  ```
  + `Shaders/Particle/RibbonParticle.hlsl` 신규 작성.

---

## §3 Ribbon-specific 구조 점검

### 3.1 linked list payload 의 SlotIndex 안전성 — [FACT]

- `KillParticle` swap-pop: [ParticleEmitterInstance.cpp:205-215](JSEngine/Source/Engine/Particle/ParticleEmitterInstance.cpp:205)
  ```
  std::swap(ParticleStorage.ParticleIndices[Index], ParticleStorage.ParticleIndices[LastActiveIndex]);
  --ActiveParticles;
  ```
  → **ParticleIndices 만 swap, ParticleData 자체는 move 0건**. SlotIndex (physical position) 가 lifetime 동안 불변.
- Mesh 의 SlotIndex 사용 패턴: [ParticleMeshEmitterInstance.cpp:36-37](JSEngine/Source/Engine/Particle/ParticleMeshEmitterInstance.cpp:36)
  ```
  const int32 SlotIndex = static_cast<int32>(ParticleStorage.ParticleIndices[ActiveIdx]);
  if (FMeshRotationPayload* Payload = GetMeshPayload(SlotIndex)) { ... }
  ```
  → Mesh 가 이미 SlotIndex 기반 payload access 패턴 검증 완료.

**Ribbon 영향**: plan §B-b 가정 (NextIndex/PrevIndex 에 SlotIndex 저장 → swap-pop 안전) 그대로 적용 가능. 단 KillParticle 자체는 base 가 ParticleIndices 만 swap 하므로 linked list 가 가리키는 dead slot 은 자동 정리되지 않음 → Ribbon derived 가 `KillParticle` override 해서 chain 정리 필요 (plan §B-c 명시).

### 3.2 base SpawnParticles 의 derived hook 패턴 — [FACT]

- base SpawnParticles 본문: [ParticleEmitterInstance.cpp:165-199](JSEngine/Source/Engine/Particle/ParticleEmitterInstance.cpp:165) — 신규 슬롯 인덱스를 `const uint16 SlotIndex = ParticleStorage.ParticleIndices[ActiveIndex]` 로 회수 ([line 178](JSEngine/Source/Engine/Particle/ParticleEmitterInstance.cpp:178)) → `FBaseParticle*` 초기화 + module Spawn 호출 → `++ActiveParticles`.
- Mesh override 패턴: [ParticleMeshEmitterInstance.cpp:25-44](JSEngine/Source/Engine/Particle/ParticleMeshEmitterInstance.cpp:25)
  ```
  const int32 OldActiveCount = ActiveParticles;
  FParticleEmitterInstance::SpawnParticles(...);  // base 호출
  for (int32 ActiveIdx = OldActiveCount; ActiveIdx < ActiveParticles; ++ActiveIdx) {
      const int32 SlotIndex = static_cast<int32>(ParticleStorage.ParticleIndices[ActiveIdx]);
      // ... payload 초기화
  }
  ```
  → `[OldActiveCount, ActiveParticles)` 범위 + `ParticleIndices` 로 SlotIndex 추출 패턴 검증.

**Ribbon 영향**: 동일 패턴으로 Ribbon 도 신규 SlotIndex 회수 → `HeadIndices[trail]` 에 prepend → 이전 head 의 PrevIndex 갱신 가능. base SpawnParticles 변경 0건.

### 3.3 FBaseParticle 멤버 vs Ribbon 필요 — [FACT]

- FBaseParticle 정의: [ParticleTypes.h:28-44](JSEngine/Source/Engine/Particle/ParticleTypes.h:28)
  - Location, OldLocation, Velocity, BaseVelocity, RelativeTime, Lifetime, Size, Color, Rotation, RotationRate, ParticleId, Flags, CollisionCount, SubUVIndex.
- Ribbon 가 필요로 하는 필드 (plan §B-b):
  - `int32 NextIndex` — 없음 → **payload 필요**.
  - `int32 PrevIndex` — 없음 → **payload 필요**.
  - `FVector Tangent` — 없음 → **payload 필요**.
  - `float SpawnedTangentStrength` — 없음 → **payload 필요**.
  - `int32 TrailIndex` — 없음 → **payload 필요**.
  - `float Distance` — 없음 → **payload 필요**.

**payload sizeof 추정**: 2×int32 + FVector + float + int32 + float = 8 + 12 + 4 + 4 + 4 = **32B**.
Mesh 의 36B 와 유사 수준 → container Stride 추가 가산 분 (32B + alignment) 이 Sprite 와 정상 분리됨.

### 3.4 dynamic VB 생성 패턴 — reference 사례 — [FACT]

| 사례 | 위치 | 패턴 | grow | 적합도 |
| --- | --- | --- | --- | --- |
| `FInstanceBuffer` | [InstanceBuffer.cpp:5-54](JSEngine/Source/Engine/Render/Resource/InstanceBuffer.cpp:5) | 클래스 래퍼, MAP_WRITE_DISCARD | **2x auto** | **Ribbon 직접 재사용 가능** |
| `LineBatcher::CreateDynamicBuffer` | [LineBatcher.cpp:145-160](JSEngine/Source/Engine/Render/LineBatcher.cpp:145) | raw D3D11 buffer, 고정 max size | 없음 | 참고용 |
| `SubUVBatcher` | [SubUVBatcher.cpp:72-84](JSEngine/Source/Engine/Render/SubUVBatcher.cpp:72) | raw, 고정 max | 없음 | 참고용 |

**Ribbon 권고**: `FInstanceBuffer` 그대로 사용. ribbon strip 정점 수가 trail × particle-per-trail × 2 (strip 양쪽 폭) 이므로 max 가 불확정 → grow 자동이 필수.

---

## §4 사용자 결정 필요 항목 (Claude 의견 첨부)

### 결정 6 — Ribbon payload 구조

| 옵션 | 내용 | 영향 |
| --- | --- | --- |
| **A (plan §B-b)** | `FRibbonParticlePayload { NextIndex, PrevIndex, Tangent, SpawnedTangentStrength, TrailIndex, Distance }` ≈ 32B + alignment | linked list 기반 효율적 trail 관리. KillParticle override 로 chain 정리 필수. |
| **B** | linked list 없이 매 frame TArray rebuild | KillParticle override 불필요. 그러나 ActiveParticleCount 가 큰 경우 O(N×TrailCount) rebuild → 성능 손해. |
| **C** | Mesh 처럼 NextIndex/PrevIndex 없이 spawn 순서 (ActiveIndex) 로 trail 추적 | swap-pop 으로 순서 깨짐 → 사용 불가. |

**Claude 의견**: **옵션 A 권고**. 근거:
1. (3.1) 의 SlotIndex 안전성 확인됨 → linked list 가 swap-pop 영향 0.
2. (3.2) 의 Mesh derived 패턴 검증 완료 → 동일 구조로 Ribbon 도입.
3. Trail 당 head 추적 (HeadIndices[trail]) 만 추가하면 chain traversal 자연스러움.
4. payload sizeof 32B 가 Mesh 의 36B 와 유사 → memory footprint 충격 0.
5. 옵션 B 는 성능 손해 + 결정 8 (캐시 시점) 과 충돌 가능 — 다른 방향으로 가는 게 단순.

### 결정 7 — Ribbon spawn rate vs frame time 안정성

| 옵션 | 내용 | 영향 |
| --- | --- | --- |
| **A** | fixed timestep (예: 60Hz) — spawned tangent 보간 시 안정 | 코드베이스는 variable dt → 변경 범위 거대 (Timer 전체 재설계). |
| **B** | variable dt 그대로 — 실제 게임 동작 | 현재 codebase 와 일치. tangent 계산 시 dt 보정 필요. |

- **현재 codebase**: [Timer.cpp:8-32](JSEngine/Source/Engine/Core/Logging/Timer.cpp:8) — `DeltaTime = duration<float>(CurrentTime - LastTime).count()` (변동). `TargetFrameTime` cap 만 있을 뿐 fixed step 아님.
- `EngineLoop` → `GEngine->Tick(GetDeltaTime())` → `WorldTick` → `World->Tick` → `Actor->Tick` → `Component->ExecuteTick` → `TickComponent` → `Instance->Tick(DeltaTime)` ([ParticleSystemComponent.cpp:197](JSEngine/Source/Engine/Particle/ParticleSystemComponent.cpp:197)).

**Claude 의견**: **옵션 B 권고**. 옵션 A 는 Particle scope 를 벗어남 (Timer 전체 재설계). dt 보정은 spawned tangent 강도 결정 시 spawned dt 를 평균값으로 normalize 하거나 EMA smoothing 으로 처리 가능. 옵션 A 는 별도 cycle (Game Time 개선) 으로 분리하는 게 안전.

### 결정 8 — Ribbon trail 수 상한 + 트레일 식별 방식

| 옵션 | 내용 | 영향 |
| --- | --- | --- |
| **A (plan §B-a)** | `MaxTrailCount` TypeData 멤버 — emitter 당 multiple trail. payload 의 `TrailIndex` 가 식별. | TArray<int32> HeadIndices(MaxTrailCount) 멤버 추가. KillParticle / Spawn 모두 TrailIndex 분기. |
| **B** | emitter 당 single trail 만 — TrailIndex 필드 제거 | payload 4B 절약. SubUV-스타일 다중 trail 사용자는 emitter 를 여러 개 만들어야 함. |

**Claude 의견**: 작업량 enumerate 만 (사용자 결정).
- 옵션 A: payload 32B (TrailIndex 포함), TypeData 멤버 1개 추가, instance 멤버 `HeadIndices` 추가, Spawn 모듈에서 trail 선택 로직 1건.
- 옵션 B: payload 28B (TrailIndex 제거), TypeData 멤버 0개 추가, instance 멤버 `HeadIndex` 단일 int.

Cascade 의 reference Ribbon 은 (A) 채택. 그러나 Cycle 12 의 **단일 issue 원칙** (Cycle 11 에서 결정 4 로 옵션 A 채택한 전례 — 디버깅 표면적 최소) 따르면 (B) 가 더 안전. 옵션 A 는 별도 cycle (12c: multi-trail) 으로 분리 권장.

### 결정 9 — `bRenderGeometry/bRenderSpawnPoints/bRenderTangents` 디버그 플래그 포함 여부

| 옵션 | 내용 | 영향 |
| --- | --- | --- |
| **A (plan §B-a)** | TypeData 에 디버그 플래그 3개 포함, RenderRibbonEmitter 가 각각 line/point/sprite draw 분기 | TypeData 멤버 3개 + RenderRibbonEmitter body 가 ~3x 길이 (line draw 분기 + spawn point draw 분기). |
| **B** | Cycle 12 에서는 geometry render 만, 디버그 시각화는 후속 cycle (12c: Debug Vis) | TypeData 멤버 0건. RenderRibbonEmitter body minimal. |

**Claude 의견**: **옵션 B 권고**. 근거:
1. Cycle 11 에서 옵션 A (간단한 형식적 파생) 채택 후 동작 검증된 패턴 — 동일 원칙 일관성.
2. 디버그 시각화는 LineBatcher 와의 통합 필요 (Tangent vector draw) → 별도 cycle 의 분리가 자연스러움.
3. Cycle 12 의 critical path 가 짧아짐 → silent bug 표면적 감소.

---

## §5 silent bug 후속 영향

### 5.1 ν (Stride mismatch) 후속 — [OK]

- Cycle 10d 해소 패턴 정착 확인 ([1.1] 의 Allocate 호출). Ribbon payload 도입 시 동일 자동 가산 작동.
- **보장 조건**: `RequiredPayloadBytes()` 가 frame 중 변하지 않음 (현재 모든 TypeData override 가 컴파일 타임 상수 반환 — Sprite 0, Mesh sizeof(36)). Ribbon 도 sizeof(FRibbonParticlePayload) 반환 → 컴파일 타임 상수 유지.
- 추가 위험 0.

### 5.2 ξ (PayloadOffset 계산) 후속 — [OK]

- Init 의 `PayloadOffset = ParticleSize` ([ParticleEmitterInstance.cpp:48](JSEngine/Source/Engine/Particle/ParticleEmitterInstance.cpp:48)) 가 single source — Ribbon 도 동일 사용 가능.
- Mesh derived 의 GetMeshPayload 패턴 ([ParticleMeshEmitterInstance.cpp:9-17](JSEngine/Source/Engine/Particle/ParticleMeshEmitterInstance.cpp:9))
  ```
  uint8* ParticleBase = ParticleStorage.ParticleData + SlotIndex * ParticleStorage.GetStride();
  return reinterpret_cast<FMeshRotationPayload*>(ParticleBase + PayloadOffset);
  ```
  → Ribbon 의 GetRibbonPayload 도 동일 산식 사용 가능. PayloadOffset 은 protected 멤버 ([ParticleEmitterInstance.h:85](JSEngine/Source/Engine/Particle/ParticleEmitterInstance.h:85)) 로 derived 접근 허용.

### 5.3 Ribbon-only silent bug 후보 — [GAP]

- **위험 1 (linked list sentinel)**: `NextIndex = -1` 또는 `PrevIndex = -1` 표현. payload 가 `int32` 면 음수 표현 가능 (Mesh 의 FVector 와 달리). `uint16` SlotIndex 와 mix 시 sign-extension 주의 (현재 `ParticleIndices` 가 uint16, payload 의 NextIndex 는 int32 → uint16 ↔ int32 변환 시 0xFFFF vs -1 명확히 분리 필요).
- **위험 2 (head 누락)**: KillParticle override 시 head 가 죽는 케이스 — `HeadIndices[trail]` 을 `NextIndex` 로 갱신 필요. 누락 시 dead slot 참조 → segfault 또는 silent stale tangent.
- **위험 3 (swap-pop 영향)**: base KillParticle 이 ParticleIndices 만 swap → SlotIndex 불변 → payload 의 Next/Prev 무사. 그러나 derived Tick 에서 `ParticleIndices[i]` 로 SlotIndex 회수 후 chain traversal 시 invalid SlotIndex (killed 후 미정리) 참조 가능. **chain 의 dead-end 처리 (`SlotIndex == -1` 검사) 필수**.
- **위험 4 (Spawn 시 chain 끊김)**: SpawnParticles override 가 base 호출 후 신규 slot 의 Prev/Next 초기화 누락 → garbage value → silent rendering 결함. 모든 spawned slot 의 payload 를 명시 초기화 필수.

plan §B-b 의 회귀 안전 메모 ("SlotIndex 저장 → swap-pop 시 invalidate 안 됨") 는 (3.1) 결과와 일치 — 그러나 위 위험 1-4 는 Cycle 12 plan 작성 시 추가 명시 권고.

---

## 결론 한 줄

> **Cycle 12 진입 가능 — 단 사용자 결정 4건 (결정 6/7/8/9) lock-in 후 plan 작성**. Cycle 10d 의 container 책임 승격 + Cycle 11 의 derived instance 패턴이 Ribbon 의 SlotIndex-기반 linked list payload 와 호환됨이 코드로 확인됨. `EParticleEmitterRenderMode::Ribbon`, `EVertexFactoryType::RibbonParticle`, FRenderCommand 의 RibbonVertices 슬롯, ParticleRenderPass::RenderRibbonEmitter stub, base instance 의 GetRibbonVertexData nullptr 기본값까지 wiring 완료. 신규 작업: (1) `URibbonTypeData` + `FParticleRibbonEmitterInstance` + `FRibbonParticlePayload` + `FRibbonParticleVertex`, (2) ShaderPaths.h 의 `ParticleRibbon` 항목 추가 + RibbonParticle.hlsl, (3) VertexFactoryTypes.h 의 RibbonParticle case 본문 교체, (4) RenderRibbonEmitter body, (5) vcxproj 등록. silent bug 후보 4건 (5.3 위험 1-4) 은 Cycle 12 plan 작성 시 회귀 안전 항목으로 명시 권고.

**다음 단계**: 사용자가 결정 6/7/8/9 검토 → lock-in → `Cycle12_ImplementPlan.md` 작성 진입.
