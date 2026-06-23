# Cycle 11 진입 전 진단 — Container 도입 + Cycle 10d 완료 반영

**작성일**: 2026-05-25 (Cycle 10d 완료 후 갱신)
**대상 브랜치**: `feature/ParticleRender`
**모드**: diagnose only (코드 변경 0)
**baseline**: [ParticleEmitter_InfraCheck.md](ParticleEmitter_InfraCheck.md) (Cycle 8–10 진입 시점 진단)
**선행 cycle**: Cycle 10d — Container 책임 승격 (Stride 흡수 + silent bug ν fix + ξ 자연 해소)
**전제 (재논의 금지)**: TypeData 패턴 채택, `EParticleEmitterRenderMode` 라우팅 키, 공통 infra → Mesh → Ribbon → Beam 순서

---

## 0. Cycle 10d 변경 요약 (본 갱신의 직접 근거)

Cycle 10d 가 본 진단의 Part C 결정 6/7 을 직접 해소. container 가 particle data 의 단일 책임 모듈로 승격되어 silent bug ν / ξ 동시 해소.

| 항목 | 이전 (Cycle 10c 시점) | 현재 (Cycle 10d 완료) |
|---|---|---|
| Stride source-of-truth | `FParticleEmitterInstance::ParticleStride` 멤버 (instance) | `FParticleDataContainer::ParticleStride` 멤버 (container) |
| Stride read | instance 직접 read (`* ParticleStride`) | `ParticleStorage.GetStride()` 위임 (4곳) |
| Stride 계산 | `AlignSize(ParticleSize, 16)` — payload 미반영 | container `Allocate(MaxParticles, ParticleSize + PayloadBytes)` 내부 — payload 자동 가산 + align |
| container 할당 | `Allocate()` 직후 redundant `new uint8/uint16` 로 덮어씀 → leak | 단일 `Allocate()` 만 — 단일 블록 free 일관 |
| silent bug **ν** (container 이중 할당 + leak) | 위험 높음 (매 Reset 마다 ParticleIndices leak) | **해소** |
| silent bug **ξ** (Stride payload-aware 누락) | 위험 중간 (Mesh/Ribbon/Beam payload>0 시 buffer overflow) | **해소** |
| ν/ξ 외 silent bug (ι/κ/λ/μ) | ι/κ 해소 (Cycle 8), λ/μ 미해결 | 동일 (본 cycle 범위 외) |

**Cycle 10d 변경 파일 3건**:
- [ParticleTypes.h:46-117](../JSEngine/Source/Engine/Particle/ParticleTypes.h:46) — container 에 `int32 ParticleStride` 멤버 + `GetStride()` 메서드 + Allocate 내부 source-of-truth 저장 + Reset 일관성
- [ParticleEmitterInstance.h:53, 92-94](../JSEngine/Source/Engine/Particle/ParticleEmitterInstance.h:53) — instance `ParticleStride` 멤버 삭제, `GetParticleStride()` 본문을 container 위임으로
- [ParticleEmitterInstance.cpp:27-65, 179, 224, 247, 260](../JSEngine/Source/Engine/Particle/ParticleEmitterInstance.cpp:27) — Init 재구성 (PayloadBytes 계산을 Allocate 호출 앞으로, Allocate 인자에 `+ PayloadBytes`, redundant `new` 2라인 삭제), 4곳 `* ParticleStride` → `* ParticleStorage.GetStride()`

---

## Part A. 재조사 보고 (Cycle 10d 반영)

### A.1 Container 정체 식별 — 갱신

| 항목 | 결과 |
|---|---|
| 이름 + 위치 | `FParticleDataContainer` — [ParticleTypes.h:46](../JSEngine/Source/Engine/Particle/ParticleTypes.h:46) |
| 저장 단위 | **per-emitter (per-instance)** — `FParticleEmitterInstance::ParticleStorage` 값 멤버 |
| 저장 형식 | **type-erased byte buffer + stride 책임** (Cycle 10d) — `uint8* ParticleData` + `uint16* ParticleIndices` 단일 블록 + `int32 ParticleStride` |
| 멤버 | `MemBlockSize`, `ParticleDataNumBytes`, `ParticleIndicesNumShorts`, `ParticleData*`, `ParticleIndices*`, **`ParticleStride`** (Cycle 10d 추가) |
| 메서드 | `Allocate(MaxParticles, Stride, Alignment)`, `Reset`, `GetMemoryBytes`, `AlignSize`, **`GetStride()`** (Cycle 10d 추가) |
| 누가 owns | `FParticleEmitterInstance::ParticleStorage` — [ParticleEmitterInstance.h:88](../JSEngine/Source/Engine/Particle/ParticleEmitterInstance.h:88) |
| 누가 writes | Init/Reset ([ParticleEmitterInstance.cpp:53, 72](../JSEngine/Source/Engine/Particle/ParticleEmitterInstance.cpp:53)), Spawn slot 채우기 ([cpp:178-179](../JSEngine/Source/Engine/Particle/ParticleEmitterInstance.cpp:178)), KillParticle swap-pop ([cpp:213](../JSEngine/Source/Engine/Particle/ParticleEmitterInstance.cpp:213)) |
| 누가 reads | Tick GetParticle ([cpp:247, 260](../JSEngine/Source/Engine/Particle/ParticleEmitterInstance.cpp:247)), GetRuntimeView ([cpp:220-224](../JSEngine/Source/Engine/Particle/ParticleEmitterInstance.cpp:220)), BuildInstanceData ([cpp:325-339](../JSEngine/Source/Engine/Particle/ParticleEmitterInstance.cpp:325)) — **모두 `GetStride()` 경유** (Cycle 10d) |

> **결론 (갱신)**: baseline §2-3 의 `ParticleData/Indices`/`ParticleStride` 슬롯이 **container로 완전 흡수** (Cycle 10d). Data/Indices/Stride 모두 container 내부, instance 측 stride 멤버 삭제. type 분기는 여전히 container 외부 (BuildInstanceData/CollectPrimitive)에서 발생.

### A.2 EmitterInstance 측 변화 (Cycle 10d 반영)

| baseline 항목 | 현재 상태 | 근거 |
|---|---|---|
| ParticleData / Indices | 흡수 — container 내부 | [ParticleEmitterInstance.h:57-58](../JSEngine/Source/Engine/Particle/ParticleEmitterInstance.h:57) `GetParticleData/Indices() → ParticleStorage.*` |
| **ParticleStride** | **삭제됨** — instance 멤버 제거, container로 이전 (Cycle 10d) | [ParticleEmitterInstance.h:93-94](../JSEngine/Source/Engine/Particle/ParticleEmitterInstance.h:93) 삭제 + 주석, [ParticleTypes.h:57](../JSEngine/Source/Engine/Particle/ParticleTypes.h:57) container 내부 |
| InstanceData / PayloadSize / PayloadOffset | 잔존 — slot 채워짐, **Stride 에 반영됨** (Cycle 10d) | [ParticleEmitterInstance.cpp:44-48](../JSEngine/Source/Engine/Particle/ParticleEmitterInstance.cpp:44) `PayloadBytes` 계산 후 [cpp:53](../JSEngine/Source/Engine/Particle/ParticleEmitterInstance.cpp:53) `Allocate(MaxActiveParticles, ParticleSize + PayloadBytes)` 인자에 가산 |
| Tick/Spawn/Kill virtual | 도입됨 — Cycle 9 | [ParticleEmitterInstance.h:26, 28, 30](../JSEngine/Source/Engine/Particle/ParticleEmitterInstance.h:26) |
| BuildInstanceData virtual | 신규 — Cycle 10c | [ParticleEmitterInstance.h:39](../JSEngine/Source/Engine/Particle/ParticleEmitterInstance.h:39) |
| Get{Sprite,Mesh,Ribbon,Beam}*Data 4종 | 신규 — Cycle 10c | [ParticleEmitterInstance.h:45-48](../JSEngine/Source/Engine/Particle/ParticleEmitterInstance.h:45) |
| SpriteInstanceDataBuffer | 신규 — Cycle 10b | [ParticleEmitterInstance.h:103](../JSEngine/Source/Engine/Particle/ParticleEmitterInstance.h:103) |
| 파생 클래스 생성 | TypeData->CreateInstance() hook | [ParticleSystemComponent.cpp:54-56](../JSEngine/Source/Engine/Particle/ParticleSystemComponent.cpp:54) |
| `GetParticleStride()` getter | container 위임 | [ParticleEmitterInstance.h:53](../JSEngine/Source/Engine/Particle/ParticleEmitterInstance.h:53) `return ParticleStorage.GetStride();` |

### A.3 ParticleModule 측 변화 — 변경 없음 (Cycle 10d 범위 외)

baseline §A.3 동일. 단 Cycle 10d 가 `RequiredPayloadBytes()` 를 Allocate 인자로 자동 전파 — Mesh/Ribbon/Beam TypeData 가 정확한 byte 수만 반환하면 stride 자동 가산.

| 항목 | 상태 |
|---|---|
| `UParticleModuleTypeDataBase::RequiredPayloadBytes()` | bytes 단위 — Cycle 10d 가 Allocate 인자에 가산 → Stride 에 자동 반영 |
| `UParticleModuleTypeDataBase::CreateInstance()` | container 모름 — base 생성자가 container 초기화 책임 (Init 안에서) |
| `USpriteTypeData` 회귀 안전 | 유효 — `RequiredPayloadBytes() = 0` → Allocate(... ParticleSize + 0) → Sprite Stride 비트 단위 동일 |
| `RequiredModule::RenderMode` | 잔존 — UPROPERTY NoEdit, TypeData single source |

### A.4 Builder / RenderPass / RenderCommand 측 — 변경 없음 (Cycle 10d 범위 외)

baseline §A.4 동일. Cycle 10d 가 instance 인터페이스 변경이 없어 Builder/RenderPass 영향 0.

### A.5 §7 결정 1~5 — 변경 없음

baseline §A.5 동일. 결정 1/2/3 확정 (Cycle 10a/c), 결정 4/5 재논의 필요.

### A.6 silent bug 상태 — 갱신

| § | 항목 | 상태 | 근거 |
|---|---|---|---|
| ι | TypeDataModule UPROPERTY 누락 | 해소 (Cycle 8) | [ParticleSystem.h:46-47](../JSEngine/Source/Engine/Particle/ParticleSystem.h:46) |
| κ | RenderMode vs TypeData 우선순위 | 해소 (Cycle 8) | `GetEffectiveRenderMode` TypeData 우선 |
| λ | BuildInstanceData 캐시 무효 flag | **미해결** — 본 cycle 범위 외 | [ParticleEmitterInstance.cpp:316-322](../JSEngine/Source/Engine/Particle/ParticleEmitterInstance.cpp:316) 매 frame `clear() + reserve()` |
| μ | MeshBuffer cache UUID 충돌 | **미확인** — Cycle 11 진입 시 측정 필요 | — |
| **ν** (container 이중 할당 + leak) | **해소 (Cycle 10d)** | [ParticleEmitterInstance.cpp:53](../JSEngine/Source/Engine/Particle/ParticleEmitterInstance.cpp:53) Allocate 1회만, redundant new 2라인 삭제. [ParticleTypes.h Reset](../JSEngine/Source/Engine/Particle/ParticleTypes.h:107) 단일 블록 free 일관 |
| **ξ** (Stride payload-aware 미반영) | **해소 (Cycle 10d)** | [ParticleEmitterInstance.cpp:53](../JSEngine/Source/Engine/Particle/ParticleEmitterInstance.cpp:53) `Allocate(MaxActiveParticles, ParticleSize + PayloadBytes)` → container 내부 align 후 ParticleStride 멤버 저장 → 모든 read가 `GetStride()` 경유로 payload 자동 포함 |

---

## Part B. Mesh / Ribbon / Beam 3종 구현 Plan (Cycle 10d 반영)

### B.0 작성 원칙 — 갱신

- baseline §3 4-카테고리 + Cycle 10d container 책임 승격 반영
- 각 emitter = 별도 cycle, 단일 component / 단일 issue
- **silent bug ν / ξ 는 Cycle 10d 에서 사전 해소 — Mesh/Ribbon/Beam 진입 시 추가 작업 없음**
- Mesh/Ribbon/Beam TypeData 가 `RequiredPayloadBytes()` 만 정확히 반환하면 stride 자동 가산 (container 가 책임)

### B.1 Mesh emitter plan (Cycle 11)

**목표**: payload 0 형식적 파생 + Mesh asset 1개 화면 표시.

**변경 대상 파일**
| path | 신규/수정 |
|---|---|
| `Engine/Particle/ParticleModuleTypeDataMesh.h/.cpp` | 신규 |
| `Engine/Particle/ParticleMeshEmitterInstance.h/.cpp` | 신규 (FParticleEmitterInstance 파생) |
| `Engine/Render/Resource/VertexTypes.h` | 수정 — `FMeshParticleInstanceData` 정의 추가 |
| `Engine/Render/Resource/VertexFactoryTypes.h` | 수정 — `MeshParticleLayout` + `MeshParticleDesc` 본문 (현재 `EmptyParticleDesc`) |
| `Engine/Render/Renderer/RenderFlow/ParticleRenderPass.cpp` | 수정 — `RenderMeshEmitter` 본문 ([line 253-258](../JSEngine/Source/Engine/Render/Renderer/RenderFlow/ParticleRenderPass.cpp:253) NOP 교체) |
| `Engine/Render/Scene/PrimitiveDrawCommandBuilder.cpp` | 수정 — Mesh 분기 ([line 622-627](../JSEngine/Source/Engine/Render/Scene/PrimitiveDrawCommandBuilder.cpp:622))에 Mesh asset 조회 + `Cmd.MeshBuffer` 채움 |
| `Shaders/Particle/MeshParticle.hlsl` | 신규 |
| `Engine/Render/Resource/ShaderPaths.h` | 수정 — `ParticleMesh` 경로 추가 |
| `JSEngine.vcxproj` + `.filters` | 수정 — 신규 .h/.cpp/.hlsl 등록 (silent bug §7-4) |

**container 상호작용** — 갱신
- read: derived `BuildInstanceData()` override 가 base `ParticleStorage` + `GetStride()` 그대로 사용
- write: 없음 (Mesh는 spawn/kill 모두 base 동작 그대로)
- init: `UMeshTypeData::RequiredPayloadBytes()` 가 0 또는 MeshRotation bytes 반환 — Cycle 10d 가 Allocate 인자에 자동 가산
- handoff: derived 가 `MeshInstanceDataBuffer` 채워 `GetMeshInstanceData()` override 로 노출 → Builder 가 `Cmd.MeshParticleInstances` 로 매핑

**완료 기준**
- cube/sphere mesh asset 1개 + UMeshTypeData 로 교체한 ParticleSystem asset 빌드/실행 → 화면에 N개 mesh 표시
- RenderDoc: `DrawIndexedInstanced(IndexCount, ActiveParticles, 0, 0, 0)` event 발생, slot 0 mesh VB / slot 1 instance VB

**회귀 안전 장치**
- UMeshTypeData::RequiredPayloadBytes() = 0 유지 → Sprite 와 동일 Stride → Sprite path 회귀 0
- USpriteTypeData 변하지 않음
- VertexFactoryRegistry::Get MeshParticle case 본문 채울 때만 교체 — Sprite case 미접근 (silent bug §7-1)

**silent bug 매칭** — 갱신
| § | 충돌 | 해소 |
|---|---|---|
| §7-1 | MeshParticle case 본문 누락 시 `EmptyParticleDesc` → silent | 본 cycle 명시 채움 |
| §7-4 | vcxproj 신규 파일 다수 | VS 닫고 작업 후 reload |
| §7-5 | EPT_ParticleSystem case `return true` | 이미 보장 (변경 0) |
| **ν** | — | **Cycle 10d 사전 해소 — 본 cycle 추가 작업 0** |
| **ξ** | — | **Cycle 10d 사전 해소 — payload 0 / MeshRotation 도입 어느 쪽이든 stride 자동 가산** |

### B.2 Ribbon emitter plan (Cycle 12a + 12b)

#### Cycle 12a — payload + KillParticle override

**변경 대상 파일** — 갱신
| path | 신규/수정 |
|---|---|
| `Engine/Particle/ParticleModuleTypeDataRibbon.h/.cpp` | 신규 |
| `Engine/Particle/ParticleRibbonEmitterInstance.h/.cpp` | 신규 |
| `Engine/Particle/ParticleTypes.h` 또는 `ParticleRibbonTypes.h` | 신규 `FRibbonParticlePayload` |
| ~~`Engine/Particle/ParticleEmitterInstance.cpp`~~ | **Cycle 10d 에서 ξ 선해소 완료 — 본 cycle 추가 변경 0** |

**container 상호작용** — 갱신
- read: `GetParticle(ActiveIndex)` 로 FBaseParticle 영역 + `(uint8*)Particle + PayloadOffset` 으로 RibbonPayload 영역. payload 는 container 내부에 stride 단위로 인터리브
- write: Spawn override 가 새 slot 의 payload 초기화 (NextIndex/PrevIndex/Tangent...)
- init: container.Allocate 그대로 — **Cycle 10d 가 `RibbonTypeData::RequiredPayloadBytes()` 를 자동 가산** → stride 에 RibbonPayload bytes 반영
- handoff: 12b 에서 trail 순회 결과를 `RibbonVertexBuffer` (instance 멤버) 에 unroll → `GetRibbonVertexData()` 노출

**완료 기준 (12a)**
- 디버거 watch: trail 1개에 5개 particle spawn → linked list traversal 로 head→tail 5회 hop 가능
- KillParticle (중간 노드) 후에도 linked list 무결성

**회귀 안전 장치 (12a)**
- 12a 는 렌더 없음 — Sprite path 변경 0
- RibbonPayload 는 **물리 SlotIndex (container 의 ParticleData 위치)** 저장. swap-pop 은 `ParticleIndices` 만 swap, SlotIndex 불변 → link 안전
- KillParticle override 는 base swap-pop 호출 전에 NextIndex/PrevIndex 재연결

**silent bug 매칭 (12a)** — 갱신
| § | 충돌 | 해소 |
|---|---|---|
| baseline §4.1 | swap-pop vs linked list | RibbonPayload 가 SlotIndex 저장, swap 이 link 안 깨뜨림 — 디버거 watch 검증 |
| **ξ** | — | **Cycle 10d 사전 해소 — Ribbon TypeData 가 RequiredPayloadBytes() 정확히 반환만 하면 됨** |
| **ν** | — | **Cycle 10d 사전 해소** |

#### Cycle 12b — strip VB + Pass 분기 (변경 없음)

baseline 동일.

### B.3 Beam emitter plan (Cycle 13a + 13b)

#### Cycle 13a — Beam payload + Tick override + Source/Target 모듈

**container 상호작용** — 갱신
- read: BeamPayload 는 container 내부 stride 단위. base `GetParticle(i)` + offset access
- write: Spawn override SourcePoint/TargetPoint 초기화. Tick override 매 frame Source/Target lookup 갱신
- init: container.Allocate 그대로 — **Cycle 10d 가 BeamTypeData::RequiredPayloadBytes() 자동 가산**
- Tick 의미 변경: base Tick 의 `RelativeTime >= 1.0f → KillParticle` 우회

**silent bug 매칭 (13a)** — 갱신
| § | 충돌 | 해소 |
|---|---|---|
| **ξ** | — | **Cycle 10d 사전 해소** |
| Tick 의미 분기 | base Tick 의 SpawnModule 호출 ([ParticleEmitterInstance.cpp:103-105](../JSEngine/Source/Engine/Particle/ParticleEmitterInstance.cpp:103)) 가 Beam 에서 의미 다름 | derived Tick 에서 override 명시 |

#### Cycle 13b — strip VB + Noise + Pass 분기 (변경 없음)

baseline 동일.

### B.4 cycle 간 의존성 / 순서 — 갱신

| 항목 | 권고 |
|---|---|
| 기존 순서 (Mesh → Ribbon → Beam) | **유지** |
| ~~선행 fix cycle (Cycle 10d?) 신설 권고~~ | **Cycle 10d 로 실현 완료** — silent bug ν / ξ 모두 해소 |
| Cycle 11 (Mesh) 즉시 진입 가능 여부 | **가능** — 추가 선행 fix cycle 불필요 |

### B.5 진단의 한계 / 추측 영역 — 갱신

**해소** (코드 확인 + Cycle 10d 결과)
- 추측 2 부분 (Ribbon KillParticle): Spawn 패턴 ([ParticleEmitterInstance.cpp:177-179](../JSEngine/Source/Engine/Particle/ParticleEmitterInstance.cpp:177)) 가 SlotIndex 기반 → payload 가 SlotIndex 저장 시 swap 안전
- 추측 4 (RenderPass 분리): 단일 Pass + procedural switch 확정
- 추측 5 (FRenderCommand 옵션): (i) 별도 슬롯 확정 + sizeof=464 baseline
- **추가 (Cycle 10d)**: container 의 MemBlockSize 단일 블록과 Ribbon free-list 호환성 — **ν 해소로 container 단일 블록 설계가 의도대로 작동**. 이전 진단의 신규 추측 1개 자동 해소

**추측 잔존**
- 추측 1 (MeshRotation 필요성): 결정 4와 동일. Cycle 10d 가 ξ 해소했으므로 어느 쪽이든 안전
- 추측 3 (Beam Source/Target): 단순 component world location vs actor reference 미결정
- 추측 6 (MeshBuffer cache UUID 충돌, μ): 본 진단 범위 외
- 추측 7 (Noise 데이터 모델): 결정 5와 동일

**신규 추측** (Cycle 10d 이후 잔존)
- BuildInstanceData 매 frame 전체 재구축 (λ): emitter 수 × frame 수 누적 비용 — Mesh/Ribbon/Beam 진입 시 비용 증가 가시화 [추측]

---

## Part C. 다음 cycle 진입 결정 항목 — 갱신

- **(결정 4) Mesh의 payload 0 vs MeshRotation 도입 (초기)** — 선택지 (A) payload 0 / (B) MeshRotation ~36B. **본 진단 권고**: (A) 유지 — Cycle 10d 가 ξ 를 사전 해소했으므로 (B) 도입 시 회귀 위험은 0 이지만, Cycle 11 의 단일 issue 원칙 측면에서 (A) 가 형식적 파생만으로 마무리하기에 단순
- **(결정 5) Beam Noise를 Cycle 13b에 포함 vs 별도 cycle** — 선택지 (A) 13b 포함 / (B) 13c 로 분리. **본 진단 권고**: (B)
- ~~(결정 6) silent bug ν 해소 시점~~ — **해결됨 (Cycle 10d, 옵션 B 선택)**
- ~~(결정 7) silent bug ξ 해소 시점~~ — **해결됨 (Cycle 10d, 결정 6 과 동시 — 옵션 A 선택)**
- **(결정 8) BuildInstanceData 매 frame rebuild (λ) 캐시화 시점** — 선택지 (A) Cycle 11–13 진입 전 / (B) 3종 emitter 완료 후 별도 최적화 cycle / (C) 무시. **본 진단 권고**: (B)
- **(결정 9, 신규) Cycle 10e (외부 raw 접근 → `GetParticle(SlotIndex)` API 화) 우선순위** — 선택지 (A) Cycle 11 전 / (B) 3종 emitter 완료 후 / (C) 사용자 식별 후. **본 진단 권고**: (C) — 다른 팀원의 raw 사용처 식별 우선, 우선순위 낮음

---

## 결론 한 줄 — 갱신

> Cycle 8–10c 로 type 분기 골격 완성, **Cycle 10d 로 container 책임 승격 (Stride 흡수 + silent bug ν 해소 + ξ 자연 해소) 완료**. Mesh/Ribbon/Beam 진입 전 공통 infra 작업이 모두 완결됨. **Cycle 11 (Mesh) 즉시 진입 가능 — 추가 선행 fix cycle 불필요**. 잔존 silent bug 는 λ (BuildInstanceData 매 frame rebuild) + μ (MeshBuffer cache UUID 충돌) 2건이고 둘 다 본 도입 cycle 종료 후 별도 cycle 로 처리.
