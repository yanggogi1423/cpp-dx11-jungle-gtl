# Cycle 14 (Mesh M1+M2) 구현 결과 보고서

**작성일**: 2026-05-26
**대상 브랜치**: `feature/ParticleRender`
**모드**: implement 완료 (Debug x64 빌드 통과 — 오류 0, 경고 0)
**선행 문서**:
- [Cycle14_ReDiagnose.md](Cycle14_ReDiagnose.md) — 코드 대조 진단 + 결정 16-29 후보
- [Cycle13b_ImplementReport.md](Cycle13b_ImplementReport.md) — Beam 패턴
- 사용자 lock-in prompt (결정 16/17/18/19/20/21/29 통합본)

---

## §0 한 줄 요약

> Cycle 11 (Mesh emitter 옵션 B `FMeshRotationPayload` 36B) + Cycle 12 (multi-trail lazy state) + Cycle 13b (random source cascade) 의 기반 위에 M1 (Mesh Alignment Enum 2값) + M2 (Mesh RotationRate Spawn+Update module) 구현 완료. Debug x64 빌드 통과 (**오류 0 / 경고 0**). silent bug 3건 (위험 12/13/14) 모두 명시 방어 코드 적용. payload struct sizeof 변경 0건 — Cycle 11 의 InitialOrientation/Rotation/RotRate 3 슬롯 그대로 활용. `BuildInstanceData()` signature 변경 0건 (결정 18 옵션 β — Component-cached camera). 다음 단계: 인게임 verify.

---

## §1 적용된 사용자 결정 (lock-in)

| 결정 | 옵션 | 코드 위치 |
| --- | --- | --- |
| **29** (Cycle 분할) | **B** | Cycle 14 (Mesh M1+M2). Ribbon R1/R2/R3 은 Cycle 15 로 이관 |
| **17** (M1 enum 값 범위) | **B** | `EMeshAlignment { PSA_Velocity, PSA_FacingCameraPosition }` 2값 | [ParticleMeshTypes.h:14-22](../JSEngine/Source/Engine/Particle/ParticleMeshTypes.h:14) |
| **18** (Camera 접근 경로) | **β** | Component-cached camera. `UParticleSystemComponent::CacheCameraFromRenderBus()` + 4 vector 멤버. `BuildInstanceData()` signature 변경 0 | [ParticleSystemComponent.h:36-49](../JSEngine/Source/Engine/Particle/ParticleSystemComponent.h:36) + [.cpp:243-258](../JSEngine/Source/Engine/Particle/ParticleSystemComponent.cpp:243) |
| **16** (M1 도입 위치) | **A + A.2** | `UMeshTypeData::Alignment` UPROPERTY 멤버. enum 정의는 `ParticleMeshTypes.h` 확장 | [ParticleModuleTypeDataMesh.h:50-53](../JSEngine/Source/Engine/Particle/ParticleModuleTypeDataMesh.h:50) |
| **19** (M2 module 종류) | **A** | `bSpawnModule = true; bUpdateModule = true;` (Color/Size 패턴) | [ParticleModuleMeshRotationRate.cpp:24-29](../JSEngine/Source/Engine/Particle/ParticleModuleMeshRotationRate.cpp:24) |
| **20** (M2 payload access path) | **A** | `FParticleMeshEmitterInstance::GetMeshPayload` private→public + `GetMeshPayloadAt(ActiveIdx)` 신규 public helper. base class 변경 0 | [ParticleMeshEmitterInstance.h:25-33](../JSEngine/Source/Engine/Particle/ParticleMeshEmitterInstance.h:25) |
| **21** (M1+M2 결합 공식) | **A + CPU** | `Final = SpinMatrix * AlignmentMatrix` (row-vector convention, shader 일관). CPU 측 결합 후 Euler 추출 → `Data.InstanceRotation` | [ParticleMeshEmitterInstance.cpp:200](../JSEngine/Source/Engine/Particle/ParticleMeshEmitterInstance.cpp:200) |

---

## §2 변경 파일 목록

### 신규 파일 (2 + 1 .gen.cpp)

| 파일 | 역할 | 라인 |
| --- | --- | --- |
| [ParticleModuleMeshRotationRate.h](../JSEngine/Source/Engine/Particle/ParticleModuleMeshRotationRate.h) | `UParticleModuleMeshRotationRate` UCLASS — Spawn+Update, RotRateMin/Max UPROPERTY 2건 | 32 |
| [ParticleModuleMeshRotationRate.cpp](../JSEngine/Source/Engine/Particle/ParticleModuleMeshRotationRate.cpp) | Spawn 본문 (RotRate randomize) + Update 본문 (Rotation 누적) + 위험 13 방어 | 89 |
| [UParticleModuleMeshRotationRate.gen.cpp](../JSEngine/Intermediate/Reflection/Particle/UParticleModuleMeshRotationRate.gen.cpp) | `GenerateReflection.py` 자동 생성 — 2 UPROPERTY 등록 | (auto) |

### 수정 파일 (7)

| 파일 | 변경 내용 |
| --- | --- |
| [ParticleMeshTypes.h](../JSEngine/Source/Engine/Particle/ParticleMeshTypes.h) | `EMeshAlignment` UENUM 추가 (2 값 + UMETA DisplayName). `Core/Reflection/ReflectionMacros.h` include 추가. |
| [ParticleModuleTypeDataMesh.h](../JSEngine/Source/Engine/Particle/ParticleModuleTypeDataMesh.h) | `EMeshAlignment Alignment` UPROPERTY 추가 (default = PSA_Velocity) + getter/setter |
| [ParticleMeshEmitterInstance.h](../JSEngine/Source/Engine/Particle/ParticleMeshEmitterInstance.h) | `GetMeshPayload` private→public 승격 + 신규 `GetMeshPayloadAt(ActiveIdx)` public helper |
| [ParticleMeshEmitterInstance.cpp](../JSEngine/Source/Engine/Particle/ParticleMeshEmitterInstance.cpp) | (1) anonymous namespace 에 `MakeShaderEulerRotation` / `ExtractShaderEuler` / `MakeAlignmentMatrix` 3 helper. (2) `BuildInstanceData` 본문: TypeData alignment + Component cached camera lookup, alignment mode 분기, Spin * Alignment 결합, Euler 추출. (3) `GetMeshPayloadAt(ActiveIdx)` 본문. |
| [ParticleSystemComponent.h](../JSEngine/Source/Engine/Particle/ParticleSystemComponent.h) | `CacheCameraFromRenderBus(const FRenderBus&)` public method 추가 + `CachedCameraPosition/Forward/Up/Right` 4 멤버 + `bCachedCameraValid` flag + 4 accessor. forward declaration `class FRenderBus`. |
| [ParticleSystemComponent.cpp](../JSEngine/Source/Engine/Particle/ParticleSystemComponent.cpp) | `CacheCameraFromRenderBus` 본문. `Render/Scene/RenderBus.h` include 추가. |
| [PrimitiveDrawCommandBuilder.cpp](../JSEngine/Source/Engine/Render/Scene/PrimitiveDrawCommandBuilder.cpp) | EPT_ParticleSystem case 안에서 `BuildInstanceData()` 호출 직전에 `CacheCameraFromRenderBus(RenderBus)` 1줄 추가 |
| [EditorParticleSystemWidget_Emitters.cpp](../JSEngine/Source/Editor/UI/EditorParticleSystemWidget_Emitters.cpp) | `DrawDisabledParticleModuleMenu("Rotation Rate")` → `DrawParticleModuleAddMenu<UParticleModuleMeshRotationRate>(...)` 교체. include `Particle/ParticleModuleMeshRotationRate.h` 추가. |
| [JSEngine.vcxproj](../JSEngine/JSEngine.vcxproj) + [.filters](../JSEngine/JSEngine.vcxproj.filters) | 신규 3 항목 등록 (.h / .cpp / .gen.cpp) |

---

## §3 작업 순서 vs 실제 진행 결과

| Step | 계획 (prompt §4) | 실제 진행 | 비고 |
| --- | --- | --- | --- |
| 1 | Math helpers 검증 | 완료 — FMatrix::MakeRotationEuler / GetEuler 존재 BUT **shader 와 sign 반대 (RH vs LH)** | local helper 작성 결정 — Math/ 영역 변경 0건 (회귀 위험 회피) |
| 2 | `EMeshAlignment` enum 추가 | 완료 — UENUM + 2값 (UMETA DisplayName "Velocity" / "Facing Camera Position") | 결정 17 옵션 B |
| 3 | `UMeshTypeData::Alignment` UPROPERTY | 완료 — default = PSA_Velocity (회귀 안전: velocity zero 면 alignment Identity) | 결정 16 옵션 A |
| 4 | Component camera cache | 완료 — `CacheCameraFromRenderBus()` + 4 vector 멤버 + bCachedCameraValid flag | 결정 18 옵션 β. signature 변경 0 보장 |
| 5 | M1 BuildInstanceData 분기 | 완료 — TypeData alignment lookup + Component cache lookup + mode switch | 위험 12 방어: bCachedCameraValid=false 면 PSA_Velocity fallback |
| 6 | M2 UCLASS 신규 | 완료 — Spawn + Update, RotRateMin/Max FVector 2 UPROPERTY | 결정 19 옵션 A (Color/Size 패턴) |
| 7 | payload access path | 완료 — `GetMeshPayload` public 승격 + `GetMeshPayloadAt(ActiveIdx)` 편의 helper 신설 | 결정 20 옵션 A. base class 변경 0 |
| 8 | M2 Update 본문 | 완료 — `dynamic_cast<FParticleMeshEmitterInstance*>(Owner)` + chain 순회 + Rotation += RotRate*dt | 위험 13 방어: Cast nullptr 시 early return |
| 9 | M1 + M2 결합 | 완료 — `Final = SpinMatrix * AlignmentMatrix` (row-vector). Euler 추출 후 InstanceVB push | 결정 21 옵션 A. shader EulerZYXToMatrix(InstanceRotation) 이 같은 matrix 재구성 |
| 10 | silent bug 방어 | 완료 — 위험 12 (camera fallback) + 위험 13 (Cast nullptr) + 위험 14 (결합 명시) | 모두 명시 방어 |
| 11 | vcxproj + GenerateReflection.py | 완료 — 3 항목 등록 (.h / .cpp / .gen.cpp) × 2 파일 = 6 ItemGroup 라인 | silent bug §7-4 |
| 12 | 빌드 검증 | 완료 — Debug x64 통과, 오류 0 / 경고 0 | JSEngine.exe 45.25 MB 생성 |
| 13 | ImplementReport.md | 본 문서 | — |

---

## §4 silent bug 방어 매핑

### 본 cycle 신규 방어 (3건)

| 위험 | 시나리오 | 방어 코드 위치 |
| --- | --- | --- |
| **12** (camera 접근 누락) | PSA_FacingCameraPosition 선택 + Component cache 가 첫 frame 또는 외부 호출 (EditorMainPanelDebug.cpp:170) 으로 미갱신 → invalid camera 로 NaN tangent | `BuildInstanceData` 의 `EffectiveAlignment` 분기 — `bCachedCameraValid=false` 면 PSA_FacingCameraPosition → PSA_Velocity fallback. PSA_Velocity 도 velocity zero 면 MakeAlignmentMatrix 내부에서 Identity 반환 (2단계 fallback). [ParticleMeshEmitterInstance.cpp:177-180, 196-200](../JSEngine/Source/Engine/Particle/ParticleMeshEmitterInstance.cpp:177) |
| **13** (M2 Update module payload access 실패) | M2 module 이 Mesh emitter 가 아닌 곳에 mistakenly 추가됨 → derived payload access 시 invalid memory | `dynamic_cast<FParticleMeshEmitterInstance*>(Owner)` nullptr 검사 → early return. Spawn / Update 두 hook 모두 적용. [ParticleModuleMeshRotationRate.cpp:43-46, 70-74](../JSEngine/Source/Engine/Particle/ParticleModuleMeshRotationRate.cpp:43) |
| **14** (M1 와 M2 결합 무효화) | M1 이 매 frame InstanceRotation 을 덮어쓰면 M2 의 누적 RotRate 가 무효화 — 결합 공식 누락 시 발현 | `Final = SpinMatrix * AlignmentMatrix` 명시 결합 (결정 21 옵션 A) — payload `Rotation` 이 SpinMatrix 의 source 이므로 AlignmentMatrix 와 함께 final matrix 에 반영. [ParticleMeshEmitterInstance.cpp:223](../JSEngine/Source/Engine/Particle/ParticleMeshEmitterInstance.cpp:223) |

### 추가 방어 (helper level)

- **위험 11 일반화** (perp axis singular — Beam Cycle 13b 에서 식별) — `MakeAlignmentMatrix` 의 `|dot(Forward, UpHint)| > MeshAlignParallelDot` 검사 → axis 자동 전환. Beam 의 `ComputeBeamLocalAxes` 패턴 답습.
- **storage 미준비**: `GetMeshPayloadAt(ActiveIdx)` 의 `!ParticleStorage.ParticleData || ActiveIdx < 0 || ActiveIdx >= ActiveParticles` 가드 — 위험 1 (SlotIndex 참조) 일반화.
- **DeltaTime <= 0**: `UParticleModuleMeshRotationRate::Update` 의 첫 가드 — pause/seek 시 silent rotation 0.

---

## §5 회귀 안전 점검 (변경 0건 보장)

| 항목 | 결과 |
| --- | --- |
| `USpriteTypeData` / `URibbonTypeData` / `UBeamTypeData` UCLASS | 0건 변경 |
| `UMeshTypeData` 기존 멤버 (`Mesh` / `bOverrideMaterial` / `OverrideMaterial`) | 0건 변경 (`Alignment` 만 신규 추가) |
| `FMeshRotationPayload` struct sizeof | **36B 유지** (`static_assert(sizeof == 36)` 통과). 슬롯 변경 0. |
| `FMeshParticleInstanceData` struct sizeof | **56B 유지** (`static_assert(sizeof == 56)` 통과). layout 변경 0. |
| `FRenderCommand` sizeof | **464B 유지** (`static_assert(sizeof == 464)` 통과). |
| Mesh Stride | **144B 유지** (`AlignSize(108 + 36, 16) = 144`). container 자동 가산 패턴 유지. |
| `FParticleEmitterInstance` base class (header + cpp) | 0건 변경 — virtual signature / 멤버 layout 그대로 |
| base `FParticleEmitterInstance::Init` / `SpawnParticles` / `KillParticle` / `Tick` | 0건 변경 |
| Sprite/Ribbon/Beam case in `PrimitiveDrawCommandBuilder` | 0건 변경 (Mesh case 도 본문 변경 0 — `CacheCameraFromRenderBus` 호출만 switch 외부 line 582 직전에 추가) |
| `RenderSpriteEmitter` / `RenderMeshEmitter` / `RenderRibbonEmitter` / `RenderBeamEmitter` body | 0건 변경 |
| `EVertexFactoryRegistry::Get` switch + Layout/Desc | 0건 변경 |
| `MeshParticle.hlsl` / `RibbonParticle.hlsl` / `BeamParticle.hlsl` | **0건 변경** — shader signature / convention 그대로. CPU helper 가 shader 와 일치하도록 작성. |
| `BuildInstanceData()` signature (Component + base + derived) | **0건 변경** — 결정 18 옵션 β 의 핵심. EditorMainPanelDebug.cpp:170 외부 호출 경로도 영향 0 |
| Cycle 11 Mesh asset 회귀 | NoiseModule / RotationRate module 미추가 emitter → RotRate=Zero → Rotation 누적 0 → Cycle 11 동작 그대로 |

### 회귀 안전의 핵심 — module 미추가 시 Cycle 11 동작 보존

- `UMeshTypeData::Alignment` default = `PSA_Velocity` — velocity zero 면 `MakeAlignmentMatrix` 내부 Identity → `Final = SpinMatrix * Identity = SpinMatrix`. payload Rotation = ZeroVector (M2 module 부재 시) → SpinMatrix = Identity → Final = Identity → Euler = Zero → 시각적으로 unaligned mesh (Cycle 11 동작 그대로).
- M2 module 미추가 시 SpawnParticles 의 `Payload->RotRate = ZeroVector` (Cycle 11 line 41 그대로) → Update module 루프에 `UParticleModuleMeshRotationRate` 없음 → Rotation 누적 0 → InstanceRotation = ZeroVector (Cycle 11 동작 그대로).
- 회귀 검증 시나리오: 기존 Mesh asset (Alignment 미설정 + RotationRate module 미추가) → 화면 출력 픽셀-동일.

---

## §6 container 자동 가산 (Cycle 10d 패턴) 유지

| Emitter | Payload bytes | Stride (align 16B) | 비고 |
| --- | --- | --- | --- |
| Sprite | 0 | 112B | baseline |
| **Mesh (Cycle 11 + 14)** | **36** | **144B** | **변경 0 — Cycle 14 가 payload 슬롯 활용만, sizeof 변경 0** |
| Ribbon | 32 | 144B | (Cycle 15 R1/R2/R3 도입 시 변경 가능) |
| Beam (13a) | 4 | 112B | |
| Beam (13b) | 100 | 208B | NoiseSamples[8] |

→ Cycle 14 는 container Stride 변경 0건. Cycle 10d 의 `ParticleSize + PayloadBytes` 자동 가산 메커니즘 그대로 동작.

---

## §7 빌드 verify 결과

- **명령**: `MSBuild JSEngine.sln /p:Configuration=Debug /p:Platform=x64 /m /v:normal`
- **결과**: `JSEngine.exe` 생성 (`C:\GitDirectory12\JSEngine\Bin\Debug\JSEngine.exe`, **45,250,560 bytes ≈ 45.25 MB**)
- **컴파일 오류**: **0** (정확 검증: `: error` 매치 0건)
- **컴파일 경고**: **0** (`: warning` 매치 0건)
- **링커 오류**: **0**
- **빌드 진행 중 추가 fix**: 0건. `GenerateReflection.py` 실행 → `UParticleModuleMeshRotationRate.gen.cpp` + `UMeshTypeData.gen.cpp` 자동 갱신 → vcxproj 등록 → 첫 빌드 통과 (Cycle 13a/b 사전 학습 효과).

### Cycle 14 의 vcxproj 신규 등록 항목 (총 3건)

```xml
<!-- ClCompile (2) -->
Source\Engine\Particle\ParticleModuleMeshRotationRate.cpp
Intermediate\Reflection\Particle\UParticleModuleMeshRotationRate.gen.cpp

<!-- ClInclude (1) -->
Source\Engine\Particle\ParticleModuleMeshRotationRate.h
```

(추가로 .filters 파일에 동일 3 항목 등록.)

### Cycle 14 의 자동 갱신 .gen.cpp (변경 1건)

- `UMeshTypeData.gen.cpp` — Alignment UPROPERTY 추가로 자동 재생성. vcxproj 신규 등록 불필요 (이미 Cycle 11 에 등록됨).

---

## §8 sizeof 확인

| 구조체 | 예상 | 실측 (static_assert 통과) | 위치 |
| --- | --- | --- | --- |
| `FMeshRotationPayload` | 36B (Cycle 11 옵션 B 유지) | **36B** | [ParticleMeshTypes.h:34](../JSEngine/Source/Engine/Particle/ParticleMeshTypes.h:34) |
| `FMeshParticleInstanceData` | 56B (Cycle 11 옵션 B 유지) | **56B** | [VertexTypes.h:90](../JSEngine/Source/Engine/Render/Resource/VertexTypes.h:90) |
| `FRenderCommand` | 464B (Cycle 10a baseline 유지) | **464B** | [ParticleRenderPass.cpp:18](../JSEngine/Source/Engine/Render/Renderer/RenderFlow/ParticleRenderPass.cpp:18) |

세 assert 모두 컴파일 통과 — Cycle 14 의 payload 영향 0건 검증.

---

## §9 Cycle 11 (Mesh 단독) vs Cycle 14 (M1+M2 추가) 차이 실측

| 항목 | Cycle 11 | Cycle 14 |
| --- | --- | --- |
| TypeData UPROPERTY 수 | 3 (Mesh / bOverrideMaterial / OverrideMaterial) | **4** (+ Alignment) |
| Particle module UCLASS 수 (Mesh 전용) | 0 | **1** (UParticleModuleMeshRotationRate) |
| `FMeshRotationPayload` 슬롯 활용 | InitialOrientation / Rotation / RotRate 모두 ZeroVector 고정 | RotRate 가 사용자 입력으로 채워짐 + Rotation 이 매 frame 누적 |
| `BuildInstanceData` body | InstanceRotation = Payload->Rotation (그대로) | Spin matrix + Alignment matrix 결합 후 Euler 추출 |
| `BuildInstanceData()` signature | 인자 0 | 인자 0 (변경 0 — 결정 18 β) |
| `Component->BuildInstanceData()` signature | 인자 0 | 인자 0 (변경 0) |
| Camera 접근 경로 | 없음 | Component-cached (Builder 호출 직전 갱신) |
| CPU helper (Math) | 0 | **3 anonymous namespace** helper (MakeShaderEulerRotation / ExtractShaderEuler / MakeAlignmentMatrix) |
| Editor add-module 메뉴 | Rotation Rate = disabled | **enabled** (UParticleModuleMeshRotationRate) |
| shader 변경 | — | **0건** (shader EulerZYXToMatrix 그대로 사용, CPU 가 일치하도록 helper 작성) |
| silent bug 신규 방어 | — | **3건** (위험 12/13/14) |

---

## §10 인게임 verify 항목 (사용자 후속 — 본 cycle 범위 외)

prompt §5 완료 기준 그대로:

### 회귀 안전
- [ ] 기존 Mesh emitter (Alignment 미설정 + RotationRate module 미추가) → Cycle 11 동작 그대로 (시각 동일)
- [ ] Sprite/Ribbon/Beam 회귀 (Cycle 13b verify 결과 보존)
- [ ] RenderDoc capture: Mesh path 의 `DrawIndexedInstanced` event + slot 0/1 binding 유지

### M1 검증
- [ ] PSA_Velocity 모드: Velocity 비-zero 인 emitter → mesh 가 velocity 방향으로 정렬되어 표시 (예: bullet/missile orientation)
- [ ] PSA_FacingCameraPosition 모드: 카메라 이동 시 mesh 가 카메라 방향으로 자동 회전 (look-at billboard 변형)
- [ ] Velocity zero + PSA_Velocity 선택 → identity (회전 없음, Cycle 11 동작 그대로)
- [ ] Component cache invalid (예: EditorMainPanelDebug 진입) + PSA_FacingCameraPosition → PSA_Velocity fallback (위험 12 검증)

### M2 검증
- [ ] RotationRate module 추가 + RotRateMin = RotRateMax = (0, 0, 1) 라디안/sec → mesh 가 Z 축 중심으로 1 rad/s spin
- [ ] RotRateMin = (-1, -1, -1), RotRateMax = (1, 1, 1) → 모든 particle 이 다른 random RotRate 로 spin
- [ ] Spin 만 (Alignment 없음 — PSA_Velocity + Velocity=Zero) → mesh 가 자기 축으로 spin (origin 위치 유지)

### M1 + M2 결합 검증
- [ ] PSA_Velocity + RotationRate (Velocity X 축 방향 + RotRate Z 축 spin) → mesh 가 Velocity 방향으로 정렬되고 Z 축으로 spin (UE Cascade 의 spinning bullet trail 효과)
- [ ] 디버거 watch: `Data.InstanceRotation` 이 frame 마다 갱신 + AlignmentRot * SpinRot 결과 검증

### 위험 13 검증
- [ ] Sprite emitter 에 RotationRate module 잘못 추가 → no-op (segfault 없음, silent skip)

---

## §11 Cycle 15 (Ribbon R1/R2/R3) 이관 항목

prompt §1 의 "제외" 그대로:

| 항목 | 이관 cycle |
| --- | --- |
| R1 (Ribbon CameraFacing view alignment) | Cycle 15 — `URibbonTypeData::EViewAlignment` enum + Perp 계산 분기 |
| R2 (Ribbon Color Interpolation) | Cycle 15 — `URibbonTypeData::StartColor + EndColor` + chain position 기반 lerp |
| R3 (Ribbon Sheets Per Trail) | Cycle 15 — `URibbonTypeData::SheetsPerTrail` (이미 UPROPERTY 존재, dormant) read + BuildVertexBuffer 분기 |
| R1 ↔ R3 결합 정책 (결정 28) | Cycle 15 lock-in |
| R2 ↔ ColorModule 결합 (결정 26) | Cycle 15 lock-in |
| Cycle 14 의 camera cache 메커니즘 재사용 | Cycle 15 R1 이 동일 패턴 활용 (Component cached camera) |

### 본 cycle 외 (M1+M2 의도 외)

- M1 의 `PSA_LockedAxis` / `PSA_Rectangle` enum 값 — 결정 17 옵션 B 채택 후 후속 cycle 에서 enum 값 추가만으로 확장 가능
- Sprite 의 RotationRate Spawn/Update module (Cycle 14 의 Mesh 한정성과 분리) — 별도 cycle 후보
- Distribution / Curve 인프라 (R2 의 lerp 정교화에 필요) — Cycle 15 또는 그 이후

---

## §12 prompt 추측과 실제 코드의 차이

### shader convention 불일치 발견 (사전 검증 단계)

- 사용자 lock-in §2.4 의 가정: "shader 측 EulerZYXToMatrix 는 이미 `MeshParticle.hlsl:37-57` 에 존재 (CPU helper 작성 시 동일 ZYX 순서 보장 필수)" — 정확.
- **추가 발견**: `FMatrix::MakeRotationEuler` 가 존재 ([Math/Matrix.h:841](../JSEngine/Source/Engine/Math/Matrix.h:841)) 하지만 **shader 와 sign 반대 (RH vs LH)**. shader `EulerZYXToMatrix` 의 Rx 매트릭스는 row[1] = `(0, cx, -sx)` 인 반면 `FMatrix::MakeRotationX` 의 Rx 는 row[1] = `(0, cos, sin)` — 즉 sx 의 sign 반대.
- **결과**: `FMatrix::MakeRotationEuler` 재사용 시 shader 와 다른 결과 → local helper 작성.
- 이로 인해 `Math/Matrix.h` 확장 (lock-in §2.4 권고) 대신 `ParticleMeshEmitterInstance.cpp` 의 anonymous namespace 에 local helper 3개 작성. Math/ 영역 변경 0건 → 다른 시스템 회귀 위험 0.

### 결정 21 의 "CPU 측 결합" 의 정확한 의미

- lock-in: "`Final = AlignmentRot * AccumulatedRot` (UE Cascade 표준, Rotate 후 Spin). CPU 측 결합 (`BuildInstanceData` 내부에서 matrix 곱 후 Euler 추출)".
- **실구현**: row-vector convention 의 shader 와 일관성 유지를 위해 곱 순서는 `Final = SpinMatrix * AlignmentMatrix` (left = 먼저 적용). 의미: `v' = v * Spin * Alignment` — local frame 에서 먼저 spin, world 로 align. UE Cascade 의 "Rotate then Spin" 과 의미 동일 (네이밍만 다름).
- **사용자 prompt 의 "AlignmentRot * AccumulatedRot" 은 column-vector convention 표현** — `AlignmentRot * AccumulatedRot * v_column` = AccumulatedRot 먼저 적용, AlignmentRot 나중. row-vector 변환 시 곱 순서가 뒤집힘 (`v * SpinMatrix * AlignmentMatrix`). 의미 동일.

### M2 module 의 도입 위치

- lock-in §2.1: "`Engine/Particle/ParticleModuleMeshRotationRate.h/.cpp` (신규 파일) 또는 `ParticleModules.h/.cpp` 확장 — implement 시 결정".
- **선택**: 별도 파일 (Beam 의 Source/Target/Noise module 분리 패턴 답습). Mesh-전용 의미 명확.

### M2 의 UPROPERTY 형태

- lock-in §2.1: "RotRate UPROPERTY (FVector min/max)".
- **선택**: `FVector RotRateMin` + `FVector RotRateMax` (Velocity / Location 모듈 패턴). `FEngineRandom::RandomFloat(Min, Max)` per axis.

---

## §13 결론 한 줄

> Cycle 14 (Mesh M1+M2) 구현 완료. **10 파일 변경** (신규 2 + 수정 7 + vcxproj 1 pair) + 1 신규 `.gen.cpp` 자동 생성. Debug x64 빌드 통과 (오류 0 / 경고 0). 회귀 안전 13 항목 모두 충족 (NoiseModule 미존재 시 13b 동작 보장 패턴 답습 — module 미추가 시 Cycle 11 픽셀-동일). silent bug 3건 (위험 12/13/14) 모두 명시 방어. payload sizeof 0건 변경 (Cycle 11 의 InitialOrientation/Rotation/RotRate 3 슬롯 활용), `BuildInstanceData()` signature 0건 변경 (결정 18 옵션 β — Component-cached camera). shader convention 정확 일치 보장 위해 anonymous namespace 에 local CPU helper 3개 (`MakeShaderEulerRotation` / `ExtractShaderEuler` / `MakeAlignmentMatrix`) 작성 — `FMatrix::MakeRotationEuler` 가 shader 와 sign 반대 (RH vs LH) 인 점 사전 검증. Cycle 15 (Ribbon R1+R2+R3) 이관 6건 명시. 인게임 verify 만 다음 단계.
