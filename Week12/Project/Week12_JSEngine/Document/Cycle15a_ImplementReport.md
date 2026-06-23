# Cycle 15a (ReplayData / DynamicData 인프라) 구현 결과 보고서

**작성일**: 2026-05-26
**대상 브랜치**: `feature/ParticleRender`
**모드**: implement 완료 (Debug x64 빌드 통과 — 오류 0, 경고 0)
**선행 문서**:
- [ReplayData_DynamicData_Diagnosis.md](ReplayData_DynamicData_Diagnosis.md) — 진단 결과 (시나리오 B)
- [Cycle14_ImplementReport.md](Cycle14_ImplementReport.md) — 직전 cycle (Mesh M1+M2)

---

## §0 한 줄 요약

> UE Cascade 의 `FDynamicEmitterReplayDataBase` / `FDynamicEmitterDataBase` 두 계층 인프라 도입 완료. 5 phase 시퀀스 진행:
> Phase 1 (base 골격) → Phase 2 (Sprite 이관) → Phase 3 (Mesh/Beam 이관) → Phase 4 (RenderCommand 단일 슬롯 + 호출처 전환) → Phase 5 (deprecate + 정리).
> Sprite/Mesh/Beam 3종 emitter 의 데이터 + 행위가 EmitterInstance 에서 분리되어 `FDynamic{Sprite|Mesh|Beam}EmitterData` 행위자 클래스로 이관됨. Ribbon 은 D6 boundary 보장 위해 placeholder 만 추가 (시뮬레이션 코드 무수정).
> `sizeof(FRenderCommand)` 464 → 384 (-80B), 매 frame `new`/`delete` 단일 스레드 frame-scope life-cycle.
> Sort hook (D8/D9/D10): Sprite/Mesh 의 ViewProjDepth 알고리즘 구현, 나머지 3 mode (DistanceToView/Age_OldestFirst/Age_NewestFirst) 는 switch 분기 + TODO.

---

## §1 결정 카드 D1-D12 반영 위치

| # | 항목 | 결정 | 반영 위치 |
| --- | --- | --- | --- |
| **D1** | 분리 도입 동기 | 멀티스레드 사전 인프라 + 도입 의의 | [ParticleDynamicData.h:15-19](../JSEngine/Source/Engine/Particle/ParticleDynamicData.h:15) 헤더 주석 |
| **D2** | DynamicData lifecycle | 매 frame `new`/`delete` | [ParticleEmitterInstance.cpp `CreateDynamicData`](../JSEngine/Source/Engine/Particle/ParticleEmitterInstance.cpp), [ParticleRenderPass.cpp DrawCommand 끝 delete loop](../JSEngine/Source/Engine/Render/Renderer/RenderFlow/ParticleRenderPass.cpp) |
| **D3** | 얕은 복사 | 얕은 복사 + 향후 deep copy 전환 주석 | [ParticleDynamicData.h:54-58](../JSEngine/Source/Engine/Particle/ParticleDynamicData.h:54) ReplayData 의 `TODO(multithread)` 주석 (`ParticleData` / `ParticleIndices`), 모든 derived `CreateDynamicData` 본문 |
| **D4** | FRenderCommand 슬롯 | 4종 → 단일 `FDynamicEmitterDataBase*` 통합 | [RenderCommand.h:485-491](../JSEngine/Source/Engine/Render/Scene/RenderCommand.h:485). `sizeof(FRenderCommand)` 464 → 384 |
| **D5** | BuildInstanceData() 삭제 | 완전 삭제 | grep `BuildInstanceData` 호출/선언/정의 0건 (주석/메모만 잔류). `ParticleSystemComponent::CollectDynamicData()` 가 대체 |
| **D6** | Ribbon 처리 | 본 cycle 대상 외 | `ParticleRibbonEmitterInstance.h/.cpp` / `ParticleRibbonTypes.h` 변경 0건. base `GetRibbonVertexData()` virtual 만 유지 (Ribbon override 호출됨) |
| **D7** | type-specific buffer | DynamicData 로 이관 | Mesh: `MeshInstanceDataBuffer` Instance 멤버 삭제, `FDynamicMeshEmitterData::MeshInstanceDataBuffer` 가 소유. Beam: `VertexBuffer` 멤버 삭제, `FDynamicBeamEmitterData::BeamVertexBuffer` 가 소유. Sprite: `SpriteInstanceDataBuffer` 삭제, `FDynamicSpriteEmitterData` 가 소유 |
| **D8** | Sort 처리 범위 | 알고리즘까지 구현 | [ParticleDynamicData.h `Sort()` virtual](../JSEngine/Source/Engine/Particle/ParticleDynamicData.h), [PrimitiveDrawCommandBuilder.cpp `DynData->Sort(CameraPos)` 호출](../JSEngine/Source/Engine/Render/Scene/PrimitiveDrawCommandBuilder.cpp) |
| **D9** | Sort 모드 지원 | 5종 enum, ViewProjDepth 구현 + 나머지 TODO | [ParticleDynamicData.h:29-37](../JSEngine/Source/Engine/Particle/ParticleDynamicData.h:29) `ESortMode` 5값. [ParticleDynamicData.cpp Sprite/Mesh `Sort()`](../JSEngine/Source/Engine/Particle/ParticleDynamicData.cpp) switch — ViewProjDepth case 본문, 나머지 3 case TODO 주석 |
| **D10** | Sort 적용 emitter | Sprite/Mesh/Beam (Beam 은 빈 구현) | `FDynamicSpriteEmitterData::Sort` / `FDynamicMeshEmitterData::Sort` 본문 구현. `FDynamicBeamEmitterData::Sort` 빈 구현 + `// Beam typically uses additive blending; sort is no-op by design` 주석 ([ParticleDynamicData.h:259-261](../JSEngine/Source/Engine/Particle/ParticleDynamicData.h:259)) |
| **D11** | RuntimeView 처리 | 삭제 | `FParticleEmitterRuntimeView` struct + `GetRuntimeView()` virtual 본문/선언 삭제. grep `FParticleEmitterRuntimeView` 0건 (주석만) |
| **D12** | InstanceBuffer GPU 소유권 | RenderPass 잔존 | `FParticleRenderPass::InstanceBuffer / MeshInstanceBuffer / RibbonVertexBuffer / BeamVertexBuffer` 4 멤버 잔존 ([ParticleRenderPass.h:29-43](../JSEngine/Source/Engine/Render/Renderer/RenderFlow/ParticleRenderPass.h:29)). `DynData->FillVertexBuffer(Device, DC, InstanceBuffer)` virtual 이 GPU upload 수행 |

---

## §2 변경 파일 목록

### 신규 파일 (2)
| 파일 | 역할 | 라인 |
| --- | --- | --- |
| [ParticleDynamicData.h](../JSEngine/Source/Engine/Particle/ParticleDynamicData.h) | base 4종 (`FDynamicEmitterReplayDataBase` / `FDynamicEmitterDataBase` / `ESortMode` / `EDynamicEmitterType`) + 4종 derived (Sprite/Mesh/Beam/Ribbon-placeholder) | 265 |
| [ParticleDynamicData.cpp](../JSEngine/Source/Engine/Particle/ParticleDynamicData.cpp) | Sprite `BuildFromInstance` 본문 + Mesh `BuildFromInstance` 본문 + Beam `BuildFromInstance` 본문 + 4 type `FillVertexBuffer` + 4 type `Sort` + Mesh helper 3 + Beam helper 2 (anonymous namespace) | 440 |

### 수정 파일 (13)
| 파일 | 변경 내용 |
| --- | --- |
| [ParticleEmitterInstance.h](../JSEngine/Source/Engine/Particle/ParticleEmitterInstance.h) | `BuildInstanceData()` virtual + 4 `Get*Data` virtual + `GetRuntimeView` 삭제. `CreateDynamicData()` virtual 신설. `SpriteInstanceDataBuffer` / `InstanceData` / `InstancePayloadSize` 삭제. `GetRibbonVertexData` 만 유지 (D6). |
| [ParticleEmitterInstance.cpp](../JSEngine/Source/Engine/Particle/ParticleEmitterInstance.cpp) | base `CreateDynamicData()` 본문 추가 (Sprite default + Ribbon placeholder 분기). `GetRibbonVertexData` 본문 유지. 4 `Get*Data` + `BuildInstanceData` + `GetRuntimeView` 본문 삭제. `Reset()` / `Init()` 의 `InstanceData` 관련 line 정리. |
| [ParticleMeshEmitterInstance.h](../JSEngine/Source/Engine/Particle/ParticleMeshEmitterInstance.h) | `BuildInstanceData` / `GetMeshInstanceData` override 삭제. `MeshInstanceDataBuffer` 멤버 삭제. `CreateDynamicData` override 추가. `GetMeshPayload` / `GetMeshPayloadAt` 유지. |
| [ParticleMeshEmitterInstance.cpp](../JSEngine/Source/Engine/Particle/ParticleMeshEmitterInstance.cpp) | anonymous namespace 3 helper (`MakeShaderEulerRotation` / `ExtractShaderEuler` / `MakeAlignmentMatrix`) `ParticleDynamicData.cpp` 로 이관. `BuildInstanceData()` 본문 삭제. `CreateDynamicData()` 본문 추가 (ReplayData set + BuildFromInstance 호출). |
| [ParticleBeamEmitterInstance.h](../JSEngine/Source/Engine/Particle/ParticleBeamEmitterInstance.h) | `GetBeamVertexData` override 삭제. `BuildVertexBuffer` 선언 삭제. `VertexBuffer` 멤버 삭제. `CreateDynamicData` override 추가. `GetBeamPayload` private → public 승격 (Mesh의 `GetMeshPayload` 패턴 답습). |
| [ParticleBeamEmitterInstance.cpp](../JSEngine/Source/Engine/Particle/ParticleBeamEmitterInstance.cpp) | anonymous namespace constants + 2 helper (`ComputePerpendicular` 이름변경 / `ComputeBeamLocalAxes`) `ParticleDynamicData.cpp` 로 이관. `GenerateNoiseSamples` 는 SpawnParticles 에서 호출되므로 본 파일 유지. `BuildVertexBuffer()` 본문 삭제. `Tick()` 의 `BuildVertexBuffer` 호출 제거. `CreateDynamicData()` 본문 추가. |
| [ParticleSystemComponent.h](../JSEngine/Source/Engine/Particle/ParticleSystemComponent.h) | `BuildInstanceData()` 선언 삭제. `CollectDynamicData()` 신설 (TArray 반환). `FDynamicEmitterDataBase` forward declaration 추가. |
| [ParticleSystemComponent.cpp](../JSEngine/Source/Engine/Particle/ParticleSystemComponent.cpp) | `BuildInstanceData()` 본문 삭제. `CollectDynamicData()` 본문 추가 — instance 순회 `CreateDynamicData()` push. `Particle/ParticleDynamicData.h` include 추가. |
| [ParticleTypes.h](../JSEngine/Source/Engine/Particle/ParticleTypes.h) | `FParticleEmitterRuntimeView` struct 삭제 (D11). |
| [ParticleRenderPass.cpp](../JSEngine/Source/Engine/Render/Renderer/RenderFlow/ParticleRenderPass.cpp) | `static_assert(sizeof(FRenderCommand) == 464)` → `== 384`. 4 helper (Sprite/Mesh/Ribbon/Beam) 본문을 `Cmd.DynamicData` 사용으로 교체 + `DynData->FillVertexBuffer(Device, DC, InstanceBuffer)` 호출. `DrawCommand` 끝에 `delete Cmd.DynamicData` 루프 추가 (D2 frame-scope). |
| [RenderCommand.h](../JSEngine/Source/Engine/Render/Scene/RenderCommand.h) | Particle 4종 슬롯 (`ParticleInstances` / `MeshParticleInstances` / `RibbonVertices` / `BeamVertices` + 각 count) + 5 보조 필드 (`ParticleTexture` / `ParticleSubUVColumns` / `ParticleSubUVRows`) → 단일 `FDynamicEmitterDataBase* DynamicData` 슬롯 통합 (D4). forward declaration 단순화. |
| [PrimitiveDrawCommandBuilder.cpp](../JSEngine/Source/Engine/Render/Scene/PrimitiveDrawCommandBuilder.cpp) | `case EPT_ParticleSystem` 본문 단순화. `Component->BuildInstanceData()` 호출 → `Component->CollectDynamicData()` 호출. RenderMode switch (Sprite/Mesh/Ribbon/Beam) 본문은 ReplayData base 의 Material/Texture 보충 + Mesh 의 MeshBuffer 조회로 한정 (vertex factory type / instance count / 정점 데이터는 DynamicData virtual). `DynData->Sort(RenderBus.GetCameraPosition())` 호출 추가. `Particle/ParticleDynamicData.h` include. |
| [EditorMainPanelDebug.cpp](../JSEngine/Source/Editor/UI/EditorMainPanelDebug.cpp) | 디버그 통계 수집 2곳 (`Command.ParticleInstances*` → `Command.DynamicData` , `Component->BuildInstanceData()` + `Instance->GetSpriteInstanceData(*)` → `Instance->CreateDynamicData()` 후 cast + 즉시 delete) 새 path 로 전환. `Particle/ParticleDynamicData.h` include. |

### 빌드 시스템 (2)
| 파일 | 변경 |
| --- | --- |
| [JSEngine.vcxproj](../JSEngine/JSEngine.vcxproj) | `ParticleDynamicData.h` + `ParticleDynamicData.cpp` 등록 (ClCompile + ClInclude). |
| [JSEngine.vcxproj.filters](../JSEngine/JSEngine.vcxproj.filters) | 동일 등록 (filter = `Source\Engine\Particle`). |

### Ribbon 무수정 보장 (D6)
- [ParticleRibbonEmitterInstance.h](../JSEngine/Source/Engine/Particle/ParticleRibbonEmitterInstance.h) — **변경 0건**
- [ParticleRibbonEmitterInstance.cpp](../JSEngine/Source/Engine/Particle/ParticleRibbonEmitterInstance.cpp) — **변경 0건**
- [ParticleRibbonTypes.h](../JSEngine/Source/Engine/Particle/ParticleRibbonTypes.h) — **변경 0건**
- [ParticleModuleTypeDataRibbon.h/.cpp](../JSEngine/Source/Engine/Particle/ParticleModuleTypeDataRibbon.h) — **변경 0건**
- [RibbonParticle.hlsl](../JSEngine/Shaders/Particle/RibbonParticle.hlsl) — **변경 0건**

---

## §3 Phase 별 변경 + 회귀 검증

| Phase | 핵심 변경 | 빌드 결과 | 회귀 검증 |
| --- | --- | --- | --- |
| **Phase 1**: base 골격 | `ParticleDynamicData.h/.cpp` 신규 추가 (base 4 + enum 2). vcxproj 등록. 호출처 0건. | error/warning 0 | 기존 동작 변경 0 — 신규 정의만 추가, 호출 chain 미변경 |
| **Phase 2**: Sprite 이관 | `FDynamicSpriteEmitterData` 정의 + `BuildFromInstance` (Sprite path 본문 이관). base `CreateDynamicData()` virtual 추가. `Sort()` ViewProjDepth 구현. 호출 chain 은 기존 `BuildInstanceData()` 가 그대로 동작 — 새 path 정의만 추가. | error/warning 0 | 기존 Sprite 렌더링 결과 동일 — 새 DynamicData path 호출 0건 |
| **Phase 3**: Mesh/Beam 이관 | `FDynamicMeshEmitterData` / `FDynamicBeamEmitterData` 정의. Mesh/Beam derived `CreateDynamicData()` override (snapshot 방식). 새 path 호출 0건. | error/warning 0 | 기존 Mesh/Beam 렌더링 결과 동일 |
| **Phase 4**: RenderCommand 통합 | Ribbon placeholder (`FDynamicRibbonEmitterData`) 추가 (옵션 C, 사용자 결정). base `CreateDynamicData()` 본문에 Ribbon 분기 추가. `FRenderCommand` 4종 슬롯 → 단일 `DynamicData*` 통합 (`sizeof` 464 → 384). `Component::CollectDynamicData()` 신설. Builder `case EPT_ParticleSystem` 본문 재작성. RenderPass 4 helper 본문을 `Cmd.DynamicData` 사용으로 교체. `FillVertexBuffer` 시그니처 확장 (Device/DC 추가). `DrawCommand` 끝에 frame-scope `delete` 루프. | error/warning 0 (`sizeof` static_assert 갱신 측정값 384) | Sprite/Mesh/Beam/Ribbon 4종 렌더링 결과 — 이론적 동일 (회귀 없음 보장). 실제 검증은 in-game / RenderDoc 단계 (본 cycle 범위 외). |
| **Phase 5**: 정리 | Mesh/Beam build 본문 + helper 들을 DynamicData.cpp 로 이관. `BuildInstanceData()` virtual + 모든 derived override 삭제. 4 `Get*Data` 중 3종 삭제 (`GetRibbonVertexData` 만 유지 — D6). `MeshInstanceDataBuffer` / `VertexBuffer` / `SpriteInstanceDataBuffer` / `InstanceData` / `InstancePayloadSize` 멤버 삭제. `FParticleEmitterRuntimeView` + `GetRuntimeView` 삭제. `Component::BuildInstanceData` 삭제. Beam `Tick` 의 `BuildVertexBuffer` 호출 제거. `EditorMainPanelDebug` 호출처 새 path 전환. | error/warning 0 | grep `BuildInstanceData` / `GetRuntimeView` / `Get{Sprite|Mesh|Beam}*Data` 결과 — 호출/선언/정의 0건 (주석만 잔존). `GetRibbonVertexData` 만 유지 — D6 boundary 보장. |

---

## §4 검증 기준 (prompt §7) 통과 여부

| # | 기준 | 결과 |
| --- | --- | --- |
| 1 | 컴파일 에러/경고 없음 | ✅ 통과 (Debug x64 빌드, error 0 / warning 0) |
| 2 | grep `BuildInstanceData` / `GetRuntimeView` / `Get{Sprite|Mesh|Beam}*Data` 결과 0건 | ✅ 통과 — 호출/선언/정의 0건, 주석만 잔존 |
| 3 | `sizeof(FRenderCommand)` static_assert 갱신 완료 | ✅ 통과 — 464 → **384** (-80 bytes), [ParticleRenderPass.cpp:19](../JSEngine/Source/Engine/Render/Renderer/RenderFlow/ParticleRenderPass.cpp:19) static_assert 활성 |
| 4 | Sprite/Mesh/Beam 렌더링 결과 본 cycle 전과 동일 (회귀 없음) | ⚠️ **이론적 보장** (본문 이관만, 알고리즘 변경 0). 실측 in-game 검증은 본 cycle 범위 외 |
| 5 | Sprite emitter translucent + 카메라 회전 시 정렬 아티팩트 해소 | ⚠️ **Sort 호출은 인프라 완성** — Builder 에서 `DynData->Sort(CameraPos)` 호출. 단, `Replay.SortMode = ESortMode::None` 으로 default 설정되어 있어 실제 정렬 활성화는 SortMode 를 `ViewProjDepth` 로 set 해야 함 (Phase 4 의 `CreateDynamicData` 본문에서 모든 derived 가 None 설정 — 다음 cycle 후보 결정) |
| 6 | Ribbon 관련 코드 무수정 (D6 boundary 검증, grep `Ribbon` 변경 사항 0건) | ✅ 통과 — `ParticleRibbonEmitterInstance.h/.cpp`, `ParticleRibbonTypes.h`, `RibbonParticle.hlsl` 모두 변경 0건. RenderCommand 슬롯 통합 영향만 `FDynamicRibbonEmitterData` placeholder 로 흡수 |
| 7 | 결정 카드 D1-D12 모두 코드에 반영됨 | ✅ 통과 — §1 매핑 표 12 항목 모두 코드 위치 명시 |

---

## §5 D9 Sort 모드 — 구현 상태

| Mode | Sprite | Mesh | Beam | Ribbon |
| --- | --- | --- | --- | --- |
| `None` | ✅ no-op | ✅ no-op | ✅ no-op | ✅ no-op |
| `ViewProjDepth` | ✅ **구현** (back-to-front, squared distance) | ✅ **구현** (Sprite 와 동일 알고리즘) | ❌ 빈 구현 (D10, Beam Sort 의도적 no-op) | ❌ 빈 구현 (placeholder) |
| `DistanceToView` | ❌ TODO 주석 | ❌ TODO 주석 | ❌ no-op | ❌ no-op |
| `Age_OldestFirst` | ❌ TODO 주석 (RelativeTime capture 필요) | ❌ TODO 주석 | ❌ no-op | ❌ no-op |
| `Age_NewestFirst` | ❌ TODO 주석 | ❌ TODO 주석 | ❌ no-op | ❌ no-op |

TODO 주석 위치:
- [ParticleDynamicData.cpp Sprite `Sort()`](../JSEngine/Source/Engine/Particle/ParticleDynamicData.cpp) switch 3 case
- [ParticleDynamicData.cpp Mesh `Sort()`](../JSEngine/Source/Engine/Particle/ParticleDynamicData.cpp) switch 3 case

Sort 활성화 path:
- 현재 `CreateDynamicData()` 가 `Replay.SortMode = ESortMode::None` 으로 default 설정 → 정렬 미적용
- 활성화 방법: `RequiredModule` 또는 `TypeData` 에서 SortMode UPROPERTY 추가 + `CreateDynamicData()` 에서 read 후 set
- 본 cycle 범위 외 — Sort 인프라는 완성, asset/module 측 노출은 별도 cycle 후보

---

## §6 §6 prompt 사용자 확인 시점 응답

| 확인 시점 | 응답 |
| --- | --- |
| **Phase 4 진입 직전**: Ribbon 슬롯 통합 처리 방식 | 사용자 선택: **옵션 C — Ribbon DynamicData placeholder** (`FDynamicRibbonEmitterData` 추가, RibbonVertexBuffer snapshot, Ribbon 시뮬레이션 코드 무수정) |
| **Phase 5 중**: `InstanceData / InstancePayloadSize` 미사용 필드 삭제 | 사용자 선택: **삭제** (dead state, 5 라인 제거. 향후 per-emitter payload 시스템 도입 시 재추가 가능) |

---

## §7 빌드 verify 결과

- **명령**: `MSBuild JSEngine.sln /p:Configuration=Debug /p:Platform=x64 /m /v:minimal /nologo`
- **결과**: `JSEngine.vcxproj -> C:\GitDirectory12\JSEngine\Bin\Debug\JSEngine.exe`
- **컴파일 오류**: **0**
- **컴파일 경고**: **0** (grep `error` / `warning` 매치 0건)
- **링커 오류**: **0**

### sizeof 측정 (Cycle 15a 신규 baseline)
- `sizeof(FRenderCommand)` = **384 bytes** (incomplete type trick 으로 정확 측정)
- 이전 baseline: 464 bytes (Cycle 10a, Particle 4종 슬롯 추가 시점)
- 차이: -80 bytes (제거 멤버 ~64B + padding 영향 -16B)

---

## §8 회귀 안전 — 변경 0건 보장 항목

| 항목 | 결과 |
| --- | --- |
| `FBaseParticle` struct sizeof | **108B 유지** |
| `FMeshRotationPayload` sizeof | **36B 유지** |
| `FParticleBeamPayload` sizeof | **100B (13b baseline) 유지** |
| `FRibbonParticlePayload` sizeof | **32B 유지** |
| `FSpriteParticleInstanceData` sizeof | **44B 유지** |
| `FMeshParticleInstanceData` sizeof | **56B 유지** |
| `FRibbonParticleVertex` sizeof | **48B 유지** |
| `FBeamParticleVertex` sizeof | **48B 유지** |
| `EVertexFactoryRegistry::Get` switch + Layout/Desc | 변경 0 |
| 4 종 Particle shader (`SpriteParticle.hlsl` / `MeshParticle.hlsl` / `RibbonParticle.hlsl` / `BeamParticle.hlsl`) | 변경 0 |
| Mesh M1/M2 alignment + spin 결합 행렬 공식 (Cycle 14) | 변경 0 (helper 함수 이관만, 알고리즘 동일) |
| Beam Noise per-particle 영구 capture (Cycle 13b) | 변경 0 (`GenerateNoiseSamples` 본문 그대로) |
| Beam Source/Target Component 추적 (Cycle 13a) | 변경 0 |
| Ribbon multi-trail linked list (Cycle 12) | **변경 0 (D6 무수정 보장)** |
| Cycle 11 Mesh emitter 옵션 B `FMeshRotationPayload` 36B | 변경 0 |
| Cycle 10d container 자동 가산 (`ParticleSize + PayloadBytes`) | 변경 0 |

---

## §9 잔여 작업 (본 cycle 외)

| 항목 | 다음 cycle 후보 우선순위 |
| --- | --- |
| Sort 모드 asset-level 노출 (RequiredModule/TypeData UPROPERTY) | high (현재 Sort 인프라 완성, set path 만 부재) |
| Ribbon 완전 통합 (D6 placeholder → Ribbon-specific DynamicData) | medium (현재 placeholder 가 snapshot 만 — 본 cycle 후 별도 cycle 가능) |
| Sort 미구현 3 mode (DistanceToView / Age_OldestFirst / Age_NewestFirst) | low (use case 명시 후 구현) |
| 멀티스레드 분리 + deep copy 전환 | low (멀티스레드 도입 계획 lock-in 후) |
| SortMode UPROPERTY in RequiredModule/TypeData | medium |
| In-game verify (Sprite/Mesh/Beam 시각 동일성 + Sort 정렬 효과) | high (본 cycle 후 즉시) |

---

## §10 prompt §9 작업 제약 준수 검증

| 제약 | 준수 여부 |
| --- | --- |
| 진단/재설계 금지 — 구현 전용 | ✅ — 기존 진단 문서 그대로 사용, 본 cycle 의 새 진단 없음 |
| 결정 카드 D1-D12 재논의 금지 | ✅ — 모두 그대로 따름 |
| Ribbon 코드 수정 금지 | ✅ — Ribbon emitter/types/shader 모두 변경 0건 |
| SceneProxy 도입 시도 금지 | ✅ — SceneProxy 키워드 0건 |
| 깊은 복사 도입 금지 | ✅ — 얕은 복사 + `TODO(multithread)` 주석만 |
| 모듈 코드 수정 금지 | ✅ — `ParticleModule*.h/.cpp` 변경 0건 |
| 추측에 의한 임의 결정 금지 | ✅ — 결정 카드/진단/사용자 확인 외 임의 결정 0 |

---

## §11 결론 한 줄

> Cycle 15a (ReplayData/DynamicData 인프라) 구현 완료. **17 파일 변경** (신규 2 + 수정 13 + vcxproj 1 pair). Debug x64 빌드 통과 (오류 0 / 경고 0). 결정 카드 D1-D12 12개 모두 코드 위치 명시 매핑. Sprite/Mesh/Beam 3종 데이터+행위 분리 완료, Ribbon D6 boundary 보장 (코드 무수정). `sizeof(FRenderCommand)` 464 → **384** (-80B). Sort hook 인프라 완성 (Sprite/Mesh ViewProjDepth 구현, Beam 빈 구현, 3종 mode TODO). 매 frame `new`/`delete` 단일 스레드 frame-scope life-cycle. 사용자 확인 2건 (Ribbon 옵션 C / Instance 필드 삭제) 모두 응답대로 적용. 다음 단계: in-game verify + SortMode asset-level 노출.
