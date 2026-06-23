# Mesh / Ribbon / Beam Emitter 도입 — 공통 Infra 현황 진단

작성일: 2026-05-25
대상 브랜치: `feature/ParticleRender`
모드: **진단(diagnose only) — 코드 변경 없음**
선행 문서:
- [RenderDataFlow.md](RenderDataFlow.md) — 진단의 전제 (변경 대상 아님)
- [particle_class_relation.md](particle_class_relation.md)
- [Cascade_Porting_Status.md](Cascade_Porting_Status.md) — Cycle 1–7 핸드오프 + silent bug §7
- [Particle_ControlFlow_Diagnosis.md](Particle_ControlFlow_Diagnosis.md)
- [SubUVModule_Introduction_Diagnosis.md](SubUVModule_Introduction_Diagnosis.md) — §7 Hop 6에서 본 진단의 사전 식별 있음
- [SubUVModule_Cycle_Report.md](SubUVModule_Cycle_Report.md) — Cycle 7 verification

본 진단의 전제 (재논의 금지):
- TypeData 패턴 (UE Cascade) 채택 확정
- `EParticleEmitterRenderMode { Sprite, Mesh, Beam, Ribbon }` 라우팅 키 확정
- DynamicData 계층 (`FDynamicEmitterDataBase`)은 별도 Track — 본 진단 범위 외
- 잠정 구현 순서: 공통 infra → Mesh → Ribbon → Beam

---

## 1. 진단 사전 조사 — 프로젝트 전수 결과

### 1.1 Particle 외부에 Mesh/Ribbon/Beam 렌더 인프라 재사용 후보

- [없음] `RibbonComponent` / `BeamComponent` / `FRibbon` / `FBeam` / `MeshParticle` / `RibbonEmitter` / `BeamEmitter` 키워드 grep — **코드 0건** (Document에만 언급)
- [참고] Line 렌더 인프라 존재: `FLineBatcher`, `FLineConstants` ([RenderCommand.h:468](JSEngine/Source/Engine/Render/Scene/RenderCommand.h:468)) — Beam/Ribbon strip 정점 생성에 직접 재사용은 불가하나, dynamic VB 패턴 참고 가능
- [참고] Procedural mesh: `UProceduralMeshComponent` + `MeshBufferManager.GetProcMeshBuffer(...)` ([PrimitiveDrawCommandBuilder.cpp:510](JSEngine/Source/Engine/Render/Scene/PrimitiveDrawCommandBuilder.cpp:510)) — Beam/Ribbon의 동적 strip VB 패턴 참고 가능
- [참고] Static mesh draw path: `EVertexFactoryType::StaticMesh` + `MeshBufferManager.GetMesh(UUID)` — **Mesh emitter는 이 path 직접 재사용 가능**

### 1.2 RenderMode enum의 현재 사용처 (재확인)

- [있음] [ParticleTypes.h:11-17](JSEngine/Source/Engine/Particle/ParticleTypes.h:11) — enum 정의
- [있음] [ParticleTypes.h:92](JSEngine/Source/Engine/Particle/ParticleTypes.h:92) — `FParticleEmitterRuntimeView::RenderMode`
- [있음] [ParticleModules.h:21, 36](JSEngine/Source/Engine/Particle/ParticleModules.h:21) — `UParticleModuleRequired::RenderMode` getter + private 멤버 (**non-UPROPERTY**, 에디터에서 변경 불가)
- [있음] [ParticleEmitterInstance.cpp:217](JSEngine/Source/Engine/Particle/ParticleEmitterInstance.cpp:217) — `RuntimeView.RenderMode = CurrentLODLevel->GetRequiredModule()->GetRenderMode()` (단순 미러)
- [있음] [EditorParticleSystemWidget.cpp:282-294](JSEngine/Source/Editor/UI/EditorParticleSystemWidget.cpp:282) — UI label switch (`CPU Sprites` / `Mesh Particles` / `Beam` / `Ribbon`)
- [없음] Builder/RenderPass/Component 분기 — `PrimitiveDrawCommandBuilder::case EPT_ParticleSystem` 본문에서 RenderMode 분기 0건. 모든 emitter를 Sprite로 처리

---

## 2. 공통 Infra 9개 항목 현황 분류

| # | 항목 | 분류 | 위치 / 근거 |
|---|------|-----|------------|
| 1 | `UParticleModuleTypeDataBase` class 자체 | **[없음]** | [ParticleSystem.h:6](JSEngine/Source/Engine/Particle/ParticleSystem.h:6) — forward 선언만. 정의/구현 파일 grep 0건 |
| 2 | `UParticleLODLevel::TypeDataModule` 실제 사용처 | **[선언만]** | 슬롯 존재: [ParticleSystem.h:40](JSEngine/Source/Engine/Particle/ParticleSystem.h:40) (비-UPROPERTY raw 포인터). 사용처 0건 — `CacheModuleLists` ([ParticleSystem.cpp:8-41](JSEngine/Source/Engine/Particle/ParticleSystem.cpp:8))·`Init` ([ParticleEmitterInstance.cpp:18-49](JSEngine/Source/Engine/Particle/ParticleEmitterInstance.cpp:18))·`RecreateEmitterInstances` ([ParticleSystemComponent.cpp:30-46](JSEngine/Source/Engine/Particle/ParticleSystemComponent.cpp:30)) 어디에서도 read/write 없음 |
| 3 | `FParticleEmitterInstance::ParticleStride` payload-aware 계산 | **[없음]** | [ParticleEmitterInstance.cpp:30](JSEngine/Source/Engine/Particle/ParticleEmitterInstance.cpp:30) `ParticleStride = ParticleSize` 등호 그대로. payload 가산 로직 없음. Template 없는 경로도 [cpp:38](JSEngine/Source/Engine/Particle/ParticleEmitterInstance.cpp:38) `ParticleStride = sizeof(FBaseParticle)` 동일 |
| 4 | `FParticleEmitterInstance::InstanceData / InstancePayloadSize / PayloadOffset` 사용처 | **[선언만]** | 선언: [ParticleEmitterInstance.h:59-61](JSEngine/Source/Engine/Particle/ParticleEmitterInstance.h:59). 사용: [Reset()](JSEngine/Source/Engine/Particle/ParticleEmitterInstance.cpp:54-70)의 `delete[] InstanceData` + 모두 0으로 리셋만. **`new` 호출처 0건**. PayloadOffset/InstancePayloadSize 둘 다 초기값 0에서 변하지 않음 |
| 5 | `FParticleEmitterInstance::Tick / SpawnParticles / KillParticle` virtual 여부 | **[없음]** | 모두 non-virtual ([ParticleEmitterInstance.h:14, 16, 18](JSEngine/Source/Engine/Particle/ParticleEmitterInstance.h:14)). 클래스는 `struct` 선언, `virtual` 키워드 0건. 가상 소멸자도 없음 (line 11) — **type별 override 불가능** |
| 6 | `UParticleSystemComponent::RecreateEmitterInstances`의 TypeData 기반 instance 생성 분기 | **[없음]** | [ParticleSystemComponent.cpp:30-46](JSEngine/Source/Engine/Particle/ParticleSystemComponent.cpp:30) — 항상 `new FParticleEmitterInstance()` 만 호출. TypeData 조회/분기 0건 |
| 7 | `UParticleSystemComponent::BuildSpriteInstanceData`의 RenderMode 분기 | **[없음]** | [ParticleSystemComponent.cpp:189-226](JSEngine/Source/Engine/Particle/ParticleSystemComponent.cpp:189) — 함수명 자체에 "Sprite" 박힘. emitter 루프에서 무조건 `FSpriteParticleInstanceData`로 변환. RenderMode 조회 0건 |
| 8 | `UParticleSystemComponent::EmitterInstanceData` 컨테이너 타입의 type-agnostic 여부 | **[없음]** | [ParticleSystemComponent.h:65](JSEngine/Source/Engine/Particle/ParticleSystemComponent.h:65) — 타입 `TArray<TArray<FSpriteParticleInstanceData>>` 로 **Sprite 구체 타입 고정**. type-erased 컨테이너 아님 |
| 9 | `FParticleEmitterRuntimeView`의 type별 payload 접근 가능 여부 | **[부분/있음]** | [ParticleTypes.h:83-93](JSEngine/Source/Engine/Particle/ParticleTypes.h:83) — `ParticleData`/`ParticleIndices`/`ParticleStride`/`ParticleSize`/`RenderMode` 노출. **단, `PayloadOffset`/`InstancePayloadSize` 노출 없음** → payload 영역 접근 위한 offset 정보를 view에서 얻을 수 없음. 호출처 0건 (어떤 코드도 GetRuntimeView()를 안 부름) |

### 2.1 추가 발견 (9개 외)

- [없음] `FRenderCommand`의 type-agnostic particle 슬롯 — [RenderCommand.h:482-488](JSEngine/Source/Engine/Render/Scene/RenderCommand.h:482) `ParticleInstances`가 **`const FSpriteParticleInstanceData*` 구체 타입**. `ParticleTexture/SubUVColumns/Rows` 도 Sprite 전용 의미
- [없음] `FParticleRenderPass`의 RenderMode 분기 — [ParticleRenderPass.cpp:85-209](JSEngine/Source/Engine/Render/Renderer/RenderFlow/ParticleRenderPass.cpp:85) Quad VB(4)·IB(6)·`SpriteParticleCB(b8)`·`DrawIndexedInstanced(6, ...)` 모두 Sprite 고정
- [없음] `EVertexFactoryType` 의 Mesh/Beam/Ribbon Particle entry — [VertexFactoryTypes.h:19-32](JSEngine/Source/Engine/Render/Resource/VertexFactoryTypes.h:19) `SpriteParticle` 만 있음
- [있음 / 부분] `FParticleEmitterInstance::ConsumeSpawnCount` (Rate 기반 spawn 누적) — [ParticleEmitterInstance.cpp:270-281](JSEngine/Source/Engine/Particle/ParticleEmitterInstance.cpp:270) — Mesh/Ribbon/Beam 모두 사용 가능 (type-agnostic)
- [있음 / 부분] `UParticleEmitter::CacheEmitterModuleInfo` 의 `ParticleSize = sizeof(FBaseParticle)` ([ParticleSystem.cpp:48](JSEngine/Source/Engine/Particle/ParticleSystem.cpp:48)) — payload 없을 때만 정확. TypeData payload 도입 시 `+= TypeData->RequiredBytes()` 가산 필요
- [있음] Sprite 동작 검증된 path 전반 — Cycle 1–7 완료 (`Cascade_Porting_Status.md` §2). 회귀 안전 장치의 기준선

---

## 3. Emitter Type별 추가 Infra 식별

### 3.1 Mesh emitter

**(a) TypeData module class — `UParticleModuleTypeDataMesh`**
- [필요] `UStaticMesh* Mesh` UPROPERTY (`ReferenceType = Asset` — 직렬화 자동, [DecalComponent.h:46](JSEngine/Source/Engine/Component/DecalComponent.h:46) 패턴)
- [필요] `bool bOverrideMaterial` + `UMaterialInterface* OverrideMaterial`
- [필요] `EMeshScreenAlignment` (UE 호환): `MSA_Velocity`, `MSA_MeshFaceCameraWithRoll`, `MSA_MeshFaceCameraWithLockedAxis`, `MSA_MeshFaceCameraWithSpin` — 추측: 초기 구현은 `MSA_Velocity` 1개만
- [필요] virtual hook: `RequiresPayloadBytes() → 0` / `CreateInstance(Component, EmitterIndex) → new FParticleMeshEmitterInstance`
- [필요] virtual hook: `GetVertexFactoryType() → EVertexFactoryType::MeshParticle` (또는 StaticMesh 재사용)

**(b) Particle payload**
- [없음/0 bytes] `FBaseParticle` 만으로 충분 (Location/Velocity/Size/Rotation/Color/Lifetime). 추측: orientation 기록이 필요하면 `FMeshRotationPayload { FVector InitialOrientation, FVector Rotation, FVector RotRate }` (~36 bytes) 도입 — 초기 구현은 `Particle.Rotation` 단일 float로 시작 권장

**(c) Emitter instance 파생 — `FParticleMeshEmitterInstance`**
- [필요] `FParticleEmitterInstance` 그대로 **상속만으로 충분** (Spawn/Tick/Kill 의미 동일)
- [override 불필요] Tick / SpawnParticles / KillParticle — base 동작 그대로
- [필요 전제] base class `FParticleEmitterInstance` 가 virtual 화 (현재 [없음], 공통 infra §2-5)

**(d) 렌더 어댑터**
- [필요] `FMeshParticleInstanceData` (`FSpriteParticleInstanceData` 대응) — per-instance: `FMatrix Transform` (또는 `Position + Quat + Scale`) + `FColor Color`. 추측 sizeof ≈ 64–80 bytes
- [필요] `EVertexFactoryType::MeshParticle` enum + Layout (Slot 0: mesh vertex `FNormalVertex`, Slot 1: per-instance transform/color) + `FVertexFactoryRegistry::Get` 명시 case (silent bug §7-1 회피)
- [필요] `BuildSpriteInstanceData` → `BuildInstanceData` 일반화 (RenderMode 분기 또는 instance 자체 virtual 메서드로 위임)
- [필요] `FRenderCommand` Mesh 슬롯 — 옵션: (i) `const FMeshParticleInstanceData* MeshParticleInstances` 추가, (ii) `ParticleInstances`를 `const void*` + `ParticleInstanceStride` 로 generic 화. **추측: (ii)가 type-agnostic 측면에서 옳지만 회귀 위험 큼 → 초기 구현은 (i) 별도 슬롯 추가가 안전**
- [필요] Mesh draw 분기: 기존 `MeshBuffer` 사용 (`Cmd.MeshBuffer = MeshBufferManager.GetMesh(Mesh->GetUUID())`) + `DrawIndexedInstanced(IndexCountPerInstance, InstanceCount, ...)`. ParticleRenderPass에 RenderMode 분기 또는 별도 Pass

**난이도: 낮음** — payload 0, instance 파생 형식적, 렌더 어댑터 1개 추가

### 3.2 Ribbon emitter

**(a) TypeData module class — `UParticleModuleTypeDataRibbon`**
- [필요] `int32 MaxTrailCount` (한 emitter당 동시 trail 수)
- [필요] `int32 MaxParticleInTrailCount`
- [필요] `float SheetsPerTrail` (mesh tessellation; 1=flat strip)
- [필요] `float TangentSpawningScalar` (spawn 시 tangent 강도)
- [필요] `bool bRenderGeometry / bRenderSpawnPoints / bRenderTangents` (디버그)
- [필요] virtual hook: `RequiresPayloadBytes() → sizeof(FRibbonTypeDataPayload)` / `CreateInstance() → new FParticleRibbonEmitterInstance`

**(b) Particle payload — `FRibbonParticlePayload`** (FBaseParticle 뒤에 byte payload)
- [필요] `int32 NextIndex` — linked list로 같은 trail 내 다음 particle 인덱스 (-1 = tail)
- [필요] `int32 PrevIndex` — 또는 head-only 양방향 보존
- [필요] `FVector Tangent` — 정점 위치에서 tangent 방향 (strip width 직교)
- [필요] `float SpawnedTangentStrength`
- [필요] `int32 TrailIndex` — 어떤 trail 소속인지
- [필요] `float Distance` — trail 시작부터 누적 거리 (UV용)
- 추측 sizeof ≈ 32–40 bytes

**(c) Emitter instance 파생 — `FParticleRibbonEmitterInstance`**
- [필요 override] **`KillParticle(Index)`** — swap-and-pop 금지 (linked list 깨짐). 대신 NextIndex/PrevIndex 재연결 + 슬롯을 free-list에 반환하는 방식 또는 trail head index 갱신
- [필요 override] `SpawnParticles` — 신규 particle을 head로 prepend + tangent 계산
- [필요 override] `Tick` — particle update 후 trail head 위치 변화로 tangent 재계산
- [필요] `TArray<int32> HeadIndices` — trail별 head particle 인덱스 (MaxTrailCount 만큼)
- [필요] Source linking — 어떤 transform이 trail의 출발점인지 (`Component->WorldLocation` 기본, 또는 별도 Source module)

**(d) 렌더 어댑터**
- [필요] `FRibbonParticleVertex` — strip 정점. per-vertex: Position(중앙) + 좌우 offset(tangent×width) + UV + Color + TrailIndex
- [필요] **Dynamic VB** (per-frame 생성) — `FInstanceBuffer` 패턴 응용 가능 (grow-by-2x). Sprite의 instanced quad가 아닌 명시 정점 스트림
- [필요] CPU에서 trail 순회 → strip 정점 unroll (head→tail) → VB 채우기
- [필요] `EVertexFactoryType::RibbonParticle` + Layout (Slot 0 per-vertex only, 인스턴싱 아님) + `Registry::Get` 명시 case
- [필요] Topology: `D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP` (Sprite의 TRIANGLELIST와 다름)
- [필요] ParticleRenderPass 분기 또는 `FRibbonParticleRenderPass` 별도 Pass — **추측: 토폴로지·VB 패턴 차이로 별도 Pass가 깔끔**
- [필요] `BuildInstanceData` 일반화 또는 instance-side build (Ribbon은 trail 순회 로직이 instance 내부 상태에 의존하므로 instance 자체에 build 위임이 더 자연스러움)

**난이도: 높음** — KillParticle override 정확성이 critical, dynamic strip VB 신규

### 3.3 Beam emitter

**(a) TypeData module class — `UParticleModuleTypeDataBeam2`**
- [필요] `int32 MaxBeamCount` (동시 beam)
- [필요] `EBeam2Method BeamMethod` (UE: `PEB2M_Distance` / `PEB2M_Target`)
- [필요] `float TextureTile / TextureTileDistance`
- [필요] `int32 Sheets` (cross-section)
- [필요] `int32 Frequency` (noise sample points)
- [필요] virtual hook: `RequiresPayloadBytes() → sizeof(FBeamParticlePayload)` / `CreateInstance() → new FParticleBeamEmitterInstance`

**(b) Particle payload — `FBeamParticlePayload`**
- [필요] `FVector SourcePoint` — beam 시작
- [필요] `FVector TargetPoint` — beam 끝
- [필요] `FVector SourceTangent` / `FVector TargetTangent` — curvature
- [필요] `int32 InterpolationSteps`
- [필요] (옵션) noise point 배열 포인터 또는 sentinel
- 추측 sizeof ≈ 56+ bytes (noise 제외)

**(c) Emitter instance 파생 — `FParticleBeamEmitterInstance`**
- [필요 override] **`Tick` 의미 변경** — Spawn은 beam 생성/유지 (매 프레임 1개 또는 source/target 변화 시 갱신). 일반 particle처럼 RelativeTime으로 죽지 않을 수 있음 (Beam은 instantaneous 또는 sustained 양 모드)
- [필요 override] `SpawnParticles` — Source/Target module 조회 후 SourcePoint/TargetPoint 페이로드 초기화
- [필요 override] `KillParticle` — swap-pop 가능 (Beam은 linked list 아님, particle = beam 1개)
- [필요] Source/Target 모듈 의존성 — `UParticleModuleBeamSource` / `UParticleModuleBeamTarget` / `UParticleModuleBeamNoise` 별도 모듈 클래스 신규 (이번 Beam cycle에 포함)
- [필요] Component → World source/target lookup hook (Component 외부 actor 참조 가능성)

**(d) 렌더 어댑터**
- [필요] `FBeamParticleVertex` — strip 정점 (Source→Target curvature 따라 unroll)
- [필요] Dynamic VB — `FInstanceBuffer` 패턴 응용 (Ribbon과 사실상 동일 형태)
- [필요] CPU에서 beam 1개당 Source→Target 보간 → 정점 unroll. Noise 적용 시 추가 perturbation
- [필요] `EVertexFactoryType::BeamParticle` + Layout + `Registry::Get` 명시 case
- [필요] Topology: TRIANGLESTRIP
- [필요] ParticleRenderPass 분기 또는 `FBeamParticleRenderPass` 별도 Pass
- [필요] `BuildInstanceData` 일반화 또는 instance-side build (Beam도 instance 상태 의존)

**난이도: 매우 높음** — Tick 의미 변경 + 다중 Source/Target 모듈 신규 + Noise 시스템

### 3.4 4카테고리 요약표

| 카테고리 | Mesh | Ribbon | Beam |
|---------|------|--------|------|
| (a) TypeData class 신규 | TypeDataMesh | TypeDataRibbon | TypeDataBeam2 |
| (b) Particle payload | 0 (또는 MeshRotation ~36B) | FRibbonParticlePayload (~32B, linked list) | FBeamParticlePayload (~56B) |
| (c) Instance 파생 | 형식적 (override 0) | KillParticle override 필수 + Tick/Spawn 일부 | Tick/Spawn 의미 변경 + Source/Target 모듈 신규 |
| (d) 렌더 어댑터 | MeshParticleInstanceData + 기존 MeshBuffer 재사용 | RibbonParticleVertex + Dynamic VB + TRIANGLESTRIP + 신규 Pass | BeamParticleVertex + Dynamic VB + TRIANGLESTRIP + 신규 Pass + Noise |

---

## 4. 충돌·리스크 식별

### 4.1 KillParticle swap-and-pop vs Ribbon linked list

- [위험: 높음] [ParticleEmitterInstance.cpp:192-202](JSEngine/Source/Engine/Particle/ParticleEmitterInstance.cpp:192) — `std::swap(ParticleIndices[Index], ParticleIndices[LastActiveIndex])`. Ribbon payload의 `NextIndex/PrevIndex`가 **물리 슬롯 인덱스(ParticleData의 위치)** 를 가리키면 swap 후에도 안전, 하지만 **active list 인덱스(ParticleIndices의 위치)** 를 가리키면 swap으로 link 깨짐
- [회피] Ribbon payload는 반드시 **물리 슬롯 인덱스**를 저장 + `FParticleRibbonEmitterInstance::KillParticle` 에서 swap-pop 대신 NextIndex/PrevIndex 재연결 + active list에서 슬롯 제거하는 별도 알고리즘
- [추측] base의 swap-pop이 일반 sprite에는 최적이므로 base를 그대로 두고 ribbon만 override가 정공법. Mesh/Beam은 base 그대로 OK

### 4.2 `BuildSpriteInstanceData`를 RenderMode 분기로 일반화할 때 호출자 영향

- [위험: 중간] 호출자 = [PrimitiveDrawCommandBuilder.cpp:547](JSEngine/Source/Engine/Render/Scene/PrimitiveDrawCommandBuilder.cpp:547) `ParticleSystemComponent->BuildSpriteInstanceData();` 1건만 존재 (grep 확인)
- [영향 범위] 함수명에 "Sprite" 박혀 있어 이름 변경 시 1건만 수정. 내부 분기로 변경 시 호출자 코드 동일하게 유지 가능
- [영향 범위 확장] `GetEmitterInstanceData(int32)` 의 반환 타입이 `const TArray<FSpriteParticleInstanceData>&` 로 고정 ([ParticleSystemComponent.h:33](JSEngine/Source/Engine/Particle/ParticleSystemComponent.h:33)) — Mesh/Ribbon/Beam은 다른 type을 반환해야 하므로 **함수 자체를 type별로 분리 (3–4 getter) 또는 type-erased 반환** 필요
- [추측] 가장 침습성 낮은 방식: emitter instance 자체가 "내 instance data를 빌드하고 builder에게 raw bytes+stride+vertex factory type을 노출" 하는 virtual 메서드를 갖는 형태. component는 dispatch만 함

### 4.3 TypeData payload 도입 시 기존 Sprite emitter의 `ParticleStride` 회귀 위험

- [위험: 낮음] [ParticleEmitterInstance.cpp:30](JSEngine/Source/Engine/Particle/ParticleEmitterInstance.cpp:30) `ParticleStride = ParticleSize`
- [회귀 가능 시나리오] payload-aware로 바뀌면 `ParticleStride = ParticleSize + TypeData->RequiredBytes()`. Sprite는 `RequiredBytes() = 0` 이므로 결과값 동일 → **회귀 없음**
- [영향처] [ParticleEmitterInstance.cpp:42](JSEngine/Source/Engine/Particle/ParticleEmitterInstance.cpp:42) `new uint8[ParticleStride * MaxActiveParticles]`, [cpp:166](JSEngine/Source/Engine/Particle/ParticleEmitterInstance.cpp:166) SpawnParticles의 `ParticleData + SlotIndex * ParticleStride`, [cpp:233](JSEngine/Source/Engine/Particle/ParticleEmitterInstance.cpp:233) GetParticle의 동일 계산 — 모두 ParticleStride 사용하므로 자동 적용 (변경 불필요)
- [안전 장치] payload bytes를 추가하기 전에 USpriteTypeData::RequiredPayloadBytes() = 0 부터 wire up 하고 Sprite 빌드 통과 확인 → 그 후 Mesh/Ribbon/Beam 도입

### 4.4 FParticleEmitterInstance virtualize 시 회귀

- [위험: 중간] 현재 `struct FParticleEmitterInstance` 는 virtual 0개, 소멸자 non-virtual ([ParticleEmitterInstance.h:11](JSEngine/Source/Engine/Particle/ParticleEmitterInstance.h:11))
- [회귀 가능 시나리오] `class`로 바꾸거나 가상 소멸자 추가 시 `sizeof(FParticleEmitterInstance)` 변동 (vtable pointer +8 bytes). [ParticleSystemComponent.cpp:42](JSEngine/Source/Engine/Particle/ParticleSystemComponent.cpp:42) `new FParticleEmitterInstance()` 호출처는 영향 없음. 단, **delete 시점에 base pointer만 들고 있어야 함** — [ParticleSystemComponent.cpp:55](JSEngine/Source/Engine/Particle/ParticleSystemComponent.cpp:55) `delete Instance` 가 base 포인터에서 호출되므로 **가상 소멸자 필수** (Mesh/Ribbon/Beam instance가 base 포인터로 저장될 때 derived 소멸자 호출 보장)
- [영향처] `TArray<FParticleEmitterInstance*> EmitterInstances` ([ParticleSystemComponent.h:61](JSEngine/Source/Engine/Particle/ParticleSystemComponent.h:61)) — base 포인터 컨테이너. 이미 polymorphic 기반 구조

### 4.5 `FRenderCommand`의 Sprite 고정 필드

- [위험: 중간] [RenderCommand.h:482-488](JSEngine/Source/Engine/Render/Scene/RenderCommand.h:482) `const FSpriteParticleInstanceData* ParticleInstances` 가 구체 타입 — Mesh/Ribbon/Beam은 별도 슬롯 추가 또는 generic 화 필요
- [옵션 i] type별 별도 슬롯: `MeshParticleInstances`, `RibbonVertices`, `BeamVertices` 4개 추가 — 회귀 0, 메모리 약간 낭비 (RenderCommand sizeof 증가)
- [옵션 ii] generic: `const void* ParticleData + uint32 InstanceCount + uint32 Stride + EVertexFactoryType` — RenderPass가 type 보고 분기. 회귀 위험 있음 (Sprite path 영향)
- [추측] 옵션 (i)가 본 cycle엔 안전. 후속 리팩토링으로 (ii) 검토 가능

### 4.6 silent bug §7 함정 매칭

`Cascade_Porting_Status.md §7` 의 함정과의 충돌:

| § | 함정 | 본 도입에서의 위험 |
|---|------|------------------|
| 7-1 | `FVertexFactoryRegistry::Get` default StaticMeshDesc | ⚠️ **직접 충돌**. Mesh/Ribbon/Beam VertexFactoryType 신규 시 명시 case 추가 누락 → silent하게 StaticMesh layout으로 그려져 디버깅 어려움 |
| 7-2 | `HashVertexLayout` 동기화 | ❌ 무관 (Cycle 1에서 이미 동기화됨) |
| 7-3 | `PickPasses[]`에 Particle 추가 금지 | ❌ 무관 (Mesh/Ribbon/Beam도 동일 정책) |
| 7-4 | vcxproj VS 자동 덮어쓰기 | ⚠️ **직접 충돌**. 신규 .h/.cpp (TypeData 4종 + instance 파생 3종 + vertex/instance struct 등) 다수 추가 → vcxproj 갱신 필수. Cycle 7의 `θ` 함정 (`.gen.cpp` 자동 생성 + vcxproj 등록)도 반복 |
| 7-5 | `EPT_ParticleSystem` case `return true` 종결 | ⚠️ **직접 충돌**. case 본문에 RenderMode 분기 추가 시 모든 분기 끝에서 `return true` 보장 필요 |
| 7-6 | SupportsOutline 가드 | ❌ 무관 |
| 7-7 | PassBatchers[Particle] 미등록 | ❌ 무관 (Mesh/Ribbon/Beam은 RenderPass가 직접 GetCommands 소비, batcher 우회) |

### 4.7 신규 silent bug 후보

- **ι** : TypeDataModule slot이 비-UPROPERTY ([ParticleSystem.h:40](JSEngine/Source/Engine/Particle/ParticleSystem.h:40)) — 직렬화 자동 안 됨. 도입 시 `UPROPERTY` 마크 누락하면 .particlesystem 저장 후 로드해도 TypeData가 nullptr → 모든 emitter가 Sprite로 fallback 되는 silent regression
- **κ** : `UParticleModuleRequired::RenderMode` 가 비-UPROPERTY private ([ParticleModules.h:36](JSEngine/Source/Engine/Particle/ParticleModules.h:36)) — 에디터에서 변경 불가. TypeData 도입 후에도 이 필드를 유지하면 "RenderMode와 TypeData 둘 중 어느 게 우선인지" 모호 → **둘 중 하나로 통일 필요**. 추측: TypeData를 single source of truth로 하고 RenderMode는 `TypeData ? TypeData->GetRenderMode() : Sprite` 로 derive
- **λ** : `BuildSpriteInstanceData` 가 component에 있어 매 viewport마다 재계산 (선행 진단 §7 α 후보) — type별 다양화 시 viewport 수 × type 수 곱연산 → 성능 영향 누적. 추측: 본 도입 cycle에서 캐시 무효 flag 추가 검토
- **μ** : Mesh emitter가 기존 StaticMesh path를 재사용하면 `Cmd.MeshBuffer = MeshBufferManager.GetMesh(UUID)`. **UUID 키 충돌 가능성** — 같은 mesh를 staticmesh actor와 particle이 공유할 때 buffer cache가 race 없는지 확인 필요. 추측: 현재 GetMesh는 mutex 없는 단일 스레드 가정

---

## 5. 구축 순서 추천

### 5.1 진단 결론 — 공통 infra 결손도

| 영역 | 결손 | 코멘트 |
|------|-----|-------|
| TypeData 클래스 체계 | **거의 전부 없음** | class 정의 0, slot은 비-UPROPERTY |
| Instance 가상화 | **전부 없음** | virtual 0, struct 선언 |
| Builder 분기 | **전부 없음** | RenderMode 미참조 |
| Component instance build/getter | **Sprite 고정** | 함수명·반환타입 모두 |
| RenderCommand 슬롯 | **Sprite 고정** | 구체 타입 포인터 |
| RenderPass | **Sprite 고정** | Quad·b8·DrawIndexedInstanced(6) |
| `InstanceData`/`InstancePayloadSize`/`PayloadOffset` 슬롯 | **선언만, new 0건** | per-emitter 인스턴스 payload 시스템 미구현 |
| `FParticleEmitterRuntimeView::RenderMode` | **있음/미사용** | RuntimeView getter 호출처 0 |
| Editor RenderMode 변경 | **불가능** | RequiredModule.RenderMode 비-UPROPERTY private |

**판단: 공통 infra 결손이 크다.** 잠정 합의(공통 infra → Mesh → Ribbon → Beam)는 **유지**하되, "공통 infra" 안에서 다시 sub-cycle로 쪼개야 한다.

### 5.2 추천 cycle 순서 (각 cycle = 한 영역)

#### Phase 1 — 공통 infra (필수, Mesh 진입 전에 모두 완료)

- **Cycle 8** — TypeData class 체계 도입 (회귀 없음 보장)
  - 작업: `UParticleModuleTypeDataBase` 정의 (신규 .h/.cpp) + `USpriteTypeData` 신규 (Sprite도 TypeData 시스템 안으로 편입) + `UParticleLODLevel::TypeDataModule` UPROPERTY화 + `CacheModuleLists()`에서 `Cast<UParticleModuleTypeDataBase>` 후 슬롯 캐싱
  - **완료 기준**: 기존 Sprite asset에 `USpriteTypeData` 1개 추가하고 빌드·실행 → 화면 동일 결과 (회귀 0). `LOD->TypeDataModule != nullptr` 디버거 확인
  - **회귀 안전 장치**: USpriteTypeData::RequiredPayloadBytes() = 0 보장 → ParticleStride 변경 없음. RenderMode getter는 TypeData 우선, fallback으로 RequiredModule.RenderMode

- **Cycle 9** — `FParticleEmitterInstance` virtualize
  - 작업: `struct` → `class` (또는 struct 유지하고 virtual만 추가), 가상 소멸자, Tick/SpawnParticles/KillParticle/GetParticleStride/BuildInstanceData(신규) virtual 화, ParticleStride 계산을 `ParticleSize + TypeData->RequiredPayloadBytes()` 로
  - **완료 기준**: 빌드·실행 → Sprite 동작 동일 (회귀 0). `sizeof(FParticleEmitterInstance)` 변동 측정·기록
  - **회귀 안전 장치**: Cycle 8의 USpriteTypeData가 0 byte payload → Stride 계산 결과 변동 없음

- **Cycle 10** — Component·Builder·RenderCommand·RenderPass 의 type-agnostic 화
  - 작업: `BuildSpriteInstanceData` → `BuildInstanceData(EmitterIdx)` 또는 instance-side virtual로 위임. `GetEmitterInstanceData<T>` 또는 type별 getter. `FRenderCommand` 에 mesh/ribbon/beam 슬롯 추가 (옵션 i). `EVertexFactoryType` 에 `MeshParticle/RibbonParticle/BeamParticle` 3종 enum + `Registry::Get` 명시 case (silent bug §7-1 회피, 단 layout/desc는 비워둠 - 다음 cycle에서 채움). `PrimitiveDrawCommandBuilder::case EPT_ParticleSystem` 에 RenderMode 분기 (Sprite 외 경로는 NOP, return true)
  - **완료 기준**: 빌드·실행 → Sprite 동작 동일. Mesh/Ribbon/Beam 분기는 정의되어 있으나 NOP라 결과 없음
  - **회귀 안전 장치**: 모든 분기가 명시 `return true` 종결 (silent bug §7-5)

#### Phase 2 — Mesh emitter (가장 단순)

- **Cycle 11** — UParticleModuleTypeDataMesh + FParticleMeshEmitterInstance(파생, override 없음) + MeshParticle VertexFactory Desc/Layout + ParticleRenderPass의 Mesh 분기
  - **완료 기준**: cube/sphere mesh asset 1개 + USpriteTypeData → UMeshTypeData 교체한 asset → 화면에 mesh particle 다수 표시

#### Phase 3 — Ribbon emitter (복잡)

- **Cycle 12a** — Ribbon payload (`FRibbonParticlePayload`) + `FParticleRibbonEmitterInstance` 의 KillParticle override + Spawn override (NextIndex/PrevIndex 연결)
  - **완료 기준**: trail 1개에 particle N개 동시 spawn → KillParticle 후에도 trail 무결성 (디버거에서 linked list traversal 확인)
- **Cycle 12b** — Ribbon dynamic strip VB + RibbonParticle VertexFactory + ParticleRenderPass의 Ribbon 분기 (또는 별도 Pass) + TRIANGLESTRIP
  - **완료 기준**: 화면에 ribbon strip 표시 (texture/material은 단순)

#### Phase 4 — Beam emitter (가장 복잡)

- **Cycle 13a** — Beam payload + Source/Target/Noise 모듈 신규 (별도 module class 3종) + `FParticleBeamEmitterInstance` 의 Tick/Spawn 의미 변경
  - **완료 기준**: source actor 위치를 빌드/이동 시 beam particle의 SourcePoint payload 자동 추적
- **Cycle 13b** — Beam dynamic strip VB + BeamParticle VertexFactory + Render 분기 + Noise 시각 검증
  - **완료 기준**: 두 점 간 beam + noise 화면 표시

### 5.3 각 Phase의 회귀 방지 공통 원칙

- 각 cycle 종료 시 **기존 Sprite 동작 동일성** 확인 (smoke test) — Cycle 5 Phase A 패턴 적용 가능
- `USpriteTypeData::RequiredPayloadBytes() = 0` 불변 — Mesh/Ribbon/Beam payload 도입이 Sprite에 영향 0 보장
- 새 UCLASS 추가 시 매번 `GenerateProjectFiles.py` 실행 (Cycle 7의 `θ` 함정)
- `EPT_ParticleSystem` case 의 RenderMode 분기 모든 끝에서 `return true` 명시 (silent bug §7-5)
- `EVertexFactoryType` 추가 시 `Registry::Get` 명시 case 동시 추가 (silent bug §7-1)
- `Hash` 관련 (Cycle 1처럼 layout field 추가가 있다면) `BuildInputLayoutFromDesc` 와 `HashVertexLayout` 동기화 (silent bug §7-2)

### 5.4 잠정 합의에 대한 미세 조정 권고

| 기존 합의 | 조정 권고 |
|----------|---------|
| 공통 infra → Mesh → Ribbon → Beam | **유지**, 단 공통 infra를 **Cycle 8/9/10 세 sub-cycle로 분할** |
| (sprite도 TypeData 시스템 안으로) | **명시화**: Cycle 8에서 USpriteTypeData를 함께 도입해야 회귀 안전. 이게 빠지면 Sprite 경로가 "TypeData 없음" 특수 케이스로 남아 분기 점증 |
| `UParticleModuleRequired::RenderMode` 잔존 | **deprecate 권고**: TypeData를 single source로. 본 결정은 Cycle 8 plan 작성 시 사용자 결정 항목 |
| RenderPass 분리 vs 단일 Pass + 분기 | **추측**: Mesh는 단일 Pass 분기, Ribbon/Beam은 토폴로지·VB 패턴 차이로 별도 Pass 권장. Cycle 11/12b/13b 작성 시 재검토 |

---

## 6. 본 진단의 한계 / 추측 영역

> 다음 항목들은 코드 직접 확인이 아닌 추론. 구현 cycle 진입 시 사용자 검토 권장.

- **추측 1**: Mesh emitter의 `FMeshRotationPayload`(~36B) 필요성 — `Particle.Rotation`(float 1개)만으로 충분한지, 3축 회전이 필요한지는 사용 케이스에 따라 다름. 초기 구현은 단일 float 권장하고 필요 시 payload로 확장
- **추측 2**: Ribbon `KillParticle` override의 정확한 알고리즘 — UE Cascade는 free-list + head index 갱신 방식. 본 진단에서는 알고리즘 outline만 식별, 정확한 구현 패턴은 Cycle 12a plan 작성 시 UE 참고 필요
- **추측 3**: Beam의 Source/Target 모듈 — 단순 actor reference인지, 다른 component lookup인지, 더 추상적인 source인지는 본 엔진의 actor/component 모델에 맞춰 결정. 초기 구현은 component world location 1개 권장
- **추측 4**: RenderPass 분리 vs 단일 — Sprite + Mesh는 instanced quad/mesh로 공통점 있어 단일 Pass 분기 가능. Ribbon/Beam은 dynamic VB + TRIANGLESTRIP으로 별도 Pass가 코드 단순. 정확한 trade-off는 Cycle 10/11/12b/13b 진입 시 측정
- **추측 5**: `FRenderCommand` 옵션 (i) vs (ii) — 본 cycle은 (i) 별도 슬롯이 안전하다고 판단했으나, RenderCommand sizeof 증가가 frame 당 N command × sizeof 만큼 cache 영향이 클 수 있음. 측정 필요
- **추측 6**: `μ` silent bug 후보 (MeshBuffer cache UUID 충돌) — `MeshBufferManager` 의 내부 동기화 보장 여부 실측 안 함. Mesh emitter cycle 진입 시 확인 필요
- **추측 7**: Beam의 Noise — `UParticleModuleBeamNoise` 의 데이터 모델(per-frame 재생성 vs per-particle 영구)이 UE Cascade와 정확히 매칭되는지 확인 필요. 초기 구현은 noise 제외 권장 (Phase 4 후속 cycle로)

---

## 7. 다음 cycle 진입 결정 (사용자 영역)

진단 결과 기반 권고:

> **공통 infra 결손이 크다.** 잠정 합의대로 **공통 infra 구현 cycle 먼저** 진입 권장. 단, 공통 infra 하나의 cycle로 묶기엔 변경 영역이 너무 커서 **Cycle 8 (TypeData 체계) → Cycle 9 (Instance virtualize) → Cycle 10 (Component/Builder/RenderCommand/RenderPass type-agnostic 화) 세 sub-cycle로 분할** 권장.

각 sub-cycle은 **기존 Sprite 동작 동일성 확인** 을 완료 기준으로 가져가서 회귀 방지. Phase 2 (Mesh) 는 Cycle 8–10 완료 후 진입.

본 진단의 한계 영역(§6)과 Cycle 8 plan 작성에 필요한 사용자 결정 항목은 별도로 추출되어야 함:

- (결정 1) `UParticleModuleRequired::RenderMode` 잔존 vs 제거 vs deprecate
- (결정 2) `FRenderCommand` 옵션 (i) 별도 슬롯 vs (ii) generic 슬롯
- (결정 3) RenderPass 분리 vs 단일 Pass 분기 (Mesh/Ribbon/Beam 각각)
- (결정 4) Mesh의 payload 0 vs MeshRotation 도입 (초기)
- (결정 5) Beam의 Noise를 Phase 4에 포함 vs 별도 cycle

---

## 8. 결론 한 줄

> Sprite 단일 emitter를 정상 구동시키는 **렌더링 path는 완전**하나, **emitter type 분기의 모든 hop**(TypeData class·LODLevel slot 사용·Instance virtualize·Component build/getter·RenderCommand 슬롯·RenderPass 분기)이 **거의 전부 부재**다. 잠정 합의 순서(공통 infra → Mesh → Ribbon → Beam)는 유지하되, 공통 infra 단계를 **Cycle 8/9/10 세 sub-cycle로 분할**하고 각 sub-cycle의 완료 기준을 **Sprite 동작 동일성** 으로 잡아 회귀 없이 type 분기 인프라를 단계적으로 구축한다. Mesh는 거의 형식적 파생 + 렌더 어댑터 1개 추가로 비교적 단순, Ribbon은 KillParticle override의 정확성이 critical, Beam은 Tick 의미 변경 + 다중 신규 모듈로 가장 복잡하다.
