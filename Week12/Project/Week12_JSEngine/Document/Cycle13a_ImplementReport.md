# Cycle 13a (Beam Emitter — Noise 제외) 구현 결과 보고서

**작성일**: 2026-05-26
**대상 브랜치**: `feature/ParticleRender`
**모드**: implement 완료 (빌드 검증 통과 — 오류/경고 0)
**선행 문서**:
- [Cycle13_ReDiagnose.md](Cycle13_ReDiagnose.md) — 코드 대조 진단
- [Cycle12_ImplementReport.md](Cycle12_ImplementReport.md) — Ribbon 구현 패턴
- 사용자 lock-in prompt (결정 10/11/12/13/14/15 통합본)

---

## §0 한 줄 요약

> Cycle 10a (Beam wiring 사전 통과) + Cycle 11 (derived instance 패턴) + Cycle 12 (Ribbon dynamic strip VB) 의 3중 기반 위에 Beam 의 Source/Target Component 추적 + multi-beam round-robin + dynamic strip VB 구현 완료. Debug x64 빌드 통과 (**오류 0, 경고 0**). silent bug 4건 (위험 1/7/8/9) 모두 명시 방어 코드 적용. Cycle 13b (Noise + 디버그 시각화) 로 이관 5건. 인게임 verify 만 남음.

---

## §1 적용된 사용자 결정 (lock-in)

| 결정 | 옵션 | 코드 위치 |
| --- | --- | --- |
| 10 (Source/Target 추상화) | A — `TObjectPtr<USceneComponent>` Component reference | [ParticleModuleBeamSource.h:24-26](../JSEngine/Source/Engine/Particle/ParticleModuleBeamSource.h:24) + [ParticleModuleBeamTarget.h:21-22](../JSEngine/Source/Engine/Particle/ParticleModuleBeamTarget.h:21) |
| 11 (Source/Target 갱신 시점) | B — Tick 매 frame 추적 (Component 매 frame `GetWorldLocation()`) | [ParticleBeamEmitterInstance.cpp:166-184](../JSEngine/Source/Engine/Particle/ParticleBeamEmitterInstance.cpp:166) (BuildVertexBuffer 내부) |
| 12 (Noise) | B/13a — 본 sub-cycle 에서 Noise 제외, 13b 로 이관 | NoiseModule UCLASS 0건. payload Noise sample 없음. |
| 13 (Multi-beam) | A — `MaxBeamCount` + `BeamIndex` + `BeamStates[]` + `EnsureBeamState` lazy init | [ParticleModuleTypeDataBeam.h:55-57](../JSEngine/Source/Engine/Particle/ParticleModuleTypeDataBeam.h:55) + [ParticleBeamEmitterInstance.cpp:79-95](../JSEngine/Source/Engine/Particle/ParticleBeamEmitterInstance.cpp:79) |
| 14 (Cycle 분할) | C — 13a/13b 분할 (본 보고서는 13a 만) | 본 보고서 §11 의 13b 이관 항목 list |
| 15 (BeamMethod) | B — `PEB2M_Target` 만, Target nullptr fallback (Source + Forward × FallbackDistance) | [ParticleBeamEmitterInstance.cpp:177-184](../JSEngine/Source/Engine/Particle/ParticleBeamEmitterInstance.cpp:177) |

---

## §2 변경 파일 목록

### 신규 파일 (10건)

| 파일 | 역할 | bytes / 라인 |
| --- | --- | --- |
| [ParticleBeamTypes.h](../JSEngine/Source/Engine/Particle/ParticleBeamTypes.h) | `FParticleBeamPayload` (4B) + `FBeamParticleVertex` (48B) + `static_assert` 2건 | 42 lines |
| [ParticleModuleTypeDataBeam.h](../JSEngine/Source/Engine/Particle/ParticleModuleTypeDataBeam.h) | `UBeamTypeData` UCLASS 선언 — 6 UPROPERTY | 64 lines |
| [ParticleModuleTypeDataBeam.cpp](../JSEngine/Source/Engine/Particle/ParticleModuleTypeDataBeam.cpp) | `UBeamTypeData::CreateInstance` 본문 | 15 lines |
| [ParticleModuleBeamSource.h](../JSEngine/Source/Engine/Particle/ParticleModuleBeamSource.h) | `UParticleModuleBeamSource` UCLASS — `TObjectPtr<USceneComponent>` UPROPERTY | 29 lines |
| [ParticleModuleBeamSource.cpp](../JSEngine/Source/Engine/Particle/ParticleModuleBeamSource.cpp) | (단순 데이터 컨테이너 — Spawn/Update 무) | 4 lines |
| [ParticleModuleBeamTarget.h](../JSEngine/Source/Engine/Particle/ParticleModuleBeamTarget.h) | `UParticleModuleBeamTarget` UCLASS — Source 와 동일 패턴 | 25 lines |
| [ParticleModuleBeamTarget.cpp](../JSEngine/Source/Engine/Particle/ParticleModuleBeamTarget.cpp) | (단순 데이터 컨테이너) | 4 lines |
| [ParticleBeamEmitterInstance.h](../JSEngine/Source/Engine/Particle/ParticleBeamEmitterInstance.h) | `FParticleBeamEmitterInstance` 선언 — 3 override + `BeamStates` / `NextBeamIndex` / `VertexBuffer` 멤버 | 47 lines |
| [ParticleBeamEmitterInstance.cpp](../JSEngine/Source/Engine/Particle/ParticleBeamEmitterInstance.cpp) | 3 override + 3 helper 본문 | 270 lines |
| [BeamParticle.hlsl](../JSEngine/Shaders/Particle/BeamParticle.hlsl) | VS (`BeamParticleVS`) + PS (`BeamParticlePS`) — slot 0 only, `SV_VertexID` 로 V 좌표 결정 | 54 lines |
| **소계** | | **554 lines** |

### 수정 파일 (6건)

| 파일 | 변경 내용 |
| --- | --- |
| [ShaderPaths.h](../JSEngine/Source/Engine/Render/Resource/ShaderPaths.h) | `ParticleBeam` 경로 상수 1줄 추가 |
| [VertexFactoryTypes.h](../JSEngine/Source/Engine/Render/Resource/VertexFactoryTypes.h) | `BeamParticleLayout` (5 입력) + `BeamParticleDesc` 정의 + switch case 본문 교체 (`EmptyParticleDesc` → `BeamParticleDesc`). `#include "Particle/ParticleBeamTypes.h"` 추가. |
| [ParticleRenderPass.h](../JSEngine/Source/Engine/Render/Renderer/RenderFlow/ParticleRenderPass.h) | `FInstanceBuffer BeamVertexBuffer` 멤버 추가 |
| [ParticleRenderPass.cpp](../JSEngine/Source/Engine/Render/Renderer/RenderFlow/ParticleRenderPass.cpp) | `GetBeamParticleProgram` helper + `BeamVertexBuffer.Create / Release` + `RenderBeamEmitter` body 채움 (BlendAlpha + DepthReadOnly + SolidNoCull + TRIANGLESTRIP + indexless `Draw`). 기존 stub 본문 교체. |
| [PrimitiveDrawCommandBuilder.cpp](../JSEngine/Source/Engine/Render/Scene/PrimitiveDrawCommandBuilder.cpp) | Beam case 에 `Cmd.Material = BeamTD->GetMaterial()` 추가 + ParticleTexture 추출 분기를 `Mesh \|\| Ribbon \|\| Beam` 으로 확장. `#include "Particle/ParticleModuleTypeDataBeam.h"` 추가. |
| [JSEngine.vcxproj](../JSEngine/JSEngine.vcxproj) + [.filters](../JSEngine/JSEngine.vcxproj.filters) | 10 신규 파일 + 3 `*.gen.cpp` 등록 (총 13 ItemGroup 항목 × 2 파일) |

---

## §3 작업 순서 vs 실제 진행 결과

| Phase | 계획 (prompt §2) | 실제 진행 | 비고 |
| --- | --- | --- | --- |
| 1 | 데이터 구조 (ParticleBeamTypes.h) | 완료 | `FParticleBeamPayload` (4B) + `FBeamParticleVertex` (48B) + 2 static_assert. 진단 §12 옵션 Y (별도 struct) 채택. |
| 2 | Module UCLASS 3종 + GenerateReflection.py | 완료 | TypeData / Source / Target 3 UCLASS + 3 `*.gen.cpp` 자동 생성 확인 |
| 3 | Instance 구현 | 완료 | 3 override (Spawn/Tick/GetBeamVertexData) + 3 helper (GetBeamPayload/EnsureBeamState/BuildVertexBuffer). KillParticle override 안 함 (base swap-pop 안전). BuildInstanceData override 안 함 (slot 0 only). |
| 4 | VertexFactory / Render | 완료 | `BeamParticleLayout` + `BeamParticleDesc` + case 본문 교체 + `ParticleBeam` shader path 1줄 + `BeamParticle.hlsl` 신규 + `RenderBeamEmitter` body 채움 + `BeamVertexBuffer` 멤버 + `GetBeamParticleProgram` helper. |
| 5 | Builder Material 분기 | 완료 | Beam case 에 `Cmd.Material` 라인 1줄 + Material/Texture 분기 조건에 `\|\| Beam` 확장 |
| 6 | vcxproj 등록 | 완료 | 3 `*.gen.cpp` + 5 `.h` + 5 `.cpp` + 1 `.hlsl` (총 14 항목 × 2 파일 = 28 라인 추가) |
| 7 | 빌드 verify | 완료 | Debug x64 통과. 오류 0, 경고 0. JSEngine.exe 45.16 MB (Cycle 12 와 동일 scale). |

### Phase 6 의 빌드 진행 중 발견된 이슈

없음. Cycle 12 의 `LNK2019 URibbonTypeData::StaticClass()` 미정의 이슈를 사전 인지 — `GenerateReflection.py` 실행 → `*.gen.cpp` 3개 vcxproj 수동 등록 → 첫 빌드 통과.

---

## §4 silent bug 방어 매핑

| 위험 | 내용 | 방어 코드 위치 |
| --- | --- | --- |
| **1** | sentinel/invalid SlotIndex 참조 (`GetBeamPayload` 의 SlotIndex 범위 검사) | `GetBeamPayload` 의 `SlotIndex < 0 \|\| SlotIndex >= GetMaxActiveParticleCount()` 검사 ([ParticleBeamEmitterInstance.cpp:60-63](../JSEngine/Source/Engine/Particle/ParticleBeamEmitterInstance.cpp:60)) + `SpawnParticles` 내부 nullptr 검사 ([cpp:109-113](../JSEngine/Source/Engine/Particle/ParticleBeamEmitterInstance.cpp:109)) |
| **5** | dangling pointer (결정 11 B Tick 추적 모드 결과) | `BuildVertexBuffer` 의 SourceComp/TargetComp nullptr 시 fallback ([cpp:155-156, 158, 178-184](../JSEngine/Source/Engine/Particle/ParticleBeamEmitterInstance.cpp:155)) — SourceComp nullptr → emitter 위치, TargetComp nullptr → Source + Forward × FallbackDistance |
| **7** | zero-length beam (Source == Target → NaN tangent) | `BuildVertexBuffer` 의 `BeamLength < BeamSmallNumber` 시 continue ([cpp:189-194](../JSEngine/Source/Engine/Particle/ParticleBeamEmitterInstance.cpp:189)) |
| **8** | InterpolationPoints 음수/과대 | `BuildVertexBuffer` 의 `std::clamp(..., 0, BeamInterpolationPointsMax=64)` ([cpp:163-164](../JSEngine/Source/Engine/Particle/ParticleBeamEmitterInstance.cpp:163)) + TypeData UPROPERTY Min/Max(0..64) 1차 방어 ([ParticleModuleTypeDataBeam.h:55-57](../JSEngine/Source/Engine/Particle/ParticleModuleTypeDataBeam.h:55)) |
| **9** | MaxBeamCount 초과 spawn (round-robin overflow) | `SpawnParticles` 의 `NextBeamIndex = (NextBeamIndex + 1) % MaxBeams` ([ParticleBeamEmitterInstance.cpp:128-130](../JSEngine/Source/Engine/Particle/ParticleBeamEmitterInstance.cpp:128)) |

**제외 (13b 이관)**: 위험 6 (Noise determinism), 위험 10 (race — 단일 thread 가정).

---

## §5 회귀 안전 점검 (변경 0건 보장)

| 항목 | 결과 |
| --- | --- |
| `USpriteTypeData` / `UMeshTypeData` / `URibbonTypeData` 변경 | 0건 |
| base `FParticleEmitterInstance::Init` 변경 | 0건 |
| base `FParticleEmitterInstance::SpawnParticles` 변경 | 0건 (derived 가 base 호출 후 hook) |
| base `FParticleEmitterInstance::KillParticle` 변경 | 0건 (Beam derived 는 override 안 함 — base swap-pop 직접 사용) |
| base `FParticleEmitterInstance::Tick` 변경 | 0건 (derived 가 base 호출 후 BuildVertexBuffer) |
| `FVertexFactoryRegistry::Get` 의 Sprite/Mesh/Ribbon case | 변경 0건 (Beam case 만 본문 교체) |
| `PrimitiveDrawCommandBuilder` 의 Sprite/Mesh/Ribbon case | 변경 0건 (Beam case 내부 + Material 분기 조건만 확장) |
| `RenderSpriteEmitter` / `RenderMeshEmitter` / `RenderRibbonEmitter` body | 변경 0건 |
| `FRenderCommand` sizeof | 464B 그대로 (`static_assert` 통과 — Beam slot 이 이미 baseline 에 포함된 상태) |
| `EPT_ParticleSystem` case `return true` 도달 보장 | break 후 함수 끝 도달 — 유지 |
| base `GetBeamVertexData` 기본 `nullptr/0` 반환 | 유지 ([ParticleEmitterInstance.cpp:377-381](../JSEngine/Source/Engine/Particle/ParticleEmitterInstance.cpp:377)) — derived 가 override |
| Cycle 12 multi-trail / chain prepend / topology 복원 본문 | 0건 변경 |

---

## §6 container 자동 가산 세 번째 실측 검증

- **Stride 산식**: `AlignSize(sizeof(FBaseParticle) + sizeof(FParticleBeamPayload), 16)` = `AlignSize(108 + 4, 16)` = `AlignSize(112, 16)` = **112B**.
- **PayloadOffset**: `sizeof(FBaseParticle)` = 108. payload 영역은 `[108, 112)` 범위에 인터리브 배치.
- **자동 가산**: Cycle 10d 의 container.Allocate(`ParticleSize + PayloadBytes`) 패턴이 Beam 에서 **세 번째 실측 검증** (Sprite 0B → Mesh 36B → Ribbon 32B → **Beam 4B**).

| Emitter | Payload bytes | Stride (post-align 16B) | 차이 |
| --- | --- | --- | --- |
| Sprite | 0 | 112B | baseline |
| Mesh | 36 | 144B | +32B |
| Ribbon | 32 | 144B | +32B (Mesh 와 동일) |
| **Beam (13a)** | **4** | **112B** | **0B (Sprite 와 동일)** |

- **SlotIndex 안전성**: Beam 은 chain 의존 없음 → base `KillParticle` 의 swap-pop 직접 사용 → SlotIndex 불변. `GetBeamPayload(SlotIndex)` 의 SlotIndex 는 ParticleStorage.ParticleIndices[ActiveIdx] 에서 직접 회수 (`SpawnParticles` [cpp:107](../JSEngine/Source/Engine/Particle/ParticleBeamEmitterInstance.cpp:107)).
- **base `Init` 의 PayloadBytes 가산**: [ParticleEmitterInstance.cpp:44-46](../JSEngine/Source/Engine/Particle/ParticleEmitterInstance.cpp:44) — `UBeamTypeData::RequiredPayloadBytes() = sizeof(FParticleBeamPayload) = 4` 반환 → `ParticleSize + PayloadBytes = 108 + 4 = 112` → container.Allocate(112) → align(16) → 112B stride.

---

## §7 Ribbon (Cycle 12) vs Beam (Cycle 13a) 차이 실측

| 항목 | Ribbon | Beam (13a) |
| --- | --- | --- |
| 신규 모듈 수 | **1** (TypeData) | **3** (TypeData + Source + Target) |
| `RequiredPayloadBytes` | 32 (`sizeof(FRibbonParticlePayload)`) | **4** (`sizeof(FParticleBeamPayload)`) |
| Stride | 144B | **112B** |
| `SpawnParticles` override | payload init + chain prepend + tangent init | **BeamIndex round-robin 분배만** |
| `KillParticle` override | **있음** — chain 재연결 + head 갱신 | **없음** (base swap-pop 안전) |
| `Tick` override | 있음 — chain 순회 + VertexBuffer rebuild | 있음 — **Source/Target Component 추적 + VertexBuffer rebuild** |
| `BuildInstanceData` override | 없음 (slot 0 only) | 없음 (slot 0 only) |
| GPU instancing | no (slot 0 dynamic VB only) | **no** (Ribbon 와 동일 카테고리) |
| Primitive topology | TRIANGLESTRIP + degenerate seam | **TRIANGLESTRIP + degenerate seam** (동일 패턴) |
| Draw call | `Draw` (indexless) | `Draw` (indexless) |
| MeshBuffer 의존 | no (strip 정점 직접 생성) | no |
| D3D state | AlphaBlend + DepthReadOnly + NoCull | **AlphaBlend + DepthReadOnly + NoCull** (Additive 본 cycle 외) |
| Material 출처 | `URibbonTypeData::GetMaterial()` | `UBeamTypeData::GetMaterial()` |
| ParticleTexture 추출 | Material.DiffuseMap (Mesh 와 공통 분기) | Material.DiffuseMap (Mesh/Ribbon 와 공통 분기 — 확장) |
| topology 복원 | helper 끝에서 TRIANGLELIST 복원 | helper 끝에서 TRIANGLELIST 복원 (동일) |
| linked list | ✅ chain 의존 | ❌ |
| multi-instance 식별 | `TrailIndex` + `HeadIndices[]` | `BeamIndex` + `BeamStates[]` (동일 패턴) |
| Source/Target 추적 | 없음 | **있음 (Tick 매 frame `GetWorldLocation()` 호출)** |
| Noise | 없음 | **13a 에서 없음, 13b 에서 추가 예정** |
| 신규 silent bug 후보 | 4건 (위험 1-4 chain 관리) | **5건 (위험 1/5/7/8/9)** — 모두 방어 코드 적용 |

---

## §8 빌드 verify 결과

- **명령**: `MSBuild JSEngine.sln /p:Configuration=Debug /p:Platform=x64 /m /v:minimal`
- **결과**: `JSEngine.exe` 생성 (`C:\GitDirectory12\JSEngine\Bin\Debug\JSEngine.exe`, **45.16 MB** — Cycle 12 의 45 MB 와 동일 scale)
- **컴파일 오류**: **0**
- **컴파일 경고**: **0** (정확 검증: `MSBuild ... /v:normal` 출력에서 `: warning` 매치 0건)
- **링커 오류**: **0**
- **빌드 진행 중 추가 fix**: 0건 (Cycle 12 의 `LNK2019` 이슈 사전 인지로 .gen.cpp 등록 첫 시도 통과)

### Cycle 13a 의 vcxproj 신규 등록 항목 (총 14건)

```xml
<!-- ClCompile (8) -->
Source\Engine\Particle\ParticleBeamEmitterInstance.cpp
Source\Engine\Particle\ParticleModuleBeamSource.cpp
Source\Engine\Particle\ParticleModuleBeamTarget.cpp
Source\Engine\Particle\ParticleModuleTypeDataBeam.cpp
Intermediate\Reflection\Particle\UBeamTypeData.gen.cpp
Intermediate\Reflection\Particle\UParticleModuleBeamSource.gen.cpp
Intermediate\Reflection\Particle\UParticleModuleBeamTarget.gen.cpp

<!-- ClInclude (5) -->
Source\Engine\Particle\ParticleBeamEmitterInstance.h
Source\Engine\Particle\ParticleBeamTypes.h
Source\Engine\Particle\ParticleModuleBeamSource.h
Source\Engine\Particle\ParticleModuleBeamTarget.h
Source\Engine\Particle\ParticleModuleTypeDataBeam.h

<!-- None (1) -->
Shaders\Particle\BeamParticle.hlsl
```

---

## §9 sizeof 확인

| 구조체 | 예상 | 실측 (static_assert 통과) | 위치 |
| --- | --- | --- | --- |
| `FParticleBeamPayload` | 4B (int32 BeamIndex 만) | **4B** | [ParticleBeamTypes.h:21-22](../JSEngine/Source/Engine/Particle/ParticleBeamTypes.h:21) |
| `FBeamParticleVertex` | 48B (FVector + FVector + FColor + float + float = 12+12+16+4+4) | **48B** | [ParticleBeamTypes.h:40-41](../JSEngine/Source/Engine/Particle/ParticleBeamTypes.h:40) |
| `FRenderCommand` | 464B (Cycle 10a baseline, Beam 슬롯 이미 포함) | **464B** | [ParticleRenderPass.cpp:15](../JSEngine/Source/Engine/Render/Renderer/RenderFlow/ParticleRenderPass.cpp:15) |

세 assert 모두 컴파일 통과 — 한 개라도 실패했으면 빌드 오류로 즉시 감지.

---

## §10 인게임 verify 항목 (사용자 후속 — 본 cycle 범위 외)

prompt §8 의 verify 항목 그대로:

- [ ] `UBeamTypeData` asset 생성 → MaxBeamCount=1 → SourceComponent/TargetComponent 둘 다 set → 화면에 strip 1개 직선
- [ ] TargetComponent nullptr → fallback 동작 (Source + Forward × FallbackDistance)
- [ ] MaxBeamCount=3 + 동시 spawn → 3 strip + degenerate seam (시각적으로는 13a 에서 동일 strip — Source/Target 공유, 13b 의 Noise 도입 후 시각 차이 가시화)
- [ ] SourceComponent 가 움직이면 beam 끝점 실시간 추적 (결정 11 B 검증)
- [ ] InterpolationPoints=10 → 직선이 10 등분된 정점 (시각 동일하나 정점 수 확인 — RenderDoc)
- [ ] InterpolationPoints=-5 또는 1000 → clamp 작동 (silent bug 8 검증)
- [ ] SourceComponent destroy → segfault 없음 (silent bug 5 검증)
- [ ] Sprite/Mesh/Ribbon 회귀 동일 동작
- [ ] RenderDoc capture: topology TRIANGLESTRIP, slot 0 only, slot 1 binding 없음

### verify 방법

1. **에디터 in-engine**: ParticleSystem asset 신규 생성 → `UBeamTypeData` 모듈 추가 (현재 UI 가 자동 노출되는지 본 cycle 미검증) → `UParticleModuleBeamSource` / `UParticleModuleBeamTarget` 추가 → Source/Target component picker → spawn module + lifetime module 추가 → world 에 배치 → PIE 실행
2. **RenderDoc**: capture 시점에 Particle pass 의 `Draw` 이벤트 검색 → topology / VB binding / VertexCount 확인
3. **회귀**: 기존 Sprite/Mesh/Ribbon ParticleSystem asset 실행해 시각적 변화 없음 확인

---

## §11 13b 이관 항목 (Cycle 13a 범위 외)

prompt §1 의 "제외 (DON'T — 13b 로 이관)" 그대로:

| 항목 | 13b 작업 |
| --- | --- |
| `UParticleModuleBeamNoise` UCLASS 생성 | 신규 UCLASS — Frequency / NoiseRange / NoiseSpeed / bSmooth / bNoiseLock / bTargetNoise 멤버 |
| Noise sample 배열 (FVector NoiseSamples[]) payload 확장 | `FParticleBeamPayload` 확장 — bNoiseLock 채택 시 per-particle 영구 sample 보존 |
| `BuildVertexBuffer` 의 Noise perturbation 로직 | 각 interpolation point 의 CenterPos 에 noise sample 더하기 |
| LineBatcher 디버그 시각화 (각 noise sample 점 line draw) | LineBatcher 통합 (Cycle 12 의 디버그 시각화 분리와 동일 후속 작업) |
| 위험 6 (Noise determinism) 방어 | `FEngineRandom::SetSeed(ParticleId)` 기반 deterministic noise 또는 시간 누적 Perlin |

### 본 emitter 의도 외 (cycle 외)

- Spawn 캡처 모드 (결정 11 A — Tick 추적과 양자택일)
- `PEB2M_Distance` BeamMethod (결정 15 B 채택 → Target 미설정 fallback 으로 흡수됨)
- BeamMethod enum (다중 method 도입 시 후속 cycle)
- `Speed` (beam 전파 속도)
- `Sheets` (strip 두께 분할) — 본 cycle 1 고정
- Additive blend (EBlendType 에 Additive 값 없음 — Material/RenderResources 별도 cycle)
- BeamMethod 의 `Emitter` / `Particle` / `Branch` 종

---

## §12 prompt 추측과 실제 코드의 차이

진단 §3.2.3 의 권고대로 진행 — 본 엔진의 reflection 에 `AActor*` UPROPERTY 패턴 0건, `ReferenceKind` 에 `Actor` 값 없음. `TObjectPtr<USceneComponent>` 으로 결정 10 옵션 A 채택. 코드 생성기가 자동으로 `EObjectReferenceKind::ActorComponent` 등록 ([UParticleModuleBeamSource.gen.cpp:31](../JSEngine/Intermediate/Reflection/Particle/UParticleModuleBeamSource.gen.cpp:31) 확인).

prompt §1 의 멤버 후보 중 본 구현이 부분 채택한 항목:
- `TextureTile` / `TextureTileDistance` — 둘 다 채택 (BuildVertexBuffer 의 TexU 계산 분기 [ParticleBeamEmitterInstance.cpp:208-210](../JSEngine/Source/Engine/Particle/ParticleBeamEmitterInstance.cpp:208)).
- `Speed` / `Sheets` 등 — 미채택 (본 cycle 외, §11 list).

prompt §3.5 의 "FBeamParticleVertex Ribbon vertex alias vs 별도 struct" 옵션:
- **옵션 Y (별도 struct) 채택** — 진단 §12 권고 그대로. forward declaration 충돌 회피 + 13b 의 Beam 전용 멤버 추가 여지 보존.

prompt §1 (포함 DO) 의 모든 항목 구현 완료. 다른 차이 없음.

---

## 결론 한 줄

> Cycle 13a (Beam Emitter — Noise 제외) 구현 완료. 16 파일 변경 (신규 10 + 수정 6 + vcxproj 1 pair). Debug x64 빌드 통과 (오류/경고 0). 회귀 안전 11 항목 모두 충족. silent bug 5건 (위험 1/5/7/8/9) 명시 방어 코드 적용. container 자동 가산 패턴 **세 번째 실측 검증** (Sprite 0B → Mesh 36B → Ribbon 32B → **Beam 4B**, Stride **112B** — Sprite 와 동일). Source/Target 추상화는 `TObjectPtr<USceneComponent>` 로 검증된 패턴 채택 (REFLECTION_GUIDE.md §2.2 의 ActorComponent kind 자동 도출 확인). 13b 이관 5건 (Noise UCLASS + payload 확장 + perturbation + LineBatcher 디버그 + 위험 6 방어) 명시. 인게임 verify 만 다음 단계.
