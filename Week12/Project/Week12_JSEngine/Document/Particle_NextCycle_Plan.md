# Particle Porting Cycle 6 Plan: RequiredModule Atlas/SubUV Grid → Render Command

작성일: 2026-05-24
대상 브랜치: `feature/ParticleRender`
작업 형태: 단일 영역 (Asset `UParticleModuleRequired` + Builder `EPT_ParticleSystem` case)
상태: **plan-only — 구현 대기 (사용자 "go" 신호 필요)**

---

## 0. 선행 문서

- [Particle_ControlFlow_Diagnosis.md](Document/Particle_ControlFlow_Diagnosis.md) — 본 cycle 선정의 직접 근거
- [Cascade_Porting_Status.md](Document/Cascade_Porting_Status.md) — Cycle 1–5 핸드오프 및 silent bug §7
- [VertexFactory_Cascade_Investigation.md](Document/VertexFactory_Cascade_Investigation.md) — v3 사전 조사

---

## 1. 목표

> `UParticleModuleRequired`에 atlas texture + SubUV grid (Columns/Rows) UPROPERTY를 추가하고, `PrimitiveDrawCommandBuilder`의 `EPT_ParticleSystem` case에서 이 값들을 `FRenderCommand` 신규 필드로 전달한다.

**검증 가능한 성공 기준**:
- atlas texture가 할당된 `UParticleSystem` asset을 component에 셋업 → 런타임에 sprite particle이 atlas grid의 첫 셀(SubUVIndex=0)로 화면에 보임. 색은 per-particle `Color`와 atlas sample의 곱.

---

## 2. 범위

### 포함
- [ ] `UParticleModuleRequired`에 3개 UPROPERTY 추가: `SubUVTexture: UTexture*`, `SubUVColumns: int32`, `SubUVRows: int32` + getter
- [ ] `PrimitiveDrawCommandBuilder::CollectPrimitive` 의 `case EPT_ParticleSystem:` 본문에서 `CurrentLODLevel.RequiredModule` 으로부터 3 값 읽어 `Cmd.ParticleTexture` / `Cmd.ParticleSubUVColumns` / `Cmd.ParticleSubUVRows`에 넣기
- [ ] 기존 `return true` 종결 유지 (silent bug §7-5 회피)

### 명시적 제외 (다음 cycle로 미룸)
- ❌ Per-particle `SubUVIndex` 산출 (`BuildSpriteInstanceData`의 `Data.SubUVIndex = 0` 고정 유지) — 본 cycle 후 별도 cycle (1순위 우선순위 표의 **B**)
- ❌ 별도 `USubUVModule` 클래스 신설 (RequiredModule에 얹는 방식 채택)
- ❌ Rotation/RotationRate 동적 갱신 (**D**)
- ❌ Material 결합 (**E**)
- ❌ 거리순 정렬 (**F**)
- ❌ `ParticleTexture == nullptr` 시 white 1×1 fallback (별도 cycle 가능, silent bug 후보 β)
- ❌ EditorParticleSystemWidget UI 별도 손질 (UPROPERTY 추가만으로 자동 노출되는지는 추론, 실측 시 별도 cycle)

---

## 3. 1순위 cycle 선정 근거

진단 문서 §5 표의 항목 **A** ("Asset(RequiredModule)에 atlas Texture + SubUVColumns/Rows UPROPERTY 추가 → builder가 Cmd로 전달") 선정.

### prompt §1 4규칙 적용

| 규칙 | 점수 | 근거 |
|------|------|------|
| (1) 가장 상류의 끊김 | ✅ 강 | atlas nullptr → PS sample zero → alpha discard → 화면 공백. 이 path 1개가 풀려야 visual 검증 시작 가능. SubUVIndex 산출(B), Rotation(D)는 이 cycle에 dependent. |
| (2) 변경 영역 좁음 | ✅ 강 | 3 파일, 추정 25–35 라인. 신규 .h/.cpp 없음 → vcxproj 안 건드림 (silent bug §7-4 자동 회피). |
| (3) silent bug 함정 비충돌 | ✅ | §7-1~7 어느 것과도 충돌 없음. case 본문 수정 시 `return true` 종결만 유지하면 §7-5 무사. |
| (4) use case (2)+(3) 양쪽 공통 | ✅ | 코드 spawn에서 `RequiredModule->SubUVTexture = T;` 한 줄로 atlas 지정. asset 로드에서는 UPROPERTY 자동 직렬화에 포함. |

### 다른 후보를 미룬 이유

- **B (per-particle SubUVIndex 산출)**: A에 dependent. atlas가 nullptr이면 SubUVIndex가 무엇이든 의미 없음. A 이후 동등하게 1줄 변경으로 가능.
- **C (별도 USubUVModule 신설)**: 신규 .h/.cpp → vcxproj 갱신 필요 (§7-4 함정). RequiredModule에 얹으면 같은 결과를 침습성 더 낮게 달성.
- **D (Rotation 동적 갱신)**: visual 가치 있으나 atlas보다 부차적. atlas 없으면 회전해도 검은 사각형.
- **E (Material 결합)**: 변경 영역 큼 (Material 시스템 연동). 본 cycle 범위 초과.

---

## 4. 진단 문서 §X.Y에서의 실측 근거

### §3.1 (Asset 측) — Missing
- `USubUVModule` 클래스 없음 (grep 0건)
- `UParticleModuleRequired`의 멤버: `MaxParticles`, `EmitterDuration`, `bLooping`, `bUseLocalSpace`, `SubUVName(FName)`, `RenderMode(non-UPROPERTY private)` — [ParticleModules.h:23-39](JSEngine/Source/Engine/Particle/ParticleModules.h:23)
  - **`UTexture*` 필드 없음 / `Columns`/`Rows` 필드 없음**

### §3.4 (Command 브릿지) — Hardcoded
[PrimitiveDrawCommandBuilder.cpp:566-568](JSEngine/Source/Engine/Render/Scene/PrimitiveDrawCommandBuilder.cpp:566):
```cpp
Cmd.ParticleTexture = nullptr; // TODO: SubUV atlas 텍스처 — 모듈 포팅 후 연결
Cmd.ParticleSubUVColumns = 1;
Cmd.ParticleSubUVRows = 1;
```

### §3.5 (GPU 측) — 이미 정상
- VS의 SubUV cell 매핑 로직 (Columns/Rows 사용) 동작 — [SpriteParticle.hlsl:57-64](JSEngine/Shaders/Particle/SpriteParticle.hlsl:57)
- PS의 `SpriteAtlas.Sample(SpriteSampler, TexCoord) * Color` 동작 — [SpriteParticle.hlsl:70-79](JSEngine/Shaders/Particle/SpriteParticle.hlsl:70)
- `PSSetShaderResources(0, Cmd.ParticleTexture->GetSRV())` 코드 동작 — [ParticleRenderPass.cpp:168-173](JSEngine/Source/Engine/Render/Renderer/RenderFlow/ParticleRenderPass.cpp:168)

→ GPU는 "값만 들어오면" 동작 상태. asset → builder path만 잇는 본 cycle이면 visual 검증 가능.

### 패턴 일관성 근거 (`UBillboardComponent::Texture`)
[BillboardComponent.h:62](JSEngine/Source/Engine/Component/BillboardComponent.h:62):
```cpp
UTexture* Texture = nullptr; // ResourceManager 소유, 여기선 참조만
```
→ 본 cycle도 동일 패턴 적용 (raw `UTexture*` + 소유 ResourceManager).

---

## 5. 변경 파일 목록

- [ ] **`JSEngine/Source/Engine/Particle/ParticleModules.h`** (UParticleModuleRequired 클래스만)
  - `class UTexture;` forward 선언 추가 (`#include "Render/Resource/Texture.h"` 대신 forward로 최소화)
  - 3 UPROPERTY 추가: `SubUVTexture`, `SubUVColumns`, `SubUVRows`
  - 3 getter 추가: `GetSubUVTexture()`, `GetSubUVColumns()`, `GetSubUVRows()`
  - **다른 module 클래스 손대지 않음**
  - 추정 라인 추가: +12 라인
- [ ] **`JSEngine/Source/Engine/Particle/ParticleModules.cpp`**
  - **변경 없음 예상** (생성자 본문은 `bSpawnModule = true`만 — 신규 필드 기본값은 헤더에서 처리). 만약 `#include "Render/Resource/Texture.h"`가 필요하면 1줄 추가.
  - 추정 라인 추가: 0–1 라인
- [ ] **`JSEngine/Source/Engine/Render/Scene/PrimitiveDrawCommandBuilder.cpp`**
  - `case EPT_ParticleSystem:` 본문에서 nullptr/1 고정 줄 3개를 RequiredModule 조회로 교체
  - `Cmd.ParticleTexture` / `SubUVColumns` / `SubUVRows` 채우는 식 (LOD/RequiredModule null 가드 포함)
  - `return true` 종결 유지 (silent bug §7-5)
  - `#include "Particle/ParticleSystem.h"` 또는 `ParticleModules.h` 필요 시 추가 (ParticleSystemComponent.h가 이미 포함하지만 RequiredModule 클래스 자체는 ParticleModules.h)
  - 추정 라인 변동: ±10 (3 줄 제거, 10–13 줄 추가)

### 변경 안 함 (명시)
- `ParticleSystemComponent.h/.cpp` — `BuildSpriteInstanceData`의 `SubUVIndex = 0` TODO 유지
- `ParticleRenderPass.h/.cpp` — GPU 경로 이미 동작, 변경 불필요
- `SpriteParticle.hlsl` — 셰이더 변경 없음
- `RenderCommand.h` — 필드 이미 존재, 변경 없음
- `VertexFactoryTypes.h` — 변경 없음
- `vcxproj`/`filters` — 신규 파일 없으므로 안 건드림

---

## 6. 단계별 작업 체크리스트

- [ ] **Step 1**: `ParticleModules.h` 상단에 `class UTexture;` forward 선언 추가
  - 검증: 빌드 시 `UTexture*` 멤버 컴파일 에러 없음
- [ ] **Step 2**: `UParticleModuleRequired` 클래스 private 영역에 3 UPROPERTY 추가:
  ```
  UPROPERTY(DisplayName = "Sub UV Texture")
  UTexture* SubUVTexture = nullptr;
  UPROPERTY(DisplayName = "Sub UV Columns", Min = 1)
  int32 SubUVColumns = 1;
  UPROPERTY(DisplayName = "Sub UV Rows", Min = 1)
  int32 SubUVRows = 1;
  ```
  - 검증: 헤더만 컴파일되어 통과
- [ ] **Step 3**: public 영역에 3 getter 추가 (`GetSubUVTexture`, `GetSubUVColumns`, `GetSubUVRows`) — RequiredModule의 기존 getter 스타일 그대로
  - 검증: 빌드 통과
- [ ] **Step 4**: `PrimitiveDrawCommandBuilder.cpp`의 `case EPT_ParticleSystem:` 본문 수정:
  - emitter 루프 안에서 `Instance->GetCurrentLODLevel()` → null 체크 → `LOD->GetRequiredModule()` → null 체크 → 3 값 read
  - `Cmd.ParticleTexture = Required ? Required->GetSubUVTexture() : nullptr;`
  - `Cmd.ParticleSubUVColumns = Required ? Required->GetSubUVColumns() : 1;`
  - `Cmd.ParticleSubUVRows = Required ? Required->GetSubUVRows() : 1;`
  - 기존 `return true` 종결 위치 유지
  - 검증: 빌드 통과, 기존 EPT_ParticleSystem case가 fall-through로 떨어지지 않음
- [ ] **Step 5**: include 정리 — `PrimitiveDrawCommandBuilder.cpp` 상단에 `#include "Particle/ParticleSystem.h"` 이미 포함된 `ParticleSystemComponent.h`가 transitive로 제공할 가능성 높음. 필요 시 `ParticleModules.h` 명시 include 추가.
  - 검증: 빌드 통과, RequiredModule 타입 인식
- [ ] **Step 6**: 빌드 전체 통과 확인 (Debug|x64, 경고 0, 오류 0)

---

## 7. 검증 시퀀스

### 7.1 빌드 검증
- [ ] `msbuild JSEngine.sln /p:Configuration=Debug /p:Platform=x64` → 경고/오류 0
- [ ] Cycle 1에서 추가된 `HashVertexLayout`이 영향 받지 않는지 확인 (이번엔 vertex layout 변경 없음, OK)

### 7.2 코드 spawn 검증 (use case 2)
- [ ] 임시 `EnsureFallbackTemplate()` 또는 BeginPlay 코드:
  ```cpp
  UParticleSystem* PS = UParticleSystem::CreateDefaultSpriteSystem();
  UTexture* Atlas = FResourceManager::Get().GetTexture("path/to/sprite.png");  // 기존 helper 활용
  if (auto* Required = PS->Emitters[0]->LODLevels[0]->RequiredModule)
  {
      Required->SubUVTexture = Atlas;   // private라면 setter 또는 friend 또는 임시 public
      Required->SubUVColumns = 1;
      Required->SubUVRows = 1;
  }
  SetTemplate(PS);
  ```
- [ ] 액터 배치 후 실행 → 액터 위치 근처에 sprite 다수 표시 (Color×Atlas)
- [ ] **확인 사항**: 화면에 뭔가 보이면 성공. 안 보이면 §7.5 분기점 좁히기.

### 7.3 디버거 watch
- [ ] `Cmd.ParticleTexture != nullptr` (PrimitiveDrawCommandBuilder.cpp `EPT_ParticleSystem` case 출구)
- [ ] `Cmd.ParticleSubUVColumns / Rows` == 셋업한 값
- [ ] `FParticleRenderPass::DrawCommand` 안에서 `TextureSRV != nullptr`

### 7.4 RenderDoc (정밀, 선택)
- [ ] GPU Event List에 `"RenderPass.Particle"` 마커
- [ ] `DrawIndexedInstanced(6, ActiveParticles, ...)` 매 프레임 1+회
- [ ] PS 단계 `t0` SRV 슬롯에 atlas 텍스처 binding
- [ ] cbuffer b8 (VS)에 `SubUVColumns=N, SubUVRows=M` 값 확인

### 7.5 화면 안 보일 때 분기점 좁히기 (status.md §5.4 참조)
| 단계 | 위치 | 확인 |
|------|------|------|
| 1 | `PrimitiveDrawCommandBuilder.cpp` 본 cycle 수정 부 | `Required != nullptr`, atlas != nullptr |
| 2 | `Cmd.ParticleTexture` 채워진 직후 | 값 확인 |
| 3 | `ParticleRenderPass.cpp:168` `TextureSRV` | nullptr이면 atlas 로드 실패 |
| 4 | RenderDoc PS sample | 검은 픽셀이면 atlas 자체가 검정 |
| 5 | alpha discard | 화면 안 보임 ↔ atlas alpha 채널 0 가능성 |

---

## 8. 회피해야 할 silent bug

`Cascade_Porting_Status.md §7`의 7개 함정 중 관련 항목:

- **§7-1 (`Registry::Get` default StaticMeshDesc)**: 본 cycle에서 `EVertexFactoryType` 추가 없음 → 무관
- **§7-2 (`HashVertexLayout` 동기화)**: vertex layout 변경 없음 → 무관
- **§7-3 (`PickPasses[]` Particle 절대 추가 금지)**: 본 cycle에서 PickPasses 안 건드림. ⚠️ **future 작업자가 Particle을 outline에 보이게 만들고 싶다고 ParticleSystemComponent::SupportsOutline()을 true로 바꾸면 silent bug 발생**. 본 cycle에서는 변경 없음.
- **§7-4 (`vcxproj` 외부 수정)**: ⚠️ **본 cycle은 신규 파일 없음 → vcxproj 안 건드림으로 회피**. 만약 작업 중 VS가 vcxproj를 자동 갱신하지 않도록 주의.
- **§7-5 (`EPT_ParticleSystem` case 명시 `return true` 종결)**: ⚠️ **본 cycle은 이 case 본문을 수정하므로 직접 위험**. Step 4 수정 후 case 출구가 `return true`로 종결되는지 (그리고 default로 fall-through하지 않는지) 명시 확인.
- **§7-6 (`SupportsOutline` 가드)**: 변경 없음 → 무관
- **§7-7 (`PassBatchers[Particle]` 미등록)**: 변경 없음 → 무관

### 진단 문서 §7 신규 silent bug 후보 중 본 cycle 관련
- **후보 β** (`ParticleTexture == nullptr` 시 PS sample zero → discard → 화면 공백): 본 cycle은 atlas를 채울 path를 잇지만, **셋업이 잘못되어 SubUVTexture가 nullptr인 상태로 액터를 배치하면** 여전히 화면에 안 보임. 즉, "본 cycle 적용 후에도 atlas 셋업이 빠지면 디버깅 어려움" 함정은 남는다. fallback white 1×1 texture는 별도 cycle에서 처리 가능 (본 cycle 범위 외).

---

## 9. 이번 cycle에서 명시적으로 안 다루는 것

| 항목 | 다음 cycle 후보 우선순위 | 이유 |
|------|------------------------|------|
| Per-particle SubUVIndex 산출 (RelativeTime → frame index) | **B** (본 cycle 직후) | 본 cycle 의 atlas가 들어와야 검증 가능 |
| `ParticleTexture == nullptr` 시 white 1×1 fallback | low | 디버깅 편의성 — 본 cycle 검증 후 필요 판단 |
| Rotation/RotationRate 동적 갱신 | D | visual 가치 있지만 후순위 |
| Material 결합 | E | 변경 영역 큼 |
| 거리순 정렬 | F | alpha 정확성, 시각 차이 작음 |
| 별도 `USubUVModule` 클래스 신설 (정통 Cascade 호환) | C | RequiredModule에 얹는 게 침습성 낮음 — 후속 리팩토링 가능 |
| EditorParticleSystemWidget UPROPERTY 자동 노출 검증 | (보조) | 추론 4에 따르면 자동 노출 가능성 높음 — 실측은 cycle 후 |
| `Required->SubUVTexture` private setter (코드 spawn 편의) | (보조) | UPROPERTY로 reflection 접근하거나 friend, 본 cycle 검증 시 friend 또는 임시 public 검토 |

---

## 10. Commit 후보 메시지

```
[ParticleRender] Cycle 6: Wire RequiredModule atlas/SubUV grid → render command

- Add 3 UPROPERTY to UParticleModuleRequired: SubUVTexture (UTexture*),
  SubUVColumns / SubUVRows (int32, default 1) + getters.
- PrimitiveDrawCommandBuilder EPT_ParticleSystem case now reads these
  from CurrentLODLevel->RequiredModule and fills Cmd.ParticleTexture /
  ParticleSubUVColumns / ParticleSubUVRows instead of nullptr/1.
- Existing return true terminator preserved (silent bug §7-5).
- No vcxproj change (no new files).
- Per-particle SubUVIndex still 0 (TODO in BuildSpriteInstanceData) —
  next cycle.

Verification:
- Build Debug|x64, 0 warnings, 0 errors.
- Setup default sprite system + assign Required->SubUVTexture → sprites
  now render with atlas sample × per-particle Color (visible on screen).
```

---

## 11. 추정 규모 요약

| 항목 | 추정 |
|------|------|
| 변경 파일 수 | 2–3 (헤더 1, cpp 1–2) |
| 추가 라인 | +20 ~ +30 |
| 삭제 라인 | -3 (기존 nullptr/1 고정 줄) |
| 신규 .h/.cpp | 0 |
| vcxproj 변경 | 없음 |
| 셰이더 변경 | 없음 |
| 의존 cycle | 없음 (Cycle 1–5 인프라 위) |
| 검증 시간 추정 | 30분 (빌드 + 코드 spawn 셋업 + 실행 확인) |

---

## 12. 사용자 승인 요청

본 plan대로 구현 cycle (Cycle 6)에 진입해도 되는지 확인 부탁드립니다.

**대안 후보**:
- 별도 `USubUVModule` 클래스로 분리하는 정통 Cascade 호환 방식 (cycle C로 분리해서 진행)
- atlas null fallback (white 1×1)을 본 cycle에 묶기 (검증 안정성 향상, 변경 +5 라인)
- 본 cycle 대신 B (per-particle SubUVIndex 산출)를 먼저 (단, A 없으면 visual 검증 불가하므로 비추천)
