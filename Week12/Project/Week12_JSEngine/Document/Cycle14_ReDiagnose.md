# Cycle 14 (Mesh / Ribbon 추가 module) ReDiagnose Report

**작성일**: 2026-05-26
**대상 브랜치**: `feature/ParticleRender`
**모드**: diagnose (read-only — 코드 변경 0건)
**대상 module 5건**: M1 (Mesh Alignment enum), M2 (Mesh RotationRate module), R1 (Ribbon CameraFacing align), R2 (Ribbon Color Interpolation), R3 (Ribbon Sheets Per Trail)

**선행 baseline (확정 — 본 진단 시점)**:
- Cycle 10d (container 책임 승격, silent bug ν/ξ 해소)
- Cycle 11 (Mesh emitter, 옵션 B `FMeshRotationPayload` 36B 도입)
- Cycle 12 (Ribbon emitter, `FRibbonParticlePayload` 32B + multi-trail linked list)
- Cycle 13a/b (Beam emitter, `FParticleBeamPayload` 100B + Source/Target/Noise)

**진단 범위**: 코드 ↔ md 정합성 + 5건 module 도입 영향 + 의존성 그래프 + silent bug 후보. 코드 변경 0 / 우선순위 결정 0 / 구현 plan 작성 0.

---

## §1 코드 ↔ md 정합성 (Phase 1)

### 1.1 Cycle 13b 종료 baseline (확정)

| 항목 | 값 | 근거 |
| --- | --- | --- |
| `sizeof(FBaseParticle)` | 108B | [ParticleTypes.h:28-44](../JSEngine/Source/Engine/Particle/ParticleTypes.h:28) |
| `sizeof(FMeshRotationPayload)` | 36B | [ParticleMeshTypes.h:22](../JSEngine/Source/Engine/Particle/ParticleMeshTypes.h:22) static_assert |
| `sizeof(FRibbonParticlePayload)` | 32B | [ParticleRibbonTypes.h:24](../JSEngine/Source/Engine/Particle/ParticleRibbonTypes.h:24) static_assert |
| `sizeof(FParticleBeamPayload)` | 100B (13b) | [ParticleBeamTypes.h:27-28](../JSEngine/Source/Engine/Particle/ParticleBeamTypes.h:27) static_assert |
| Sprite Stride | 112B (align 16) | `108 + 0 = 108 → align 112` |
| Mesh Stride | 144B (align 16) | `108 + 36 = 144` |
| Ribbon Stride | 144B (align 16) | `108 + 32 = 140 → align 144` |
| Beam Stride (13b) | 208B (align 16) | `108 + 100 = 208` |
| `sizeof(FRenderCommand)` | 464B | [ParticleRenderPass.cpp:18](../JSEngine/Source/Engine/Render/Renderer/RenderFlow/ParticleRenderPass.cpp:18) static_assert |
| `sizeof(FMeshParticleInstanceData)` | 56B (Cycle 11 옵션 B) | [VertexTypes.h:90](../JSEngine/Source/Engine/Render/Resource/VertexTypes.h:90) static_assert |
| `sizeof(FRibbonParticleVertex)` | 48B | [ParticleRibbonTypes.h:39-40](../JSEngine/Source/Engine/Particle/ParticleRibbonTypes.h:39) static_assert |
| `sizeof(FBeamParticleVertex)` | 48B | [ParticleBeamTypes.h:47-48](../JSEngine/Source/Engine/Particle/ParticleBeamTypes.h:47) static_assert |

### 1.2 등록된 TypeData UCLASS 목록 (Cycle 8 → 13b)

| UCLASS | 위치 | RequiredPayloadBytes | RenderMode |
| --- | --- | --- | --- |
| `USpriteTypeData` | [ParticleModuleTypeData.h:33-41](../JSEngine/Source/Engine/Particle/ParticleModuleTypeData.h:33) | 0 | Sprite |
| `UMeshTypeData` | [ParticleModuleTypeDataMesh.h:11-44](../JSEngine/Source/Engine/Particle/ParticleModuleTypeDataMesh.h:11) | 36 | Mesh |
| `URibbonTypeData` | [ParticleModuleTypeDataRibbon.h:12-50](../JSEngine/Source/Engine/Particle/ParticleModuleTypeDataRibbon.h:12) | 32 | Ribbon |
| `UBeamTypeData` | [ParticleModuleTypeDataBeam.h](../JSEngine/Source/Engine/Particle/ParticleModuleTypeDataBeam.h) | 100 | Beam |

### 1.3 등록된 Beam Source/Target/Noise module UCLASS

| UCLASS | 위치 | 비고 |
| --- | --- | --- |
| `UParticleModuleBeamSource` | [ParticleModuleBeamSource.h](../JSEngine/Source/Engine/Particle/ParticleModuleBeamSource.h) | `TObjectPtr<USceneComponent>` |
| `UParticleModuleBeamTarget` | [ParticleModuleBeamTarget.h](../JSEngine/Source/Engine/Particle/ParticleModuleBeamTarget.h) | + bUseLocalTarget / TargetLocalVector |
| `UParticleModuleBeamNoise` | [ParticleModuleBeamNoise.h](../JSEngine/Source/Engine/Particle/ParticleModuleBeamNoise.h) | 4 UPROPERTY (Frequency/NoiseRange/bTargetNoise/bSmooth) |

### 1.4 md ↔ code 정합성 checklist

- [OK] `EParticleEmitterRenderMode { Sprite, Mesh, Beam, Ribbon }` — [ParticleTypes.h:12-18](../JSEngine/Source/Engine/Particle/ParticleTypes.h:12) 일치
- [OK] `EVertexFactoryType` — Mesh/Ribbon/Beam 모두 entry + 명시 case + Layout/Desc 본문 채워짐 ([VertexFactoryTypes.h:37-39, 350-355](../JSEngine/Source/Engine/Render/Resource/VertexFactoryTypes.h:37))
- [OK] `FRenderCommand sizeof = 464B` static_assert 유지 ([ParticleRenderPass.cpp:18](../JSEngine/Source/Engine/Render/Renderer/RenderFlow/ParticleRenderPass.cpp:18))
- [OK] container 자동 가산: Init에서 `ParticleSize + PayloadBytes` 그대로 ([ParticleEmitterInstance.cpp:44-53](../JSEngine/Source/Engine/Particle/ParticleEmitterInstance.cpp:44))
- [OK] `EditorParticleSystemWidget_Emitters.cpp:138-145` 의 RenderMode 4종 메뉴 — 4종 모두 `ChangeEmitterRenderMode` 지원
- [OK] Editor add-module 메뉴 ([EditorParticleSystemWidget_Emitters.cpp:240-258](../JSEngine/Source/Editor/UI/EditorParticleSystemWidget_Emitters.cpp:240)) — `Rotation` / `Rotation Rate` 항목은 **`DrawDisabledParticleModuleMenu` 호출**로 placeholder만 노출 (line 249-250).
- [STALE] `InfraCheck.md §3.1 (a) line 74`: `EMeshScreenAlignment { MSA_Velocity, MSA_MeshFaceCameraWithRoll, MSA_MeshFaceCameraWithLockedAxis, MSA_MeshFaceCameraWithSpin }` — **현재 코드 0건** (enum 정의·UPROPERTY·계산 코드 모두 부재). Cycle 11에서 옵션 B 채택 시 도입 예정이었으나 미구현 (옵션 A 채택 시점에 deferred됨, 그러나 옵션 B로 lock-in된 후에도 미도입 — [Cycle11_ImplementReport에 해당 없음]).
- [STALE] `InfraCheck.md §3.1 (b) line 79`: "초기 구현은 `Particle.Rotation` 단일 float로 시작 권장" — 실제로는 Cycle 11이 옵션 B로 진행, `FMeshRotationPayload`에 `Rotation: FVector` 3축으로 정착. md의 권고와 다름.
- [STALE] `URibbonTypeData::SheetsPerTrail` 멤버는 정의됨 ([ParticleModuleTypeDataRibbon.h:42-43](../JSEngine/Source/Engine/Particle/ParticleModuleTypeDataRibbon.h:42)) + getter/setter 있음 ([ParticleModuleTypeDataRibbon.h:24, 32](../JSEngine/Source/Engine/Particle/ParticleModuleTypeDataRibbon.h:24)) — **그러나 `ParticleRibbonEmitterInstance.cpp` / `BuildVertexBuffer` / `ParticleRenderPass.cpp` 어디에서도 read 0건**. Cycle 12 `EditorPropertyWidget.cpp:1717-1734` 의 RibbonTD UI 블록에서도 노출 안 됨. → **dormant UPROPERTY** (선언만 + 자동 직렬화 + UI 미노출). R3 도입 시 read·UI 추가만 필요 (선언 0건 추가).
- [STALE] `URibbonTypeData::MaxParticleInTrailCount` — 정의 + UI 노출 ([EditorPropertyWidget.cpp:1723](../JSEngine/Source/Editor/UI/EditorPropertyWidget.cpp:1723)) 됐으나 `ParticleRibbonEmitterInstance.cpp` 에서 chain length cap 강제 read 0건. [Cycle12_ImplementReport.md §9 line 179](Cycle12_ImplementReport.md) 가 "후속 cycle (12c)" 로 분리 명시. 본 cycle 14의 R3 와 무관.
- [STALE] Cycle 11 옵션 B의 `BuildInstanceData` cycle 11 라인 50-51 주석: "RotRate가 0 고정이므로 Rotation 누적은 본 cycle에서 no-op. 후속 cycle (RotRate 모듈 도입) 진입 시 DeltaTime 전달 경로 (base Tick → protected LastDeltaTime) 추가 필요" — **본 cycle 14의 M2가 정확히 이 경로의 첫 도입**. 즉 (M2 = 그 주석의 후속 cycle).

### 1.5 본 cycle 변경 영역의 stale 별표

| 변경 대상 module | stale 항목 |
| --- | --- |
| M1 (Mesh Alignment) | `InfraCheck.md §3.1 (a) line 74` 의 enum 4종 후보 — 실제 코드 0건. enum 신규 작성 필요 |
| M2 (Mesh RotationRate) | `ParticleMeshEmitterInstance.cpp:23, 50-51` 의 "후속 cycle 도입" 주석 — M2의 진입 지점. payload는 이미 36B 도입됨 (InitialOrientation/Rotation/RotRate 셋 다 존재) → struct 변경 0건 |
| R1 (Ribbon CameraFacing) | `ParticleRibbonEmitterInstance.cpp:14-15` 의 "후속 cycle (12c: View-aligned ribbon) 에서 camera up 으로 교체 가능" 주석 — R1의 진입 지점 |
| R2 (Ribbon Color Interp) | md 직접 매칭 항목 0. Cycle 12 결과의 per-vertex `V0.Color = P->Color` ([ParticleRibbonEmitterInstance.cpp:288](../JSEngine/Source/Engine/Particle/ParticleRibbonEmitterInstance.cpp:288)) 그대로 — interp 매커니즘 0건 |
| R3 (Ribbon SheetsPerTrail) | `URibbonTypeData::SheetsPerTrail` UPROPERTY 자체는 존재 (선언만) — 정확히 dormant. R3는 (read + BuildVertexBuffer 분기 + UI 노출) 3건 추가 |

---

## §2 Phase 2 — Mesh 추가 module 진단

### 2.1 M1. Mesh Alignment Enum

#### M1.1 enum 정의 위치 후보 (코드 사실)

- 후보 A: `ParticleTypes.h` 확장 — 현재 `EParticleEmitterRenderMode` 가 있는 동일 파일 ([ParticleTypes.h:11-18](../JSEngine/Source/Engine/Particle/ParticleTypes.h:11)). 다른 emitter도 enum이 필요해질 가능성 큼.
- 후보 B: `ParticleMeshTypes.h` 확장 — 현재 `FMeshRotationPayload` 만 있는 mesh-전용 파일 ([ParticleMeshTypes.h](../JSEngine/Source/Engine/Particle/ParticleMeshTypes.h)). Mesh emitter 한정성을 명확히 함.
- 후보 C: `ParticleModuleTypeDataMesh.h` 안에 nested — UCLASS 선언과 함께. UENUM은 헤더 anywhere 가능 ([ShadowTypes.h:6-11](../JSEngine/Source/Engine/Render/Common/ShadowTypes.h:6) 패턴).

#### M1.2 UPROPERTY 위치 후보

- 후보 A: `UMeshTypeData` 멤버로 직접 추가 — TypeData가 alignment의 single source. 분기 비용 0.
- 후보 B: 신규 `UParticleModuleMeshAlignment` UCLASS — 별도 module class. base `UParticleModule` 상속. Mesh emitter에만 add. M2와 같은 패턴 (Spawn/Update 분리 가능).
- 추측: UE Cascade는 (A) — `UParticleModuleTypeDataMesh::ScreenAlignment` 멤버로 보유. 본 엔진의 검증된 패턴 (`UMeshTypeData::Mesh`, `UBeamTypeData::MaxBeamCount` 등 module-wide 설정은 TypeData 멤버)과 일치.

#### M1.3 enum 값 별 instance transform 행렬 산출

`FParticleMeshEmitterInstance::BuildInstanceData()` ([ParticleMeshEmitterInstance.cpp:52-78](../JSEngine/Source/Engine/Particle/ParticleMeshEmitterInstance.cpp:52)) 안에서 `Data.InstanceRotation` (3축 Euler radians) 결정 시점에 alignment 분기:

| enum 값 | 행렬 산출 공식 (추측) | 입력 |
| --- | --- | --- |
| `PSA_Velocity` | Tangent = `Particle->Velocity.Normalize()`. 그 Tangent와 일치하는 forward axis(예: +X)를 가진 회전행렬. Euler 변환 → InstanceRotation | `Particle->Velocity` |
| `PSA_LockedAxis` | 사용자 입력 `FVector LockAxis` 고정 + 다른 두 축은 Velocity/Camera 방향으로 회전 가능 | `Particle->Velocity` + UPROPERTY `LockAxis` |
| `PSA_FacingCameraPosition` | Look-at: forward = `(CameraPos - Particle->Location).Normalize()`. Up = world Up | `Particle->Location` + **CameraPosition** |
| `PSA_Rectangle` / `PSA_Square` | Sprite-like billboard: forward = `CameraForward` (Negative), Up/Right = Camera vectors | **Camera vectors** (Forward/Up/Right) |

#### M1.4 Camera/View 정보 접근 경로 (FACT)

- **현재 BuildInstanceData 시그니처**: `void BuildInstanceData()` ([ParticleEmitterInstance.h:39](../JSEngine/Source/Engine/Particle/ParticleEmitterInstance.h:39)) — **인자 0**. Camera 접근 경로 0.
- **호출자**: `UParticleSystemComponent::BuildInstanceData()` ([ParticleSystemComponent.cpp:231-240](../JSEngine/Source/Engine/Particle/ParticleSystemComponent.cpp:231)) → `Instance->BuildInstanceData()` (인자 0). 호출자도 Camera 접근 0.
- **상위 호출자**: `PrimitiveDrawCommandBuilder::CollectPrimitive` ([PrimitiveDrawCommandBuilder.cpp:582](../JSEngine/Source/Engine/Render/Scene/PrimitiveDrawCommandBuilder.cpp:582)) → `ParticleSystemComponent->BuildInstanceData()`. Builder 함수 시그니처 ([PrimitiveDrawCommandBuilder.h:12-13](../JSEngine/Source/Engine/Render/Scene/PrimitiveDrawCommandBuilder.h:12)) 에 **`FRenderBus& RenderBus`** 인자 있음 → Builder에는 Camera 접근 가능 (`RenderBus.GetCameraPosition/Forward/Up/Right`).
- **결론**: Camera-aware alignment 도입 시 신호 전달 경로는 3 옵션 —
  - 옵션 (α): `BuildInstanceData` 시그니처 확장 (`const FRenderBus&` 또는 camera 4 vector 전달). 가장 침습적이나 명확. 회귀 영향: 모든 derived (Mesh/Ribbon/Beam) 의 signature 변경 강제.
  - 옵션 (β): Component가 frame 시작 시 RenderBus에서 camera 4 vector를 멤버에 캐싱 → derived는 `Owner->Component->GetCachedCameraXxx()` 로 lookup. Cycle 12 의 EnsureTrailState 패턴과 유사한 lazy approach. 시그니처 변경 0.
  - 옵션 (γ): alignment 계산을 **shader VS**에서 수행 — instance VB에 alignment mode + 필요한 입력만 push, VS에서 view matrix 통해 camera-aware 변환. CPU 계산 0. 결국 alignment 의미만큼 VS 복잡도 증가.

#### M1.5 MeshInstanceDataBuffer Transform 필드 layout (FACT)

- 현재 layout ([VertexTypes.h:82-89](../JSEngine/Source/Engine/Render/Resource/VertexTypes.h:82)): InstancePosition(12) + InstanceRotation(12 Euler) + InstanceScale(12) + Padding(4) + InstanceColor(16) = 56B.
- **InstanceRotation은 4x4 matrix 아님, FVector Euler 3축**. Cycle 11 옵션 B 채택 결과. Cycle 11 plan §B.1 line 82-95의 옵션 B (FMatrix 64B 또는 Quat+Position+Scale 32B) 가 아니라 Euler 3축 12B로 정착.
- VS 측 변환 ([MeshParticle.hlsl:37-57, 59-73](../JSEngine/Shaders/Particle/MeshParticle.hlsl:37)): `EulerZYXToMatrix(InstanceRotation)` 으로 3x3 변환 → `mul(ScaledLocal, RotMat) + InstancePosition`. layout 자체는 alignment-agnostic — alignment에 따른 회전값 산출만 CPU 쪽 BuildInstanceData에서 결정.

#### M1.6 PSA_LockedAxis 의 lock axis 사용자 입력 형식

- 후보 A: TypeData에 `FVector LockedAxis` UPROPERTY 추가 (default = `(0,0,1)` world Up).
- 후보 B: 별도 module class 의 멤버.
- 본 엔진의 검증 패턴: `FVector` UPROPERTY는 사례 다수 (예: `UParticleModuleLocation::StartLocationMin/Max` [ParticleModules.h:103-107](../JSEngine/Source/Engine/Particle/ParticleModules.h:103)). picker 자동.

### 2.2 M2. Mesh RotationRate Module

#### M2.1 module 종류 (Spawn vs Spawn+Update)

- 본 엔진의 module 두 분류 ([ParticleModule.h:35-41](../JSEngine/Source/Engine/Particle/ParticleModule.h:35)): `bSpawnModule` / `bUpdateModule` 둘 다 true 가능.
- Color/Size module 패턴 ([ParticleModules.cpp:159-225](../JSEngine/Source/Engine/Particle/ParticleModules.cpp:159)): 둘 다 `bSpawnModule = true; bUpdateModule = true;` — Spawn에서 initial값, Update에서 lerp.
- **권고 패턴 (FACT)**: M2는 `bSpawnModule = true` (RotRate 초기값 셋) + Update 매커니즘은 별도. **단 Update가 base Tick의 Update 루프를 그대로 사용한다 가정**.

#### M2.2 payload 슬롯 현황 (FACT — 본 cycle 영향 0)

- `FMeshRotationPayload` 멤버 ([ParticleMeshTypes.h:15-20](../JSEngine/Source/Engine/Particle/ParticleMeshTypes.h:15)): `InitialOrientation` (FVector, offset 0) + `Rotation` (FVector, offset 12) + `RotRate` (FVector, offset 24). 3개 슬롯 모두 존재 + offset 명확.
- 현재 Cycle 11의 `FParticleMeshEmitterInstance::SpawnParticles` ([cpp:25-44](../JSEngine/Source/Engine/Particle/ParticleMeshEmitterInstance.cpp:25)) 가 모두 `ZeroVector` 로 초기화.
- → **M2 도입은 payload struct 변경 0건, sizeof 변경 0건, Stride 변경 0건**. (M2의 RotRate 값을 Spawn 시 모듈러 세팅하기만 하면 됨.)

#### M2.3 Update 단계 누적 로직 — **gap 분석 (CRITICAL)**

- **base `Tick`의 Update module 루프** ([ParticleEmitterInstance.cpp:129-135](../JSEngine/Source/Engine/Particle/ParticleEmitterInstance.cpp:129)):
  ```cpp
  for (UParticleModule* Module : CurrentLODLevel->GetUpdateModules())
  {
      if (Module && Module->IsEnabled())
      {
          Module->Update(this, DeltaTime);  // (Owner, DeltaTime)
      }
  }
  ```
  → Update module은 `Owner` (FParticleEmitterInstance*) + `DeltaTime` 두 인자만 받음. **본 hook이 누적의 정공법 경로** — Update module에서 `Owner->GetParticle(i)` 순회 + payload pointer 회수 + Rotation += RotRate * DeltaTime 누적.
- **payload 접근 경로**: Color/Size module은 `FBaseParticle.Color/Size`에 직접 write ([ParticleModules.cpp:189, 223](../JSEngine/Source/Engine/Particle/ParticleModules.cpp:189)). 반면 M2는 **`FMeshRotationPayload` 영역에 write 필요** — 현재 base의 Update 인터페이스 (`Owner->GetParticle(i)`) 로는 payload access 0건 (FBaseParticle 외 영역 무관).
- **payload 접근에 필요한 정보**:
  - `Owner->GetParticleStride()` (public — line 53) — OK
  - PayloadOffset — **현재 base의 private 멤버, derived만 protected 접근** ([ParticleEmitterInstance.h:85](../JSEngine/Source/Engine/Particle/ParticleEmitterInstance.h:85)). Update module은 **외부** — 접근 불가.
  - `ParticleStorage.ParticleData/Indices` — public getter `GetParticleData()/GetParticleIndices()` 존재 (h:57-58) BUT const-only.
  - **결론**: Update module이 외부에서 payload write 하려면 (a) `PayloadOffset` public getter 신설, (b) `GetParticleData()` 의 non-const 버전 추가, (c) Cast<FParticleMeshEmitterInstance>(Owner) 후 derived의 payload helper 호출 — 3 옵션.
- **(c) 옵션의 검증된 패턴** : `FParticleMeshEmitterInstance::GetMeshPayload(SlotIndex)` ([ParticleMeshEmitterInstance.cpp:9-17](../JSEngine/Source/Engine/Particle/ParticleMeshEmitterInstance.cpp:9))은 **private**. Update module에서 호출하려면 public화 또는 friend 선언 필요.
- **추측**: 가장 자연스러운 패턴 — `UParticleModuleMeshRotationRate::Update(Owner, dt)` 가 `Cast<FParticleMeshEmitterInstance>(Owner)` 후 새 public helper (예: `GetMeshPayloadAt(int32 ActiveIdx)`) 호출 → Rotation 누적.

#### M2.4 공통 infra 영향 판정 — **본 cycle 외 분리 여부**

- Update 루프 자체는 이미 base Tick에 존재 ([cpp:129-135](../JSEngine/Source/Engine/Particle/ParticleEmitterInstance.cpp:129)). **base class 시그니처 변경 없음**.
- payload access를 위한 변경:
  - **옵션 A (minimal)**: `FParticleMeshEmitterInstance::GetMeshPayload` 를 public화 (또는 별도 ActiveIdx 인자 public helper 추가). M2 module이 derived cast 후 호출. 본 cycle 내부 범위.
  - **옵션 B (broader)**: base `FParticleEmitterInstance` 에 `GetPayloadAt<T>(int32 SlotIndex)` template 추가. 다른 emitter (Ribbon/Beam) 도 동일 패턴 사용 가능. **base class 변경 — silent bug ν/ξ 식별 영역에 추가 표면** → §5.5 별도 cycle 후보로 분리 권고.
- **판정**: 옵션 (A) 채택 시 본 cycle 내부 가능. 옵션 (B) 는 별도 cycle.

#### M2.5 Sprite의 Rotation 단일 float vs Mesh의 FVector Rotation 차이

- `FBaseParticle.Rotation = 0.0f; RotationRate = 0.0f` ([ParticleTypes.h:38-39](../JSEngine/Source/Engine/Particle/ParticleTypes.h:38)) — 단일 float (Sprite billboard 회전용).
- Mesh의 3축은 별도 payload (`FMeshRotationPayload.Rotation` FVector). **두 시스템은 독립** — Sprite의 RotationRate를 Mesh가 사용하지 않음 ([ParticleMeshEmitterInstance.cpp:73](../JSEngine/Source/Engine/Particle/ParticleMeshEmitterInstance.cpp:73): `Data.InstanceRotation = Payload->Rotation`).
- **M2 누적 대상은 `FMeshRotationPayload.Rotation` FVector 3축**. `FBaseParticle.RotationRate` 단일 float는 무관.

#### M2.6 기존 Sprite의 RotationRate 모듈 존재 여부

- grep 결과: `RotationRate` 단어는 `FBaseParticle` 멤버 정의 ([ParticleTypes.h:39](../JSEngine/Source/Engine/Particle/ParticleTypes.h:39)) + Sprite VS 의 INSTANCE_ROTATION semantic 만. **Sprite의 단일 float Rotation을 Spawn 시 세팅하는 module 0건 + Update 시 누적하는 module 0건**.
- → M2는 Mesh 한정 신규. Sprite의 동일 결손은 별도 cycle 후보 (§5.5).

---

## §3 Phase 3 — Ribbon 추가 module 진단

### 3.1 R1. Ribbon View Align: CameraFacing

#### R1.1 현재 Ribbon view alignment 옵션 (FACT)

- **현재 옵션 정의 0건**. enum 없음. 단일 모드 고정 — `ComputePerpendicular(Tangent)` ([ParticleRibbonEmitterInstance.cpp:16-26](../JSEngine/Source/Engine/Particle/ParticleRibbonEmitterInstance.cpp:16)) 가 항상 `world Up (0,0,1)` reference 사용 + degenerate시 X 축 fallback.
- 코드 주석 line 14-15: "후속 cycle (12c: View-aligned ribbon) 에서 camera up 으로 교체 가능".
- R1 도입은 (a) 신규 enum 정의 + (b) `URibbonTypeData` 멤버 추가 + (c) `ComputePerpendicular` 본문 분기 + (d) camera 접근 경로.

#### R1.2 Tangent 계산 코드 위치 (FACT)

- `BuildVertexBuffer` ([ParticleRibbonEmitterInstance.cpp:260-308](../JSEngine/Source/Engine/Particle/ParticleRibbonEmitterInstance.cpp:260)) 의 line 282: `const FVector Perp = ComputePerpendicular(Payload->Tangent);`
- `Tangent` 자체는 `Tick` ([cpp:229-238](../JSEngine/Source/Engine/Particle/ParticleRibbonEmitterInstance.cpp:229)) 에서 trail chain의 위치 변화로 갱신 (variable dt 보정 — 결정 7 옵션 B).
- → R1의 CameraFacing 모드 시 `Perp` 계산 공식 변경: `Perp = Cross(Tangent, ViewDir).Normalize()` (또는 `Cross(Tangent, CameraUp)` — 정확한 공식은 UE Cascade의 ViewAlignment 정의 따름).

#### R1.3 CameraFacing 시 strip width 평면 산출 공식 (추측)

- 표준: `ViewDir = (CameraPos - VertexPos).Normalize()` (per-vertex perspective) 또는 `ViewDir = CameraForward` (orthographic). `Perp = Cross(Tangent, ViewDir).Normalize()`.
- **per-vertex 비용**: trail의 각 particle마다 `CameraPos - P->Location` 계산 → 1 normalize. Cycle 12의 기존 BuildVertexBuffer가 per-particle 2 vertex 생성 — 비용 영향 미미.
- **per-frame 비용**: 단일 ViewDir 사용 시 BuildVertexBuffer 진입 시 1회 계산만. 더 단순.

#### R1.4 Camera position 접근 경로 — **M1.4와 동일 문제**

- 현재 `BuildVertexBuffer`는 `Tick`에서 호출 ([cpp:251](../JSEngine/Source/Engine/Particle/ParticleRibbonEmitterInstance.cpp:251)). `Tick` 시그니처는 `(float DeltaTime, bool bAllowSpawning)` — camera 0.
- `Tick`은 base Tick 흐름에서 호출됨 → `Owner->Component->BuildInstanceData()` 와 다른 경로지만 동일하게 외부 (Builder)로부터 camera 정보 안 받음.
- → R1는 M1과 동일하게 (α) signature 확장 / (β) Component-cached camera / (γ) shader-side 중 하나 필요.

#### R1.5 매 frame 재계산 비용 (FACT)

- 현재 Cycle 12 `BuildVertexBuffer`는 매 Tick 호출 ([cpp:202-251](../JSEngine/Source/Engine/Particle/ParticleRibbonEmitterInstance.cpp:202)) — `λ` silent bug 패턴 그대로 유지 (clear → rebuild). CameraFacing 도입 시 Perp 계산이 카메라에 의존할 뿐, 호출 빈도 변경 0.

#### R1.6 Beam 의 `ComputeBeamLocalAxes` 패턴 재사용 가능성

- [ParticleBeamEmitterInstance.cpp:47-56](../JSEngine/Source/Engine/Particle/ParticleBeamEmitterInstance.cpp:47): `Cross(Tangent, RefAxis)` + `RefAxis = WorldUp` 또는 `WorldRight` (parallel 시 자동 전환). 위험 11 방어 패턴.
- R1의 CameraFacing 도 동일 — `RefAxis = ViewDir` 로 교체하면 됨 + parallel-시 fallback (Tangent ≈ ViewDir 평행하면 분기 처리 필요).
- → **R1는 Beam의 ComputeBeamLocalAxes 일반화 패턴을 답습 가능** (인자에 RefAxis 또는 mode enum 추가).

### 3.2 R2. Ribbon Color Interpolation

#### R2.1 현재 vertex Color 산출 위치 (FACT)

- `BuildVertexBuffer` ([ParticleRibbonEmitterInstance.cpp:288, 293](../JSEngine/Source/Engine/Particle/ParticleRibbonEmitterInstance.cpp:288)): `V0.Color = P->Color; V1 = V0;` — strip 양쪽 vertex가 같은 particle color 사용. trail interp 0건.
- VS 측 ([RibbonParticle.hlsl:43](../JSEngine/Shaders/Particle/RibbonParticle.hlsl:43)): `output.Color = input.Color;` — 그대로 pass-through.
- PS 측 ([RibbonParticle.hlsl:49](../JSEngine/Shaders/Particle/RibbonParticle.hlsl:49)): `Sample * input.Color` — texture × per-vertex color.

#### R2.2 Trail head/tail 식별 방식 (FACT)

- `HeadIndices[TrailIdx]` 가 head의 SlotIndex ([ParticleRibbonEmitterInstance.h:41](../JSEngine/Source/Engine/Particle/ParticleRibbonEmitterInstance.h:41)). chain은 `Payload->NextIndex` 따라 traversal — tail은 `NextIndex == -1`.
- `Payload->Distance` ([ParticleRibbonTypes.h:21](../JSEngine/Source/Engine/Particle/ParticleRibbonTypes.h:21)) — head로부터 누적 거리, Tick에서 갱신 ([cpp:233-234](../JSEngine/Source/Engine/Particle/ParticleRibbonEmitterInstance.cpp:233)). interp의 좌표(x축)로 자연 사용 가능.
- `Payload->TrailIndex` — 어떤 trail 소속.
- `FBaseParticle.RelativeTime` ([ParticleTypes.h:34](../JSEngine/Source/Engine/Particle/ParticleTypes.h:34)) — 0~1 normalized lifetime. interp의 시간축 후보.

#### R2.3 Interpolation 모드 후보

- 모드 A (StartColor → EndColor linear): TypeData 또는 module에 `FColor StartColor, EndColor` 두 멤버 → interp 기준은 (i) RelativeTime, (ii) chain position (head=0, tail=1 normalized), (iii) Distance.
- 모드 B (Distribution / Curve): 본 엔진의 Distribution/Curve 인프라 현황 확인 필요 (3.2.5).
- 모드 C (Spawn 시 per-particle 캡처 → Tick 누적): chain 의 위치에 따라 color 변동.

#### R2.4 Per-particle color vs trail interp 결합 방식

- 현재 per-particle color는 `UParticleModuleColor::Update` ([ParticleModules.cpp:183-191](../JSEngine/Source/Engine/Particle/ParticleModules.cpp:183)) 에서 lifetime interp 수행 → `Particle.Color = Lerp(StartColor, EndColor, RelativeTime)`.
- **Cycle 12 의 Ribbon BuildVertexBuffer는 `Particle->Color` 그대로 read** ([cpp:288](../JSEngine/Source/Engine/Particle/ParticleRibbonEmitterInstance.cpp:288)) — `UParticleModuleColor` 가 이미 적용된 결과가 들어옴.
- → **이미 lifetime 기반 interp는 기존 ColorModule이 제공함**. R2의 "trail head→tail interp"는 다음 둘 중 하나:
  - (i) **lifetime interp 와 별개**: trail 의 head→tail 좌표 (chain position 또는 Distance) 기반 — 같은 spawn 시점의 모든 particle이 시간이 지남에 따라 chain 위치가 변하므로 같은 trail 안에서도 다른 색.
  - (ii) **lifetime interp 와 동일하나 강조**: 단순히 head는 진하게, tail은 옅게 (alpha만 변경).
- **차이점**: (i) 는 spatial interp (chain 위치), (ii) 는 temporal interp (이미 ColorModule 이 함). R2 사용자 의도는 (i) 가 정공법.

#### R2.5 Distribution / Curve 인프라 존재 여부 (FACT)

- grep 결과: `UCurveFloatAsset` 존재 ([JSEngine.vcxproj:370](../JSEngine/JSEngine.vcxproj:370) — `Intermediate/Reflection/Asset/UCurveFloatAsset.gen.cpp`).
- `FDistribution` 또는 `FDistributionVector` 등 UE Cascade 의 Distribution 인프라는 본 엔진에 grep 결과 0건 — 다음 단계에서 별도 확인 필요.
- **추측**: Cycle 14 의 R2는 Distribution 의존 없이 **(StartColor, EndColor) 두 UPROPERTY 만으로 시작** 권고. 그러나 사용자 결정 영역.

#### R2.6 보간 기준 후보 (chain position vs Distance vs RelativeTime)

- **chain position** (head index 0, tail index N-1 → normalized 0~1): 추가 payload 없음. Tick의 chain 순회 중 index counter만 추가하면 됨. trail 안 particle 수 변동 시 색 갱신 명확.
- **Distance** (head=0, tail=AccumDist → normalized 0~`AccumDist max`): 이미 `Payload->Distance` 가 있음. tail의 Distance 가 trail 마다 다르므로 normalize 필요.
- **RelativeTime**: 이미 ColorModule 이 사용 — 중복.

### 3.3 R3. Ribbon Sheets Per Trail

#### R3.1 현재 `URibbonTypeData::SheetsPerTrail` 존재 여부 (FACT)

- UPROPERTY 선언: [ParticleModuleTypeDataRibbon.h:42-43](../JSEngine/Source/Engine/Particle/ParticleModuleTypeDataRibbon.h:42) `float SheetsPerTrail = 1.0f;` + getter line 24 + setter line 32.
- **read 0건**: `ParticleRibbonEmitterInstance.cpp` / `ParticleRenderPass.cpp` / `PrimitiveDrawCommandBuilder.cpp` grep 결과 0건. dormant.
- UI 노출 0건: `EditorPropertyWidget.cpp:1717-1734` 의 Ribbon 블록에 노출 없음 (Cycle 12 작성 시 의도적 deferral 추측). 자동 reflection 노출은 UCLASS 의 자동 detail panel 에 표시될 수 있으나 직접 코드 확인 필요.
- → R3 도입 = (a) read 추가 + (b) BuildVertexBuffer 분기 + (c) UI 노출. **선언 변경 0건**.

#### R3.2 Sheets > 1 시 vertex 수 증가 (추측)

- 현재 strip: chain 의 각 particle 마다 2 vertex (V0/V1, Perp 양쪽).
- Sheets = N (>=1): 같은 chain 위치에 N장의 cross strip → 각 particle 마다 2N vertex.
- 정확한 강도: UE Cascade 는 `2π / N` 라디안 간격으로 Tangent 축 중심 회전된 N개 Perp 평면. Sheets=2 = cross (V 평면 + H 평면), Sheets=3 = 3중.

#### R3.3 cross 배치 회전 각도

- 각도 = `(2π / Sheets) * SheetIdx` (단 Sheets=2 일 때는 `π/2` 차이 — UE Cascade 의 정확한 공식 확인 필요).
- 새 Perp 계산: 기준 Perp0 = `ComputePerpendicular(Tangent)` (현재 코드). Sheet I의 Perp_I = Rotate(Perp0, Tangent axis, angle).
- `Tangent` 축 중심 회전은 Rodrigues' formula 또는 quaternion. 추가 행렬 연산.

#### R3.4 Strip topology 변화 (추측)

- 옵션 (i): **단일 strip + degenerate seam**: chain 끝에서 마지막 vertex 복제 + 다음 sheet 시작 vertex 복제 → strip 연결 끊김. Cycle 12 의 multi-trail seam 패턴 ([cpp:301-306](../JSEngine/Source/Engine/Particle/ParticleRibbonEmitterInstance.cpp:301)) 답습. Draw call 1회 유지.
- 옵션 (ii): **multi-draw**: Sheets 마다 별도 Draw call. 코드 단순하나 성능 N배. Sheets 가 일반적으로 1~3 이므로 성능 영향 미미.
- 추측: (i) 가 코드 일관성 + 성능 면에서 유리.

#### R3.5 dynamic VB 용량 영향 (FACT)

- 현재 `FInstanceBuffer RibbonVertexBuffer` ([ParticleRenderPass.h:40](../JSEngine/Source/Engine/Render/Renderer/RenderFlow/ParticleRenderPass.h:40)) 의 Cycle 12 grow-by-2x 패턴 — capacity 부족 시 자동 2배 ([InstanceBuffer.cpp:31-46](../JSEngine/Source/Engine/Render/Resource/InstanceBuffer.cpp:31)).
- Sheets=N 일 때 vertex 수가 N배 → 첫 frame 에서 grow 발생할 가능성 (256 initial capacity).
- → R3 도입에 InstanceBuffer 클래스 변경 0 (이미 grow 지원). 단 max 가능치 추정 시 메모리 영향 확인 권장.

#### R3.6 Draw call 변화

- 옵션 (i) (단일 strip) 채택 시 Draw 1회 유지.
- 옵션 (ii) 채택 시 Sheets 회 Draw.

---

## §4 Phase 4 — 공통 영향 / 의존성 그래프

### 4.1 M1 ↔ M2 의존

- M1 (Alignment): `BuildInstanceData()` 의 InstanceRotation 결정에 alignment 분기 — `Particle->Velocity` / camera / locked axis 기반.
- M2 (RotationRate): payload `Rotation` 누적 → `BuildInstanceData()` 의 InstanceRotation read.
- **충돌 시나리오**: M1 이 InstanceRotation 을 매 frame override 한다면, M2의 누적 Rotation 이 매 frame 덮어쓰기 됨 → 누적 효과 시각화 안 됨.
- **해결 옵션** (추측):
  - (a) M1 결과 + M2 누적이 곱 — `FinalRot = AlignmentRot * AccumulatedRot`. 자연스러운 결합. Mesh 가 PSA_Velocity 면서 동시에 spin 도 함 — UE Cascade의 표준.
  - (b) M1 단독 사용 시 M2 무시. 사용자가 module 셋업으로 결정.
- **의존성 판정**: M1 와 M2 는 독립 도입 가능하지만 결합 시 정의 명시 필요. **본 cycle 에서 한 번에 도입한다면 (a) 결합 공식 lock-in 필요**.

### 4.2 R1 ↔ R3 의존

- R1 (CameraFacing): Perp 계산이 ViewDir 의존 → 매 frame 갱신.
- R3 (Sheets): N장 cross plane — 각 plane의 Perp_I = Rotate(Perp0, Tangent, angle_I).
- **충돌 시나리오**: R3 의 Sheets > 1 일 때 모든 sheet 의 Perp 가 카메라 방향과 일치하면 cross 의미 0 (모든 sheet 가 같은 평면).
- **해결 옵션** (추측):
  - (a) Sheet 0 만 CameraFacing 적용, Sheet 1~N-1 은 world Up reference 유지 — cross 의 시각 효과 보존.
  - (b) Sheet 0 만 CameraFacing 적용, Sheet 1~N-1 은 cross 형태 자체를 무시 (CameraFacing 모드에서는 Sheets=1 강제).
- **의존성 판정**: R1 와 R3 는 동시 활성화 시 의미 충돌. **본 cycle 에서 한 번에 도입한다면 결합 정책 lock-in 필요**.

### 4.3 Shader 변경 필요 여부

| module | shader 변경 | 이유 |
| --- | --- | --- |
| M1 (Alignment) | 옵션 (γ) 채택 시 `MeshParticle.hlsl` VS 변경 — alignment 분기 추가. 옵션 (α)/(β) 채택 시 CPU 측에서 InstanceRotation 결정 → shader 변경 0건 |
| M2 (RotationRate) | shader 변경 0 — InstanceRotation 값만 다르게 들어옴 |
| R1 (CameraFacing) | 옵션 (γ) 채택 시 `RibbonParticle.hlsl` VS 변경. 옵션 (α)/(β) 채택 시 CPU 측 Perp 결정 → shader 변경 0건 |
| R2 (Color Interp) | shader 변경 0 — vertex color 값만 다르게 들어옴 (V0/V1 별도 색 push 시 layout 영향 0) |
| R3 (Sheets) | 옵션 (i) 채택 시 strip vertex 가 그대로 stream → shader 변경 0. degenerate seam 만 추가. 옵션 (ii) 다중 Draw — 변경 0 |

→ **camera-aware 옵션 (γ) 채택할 경우만 shader 변경 발생**. CPU 측 결정 (α/β) 시 shader 변경 0.

### 4.4 vcxproj / GenerateReflection.py 영향 (silent bug §7-4)

| module | 신규 UCLASS | 신규 .h/.cpp | vcxproj 영향 |
| --- | --- | --- | --- |
| M1 — TypeData 멤버 추가 (옵션 A) | 0건 | 0건 (UMeshTypeData에 enum + 멤버 추가) | UMeshTypeData.gen.cpp 재생성 — 이미 등록된 항목 갱신만 |
| M1 — 신규 module class (옵션 B) | 1건 (`UParticleModuleMeshAlignment`) | 2건 (.h/.cpp) | vcxproj 신규 등록 3건 (.h/.cpp/.gen.cpp) |
| M2 (필수 신규) | 1건 (`UParticleModuleMeshRotationRate`) | 2건 (.h/.cpp) | vcxproj 신규 등록 3건 |
| R1 — TypeData enum 추가 (옵션 A) | 0건 | 0건 | URibbonTypeData.gen.cpp 재생성 |
| R1 — 신규 module class (옵션 B) | 1건 (`UParticleModuleRibbonCameraFacing`) | 2건 | vcxproj 신규 등록 3건 |
| R2 (필수 신규) | 0~1건 (TypeData 멤버 vs 신규 module) | 0~2건 | TypeData 멤버 옵션이면 .gen.cpp 갱신만 |
| R3 (UPROPERTY 이미 존재) | 0건 | 0건 (UI 노출 + BuildVertexBuffer read 추가) | 영향 0 |

### 4.5 Editor UI 영향

- **RenderMode 메뉴** ([EditorParticleSystemWidget_Emitters.cpp:138-145](../JSEngine/Source/Editor/UI/EditorParticleSystemWidget_Emitters.cpp:138)): 4종 enum 그대로 — 변경 0건.
- **module add 메뉴** ([EditorParticleSystemWidget_Emitters.cpp:240-258](../JSEngine/Source/Editor/UI/EditorParticleSystemWidget_Emitters.cpp:240)):
  - line 249: `DrawDisabledParticleModuleMenu("Rotation");` — Sprite 의 단일 float Rotation 누적 module placeholder. **M2와 무관 (Sprite 전용 — Mesh는 별도 payload)**.
  - line 250: `DrawDisabledParticleModuleMenu("Rotation Rate");` — Mesh 의 RotRate 도입 시 이 placeholder → enabled menu 로 교체 가능. **M2 의 도입 hook**.
  - **M1 의 Alignment 메뉴**: 현재 항목 0건. 신규 추가 시 `DrawParticleModuleAddMenu<UParticleModuleMeshAlignment>(...)` (옵션 B 채택 시) 또는 TypeData detail panel 의 UPROPERTY 자동 노출 (옵션 A 채택 시).
- **UPROPERTY picker 자동 노출**: `URibbonTypeData::SheetsPerTrail` 가 이미 UPROPERTY ([line 42-43](../JSEngine/Source/Engine/Particle/ParticleModuleTypeDataRibbon.h:42)) — auto-detail panel에 노출됨 (자동 generation). 단 [EditorPropertyWidget.cpp:1717-1734](../JSEngine/Source/Editor/UI/EditorPropertyWidget.cpp:1717) 의 explicit Ribbon block 에는 누락 → 검증 필요 (R3 의 UI 노출 점검).

### 4.6 BuildInstanceData / BuildVertexBuffer signature 변경 영향 (camera 접근 옵션 α)

| 호출자 | 현재 시그니처 | 옵션 α 후 시그니처 |
| --- | --- | --- |
| `Component->BuildInstanceData()` ([cpp:231](../JSEngine/Source/Engine/Particle/ParticleSystemComponent.cpp:231)) | 인자 0 | `BuildInstanceData(const FRenderBus&)` |
| `Instance->BuildInstanceData()` ([cpp:319](../JSEngine/Source/Engine/Particle/ParticleEmitterInstance.cpp:319) base, [cpp:52](../JSEngine/Source/Engine/Particle/ParticleMeshEmitterInstance.cpp:52) Mesh) | 인자 0 | 동일 패턴 |
| derived Mesh/Ribbon/Beam | 인자 0 | 동일 |
| Builder 호출처 ([cpp:582](../JSEngine/Source/Engine/Render/Scene/PrimitiveDrawCommandBuilder.cpp:582)) | `Component->BuildInstanceData()` | `Component->BuildInstanceData(RenderBus)` |
| 외부 호출처 [EditorMainPanelDebug.cpp:170](../JSEngine/Source/Editor/UI/EditorMainPanelDebug.cpp:170) | 동일 | RenderBus 객체 없음 → fallback signature (default camera) 또는 별도 진입점 필요 |

→ 옵션 (α) 채택 시 `EditorMainPanelDebug.cpp:170` 의 디버그 호출이 RenderBus 없이 호출하는 케이스 처리 필요.

---

## §5 산출물 — 5건 module 별 진단표 + 결정 / 위험 / 다음 cycle 후보

### 5.1 코드 ↔ md 정합성 결과 (checklist)

- [OK 일치] `EParticleEmitterRenderMode` 4종, `EVertexFactoryType` 4종 Particle entry, container 자동 가산, `sizeof(FRenderCommand) == 464`, 4종 TypeData UCLASS, 3종 Beam Source/Target/Noise module
- [STALE — 본 cycle 의 진입점] `InfraCheck.md §3.1 (a) line 74` 의 EMeshScreenAlignment 4종 후보 → 실제 코드 0건 (M1 의 도입 영역)
- [STALE — 본 cycle 의 진입점] `ParticleMeshEmitterInstance.cpp:23, 50-51` 의 "후속 cycle (RotRate 모듈 도입)" 주석 → M2 의 도입 영역
- [STALE — 본 cycle 의 진입점] `ParticleRibbonEmitterInstance.cpp:14-15` 의 "후속 cycle (12c: View-aligned ribbon)" 주석 → R1 의 도입 영역
- [STALE — R3] `URibbonTypeData::SheetsPerTrail` dormant UPROPERTY (선언만, read 0건, UI 노출 명시 0건) — R3 의 도입 영역
- [STALE — R2 영향 없음] `URibbonTypeData::MaxParticleInTrailCount` dormant (chain length cap read 0건) — Cycle 12c 별도

### 5.2 module 별 진단표

| 항목 | 도입 위치 후보 | 의존 module | hook 위치 | 신규 UCLASS | payload 영향 | shader 영향 | silent bug 후보 |
| --- | --- | --- | --- | --- | --- | --- | --- |
| **M1** (Mesh Alignment Enum) | (A) `UMeshTypeData` 멤버 + enum 정의는 `ParticleMeshTypes.h` 또는 `ParticleTypes.h` / (B) 신규 `UParticleModuleMeshAlignment` UCLASS | M2 와 InstanceRotation 결합 정의 필요 (§4.1) | `FParticleMeshEmitterInstance::BuildInstanceData()` 의 `Data.InstanceRotation` 결정 직전 분기 | (A) 0건 / (B) 1건 | 0건 (`FMeshRotationPayload` 변경 없음) | 옵션 (γ) shader-side 결정 시만 변경 — 권고는 CPU side (옵션 α/β) → 0건 | §7-4 (옵션 B 시 vcxproj), §5.4 위험 12 (camera 접근 경로) |
| **M2** (Mesh RotationRate Module) | 신규 `UParticleModuleMeshRotationRate` UCLASS — `ParticleModules.h/.cpp` 확장 또는 별도 `ParticleModuleMeshRotationRate.h/.cpp` | M1 결합 시 §4.1 | Spawn hook: `Spawn(Owner, Particle, SpawnTime)` 에서 payload `RotRate` 초기화 (Color/Size 패턴 답습). Update hook: `Update(Owner, dt)` 에서 chain 순회 — payload `Rotation += RotRate*dt` 누적. BuildInstanceData 에 변경 0 (이미 payload `Rotation` read) | 1건 | 0건 (`FMeshRotationPayload` 의 RotRate 슬롯 이미 존재) | 0건 | §7-4, §5.4 위험 13 (Update module의 payload access path), §5.4 위험 14 (M1 결합) |
| **R1** (Ribbon CameraFacing) | (A) `URibbonTypeData::EViewAlignment` enum 멤버 + 기존 4 멤버 옆 / (B) 신규 module class | R3 와 cross plane 충돌 (§4.2) | `ComputePerpendicular(Tangent)` ([ParticleRibbonEmitterInstance.cpp:16](../JSEngine/Source/Engine/Particle/ParticleRibbonEmitterInstance.cpp:16)) — 분기 추가 또는 ref axis 인자화 | (A) 0건 / (B) 1건 | 0건 | 0건 (옵션 α/β) | §5.4 위험 12 (camera 접근), §7-4 (옵션 B) |
| **R2** (Ribbon Color Interp) | (A) `URibbonTypeData::StartColor / EndColor` UPROPERTY 추가 / (B) 신규 module class | 기존 `UParticleModuleColor` 와 의미 충돌 가능 (lifetime 기준 vs chain 기준) | `BuildVertexBuffer` ([ParticleRibbonEmitterInstance.cpp:285-295](../JSEngine/Source/Engine/Particle/ParticleRibbonEmitterInstance.cpp:285)) — V0.Color 결정 시 chain position 기반 lerp | (A) 0건 / (B) 1건 | 0건 (per-vertex color layout 그대로) | 0건 | §5.4 위험 15 (ColorModule 와 결합 정의 누락) |
| **R3** (Ribbon Sheets Per Trail) | UPROPERTY 이미 존재 (`URibbonTypeData::SheetsPerTrail` [.h:42-43](../JSEngine/Source/Engine/Particle/ParticleModuleTypeDataRibbon.h:42)). 신규 정의 0. **read + UI + BuildVertexBuffer 분기만 추가** | R1 과 cross plane 충돌 (§4.2) | `BuildVertexBuffer` 의 chain 순회 외부 wrapper — Sheets 마다 다른 RotAngle 의 Perp 로 N회 strip 생성 + sheet 간 degenerate seam | 0건 | 0건 | 0건 (vertex stream 그대로) | §5.4 위험 16 (Sheets > 1 시 VB 용량 — InstanceBuffer grow), §5.4 위험 17 (degenerate seam 다층화) |

### 5.3 사용자 결정 필요 항목 (lock-in 후보 — 본 cycle 결정 0건 유지, 후보만 enumerate)

#### 결정 16 (M1 도입 위치)
- **옵션 A**: `UMeshTypeData::EMeshAlignment` 멤버. **Claude 권고**: A — Cycle 13a 의 `UBeamTypeData::EBeam2Method` (잠재) 와 동일 패턴, TypeData 가 alignment의 single source.
- **옵션 B**: 신규 `UParticleModuleMeshAlignment` UCLASS. UE Cascade의 module 분리 답습.

#### 결정 17 (M1 enum 값 범위)
- **옵션 A (minimal)**: `PSA_Velocity` 1개만. Cycle 11 의 단일 issue 원칙 답습.
- **옵션 B**: `PSA_Velocity` + `PSA_FacingCameraPosition` 2개 — UE Cascade 의 main use cases.
- **옵션 C**: 4종 전부 (`PSA_Velocity`, `PSA_LockedAxis`, `PSA_FacingCameraPosition`, `PSA_Rectangle`).
- **Claude 권고**: B — single issue 원칙 + camera 접근 경로 (§4.6) 검증.

#### 결정 18 (Camera 접근 옵션 α/β/γ)
- **옵션 α**: `BuildInstanceData(const FRenderBus&)` signature 확장. 모든 derived 변경. EditorMainPanelDebug 호출처 fallback 필요.
- **옵션 β**: Component-cached camera (frame 시작 시 RenderBus → Component 멤버 저장). signature 변경 0건.
- **옵션 γ**: shader VS-side 결정. CPU 계산 0건. InstanceVB 에 alignment mode 추가 필요.
- **Claude 권고**: β — signature 변경 최소 + 본 엔진의 캐싱 패턴 (Cycle 12 `EnsureTrailState`) 일관성.

#### 결정 19 (M2 module 종류)
- **옵션 A**: `bSpawnModule = true; bUpdateModule = true;` (Color/Size 패턴). Spawn에서 RotRate 초기값 + Update 에서 chain 순회 누적.
- **옵션 B**: Spawn module 만 (RotRate 값 채움) + Update 누적은 `FParticleMeshEmitterInstance::Tick` override 에서 수행.
- **Claude 권고**: A — Color/Size 검증 패턴 답습.

#### 결정 20 (M2 의 payload access path)
- **옵션 A**: `FParticleMeshEmitterInstance::GetMeshPayload` 를 public 화 (또는 ActiveIdx 받는 public helper 신설). M2 module 이 Cast 후 호출.
- **옵션 B**: base `FParticleEmitterInstance` 에 `GetPayloadAt<T>(SlotIndex)` template 추가 — 다른 emitter 도 사용 가능. **base 변경 — 별도 cycle 후보로 분리**.
- **Claude 권고**: A — 본 cycle 내부 가능, 회귀 표면적 최소.

#### 결정 21 (M1 + M2 결합 공식)
- **옵션 A**: `Final = AlignmentRot * AccumulatedRot` (Rotate 후 Spin) — UE Cascade 표준.
- **옵션 B**: M1 단독 활성 / M2 단독 활성 (양자택일). 사용자가 module 셋업으로 선택.
- **Claude 권고**: A — UE 표준 + 사용자 직관 (mesh 가 정렬되면서 동시에 spin).

#### 결정 22 (R1 도입 위치 / 옵션)
- **옵션 A**: `URibbonTypeData::EViewAlignment` enum 멤버 (`World_Up` / `CameraFacing` 등).
- **옵션 B**: 신규 `UParticleModuleRibbonViewAlign` UCLASS.
- **Claude 권고**: A — 결정 16 (M1) 과 일관성.

#### 결정 23 (R1 의 ViewDir 정의)
- **옵션 A**: `ViewDir = CameraForward` (per-frame 단일 값). 더 단순.
- **옵션 B**: `ViewDir = (CameraPos - VertexPos).Normalize()` (per-vertex). 정확도 ↑, 비용 ↑.
- **Claude 권고**: A — 본 엔진의 SubUVBatcher / Billboard 패턴 일관성 (frame-단위 camera 캐싱).

#### 결정 24 (R2 module 위치)
- **옵션 A**: `URibbonTypeData::StartColor + EndColor` 2 UPROPERTY 추가.
- **옵션 B**: 신규 `UParticleModuleRibbonColorInterp` UCLASS.
- **Claude 권고**: A — 결정 16/22 일관성. interp 기능이 mesh/sprite 와 다른 의미 (chain 기반) 이므로 module 분리 가능하지만 본 cycle 단순화.

#### 결정 25 (R2 보간 기준)
- **옵션 A**: chain position (head=0, tail=1 normalized).
- **옵션 B**: Distance (head=0, tail=AccumDist normalized).
- **옵션 C**: 둘 다 + UPROPERTY enum 으로 선택.
- **Claude 권고**: A — chain position 이 chain 길이 변동에 안정적 + 계산 단순 (Tick 의 chain 순회 시 index counter 만 추가).

#### 결정 26 (R2 와 기존 ColorModule 결합)
- **옵션 A**: 두 module 모두 활성화 가능 — `Final = ColorLifeLerp * RibbonChainLerp` (multiply).
- **옵션 B**: 양자택일 — R2 가 활성화되면 `Particle->Color` 덮어쓰기.
- **Claude 권고**: A — Color 가 분리된 dimension (시간 vs 공간).

#### 결정 27 (R3 strip topology)
- **옵션 A**: 단일 strip + degenerate seam (Cycle 12 multi-trail 패턴 답습). Draw 1회.
- **옵션 B**: multi-draw (Sheets 회 Draw).
- **Claude 권고**: A — 일관성 + 성능.

#### 결정 28 (R1 + R3 결합 정책)
- **옵션 A**: Sheet 0 만 CameraFacing 적용, Sheet 1~N-1 은 world Up reference 유지.
- **옵션 B**: CameraFacing 활성 시 Sheets 강제 1 (R3 무시).
- **옵션 C**: 모든 Sheet 가 CameraFacing 적용 (의미 충돌이지만 사용자 결정).
- **Claude 권고**: A — cross 의미 보존.

#### 결정 29 (M1/M2/R1/R2/R3 단일 cycle vs 분할 cycle)
- **옵션 A**: 5건 한 cycle (Cycle 14) — Cycle 13a 의 통합 사례 답습.
- **옵션 B**: Mesh (M1+M2) cycle 14, Ribbon (R1/R2/R3) cycle 15 — 단일 영역 원칙.
- **옵션 C**: 각 module 별 sub-cycle (14a/b/c, 15a/b/c).
- **Claude 권고**: B 또는 C — 사용자 prompt 의 "단일 cycle 단일 issue 원칙" 명시 (§3 M2 노트). 본 진단 결과 silent bug 후보 6건 (위험 12-17) 식별 — Cycle 12/13a 의 4건/5건 수준 초과 → 분할 권장.

### 5.4 silent bug 후보 (Phase 5 — 위험 12-17)

신규 6건 식별. Cycle 12 위험 1-4 + Cycle 13a 위험 5-11 답습 형식. (위험 번호는 prompt §2.1 의 silent bug §7-1 ~ §7-7 + ι ~ ξ 외 본 cycle 신규.)

#### 위험 12 (camera 접근 경로 누락) — **M1 / R1 공통**
- **시나리오**: M1 의 PSA_FacingCameraPosition / R1 의 CameraFacing 도입 시 BuildInstanceData / BuildVertexBuffer 가 camera 정보 없이 호출되면 NaN 또는 zero-vector 결과. 시각적으로 보면 mesh 가 안 그려지거나 ribbon strip 가 invisible.
- **방어 위치 후보**:
  - 옵션 (α) 채택 시: signature 에 camera 강제 → compile-time 보장. Risk 0.
  - 옵션 (β) 채택 시: Component 가 frame 시작 시 RenderBus → cache. cache 가 갱신 안 된 frame (예: 첫 frame) 검사 필수.
  - 옵션 (γ) 채택 시: VS uniform 갱신 누락 시 검사.
- 매칭: §7-1 (default fallback silent rendering) 변형.

#### 위험 13 (M2 Update module 의 payload access path 누락)
- **시나리오**: M2 의 `Update(Owner, dt)` 에서 `Cast<FParticleMeshEmitterInstance>(Owner)` 실패 시 (Mesh emitter 가 아닌 곳에 mistakenly 추가됨) → segfault 또는 silent no-op.
- **방어 위치 후보**: `Update` 진입에서 `Cast` 결과 nullptr 검사 + Mesh emitter 가 아니면 early return.
- 매칭: 본 cycle 신규.

#### 위험 14 (M1 와 M2 결합 시 회전 누적 무시)
- **시나리오**: 결정 21 의 결합 공식이 lock-in 안 된 상태에서 구현 시 M1 이 매 frame InstanceRotation 을 덮어쓰면 M2 의 누적 RotRate 가 무효화.
- **방어 위치 후보**: `BuildInstanceData` 의 InstanceRotation 산출 시 alignment 결과 + payload 누적 결과를 명시 결합. 결정 21 lock-in 전에 implement 진입 금지.

#### 위험 15 (R2 와 기존 ColorModule 결합 시 의미 충돌)
- **시나리오**: 결정 26 lock-in 안 된 상태에서 두 module 모두 활성화 → 둘 중 어느 게 우선인지 모호. 시각 결과 예측 불가.
- **방어 위치 후보**: `BuildVertexBuffer` 의 vertex.Color 결정 시 명시 multiply (옵션 A) 또는 R2 단독 사용 명시 (옵션 B).

#### 위험 16 (R3 Sheets > 1 시 InstanceBuffer 용량 폭증)
- **시나리오**: R3 Sheets = 5, MaxTrailCount = 3, MaxParticleInTrailCount = 64 → vertex 수 = 3 × 64 × 2 × 5 = 1920. 현재 InitialCapacity 256 → grow-by-2x 3회 (256→512→1024→2048). first frame 에서 GPU buffer realloc 3회 → frame spike 가능.
- **방어 위치 후보**:
  - InitialCapacity 를 Sheets × MaxParticles 추정치로 grow.
  - 또는 EnsureGPUResources 에 SheetsPerTrail UPROPERTY read 후 capacity 초기 계산.
  - 옵션 (i) 채택 시 dynamic VB 1개로 충분 (Draw 1회).
- 매칭: 본 cycle 신규.

#### 위험 17 (R3 Sheets > 1 시 degenerate seam 다층화 누락)
- **시나리오**: Sheets = 3 일 때 Sheet 0/1/2 사이에 degenerate seam 없으면 sheet 간 strip 연결되어 invalid geometry. 시각: 검은 patch 또는 무한선.
- **방어 위치 후보**: `BuildVertexBuffer` 의 Sheets 루프 외부에서 마지막 vertex 복제 (각 sheet 마지막 vertex + 다음 sheet 첫 vertex 둘 다 복제 필요할 수 있음 — 정확한 공식 검증 필요).
- 매칭: Cycle 12 의 위험 2 (chain dead-end) 일반화.

### 5.5 다음 implement cycle 후보 — 단일 영역 원칙 준수

본 prompt §3 의 "공통 infra 영향이 큰 항목은 별도 cycle 로 명시 분리" 원칙 따름.

#### 후보 1: Cycle 14 (Mesh 추가 module 통합) — M1 + M2
- 근거: M1 와 M2 가 §4.1 의 결합 공식 (결정 21) 으로 강결합. 분리 시 결정 21 lock-in 시점이 어려움.
- 단 위험 13 (M2 의 Update payload access) 는 (결정 20 옵션 B 채택 시) base class 변경 — **별도 cycle 분리 권고** (결정 20 의 옵션 A 권고 따르면 본 cycle 통합 가능).

#### 후보 2: Cycle 15 (Ribbon 추가 module 통합) — R1 + R2 + R3
- 근거: R1 와 R3 가 §4.2 의 결합 공식 (결정 28) 으로 강결합. R2 는 R1/R3 와 결합 0.
- 단 R3 는 dormant UPROPERTY 의 활성화 수준 — 가장 단순 → **별도 sub-cycle (15a R3) → (15b R1/R2)** 분할도 가능.

#### 후보 3: Camera-aware BuildInstanceData/BuildVertexBuffer 인프라 (결정 18) — 별도 cycle 분리 권고
- 근거: 결정 18 옵션 (α) 채택 시 `Component->BuildInstanceData()` signature 변경 → 호출처 2건 ([cpp:582](../JSEngine/Source/Engine/Render/Scene/PrimitiveDrawCommandBuilder.cpp:582), [EditorMainPanelDebug.cpp:170](../JSEngine/Source/Editor/UI/EditorMainPanelDebug.cpp:170)) + derived 시그니처 강제 변경. **base class 변경 — 회귀 표면적 큼**.
- 옵션 (β) 채택 시: Component 멤버 1개 추가만 — 본 cycle 내부 가능.
- → **결정 18 옵션 (α) 가 채택될 경우만 별도 cycle 분리**.

#### 후보 4: M2 의 Update payload access 인프라 (결정 20 옵션 B 채택 시) — 별도 cycle 분리 권고
- 근거: base `FParticleEmitterInstance` 에 template helper 추가 — Mesh/Ribbon/Beam 모두 영향. **결정 20 옵션 A 권고 시 본 cycle 내부**.

#### 후보 5: Sprite 의 RotationRate Spawn/Update module (M2 와 평행) — 별도 cycle
- 근거: `FBaseParticle.Rotation/RotationRate` 단일 float 시스템 도 동일 결손 (§2.6). M2 의 Mesh 한정성과 분리.

#### 후보 6: Cycle 12c (Ribbon 디버그 시각화 + MaxParticleInTrailCount 강제) — Cycle 12 결과 보고서 §11 의 이관 항목
- 근거: Cycle 12 의 결정 9 옵션 B 분리 결과 — 본 prompt 의 5건 module 과 무관.

#### 종합 권고 (Claude 의견)
- **Cycle 14 (Mesh M1+M2)** + **Cycle 15 (Ribbon R1+R2+R3)** 의 2-cycle 분할 권고. Cycle 13a/b 의 통합 사례 답습. 단 결정 18 옵션 α 채택 시 인프라 cycle 추가 + 결정 20 옵션 B 채택 시 인프라 cycle 추가 (최대 +2).
- **사용자 결정 14건 (결정 16-29)** lock-in 후 plan 진입 권장.

---

## §6 결론 한 줄

> **Cycle 14 진입 가능 — 단 사용자 결정 14건 (결정 16-29) lock-in 후 plan 작성**. baseline 은 Cycle 13b 종료 시점 (Sprite 112B / Mesh 144B / Ribbon 144B / Beam 208B) 으로 확정. M1 (Mesh Alignment) + M2 (RotationRate) + R1 (Ribbon CameraFacing) + R2 (Color Interp) + R3 (Sheets) 5건 모두 신규 silent bug 후보 6건 (위험 12-17) 식별. payload struct 변경 0건 (M2의 RotRate 슬롯·R3의 SheetsPerTrail UPROPERTY 모두 dormant — 이미 존재). 신규 UCLASS 0~5건 (결정 16/22/24/29 옵션에 따라 다름). **camera 접근 경로 (결정 18)** 가 본 cycle 의 가장 큰 인프라 결정 — 옵션 (α) 채택 시 별도 cycle 분리 권고. 5건 module 의 결합 정책 (M1↔M2, R1↔R3, R2↔ColorModule) 3건이 본 cycle 의 critical lock-in. **공통 infra 영향이 큰 항목 (결정 18 α + 결정 20 B)** 두 항목은 §5.5 후보 3/4 로 별도 cycle 분리 명시. **다음 단계**: 사용자가 결정 16-29 검토 → lock-in → `Cycle14_ImplementPlan.md` 작성 진입.
