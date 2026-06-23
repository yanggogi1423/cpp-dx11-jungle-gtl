# Cycle 12 (Ribbon Emitter) 구현 결과 보고서

**작성일**: 2026-05-26
**대상 브랜치**: `feature/ParticleRender`
**모드**: implement 완료 (빌드 검증 통과)
**선행 문서**:
- [Cycle12_ReDiagnose.md](Cycle12_ReDiagnose.md) — 코드 대조 검증
- [Cycle11_ImplementPlan.md](Cycle11_ImplementPlan.md) — Ribbon outline §B
- 사용자 lock-in prompt (결정 6/7/8/9 통합본)

---

## §0 한 줄 요약

> Cycle 10d (container 책임 승격) + Cycle 11 (derived instance 패턴) + Cycle 10a (Ribbon wiring 사전 통과) 의 3중 기반 위에 Ribbon 의 linked list payload + multi-trail + dynamic strip VB 구현 완료. Debug x64 빌드 통과 (오류 0, 경고 0). silent bug 4건 (위험 1-4) 모두 명시 방어 코드 적용. 인게임 verify 만 남음.

---

## §1 적용된 사용자 결정 (lock-in)

| 결정 | 옵션 | 코드 위치 |
| --- | --- | --- |
| 6 (payload 구조) | A — linked list (`FRibbonParticlePayload` 32B) | [ParticleRibbonTypes.h:17-26](../JSEngine/Source/Engine/Particle/ParticleRibbonTypes.h:17) |
| 7 (spawn rate vs frame time) | B — variable dt + 위치 변화 기반 tangent | [ParticleRibbonEmitterInstance.cpp:229-238](../JSEngine/Source/Engine/Particle/ParticleRibbonEmitterInstance.cpp:229) |
| 8 (trail 수 + 식별) | A — `MaxTrailCount` + `TrailIndex` + `HeadIndices` | [ParticleModuleTypeDataRibbon.h:29-30](../JSEngine/Source/Engine/Particle/ParticleModuleTypeDataRibbon.h:29) + [ParticleRibbonEmitterInstance.h:42](../JSEngine/Source/Engine/Particle/ParticleRibbonEmitterInstance.h:42) |
| 9 (디버그 플래그) | B — 본 cycle 제외, 후속 cycle (12c) 로 분리 | TypeData 멤버에서 `bRenderGeometry/SpawnPoints/Tangents` 없음 |

---

## §2 변경 파일 목록

### 신규 파일 (6건)

| 파일 | 역할 | bytes / 라인 |
| --- | --- | --- |
| [ParticleRibbonTypes.h](../JSEngine/Source/Engine/Particle/ParticleRibbonTypes.h) | `FRibbonParticlePayload` (32B) + `FRibbonParticleVertex` (48B) + `static_assert` | 42 lines |
| [ParticleModuleTypeDataRibbon.h](../JSEngine/Source/Engine/Particle/ParticleModuleTypeDataRibbon.h) | `URibbonTypeData` UCLASS 선언 | 44 lines |
| [ParticleModuleTypeDataRibbon.cpp](../JSEngine/Source/Engine/Particle/ParticleModuleTypeDataRibbon.cpp) | `URibbonTypeData::CreateInstance` 본문 | 16 lines |
| [ParticleRibbonEmitterInstance.h](../JSEngine/Source/Engine/Particle/ParticleRibbonEmitterInstance.h) | `FParticleRibbonEmitterInstance` 선언 + `HeadIndices` / `NextTrailIndex` / `VertexBuffer` 멤버 | 50 lines |
| [ParticleRibbonEmitterInstance.cpp](../JSEngine/Source/Engine/Particle/ParticleRibbonEmitterInstance.cpp) | 5개 override + 4개 helper 본문 | 318 lines |
| [RibbonParticle.hlsl](../JSEngine/Shaders/Particle/RibbonParticle.hlsl) | VS (`RibbonParticleVS`) + PS (`RibbonParticlePS`) — slot 0 only, `SV_VertexID` 로 V 좌표 결정 | 49 lines |

### 수정 파일 (6건)

| 파일 | 변경 내용 |
| --- | --- |
| [ShaderPaths.h](../JSEngine/Source/Engine/Render/Resource/ShaderPaths.h) | `ParticleRibbon` 경로 상수 1줄 추가 |
| [VertexFactoryTypes.h](../JSEngine/Source/Engine/Render/Resource/VertexFactoryTypes.h) | `RibbonParticleLayout` (5 입력) + `RibbonParticleDesc` 정의 + switch case 본문 교체 (`EmptyParticleDesc` → `RibbonParticleDesc`). Beam case 는 `EmptyParticleDesc` 유지 (Cycle 13 까지 stub). `#include "Particle/ParticleRibbonTypes.h"` 추가. |
| [ParticleRenderPass.h](../JSEngine/Source/Engine/Render/Renderer/RenderFlow/ParticleRenderPass.h) | `FInstanceBuffer RibbonVertexBuffer` 멤버 추가 |
| [ParticleRenderPass.cpp](../JSEngine/Source/Engine/Render/Renderer/RenderFlow/ParticleRenderPass.cpp) | `GetRibbonParticleProgram` helper + `RibbonVertexBuffer.Create / Release` + `RenderRibbonEmitter` body 채움 (BlendAlpha + DepthReadOnly + SolidNoCull + TRIANGLESTRIP + indexless `Draw`). |
| [PrimitiveDrawCommandBuilder.cpp](../JSEngine/Source/Engine/Render/Scene/PrimitiveDrawCommandBuilder.cpp) | Ribbon case 에 `Cmd.Material = RibbonTD->GetMaterial()` 추가 + ParticleTexture 추출 분기를 Mesh/Ribbon 공통으로 확장. `#include "Particle/ParticleModuleTypeDataRibbon.h"` 추가. |
| [JSEngine.vcxproj](../JSEngine/JSEngine.vcxproj) + [.filters](../JSEngine/JSEngine.vcxproj.filters) | 6 신규 파일 + `URibbonTypeData.gen.cpp` 등록 (총 7 ItemGroup 항목) |

---

## §3 작업 순서 vs 실제 진행 결과

| 단계 | 계획 (prompt) | 실제 진행 | 비고 |
| --- | --- | --- | --- |
| 1 | 결정 lock-in 확인 | 사용자가 prompt 에서 lock-in | — |
| 2 | payload/vertex/TypeData/Instance stub 작성 | 완료 | 5 파일 1회 작성 |
| 3 | Init + `HeadIndices` 초기화 | **lazy `EnsureTrailState()` 로 대체** | base `Init` 이 non-virtual ([ParticleEmitterInstance.h:24](../JSEngine/Source/Engine/Particle/ParticleEmitterInstance.h:24)) 이므로 override 불가. constructor 단계엔 LOD/TypeData 미접근. Tick/SpawnParticles 진입 시 lazy 초기화로 동등 동작 + base 변경 0건 보장 (회귀 안전 §5 부합). |
| 4 | `SpawnParticles` override | 완료 — Mesh `[OldActiveCount, ActiveParticles)` 패턴 재사용 | round-robin trail + chain prepend + Velocity 기반 tangent 초기화 |
| 5 | `KillParticle` override | 완료 — base 호출 전 chain 재연결 | head death 시 `HeadIndices[trail] = NextSlot` 명시 갱신 |
| 6 | `Tick` + `BuildVertexBuffer` | 완료 — base Tick 후 chain 순회 + strip 정점 생성 | degenerate seam 으로 multi-trail 연결 끊김 |
| 7 | `ShaderPaths` + hlsl | 완료 — `ParticleRibbon` 상수 + `RibbonParticle.hlsl` | VS 는 vertex 가 이미 world-space → Model 행렬 무시. `SV_VertexID` 짝/홀수로 strip V 좌표 결정. |
| 8 | `VertexFactoryTypes.h` layout + desc + case | 완료 — Beam case 는 `EmptyParticleDesc` 유지 | Cycle 13 까지 의도된 stub |
| 9 | `RenderRibbonEmitter` body + Builder Material | 완료 — slot 0 only, `Draw(VertexCount, 0)` (indexless) | topology 본 helper 시작 시 `TRIANGLESTRIP`, 끝에서 `TRIANGLELIST` 복원 (다음 helper 영향 방지) |
| 10 | vcxproj 등록 + 빌드 verify | 완료 — `GenerateReflection.py` 실행 후 `URibbonTypeData.gen.cpp` 도 vcxproj 등록 필요 | Debug x64 빌드 통과 |

---

## §4 silent bug 4건 (위험 1-4) 방어 코드 매칭

| 위험 | 내용 | 방어 코드 위치 |
| --- | --- | --- |
| 1 | sentinel/invalid SlotIndex 참조 (`NextIndex = -1` 또는 dead slot) | `GetRibbonPayload` / `GetParticleBySlot` 의 `SlotIndex < 0 \|\| SlotIndex >= GetMaxActiveParticleCount()` 검사 ([ParticleRibbonEmitterInstance.cpp:36, 49](../JSEngine/Source/Engine/Particle/ParticleRibbonEmitterInstance.cpp:36)) |
| 2 | head death 시 `HeadIndices` 미갱신 → dead slot 참조 | `KillParticle` 의 `PrevSlot < 0` 분기에서 `HeadIndices[TrailIdx] = NextSlot` ([ParticleRibbonEmitterInstance.cpp:173-180](../JSEngine/Source/Engine/Particle/ParticleRibbonEmitterInstance.cpp:173)) |
| 3 | chain dead-end (payload nullptr) | Tick / BuildVertexBuffer 의 `if (!Payload) break;` ([ParticleRibbonEmitterInstance.cpp:219, 272](../JSEngine/Source/Engine/Particle/ParticleRibbonEmitterInstance.cpp:219)) |
| 4 | spawn 시 payload garbage | `SpawnParticles` 에서 6 필드 (`TrailIndex`/`PrevIndex`/`NextIndex`/`Tangent`/`SpawnedTangentStrength`/`Distance`) 명시 초기화 ([ParticleRibbonEmitterInstance.cpp:114-139](../JSEngine/Source/Engine/Particle/ParticleRibbonEmitterInstance.cpp:114)) |

---

## §5 회귀 안전 점검 (변경 0건 보장)

| 항목 | 결과 |
| --- | --- |
| `USpriteTypeData` 변경 | 0건 |
| `UMeshTypeData` 변경 | 0건 |
| base `FParticleEmitterInstance::Init` 변경 | 0건 |
| base `FParticleEmitterInstance::SpawnParticles` 변경 | 0건 (derived 가 base 호출 후 hook) |
| base `FParticleEmitterInstance::KillParticle` 변경 | 0건 (derived 가 base 호출 전에 chain 정리) |
| base `FParticleEmitterInstance::Tick` 변경 | 0건 (derived 가 base 호출 후 strip rebuild) |
| `FVertexFactoryRegistry::Get` 의 Sprite/Mesh case | 변경 0건 (Ribbon case 만 분리) |
| Beam case | `EmptyParticleDesc` 유지 — Cycle 13 까지 의도된 stub |
| `RenderSpriteEmitter` / `RenderMeshEmitter` body | 변경 0건 |
| `FRenderCommand` sizeof | 464B 그대로 (`static_assert` 통과) |
| `EPT_ParticleSystem` case `return true` 도달 | break 후 함수 끝 도달 — 보장 유지 |

---

## §6 container 상호작용 (실측)

- **Stride 산식**: `AlignSize(sizeof(FBaseParticle) + sizeof(FRibbonParticlePayload), 16)` = `AlignSize(108 + 32, 16)` = `AlignSize(140, 16)` = **144B**. Mesh 와 동일 stride.
- **PayloadOffset**: `sizeof(FBaseParticle)` = 108. payload 영역은 `[108, 140)` 범위에 인터리브 배치. base `Init` 의 `PayloadOffset = ParticleSize` 라인 ([ParticleEmitterInstance.cpp:48](../JSEngine/Source/Engine/Particle/ParticleEmitterInstance.cpp:48)) 그대로 사용.
- **자동 가산**: Cycle 10d 의 container.Allocate(`ParticleSize + PayloadBytes`) 패턴이 Ribbon 에서 두 번째 실측 검증됨 (Mesh 36B 후 Ribbon 32B).
- **SlotIndex 안전성**: base `KillParticle` 의 swap-pop 이 `ParticleIndices` 만 swap → physical SlotIndex 불변 → linked list 의 `NextIndex/PrevIndex` (SlotIndex 저장) 자동 안전.

---

## §7 빌드 검증

- **명령**: `MSBuild JSEngine.sln /p:Configuration=Debug /p:Platform=x64 /m /v:minimal`
- **결과**: `JSEngine.exe` 생성 (`C:\GitDirectory12\JSEngine\Bin\Debug\JSEngine.exe`, 45MB)
- **컴파일 오류**: 0
- **컴파일 경고**: 0
- **링커 오류**: 0
- **`grep "error|warning"` 결과**: 없음

### 빌드 진행 중 발견된 이슈 1건 (해결됨)

- **현상**: 첫 빌드에서 `LNK2019 URibbonTypeData::StaticClass()` 미정의 오류 발생.
- **원인**: `UCLASS()` 의 `GENERATED_BODY` 매크로가 의존하는 `*.gen.cpp` 가 `GenerateReflection.py` 실행 후에만 생성되며, 신규 `URibbonTypeData.gen.cpp` 는 별도로 vcxproj 에 등록되어야 빌드에 포함됨.
- **해결**:
  1. `Scripts\GenerateReflection.py` 실행 → `Intermediate\Reflection\Particle\URibbonTypeData.gen.cpp` 생성
  2. `JSEngine.vcxproj` + `.filters` 에 해당 .gen.cpp 항목 추가 (Mesh / Sprite 의 .gen.cpp 등록 패턴 동일)
  3. 재빌드 → 통과

### Cycle 12 의 vcxproj 신규 등록 항목 (총 7건)

```xml
<!-- ClCompile -->
Source\Engine\Particle\ParticleModuleTypeDataRibbon.cpp
Source\Engine\Particle\ParticleRibbonEmitterInstance.cpp
Intermediate\Reflection\Particle\URibbonTypeData.gen.cpp

<!-- ClInclude -->
Source\Engine\Particle\ParticleModuleTypeDataRibbon.h
Source\Engine\Particle\ParticleRibbonEmitterInstance.h
Source\Engine\Particle\ParticleRibbonTypes.h

<!-- None -->
Shaders\Particle\RibbonParticle.hlsl
```

---

## §8 Mesh (Cycle 11) vs Ribbon (Cycle 12) 차이 실측

| 항목 | Mesh | Ribbon |
| --- | --- | --- |
| `RequiredPayloadBytes` | 36 (`sizeof(FMeshRotationPayload)`) | 32 (`sizeof(FRibbonParticlePayload)`) |
| Stride | 144B | 144B (동일) |
| `SpawnParticles` override | 단순 payload init | payload init + chain prepend + `HeadIndices` 갱신 |
| `KillParticle` override | **없음** | **있음** — chain 재연결 + head 갱신 |
| `Tick` override | 없음 (base 사용) | **있음** — base Tick 후 chain 순회 + `BuildVertexBuffer` |
| GPU instancing | yes (slot 1 instance VB) | **no** (slot 0 dynamic VB only) |
| Primitive topology | `TRIANGLELIST` | **`TRIANGLESTRIP`** + degenerate seam |
| Draw call | `DrawIndexedInstanced` | `Draw` (indexless) |
| MeshBuffer 의존 | yes (StaticMesh asset) | **no** (strip 정점 직접 생성) |
| D3D state | Opaque + DepthTestWrite + Backface | **AlphaBlend + DepthReadOnly + NoCull** |
| Material 출처 | `UMeshTypeData::GetEffectiveMaterial()` | `URibbonTypeData::GetMaterial()` |
| ParticleTexture 추출 | Material.DiffuseMap | Material.DiffuseMap (동일 분기로 통합) |
| topology 복원 | 필요 없음 | helper 끝에서 `TRIANGLELIST` 복원 (다음 Sprite/Mesh helper 보호) |
| 신규 silent bug 후보 | Stride 자동 가산 검증 (해소) | **위험 1-4 (chain 관리)** — 4건 모두 명시 방어 |

---

## §9 Cycle 12 진입 시점에 lock-in 안 된 항목 (실구현 결정)

prompt §7 의 "추측 / 미결정" 항목에 대해 본 구현이 채택한 값:

| 항목 | 채택 |
| --- | --- |
| `ComputePerpendicular(Tangent)` reference axis | **world Up (0,0,1)**. Tangent 가 Up 과 평행한 degenerate 의 경우 X 축 fallback. ([ParticleRibbonEmitterInstance.cpp:18-22](../JSEngine/Source/Engine/Particle/ParticleRibbonEmitterInstance.cpp:18)) |
| Shader unlit vs lit | **unlit** (`Sample * Color`) — 후속 cycle 에서 lit/lighting 통합 |
| PerObjectConstants | `Model = Identity` (vertex 가 이미 world space, Mesh 와 동일 원칙) |
| Init 본문 vs lazy | **lazy `EnsureTrailState()`** — base `Init` non-virtual 회피 + 회귀 안전 0건 보장 |
| `MaxParticleInTrailCount` 강제 | 본 cycle 미적용 — base `SpawnParticles` 의 `ActiveParticles < MaxActiveParticles` 검사만 사용. trail 당 cap 강제는 후속 cycle |

---

## §10 verify 항목 (미실행 — 다음 단계)

빌드 검증만 통과. 인게임 verify (인스턴스 실행 + 화면 표시) 는 미수행 — 다음 단계.

### 필요 verify 항목 (prompt §4 완료 기준)

- [ ] `URibbonTypeData` 의 ParticleSystem asset (MaxTrailCount = 1) 생성 → 실행 → 화면에 strip 1개 표시
- [ ] MaxTrailCount = 3 변경 → 3개 trail 별도 strip + degenerate seam 연결 끊김 확인
- [ ] RenderDoc capture: `Draw(VertexCount, 0)` event + topology `TRIANGLESTRIP` + slot 0 dynamic VB (slot 1 binding 없음)
- [ ] 회귀: Sprite asset / Mesh asset 동일 동작 (Cycle 11 결과 보존)
- [ ] silent bug 방어 동작 확인 (디버거 watch):
  - 위험 1: invalid SlotIndex 시 chain 순회 즉시 종료
  - 위험 2: head 가 죽으면 다음 particle 이 head
  - 위험 3: chain traversal 중 dead slot 만나면 break
  - 위험 4: spawn 직후 payload 6 필드 모두 명시 값

### verify 방법

1. **에디터 in-engine**: ParticleSystem asset 신규 생성 → `URibbonTypeData` 모듈 추가 (UI 가 자동 노출되는지 본 cycle 미검증) → `MaxTrailCount` 조정 → spawn module + velocity module 추가 → world 에 배치 → PIE 실행
2. **RenderDoc**: capture 시점에 Particle pass 의 `Draw` 이벤트 검색 → topology / VB binding / VertexCount 확인
3. **회귀**: 기존 Sprite/Mesh ParticleSystem asset 실행해 시각적 변화 없음 확인

---

## §11 후속 cycle 항목 (Cycle 12 범위 외)

| 항목 | 분리 cycle |
| --- | --- |
| 디버그 시각화 (`bRenderGeometry/SpawnPoints/Tangents`) | Cycle 12c (Debug Vis) |
| View-aligned ribbon (camera up 기반 perpendicular) | Cycle 12c |
| 머티리얼 lit 통합 (현재 unlit) | Cycle 12c 또는 Material 통합 별도 cycle |
| `MaxParticleInTrailCount` 강제 (trail 당 chain length cap) | Cycle 12c |
| Beam emitter (Cycle 13a/b/c) | Cycle 13 |
| 에디터 UI 의 `URibbonTypeData` 노출 검증 | verify 실행 시 함께 확인 |

---

## 결론 한 줄

> Cycle 12 (Ribbon Emitter) 구현 완료. 11 파일 변경 (신규 6 + 수정 5 + vcxproj 1 pair). Debug x64 빌드 통과 (오류/경고 0). 회귀 안전 11 항목 모두 충족. silent bug 4건 명시 방어 코드 적용. 인게임 verify 만 다음 단계. Mesh (144B) 와 Ribbon (144B) 둘 다 동일 stride 로 container 자동 가산 패턴이 2번째 실측 검증됨.
