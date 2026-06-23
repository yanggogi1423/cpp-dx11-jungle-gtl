# Particle System: ReplayData / DynamicData 인프라 진단 보고서

**작성일**: 2026-05-26
**대상 브랜치**: `feature/ParticleRender`
**모드**: **진단 전용 — 코드 변경 0건**
**선행 문서**:
- [Particle_ControlFlow_Diagnosis.md](Particle_ControlFlow_Diagnosis.md)
- [RenderDataFlow.md](RenderDataFlow.md)
- [ParticleEmitter_InfraCheck.md](ParticleEmitter_InfraCheck.md)
- [Cycle14_ImplementReport.md](Cycle14_ImplementReport.md)

---

## 0. 진단 컨텍스트

- 환경: 자체제작 엔진 (C++, DX11), Unreal Engine reference
- 이미 구현: Particle Emitter 모듈 시스템, EmitterInstance, 페이로드 처리
- 목표 개념: UE Cascade의 두 계층 구조
  - `FDynamicEmitterReplayDataBase` 계열: 한 프레임 EmitterInstance의 raw data 스냅샷 (POD스러움, 직렬화 가능)
  - `FDynamicEmitterDataBase` 계열: 그 스냅샷을 품고 game↔render 스레드로 전달되며 정점 생성/draw call을 수행하는 행위자 (가상 함수 보유)
- 분리 동기: race-free 소유권 이전 + raw data 재사용(직렬화/디버깅)

---

## 1. 코드베이스 스캔

### 1.1 Particle 디렉터리 위치
- [x] 핵심 코드: [JSEngine/Source/Engine/Particle/](../JSEngine/Source/Engine/Particle/) — 30+ 파일
- [x] 렌더 패스: [JSEngine/Source/Engine/Render/Renderer/RenderFlow/ParticleRenderPass.cpp](../JSEngine/Source/Engine/Render/Renderer/RenderFlow/ParticleRenderPass.cpp) / [.h](../JSEngine/Source/Engine/Render/Renderer/RenderFlow/ParticleRenderPass.h)
- [x] Cmd 생성: [JSEngine/Source/Engine/Render/Scene/PrimitiveDrawCommandBuilder.cpp:568-729](../JSEngine/Source/Engine/Render/Scene/PrimitiveDrawCommandBuilder.cpp:568) (`EPT_ParticleSystem` case)
- [x] 셰이더: [JSEngine/Shaders/Particle/](../JSEngine/Shaders/Particle/) — Sprite/Mesh/Ribbon/Beam HLSL

### 1.2 EmitterInstance 클래스 식별
- [x] **Base**: `FParticleEmitterInstance` — [ParticleEmitterInstance.h:16-110](../JSEngine/Source/Engine/Particle/ParticleEmitterInstance.h:16) (struct, UObject 아님, raw new/delete, virtual 소멸자 있음)
- [x] **Derived 3종**:
  - `FParticleMeshEmitterInstance` — [ParticleMeshEmitterInstance.h](../JSEngine/Source/Engine/Particle/ParticleMeshEmitterInstance.h)
  - `FParticleRibbonEmitterInstance` — [ParticleRibbonEmitterInstance.h](../JSEngine/Source/Engine/Particle/ParticleRibbonEmitterInstance.h)
  - `FParticleBeamEmitterInstance` — [ParticleBeamEmitterInstance.h](../JSEngine/Source/Engine/Particle/ParticleBeamEmitterInstance.h)
- [x] 페이로드 보유: `FParticleEmitterInstance::ParticleStorage` (POD heap, `ParticleData/ParticleIndices/Stride`) — [.h:84](../JSEngine/Source/Engine/Particle/ParticleEmitterInstance.h:84)
- [x] payload offset: `protected int32 PayloadOffset` — [.h:85](../JSEngine/Source/Engine/Particle/ParticleEmitterInstance.h:85). Init에서 `PayloadOffset = ParticleSize` ([.cpp:48](../JSEngine/Source/Engine/Particle/ParticleEmitterInstance.cpp:48)).

### 1.3 렌더 경로 식별
- [x] **CPU → 렌더 데이터 변환 진입점**: `UParticleSystemComponent::BuildInstanceData()` → `FParticleEmitterInstance::BuildInstanceData()` virtual ([.cpp:319-344](../JSEngine/Source/Engine/Particle/ParticleEmitterInstance.cpp:319))
- [x] **Cmd 생성**: Builder가 `Instance->Get{Sprite|Mesh|Ribbon|Beam}*Data(OutCount&)` 호출 후 `FRenderCommand`에 const 포인터+count로 채움 ([PrimitiveDrawCommandBuilder.cpp:623-672](../JSEngine/Source/Engine/Render/Scene/PrimitiveDrawCommandBuilder.cpp:623))
- [x] **GPU draw 발행**: `FParticleRenderPass::DrawCommand` → `VertexFactoryType` switch → `Render{Sprite|Mesh|Ribbon|Beam}Emitter` helper → `Draw{Indexed}Instanced` ([ParticleRenderPass.cpp:201-223](../JSEngine/Source/Engine/Render/Renderer/RenderFlow/ParticleRenderPass.cpp:201))

### 1.4 게임/렌더 스레드 분리 여부
- [x] **단일 스레드** — grep `std::thread|RenderThread|GameThread|ENQUEUE_RENDER_COMMAND` 결과 **Particle 코드 0건** (GameSplashScreen만 등장, 무관)
- [x] `Tick → Build → AddCommand → RenderPass.DrawCommand`가 같은 frame 안에서 sequential. Bus는 frame-scope 컨테이너 ([RenderDataFlow.md §2](RenderDataFlow.md))
- [x] 즉 **race를 막을 분리 필요성 자체가 부재**

### 결론 한 줄
> Sprite/Mesh/Ribbon/Beam 4종 emitter type별 instance·payload·렌더 경로가 모두 도입된 상태이며, **단일 스레드 엔진**이라 UE Cascade의 game↔render 스레드 비동기 모델 자체가 없다.

---

## 2. ReplayData 역할 존재 여부 점검

### 2.1 grep 결과 — UE Cascade 명시 식별자 부재
- [ ] `FDynamicEmitterReplayDataBase` / `FDynamicSpriteEmitterReplayDataBase` / `ReplayData` 키워드 **0건** ([JSEngine/Source/](../JSEngine/Source/))

### 2.2 "한 프레임 raw 메모리 + 메타데이터를 묶은 POD" 존재 여부
- [ ] **묶음 구조 부재** — raw 메모리(`ParticleStorage.ParticleData`)와 메타데이터(`ActiveParticles`, `Stride`, `PayloadOffset`, `MaxActiveParticles`, RenderMode)는 모두 `FParticleEmitterInstance`의 **분산된 멤버**로 존재 ([.h:84-103](../JSEngine/Source/Engine/Particle/ParticleEmitterInstance.h:84))
- [x] **부분적 묶음 존재**: `FParticleEmitterRuntimeView` ([ParticleTypes.h:83-93](../JSEngine/Source/Engine/Particle/ParticleTypes.h:83)) — `ParticleData/Indices/ActiveParticles/MaxActiveParticles/ParticleStride/ParticleSize/CurrentLODLevelIndex/RenderMode` 7 필드 묶음. 그러나:
  - 호출처: `GetRuntimeView()` 1건뿐, Builder/RenderPass에서 **사용 0건**
  - `PayloadOffset` / `InstancePayloadSize` 미포함 → payload 영역 access 불가
  - 직렬화 의도 없음 (POD view 용도)

### 2.3 "EmitterInstance 분리 — 렌더 측이 instance를 직접 참조하지 않게" 여부
- [x] **분리 부분 달성** — Builder가 `Instance->Get*Data(OutCount&)`로 const 포인터+count만 회수, `FRenderCommand`는 4종 타입별 슬롯(`ParticleInstances`/`MeshParticleInstances`/`RibbonVertices`/`BeamVertices`)에 raw 포인터 보유 — [RenderCommand.h:489-505](../JSEngine/Source/Engine/Render/Scene/RenderCommand.h:489)
- [ ] **그러나 데이터 소유권은 Instance에 그대로** — `SpriteInstanceDataBuffer`는 `FParticleEmitterInstance::SpriteInstanceDataBuffer` private 멤버 ([.h:109](../JSEngine/Source/Engine/Particle/ParticleEmitterInstance.h:109)). RenderCommand는 "instance 소유 메모리에 한 frame만 유효한 포인터"를 들고 있음 ([RenderCommand.h:489-490 주석](../JSEngine/Source/Engine/Render/Scene/RenderCommand.h:489): "InstanceData는 Component 소유, FRenderCommand는 포인터만 들고 다닙니다")
- [x] **렌더 패스는 Instance를 직접 참조하지 않음** — `FParticleRenderPass::DrawCommand`는 `FRenderCommand` 슬롯만 읽음 ([ParticleRenderPass.cpp:200-223](../JSEngine/Source/Engine/Render/Renderer/RenderFlow/ParticleRenderPass.cpp:200))

### 2.4 직렬화 가능성/흔적
- [ ] **0건** — `FSpriteParticleInstanceData` / `FMeshParticleInstanceData` / `FRibbonParticleVertex` / `FBeamParticleVertex` 4종 모두 **GPU 정점 layout용 POD** ([VertexTypes.h:82-90](../JSEngine/Source/Engine/Render/Resource/VertexTypes.h:82) 등) — `Serialize`/`FArchive`/`Save`/`Load` 마커 없음, 직렬화 의도 없음
- [x] (별개) `UParticleSystem` asset 직렬화는 `.particlesystem` json + UPROPERTY 기반으로 분리되어 있음 (asset 측, simulation snapshot과 무관)

### 2.5 종합 표
| 기대 (UE Cascade `FDynamicEmitterReplayDataBase` 류) | 현재 상태 | 위치 |
|----|----|----|
| 한 프레임 raw memory + 메타데이터 묶음 POD | **부재** (분산 멤버 / RuntimeView는 부분 묶음) | — |
| `ActiveParticleCount` / `ParticleStride` / `ParticleDataSize` 멤버 | **있음 (분산)** | `FParticleEmitterInstance.ActiveParticles/Stride/Size` |
| `eEmitterType` 식별자 | **있음** | `FParticleEmitterRuntimeView.RenderMode` ([ParticleTypes.h:92](../JSEngine/Source/Engine/Particle/ParticleTypes.h:92)) + `FRenderCommand.VertexFactoryType` ([RenderCommand.h:453](../JSEngine/Source/Engine/Render/Scene/RenderCommand.h:453)) |
| `SortMode` / `bRequiresSorting` | **부재** | — |
| Material/Texture 리소스 핸들 | **있음 (분산)** | `FRenderCommand.Material/ParticleTexture` ([RenderCommand.h:449,493](../JSEngine/Source/Engine/Render/Scene/RenderCommand.h:449)) |
| 직렬화 가능 (POD + offset only) | **부재** (POD이긴 하나 직렬화 의도 0) | — |

### 결론 한 줄
> Cascade의 "ReplayData" 한 묶음 POD은 **부재**, 그러나 동일 역할 데이터가 **`FParticleEmitterInstance` 멤버 + `FParticleEmitterRuntimeView` + `FRenderCommand` 슬롯 3곳에 분산**된 형태로 흩어져 있다.

---

## 3. DynamicData 역할 존재 여부 점검

### 3.1 grep 결과 — UE Cascade 명시 식별자 부재
- [ ] `FDynamicEmitterDataBase` / `FDynamicSpriteEmitterData` / `FDynamicMeshEmitterData` / `FDynamicRibbonEmitterData` / `FDynamicBeamEmitterData` / `GetDynamicData` 키워드 **0건**

### 3.2 "ReplayData를 멤버로 품고 가상 함수로 정점 stride/버퍼/정렬 수행하는 행위자" 존재 여부
- [ ] **별도 클래스 부재** — 명시적 "DynamicData 행위자" 클래스 없음
- [x] **행위 자체는 `FParticleEmitterInstance` virtual로 구현됨** — Instance 자체가 ReplayData(데이터) + DynamicData(행위) 두 역할을 **한 클래스에 묶어** 가짐:
  - virtual `BuildInstanceData()` — 정점 버퍼 채우기 ([.h:39](../JSEngine/Source/Engine/Particle/ParticleEmitterInstance.h:39))
  - virtual `GetSpriteInstanceData / GetMeshInstanceData / GetRibbonVertexData / GetBeamVertexData` — type별 데이터 노출 ([.h:45-48](../JSEngine/Source/Engine/Particle/ParticleEmitterInstance.h:45))
  - virtual `Tick / SpawnParticles / KillParticle` — 시뮬레이션 행위 ([.h:26-30](../JSEngine/Source/Engine/Particle/ParticleEmitterInstance.h:26))
  - virtual `GetRequiredPayloadBytes / GetRuntimeView` — 메타 노출 ([.h:34,65](../JSEngine/Source/Engine/Particle/ParticleEmitterInstance.h:34))
- [ ] **정렬 수행 함수 부재** — sort/depth-sort virtual 0건

### 3.3 Sprite/Mesh/Beam/Ribbon 타입별 상속 분기
- [x] **상속 분기 존재** — `FParticleEmitterInstance`를 base로 3개 derived (`Mesh`/`Ribbon`/`Beam`). Sprite는 base가 직접 처리 ([ParticleEmitterInstance.cpp:319-353](../JSEngine/Source/Engine/Particle/ParticleEmitterInstance.cpp:319) Sprite path)
- [x] type별 `BuildInstanceData` override가 자기 buffer만 채움:
  - Mesh: `MeshInstanceDataBuffer` 채움 ([ParticleMeshEmitterInstance.cpp](../JSEngine/Source/Engine/Particle/ParticleMeshEmitterInstance.cpp))
  - Ribbon: `RibbonVertexBuffer` 채움 (CPU side, dynamic strip vertices)
  - Beam: `BeamVertexBuffer` 채움
- [x] type별 `Get*Data` override가 자기 buffer 노출, 다른 3개는 base default(`nullptr/0`) ([.cpp:350-381](../JSEngine/Source/Engine/Particle/ParticleEmitterInstance.cpp:350))

### 3.4 종합 표
| 기대 (UE Cascade `FDynamicEmitterDataBase` 류) | 현재 상태 | 위치 |
|----|----|----|
| 별도 행위자 클래스 (ReplayData 보유) | **부재** | — |
| 가상 함수: 정점 stride 반환 | **있음 (다른 의미)** | `GetParticleStride()` (per-particle stride, 정점 X) — [.h:53](../JSEngine/Source/Engine/Particle/ParticleEmitterInstance.h:53) |
| 가상 함수: 정점 버퍼 채우기 | **있음** | virtual `BuildInstanceData()` — [.h:39](../JSEngine/Source/Engine/Particle/ParticleEmitterInstance.h:39) |
| 가상 함수: 정렬 수행 | **부재** | — |
| Sprite/Mesh/Ribbon/Beam 타입별 상속 | **있음 (Instance에 직접 상속)** | `FParticleMeshEmitterInstance` / `Ribbon` / `Beam` |
| Material/RenderState 노출 | **부재 (Builder가 TypeData에서 직접 추출)** | [PrimitiveDrawCommandBuilder.cpp:637-666](../JSEngine/Source/Engine/Render/Scene/PrimitiveDrawCommandBuilder.cpp:637) |

### 결론 한 줄
> Cascade의 "DynamicData 행위자" 클래스는 **부재**하나, 동일 행위(타입별 정점 버퍼 채우기·노출)가 **`FParticleEmitterInstance` 자체의 virtual 메서드들로 흡수**되어 있다. 즉 행위자가 데이터(EmitterInstance)와 분리되지 않고 일체화됨.

---

## 4. 스냅샷 경로 점검

### 4.1 "GetDynamicData()"에 해당하는 함수
- [ ] **명시적 단일 진입점 부재** — UE처럼 `EmitterInstance->GetDynamicData()`가 새 객체를 new해서 반환하는 경로 없음
- [x] **대응 경로 존재 (분산)**:
  - 1단계 `Component->BuildInstanceData()` → 2단계 `Instance->BuildInstanceData()` virtual ([ParticleSystemComponent.cpp BuildInstanceData](../JSEngine/Source/Engine/Particle/ParticleSystemComponent.cpp), [ParticleEmitterInstance.cpp:319-344](../JSEngine/Source/Engine/Particle/ParticleEmitterInstance.cpp:319))
  - 3단계 Builder가 `Instance->Get*Data(OutCount&)`로 const 포인터+count 회수 ([PrimitiveDrawCommandBuilder.cpp:625-668](../JSEngine/Source/Engine/Render/Scene/PrimitiveDrawCommandBuilder.cpp:625))
  - 호출 순서: `Builder.CollectPrimitive(EPT_ParticleSystem)` → `Component->CacheCameraFromRenderBus(Bus)` → `Component->BuildInstanceData()` → for each emitter: `Instance->Get*Data(Count) → Cmd.<TypeSlot>` → `Bus.AddCommand(Particle, Cmd)`
- [x] **호출 frequency**: frame당 1회 (정확히는 viewport당 1회). 별도 frame 캐시·dirty flag 없음

### 4.2 페이로드 오프셋 게임→렌더 전달
- [x] **현재 전달 안 됨** — `PayloadOffset`은 `FParticleEmitterInstance::protected` 멤버 ([.h:85](../JSEngine/Source/Engine/Particle/ParticleEmitterInstance.h:85)). derived(Mesh/Ribbon/Beam)는 base의 protected에 직접 access:
  - [ParticleMeshEmitterInstance.cpp:118](../JSEngine/Source/Engine/Particle/ParticleMeshEmitterInstance.cpp:118): `reinterpret_cast<FMeshRotationPayload*>(ParticleBase + PayloadOffset)`
  - [ParticleRibbonEmitterInstance.cpp:41](../JSEngine/Source/Engine/Particle/ParticleRibbonEmitterInstance.cpp:41): `reinterpret_cast<FRibbonParticlePayload*>(ParticleBase + PayloadOffset)`
  - [ParticleBeamEmitterInstance.cpp:125](../JSEngine/Source/Engine/Particle/ParticleBeamEmitterInstance.cpp:125): `reinterpret_cast<FParticleBeamPayload*>(ParticleBase + PayloadOffset)`
- [ ] **렌더 측은 PayloadOffset을 모름** — `FRenderCommand`에 PayloadOffset/Stride/raw ParticleData 필드 자체 없음. 대신 derived가 `BuildInstanceData()` 안에서 payload를 미리 풀어 `Mesh/Ribbon/Beam{InstanceData|Vertex}` 버퍼로 변환해 둠

### 4.3 렌더 측이 페이로드를 읽는 코드 경로
- [ ] **부재** — `FRenderCommand`/`FParticleRenderPass`는 `FBaseParticle` / payload 영역을 직접 read하지 않음. 모두 **사전에 변환된 vertex/instance data만** 다룸 ([ParticleRenderPass.cpp:262-278](../JSEngine/Source/Engine/Render/Renderer/RenderFlow/ParticleRenderPass.cpp:262)는 `FSpriteParticleInstanceData` 필드만 읽음)
- [x] **stride/offset 정합성 보장 방식**: derived가 `BuildInstanceData` 안에서 payload를 풀어내 미리 변환된 POD vertex(`FMeshParticleInstanceData` 56B / `FRibbonParticleVertex` 48B / `FBeamParticleVertex` 48B)로 정착 → 정점 layout(`FVertexFactoryDesc`)이 IA에 binding될 때 그 POD stride가 그대로 사용됨 ([VertexFactoryTypes.h](../JSEngine/Source/Engine/Render/Resource/VertexFactoryTypes.h) Layout/Desc switch)

### 결론 한 줄
> 게임 측이 매 frame `BuildInstanceData()`로 raw 페이로드를 사전 변환해 type별 vertex/instance POD 배열을 만들고, 렌더 측은 그 POD 배열의 const 포인터만 받음. **PayloadOffset은 게임 측 내부 비밀**로 렌더 측에 전달되지 않으며, 렌더 측은 raw payload를 read할 일 자체가 없다.

---

## 5. 진단 결과 분류

### 5.1 시나리오 판정: **B) 부분 존재 — 두 역할이 한 클래스에 섞여 있음**
- [x] 한쪽 역할만 있는 것이 아니라 **두 역할이 동일하게 일부씩 충족됨**:
  - ReplayData 역할: `FRenderCommand` 4종 슬롯의 const 포인터 + count + RenderMode + Texture/Material — **분산된 형태로** raw "render-ready" 스냅샷을 표현
  - DynamicData 역할: `FParticleEmitterInstance` virtual 메서드 그룹(`BuildInstanceData`/`Get*Data`) — Instance에 직접 흡수
- [x] **두 역할 모두 `FParticleEmitterInstance`에 일체화** + 일부가 `FRenderCommand`로 위임

### 5.2 섞임 양상 (구체 매핑)
| Cascade 책임 | 현재 담당 위치 | 분리도 |
|----|----|----|
| ReplayData: raw particle bytes | `FParticleEmitterInstance::ParticleStorage` ([.h:84](../JSEngine/Source/Engine/Particle/ParticleEmitterInstance.h:84)) | **Instance 소유** |
| ReplayData: ActiveCount/Stride/Size | `FParticleEmitterInstance::ActiveParticles/.../ParticleSize` (분산 멤버, 분리 안 됨) | **Instance 분산** |
| ReplayData: render-ready POD batch | `SpriteInstanceDataBuffer`/`MeshInstanceDataBuffer`/`RibbonVertexBuffer`/`BeamVertexBuffer` (per-instance TArray) | **Instance 소유 + Cmd가 raw 포인터로 참조** |
| ReplayData: RenderMode 식별 | `FParticleEmitterRuntimeView.RenderMode` ([ParticleTypes.h:92](../JSEngine/Source/Engine/Particle/ParticleTypes.h:92)) + `Cmd.VertexFactoryType` | **이중 보관** |
| DynamicData: 정점 버퍼 채우기 (virtual) | `FParticleEmitterInstance::BuildInstanceData()` ([.h:39](../JSEngine/Source/Engine/Particle/ParticleEmitterInstance.h:39)) | **Instance에 virtual** |
| DynamicData: 데이터 노출 (virtual getter) | `Get{Sprite|Mesh|Ribbon|Beam}*Data(uint32&)` ([.h:45-48](../JSEngine/Source/Engine/Particle/ParticleEmitterInstance.h:45)) | **Instance에 virtual** |
| DynamicData: 타입별 상속 분기 | `FParticleMeshEmitterInstance` / `Ribbon` / `Beam` | **Instance가 직접 분기** |
| DynamicData: sort/depth-sort | **부재** | — |
| 스레드 안전한 소유권 이전 | **불필요 (단일 스레드)** | — |
| 직렬화 가능성 | **없음** (현재 의도 없음) | — |

### 결론 한 줄
> 데이터 묶음(ReplayData)과 행위자(DynamicData) 두 책임이 **`FParticleEmitterInstance` 한 클래스에 동시 흡수되어 있고**, 그 일부 출력이 `FRenderCommand`의 const 포인터 슬롯으로 위임되는 형태. 명시 분리는 어디에도 없다.

---

## 6. 구현 방식 제안 (구현 금지, 책임만 나열)

### 6.1 신규 클래스/구조체 후보 — 이름과 책임만

**(현재 단일 스레드라는 전제에서 어디까지가 필요한지 확정 필요 — §7-Q1 참고)**

- [ ] `FDynamicEmitterReplayDataBase` (가칭)
  - 책임: 한 frame raw particle bytes + 메타데이터(`ActiveParticleCount`, `ParticleStride`, `ParticleSize`, `PayloadOffset`, `MaxActiveParticles`, `RenderMode`, sort hint) 묶음 POD
  - 소유: type별 derived가 자기 type-specific 필드 추가
- [ ] `FDynamicSpriteEmitterReplayData` / `FDynamicMeshEmitterReplayData` / `FDynamicRibbonEmitterReplayData` / `FDynamicBeamEmitterReplayData`
  - 책임: type별 추가 필드 (Sprite의 SubUV/Atlas 정보, Mesh의 MeshAsset/Material, Ribbon의 chain length cap, Beam의 InterpolationPoints/Noise data 등)
- [ ] `FDynamicEmitterDataBase` (가칭)
  - 책임: 위 ReplayData를 **owned member로 보유** + virtual hook 그룹:
    - `virtual void GetVertexStride() const`
    - `virtual void FillVertexBuffer(...)` (현 BuildInstanceData를 이쪽으로 이관)
    - `virtual void Sort(...)` (현 부재)
    - `virtual EVertexFactoryType GetVertexFactoryType() const`
- [ ] `FDynamicSpriteEmitterData` / `FDynamicMeshEmitterData` / `FDynamicRibbonEmitterData` / `FDynamicBeamEmitterData`
  - 책임: 타입별 override

### 6.2 기존 코드 수정 지점 (파일/함수 단위, diff 금지)
- [ ] `FParticleEmitterInstance::BuildInstanceData()` / `GetSpriteInstanceData` 등 4 getter — **DynamicData로 이관** ([ParticleEmitterInstance.h:39-48](../JSEngine/Source/Engine/Particle/ParticleEmitterInstance.h:39))
- [ ] `FParticleEmitterInstance::GetRuntimeView()` — **ReplayData로 흡수 또는 deprecate** ([.h:65](../JSEngine/Source/Engine/Particle/ParticleEmitterInstance.h:65), [.cpp:220-238](../JSEngine/Source/Engine/Particle/ParticleEmitterInstance.cpp:220))
- [ ] `FParticleEmitterInstance` 신규 메서드: `virtual FDynamicEmitterDataBase* CreateDynamicData()` — 매 frame 새 객체 또는 풀에서 회수
- [ ] `UParticleSystemComponent::BuildInstanceData()` → `CreateDynamicDataArray()` 또는 `CollectDynamicData(TArray<FDynamicEmitterDataBase*>&)` ([ParticleSystemComponent.cpp BuildInstanceData](../JSEngine/Source/Engine/Particle/ParticleSystemComponent.cpp))
- [ ] `FRenderCommand` Particle 슬롯 — 현 4종 const 포인터/count 슬롯을 단일 `FDynamicEmitterDataBase*` 슬롯으로 통합 검토 ([RenderCommand.h:489-505](../JSEngine/Source/Engine/Render/Scene/RenderCommand.h:489)) — **sizeof 변경 회귀 위험 (현 464B static_assert, [ParticleRenderPass.cpp:18](../JSEngine/Source/Engine/Render/Renderer/RenderFlow/ParticleRenderPass.cpp:18))**
- [ ] `PrimitiveDrawCommandBuilder::case EPT_ParticleSystem` ([cpp:568-729](../JSEngine/Source/Engine/Render/Scene/PrimitiveDrawCommandBuilder.cpp:568)) — RenderMode switch 본문을 `DynamicData->VertexFactoryType()` + `DynamicData->FillCmd(Cmd)`로 단일화
- [ ] `FParticleRenderPass::Render{Sprite|Mesh|Ribbon|Beam}Emitter` 4 helper — **현 helper의 데이터 fetch 부분을 DynamicData virtual 호출로 교체**, helper는 D3D state만 셋업
- [ ] `FParticleEmitterRuntimeView` ([ParticleTypes.h:83-93](../JSEngine/Source/Engine/Particle/ParticleTypes.h:83)) — **중복 정의**, ReplayData가 흡수해 삭제 가능 (사용처 0건)
- [ ] derived(Mesh/Ribbon/Beam) 인스턴스의 type-specific buffer 멤버(`MeshInstanceDataBuffer` 등) — **DynamicData로 이관**, Instance에서 제거

### 6.3 페이로드 오프셋 전달 방안
- [ ] **현재**: PayloadOffset은 base `FParticleEmitterInstance::protected` 멤버로 derived만 access — 렌더 측에 전달 안 됨
- [ ] **제안**: ReplayData에 `int32 PayloadOffset / int32 ParticleStride / const uint8* ParticleData / const uint16* ParticleIndices / int32 ActiveCount` 5 필드 명시 보유 → Instance가 매 frame **얕은 복사**로 채워 DynamicData에 set
- [ ] **DX11 환경에선 raw payload를 GPU가 읽을 필요 없음** (정점 변환을 CPU가 미리 함) → PayloadOffset 전달은 **디버그/직렬화/툴 용도가 주된 가치**

### 6.4 DX11 환경 특성상 UE와 다르게 처리할 부분
- [x] 동적 정점 버퍼: 이미 `FInstanceBuffer` grow-by-2x 패턴으로 4 type 모두 처리 중 ([ParticleRenderPass.cpp:144-154](../JSEngine/Source/Engine/Render/Renderer/RenderFlow/ParticleRenderPass.cpp:144))
- [x] 셰이더 컨벤션: row-vector(`v * Mat`) — Cycle 14에서 정착 ([Cycle14_ImplementReport.md §12](Cycle14_ImplementReport.md))
- [ ] **DynamicData의 `FillVertexBuffer()` virtual은 CPU side로** — UE의 일부 path가 RenderResource로 정점 생성을 위임하는 부분(Mesh particle GPUParticleSimulation)은 DX11 본 엔진에선 **CPU side로 단순화** 권장
- [ ] InstanceBuffer를 **ReplayData가 직접 GPU 자원에 wire**해서 Instance가 buffer 소유를 포기하는 옵션 — DX11에선 `ID3D11DeviceContext::Map`/`Unmap`을 어디서 호출할지 책임 분리 필요

### 6.5 게임↔렌더 스레드 분리 여부에 따른 설계 차이
- [x] **현재: 단일 스레드** → ReplayData/DynamicData 분리의 **원래 목적(race-free 소유권 이전)은 부재** → 분리 도입의 명분이 약해짐
- [ ] **분리 도입의 남은 가치**:
  - (a) `FParticleEmitterInstance` 비대화 방지 (현 110라인 → 향후 sort/material override hook 추가 시 확대)
  - (b) RenderCommand의 4종 슬롯 → 1 포인터로 단순화 (`sizeof(FRenderCommand)` 감소)
  - (c) Sort 모듈 도입 시 자연 위치 확보
  - (d) 향후 멀티스레드 도입 대비 인프라
  - (e) 직렬화/디버그 툴(particle replay) 가능성

### 결론 한 줄
> 도입은 가능하나 **단일 스레드 + 현재 4종 slot 분리가 이미 동작 중**인 상태에서 추가 가치는 (a)–(e) 정도. 멀티스레드 도입 계획이 없다면 ROI가 낮을 수 있다.

---

## 7. 리스크 / 미확정 사항

### 7.1 진단 중 발견한 모호한 부분
- [ ] `FParticleEmitterRuntimeView`의 위치 — POD-스러운 ReplayData에 가까운 후보지만 호출처 0건. 이미 dormant 상태인지 정착 위치 미정
- [ ] `FRenderCommand`의 4종 Particle 슬롯과 `Constants` union 사이의 sizeof 균형 — 통합 시 464B 회귀 위험 (`static_assert` [ParticleRenderPass.cpp:18](../JSEngine/Source/Engine/Render/Renderer/RenderFlow/ParticleRenderPass.cpp:18))
- [ ] Beam의 Noise(`UParticleModuleBeamNoise`) 데이터를 ReplayData에 어디까지 포함할지 — Spawn 캡처 모드면 payload 일부(현 100B), DynamicData 도입 시 별도 필드 후보
- [ ] `FParticleEmitterInstance`의 `Instance{Data|PayloadSize}` 미사용 필드 ([.h:97-98](../JSEngine/Source/Engine/Particle/ParticleEmitterInstance.h:97)) — DynamicData 도입 시 자연 deprecate 가능한지, UE의 emitter-instance-level payload(per-emitter, per-particle 아님) 도입 보류 상태인지 미확정

### 7.2 사용자에게 확인 필요한 결정 사항 (다음 사이클 진입 전 lock-in 후보)
- [ ] **Q1**. **분리 도입 자체의 필요성** — 단일 스레드 엔진에서 ReplayData/DynamicData 두 계층 도입의 ROI를 어떻게 평가하는가? (다음 셋 중)
  - (Q1-a) 멀티스레드 렌더링을 1~2 cycle 안에 도입할 계획 — 분리 필요
  - (Q1-b) 단일 스레드 유지하되 직렬화/디버그 툴(particle replay) 필요 — 분리 가치 중간
  - (Q1-c) 단일 스레드 + 직렬화 무관, 단지 `FParticleEmitterInstance` 비대화 방지/Sort 모듈 도입 자연 위치 확보 — 리팩토링 성격
- [ ] **Q2**. **DynamicData가 새 객체를 매 frame `new`/`delete`할지, frame pool을 둘지** — UE는 매 frame `new`. DX11 단일 스레드면 pool이 단순 (per-component pool 1개)
- [ ] **Q3**. **ReplayData가 raw `ParticleData`를 얕은 복사로 참조할지, 깊은 복사할지** — 단일 스레드면 얕은 복사로 충분. 멀티스레드 도입 시 깊은 복사 필수
- [ ] **Q4**. **`FRenderCommand`의 4종 Particle 슬롯 통합 여부** — 단일 `FDynamicEmitterDataBase*` 슬롯으로 통합하면 sizeof 감소, 호환성 깨짐. 슬롯 4종 유지하고 DynamicData에서 그쪽으로 fan-out도 가능
- [ ] **Q5**. **`BuildInstanceData()` virtual을 deprecate할지** — DynamicData의 `FillVertexBuffer()`로 이관하면 deprecate 가능. 그러나 [PrimitiveDrawCommandBuilder.cpp:588](../JSEngine/Source/Engine/Render/Scene/PrimitiveDrawCommandBuilder.cpp:588) + [ParticleSystemComponent.cpp BuildInstanceData](../JSEngine/Source/Engine/Particle/ParticleSystemComponent.cpp) 호출처 수정 필요
- [ ] **Q6**. **Cycle 15 (Ribbon R1/R2/R3) 진입 vs 본 인프라 cycle 우선순위** — [Cycle14_ImplementReport.md §11](Cycle14_ImplementReport.md)이 Cycle 15를 R1/R2/R3로 lock-in. 본 ReplayData/DynamicData cycle은 그 사이 또는 이후로 들어갈지

### 결론 한 줄
> 본 cycle은 **분리 도입의 ROI 평가 자체가 가장 큰 결정 항목**이며, 멀티스레드 계획·직렬화 필요성에 따라 후속 작업의 형태가 크게 갈린다.

---

## 8. 다음 사이클(구현) 진입 전 결정해야 할 질문 목록

1. **Q1 (필수)** — ReplayData/DynamicData 분리 도입의 동기 (Q1-a/b/c 중 택1)
2. **Q2** — DynamicData 객체 lifecycle: 매 frame new/delete vs frame pool
3. **Q3** — ReplayData가 raw ParticleData를 얕은 복사로 보유 vs 깊은 복사
4. **Q4** — `FRenderCommand`의 4종 Particle 슬롯 통합 여부 (sizeof 회귀 vs 호환성)
5. **Q5** — `BuildInstanceData()` virtual deprecate 여부 (호출처 2건 수정 필요)
6. **Q6** — Cycle 15 (Ribbon R1/R2/R3)와의 우선순위 (선행/후행/병행)
7. **Q7** — derived 인스턴스 type-specific buffer 멤버 (Mesh/Ribbon/Beam VertexBuffer)의 DynamicData 이관 vs Instance 잔존
8. **Q8** — `FParticleEmitterRuntimeView` 처리 (ReplayData 흡수 vs 삭제 vs 잔존)
9. **Q9** — Sort 모듈 본 cycle 포함 여부 (분리 동기 중 가장 visual 가치 큰 항목)
10. **Q10** — InstanceBuffer GPU 리소스 소유권 (RenderPass 잔존 vs DynamicData/ReplayData 이관)

---

## 9. 최종 결론 한 줄

> 명시적인 `FDynamicEmitterReplayDataBase` / `FDynamicEmitterDataBase` 두 계층 인프라는 **부재**(시나리오 B), 동일 책임이 **`FParticleEmitterInstance` virtual 메서드 + 분산 멤버 + `FRenderCommand` 슬롯 3곳에 흩어진 형태**로 흡수되어 있고, 본 엔진이 **단일 스레드**이므로 UE의 원래 분리 동기(race-free 소유권 이전)는 부재하다. 따라서 분리 도입의 가치는 (a) 비대화 방지·Sort 위치 확보·sizeof 절감 같은 **리팩토링 가치**와 (b) 멀티스레드/직렬화 도입 시 **사전 인프라 가치**로 나뉘며, 어느 쪽 동기인지 사용자 lock-in이 다음 cycle 진입의 결정 항목이다.
