# Cascade Particle Porting — 구현 현황 및 테스트 가이드

**작성일**: 2026-05-24
**대상 브랜치**: `feature/ParticleRender`
**현재 상태**: Cycle 1–4 완료 + Cycle 5 Phase A(smoke test) 통과 후 임시 코드 revert 완료. 다음 단계는 **ParticleSystem asset 셋업으로 정상 emit 흐름 동작 확인 (Phase B)**.

---

## 1. TL;DR

| Cycle | 상태 | 핵심 산출 |
|------|------|----------|
| 1 | ✅ | `FVertexElementDesc`에 `InputSlotClass`/`InstanceDataStepRate` 추가, `HashVertexLayout` 동기화 |
| 2 | ✅ | `FInstanceBuffer` 신규 (dynamic VB 위 grow-by-2x) |
| 3 | ✅ | `EVertexFactoryType::SpriteParticle` + 7-element layout + `SpriteParticle.hlsl` |
| 4 | ✅ | `ERenderPass::Particle` + `FParticleRenderPass` + RenderPipeline 결선 + Builder `EPT_ParticleSystem` case |
| 5 Phase A | ✅ (revert됨) | Template 없이 fake instance 4개로 D3D 경로 smoke test → 화면에 색 패치 확인 → 임시 코드 제거 완료 |
| 5 Phase B | ⏳ 미진행 | 실제 `UParticleSystem` asset 셋업 → 정상 emit 흐름 확인 |

**현재 빌드의 가용 기능**: `UParticleSystemComponent::Template`이 유효한 `UParticleSystem` asset을 가리키면 sprite particle이 `DrawIndexedInstanced`로 그려진다. asset이 nullptr이면 아무것도 안 그려짐 (정상).

---

## 2. 완료된 사이클 상세

### Cycle 1 — `FVertexElementDesc` 확장
- **변경**:
  - [`Engine/Render/Resource/ShaderTypes.h`](../JSEngine/Source/Engine/Render/Resource/ShaderTypes.h): `FVertexElementDesc`에 `InputSlotClass` (기본 `PER_VERTEX_DATA`) + `InstanceDataStepRate` (기본 0) 2 필드 추가.
  - [`Engine/Core/ShaderResourceCache.cpp`](../JSEngine/Source/Engine/Core/ShaderResourceCache.cpp): `BuildInputLayoutFromDesc` 하드코딩 제거 → element 값 사용. `HashVertexLayout`에 신규 2 필드 해시 추가.
- **영향**: 기존 9개 layout(NormalVertex/Skeletal/Primitive/Texture/TexturePositionUV/PositionOnly + SubUV/Line/Font batcher)은 5-필드 brace 초기화라 default로 채워져 동작 동일성 유지. 셰이더 캐시 해시 변경으로 첫 실행 시 셰이더 재컴파일 1회 발생.

### Cycle 2 — `FInstanceBuffer` 클래스
- **변경**:
  - [`Engine/Render/Resource/InstanceBuffer.h/.cpp`](../JSEngine/Source/Engine/Render/Resource/InstanceBuffer.h) 신규.
  - vcxproj/filters 등록 (VS가 외부 수정을 덮어쓴 사례 1회 있었음 — 다음 작업자 주의).
- **API**: `Create(Device, Stride, InitialCapacity)` / `Update(Device, Context, Data, Count)` / `Release()`. Capacity 초과 시 grow-by-2x 재할당.
- **v3 보고서와의 차이**: `FVertexBuffer` API가 `Device`/`Context`를 인자로 받는 패턴이므로 `FInstanceBuffer`도 그대로 따름 (v3 가정과 시그니처 차이 4건). Stride/Count/Capacity는 내부 `FVertexBuffer`가 이미 추적하므로 멤버 중복 없음.

### Cycle 3 — `SpriteParticle` Vertex Factory 등록
- **변경**:
  - [`Engine/Render/Resource/VertexTypes.h`](../JSEngine/Source/Engine/Render/Resource/VertexTypes.h): `FSpriteParticleVertex` 20B + `FSpriteParticleInstanceData` 44B (`static_assert` 가드).
  - [`Engine/Render/Resource/VertexFactoryTypes.h`](../JSEngine/Source/Engine/Render/Resource/VertexFactoryTypes.h): `EVertexFactoryType::SpriteParticle` enum + `SpriteParticleLayout` (slot 0 per-vertex: POSITION/TEXCOORD, slot 1 per-instance: POSITION/SIZE/COLOR/ROTATION/SUBUV_INDEX) + `SpriteParticleDesc` + `Registry::Get` **명시 case 추가** (default fallback이 StaticMesh이므로 silent bug 함정 회피).
  - [`Engine/Render/Resource/ShaderPaths.h`](../JSEngine/Source/Engine/Render/Resource/ShaderPaths.h): `ParticleSprite` 경로 추가.
  - [`Shaders/Particle/SpriteParticle.hlsl`](../JSEngine/Shaders/Particle/SpriteParticle.hlsl) 신규: `SpriteParticleVS` view-space billboard + SubUV cell 매핑, `SpriteParticlePS` atlas sample × color (알파 0 discard).
  - vcxproj/filters에 hlsl + `Shaders\Particle` 필터 등록.
- **cbuffer 슬롯**: SubUV grid 메타데이터는 b8 (b3은 UberConstants와 점유 충돌이라 b8 미사용 슬롯 사용).

### Cycle 4 — `FParticleRenderPass` + 파이프라인 결선
- **변경**:
  - [`Engine/Render/Common/RenderTypes.h`](../JSEngine/Source/Engine/Render/Common/RenderTypes.h): `ERenderPass::Particle` 추가 (Translucent와 SelectionMask 사이). Translucent 재사용 시 `PickPasses[]`에 들어가 ID pick 오인이 발생하므로 별도 패스로 분리.
  - [`Engine/Render/Renderer/RenderFlow/ParticleRenderPass.h/.cpp`](../JSEngine/Source/Engine/Render/Renderer/RenderFlow/ParticleRenderPass.h): `FBaseRenderPass` 상속, `EnsureGPUResources`에서 정적 quad VB(`FSpriteParticleVertex[4]`)+IB(6개)+`SpriteParticleCB`(b8) 1회 생성. `DrawCommand`는 `GetCommands(Particle)` 소비 → 명령마다 `IASetVertexBuffers(slot0=quad, slot1=instance)` → `DrawIndexedInstanced(6, InstanceCount, 0, 0, 0)`.
  - [`Engine/Render/Renderer/RenderFlow/RenderPipeline.cpp`](../JSEngine/Source/Engine/Render/Renderer/RenderFlow/RenderPipeline.cpp): `push_back(ParticleRenderPass)` (Translucent 직후) + `GetRenderPassPerfName`에 `"RenderPass.Particle"` 라벨 추가.
  - [`Engine/Render/Scene/RenderCommand.h`](../JSEngine/Source/Engine/Render/Scene/RenderCommand.h): `FRenderCommand`에 `ParticleInstances`/`ParticleInstanceCount`/`ParticleTexture`/`ParticleSubUVColumns`/`ParticleSubUVRows` 추가 (struct 멤버, union 외부).
  - [`Engine/Render/Scene/PrimitiveDrawCommandBuilder.cpp`](../JSEngine/Source/Engine/Render/Scene/PrimitiveDrawCommandBuilder.cpp): `case EPrimitiveType::EPT_ParticleSystem` 본문 채움 + `return true` (v3가 경고했던 fall-through 버그 동시 수정).
  - [`Engine/Particle/ParticleSystemComponent.h/.cpp`](../JSEngine/Source/Engine/Particle/ParticleSystemComponent.h): `BuildSpriteInstanceData()` + `GetEmitterInstanceData(Index)` 추가. 각 emitter의 `ActiveParticles`만큼 `FSpriteParticleInstanceData`를 채움.
- **자동 안전 동작**:
  - `PassBatchers[Particle]` 미등록 → `PrepareBatchers`의 `if (!PassBatchers[i]) continue` 가드로 자동 skip.
  - `EditorOverlayCollector`의 SupportsOutline 가드: `ParticleSystemComponent::SupportsOutline() == false` → 콜렉터 자동 제외 → SelectionMask 패스에 안 들어감.
  - `PickPasses[] = {Opaque, Translucent, SubUV}`에 Particle 미포함 → ID pick에 안 잡힘.

---

## 3. Cycle 5 Phase A — Smoke Test (완료, 임시 코드 revert됨)

**목적**: ParticleSystem asset이 없는 상태에서 ParticleRenderPass의 D3D 경로(셰이더 컴파일 → InputLayout → DrawIndexedInstanced → PS 출력)가 살아 있는지 확인.

**방법 (당시 임시 변경, 현재는 revert됨)**:
- `BuildSpriteInstanceData()`에 Template==nullptr 분기 추가 → fake instance 4개(R/G/B/Y) 캐시 채움.
- `PrimitiveDrawCommandBuilder` `EPT_ParticleSystem` case에 Template==nullptr 디버그 분기 → fake 1 command 발행.
- `SpriteParticlePS` 본문을 일시 `return input.Color`로 단순화 (텍스처 없이 색만 출력).

**결과**: ParticleSystemComponent를 가진 액터를 씬에 배치 → 런타임에서 **월드 원점 근처에 4개의 색 패치(빨강/초록/파랑/노랑) 화면 표시 확인**. ParticleRenderPass D3D 경로가 end-to-end 살아 있음이 검증됨.

**현재 상태**: 4개 파일 모두 Cycle 4 시점 상태로 revert됨 (git diff 빈 출력 확인). 빌드 통과 (Debug|x64, 경고 0, 오류 0).

---

## 4. 현재 빌드에서 무엇이 가능한가

**가능**:
- `EVertexFactoryType::SpriteParticle`을 사용한 instanced rendering 인프라.
- `FInstanceBuffer`로 dynamic 인스턴스 데이터 업로드 (grow-by-2x 자동 재할당).
- `ERenderPass::Particle` 패스가 RenderPipeline에 등록되어 매 프레임 Begin/End 실행.
- `UParticleSystemComponent::BuildSpriteInstanceData()`가 `ActiveParticles`를 `FSpriteParticleInstanceData[]`로 변환.
- `PrimitiveDrawCommandBuilder`가 `EPT_ParticleSystem` Primitive를 받아 `FRenderCommand`를 발행.
- `FParticleRenderPass::DrawCommand`가 `DrawIndexedInstanced(6, ActiveParticles, ...)` 호출.

**현재 불가 / 미해결**:
- `UParticleSystem` asset(`UParticleEmitter` + `UParticleLODLevel` + `RequiredModule` + `SpawnModule` ...)이 셋업되어야 함 → 다음 단계.
- `Cmd.ParticleTexture = nullptr` 고정 (atlas 텍스처 미연결).
- `Cmd.ParticleSubUVColumns/Rows = 1` 고정 (SubUVModule 미연동).
- `FSpriteParticleInstanceData::SubUVIndex`는 채워지지만 `USubUVModule` 페이로드와 미연동.
- 거리순 정렬 / Material 결합 / MeshParticle / 멀티스레드 — 모두 후순위.

---

## 5. 정상 동작 테스트 방법 (Phase B 가이드)

### 5.1 필요한 asset 객체 그래프

`UParticleSystem` (asset)
└─ `Emitters: TArray<UParticleEmitter*>`
   └─ `LODLevels: TArray<UParticleLODLevel*>` (최소 1개, `Level=0`)
      ├─ `RequiredModule: UParticleModuleRequired*` — atlas texture, max lifetime, max active particle 등
      ├─ `SpawnModule: UParticleModuleSpawn*` — `Rate > 0` (예: 20.0f)
      └─ `Modules: TArray<UParticleModule*>` — Initial Location/Velocity/Size/Color over Life 등 가능한 만큼

위 객체 그래프를 만들어 `UParticleSystemComponent::Template`에 할당하면, `RecreateEmitterInstances()`가 호출되어 `EmitterInstances`가 채워지고 Tick 흐름이 작동한다.

### 5.2 셋업 옵션

**옵션 A — 코드로 인스턴스 만들기 (단순, 권장 1회 검증용)**
액터의 BeginPlay 또는 ParticleSystemComponent 자체에 임시 `EnsureFallbackTemplate()` 류 메서드를 두고:

```cpp
UParticleSystem* PS = NewObject<UParticleSystem>();
UParticleEmitter* Em = NewObject<UParticleEmitter>();
UParticleLODLevel* LOD = NewObject<UParticleLODLevel>();
LOD->RequiredModule = NewObject<UParticleModuleRequired>();
LOD->SpawnModule    = NewObject<UParticleModuleSpawn>();
LOD->SpawnModule->Rate = 20.0f;       // 초당 20개 spawn
LOD->Modules.push_back(LOD->SpawnModule);
LOD->CacheModuleLists();
Em->LODLevels.push_back(LOD);
Em->CacheEmitterModuleInfo();
PS->Emitters.push_back(Em);
SetTemplate(PS);                       // → RecreateEmitterInstances()
```

(정확한 `NewObject` 시그니처/오너십은 이 코드베이스 패턴을 따를 것 — `UObject` factory.)

**옵션 B — Editor inspector로 셋업**
`UPROPERTY(DisplayName=...)` 노출되어 있으므로 Inspector에서 직접 자식 객체 생성/할당이 가능하면 이 경로가 가장 깔끔. (`UParticleSystem.Emitters`, `UParticleEmitter.LODLevels`, `UParticleLODLevel.RequiredModule/Modules` 모두 UPROPERTY로 노출됨.)

### 5.3 검증 시퀀스

1. **빌드**: `msbuild JSEngine.sln /p:Configuration=Debug /p:Platform=x64` — 경고/오류 0.
2. **실행**: ParticleSystemComponent + 위 Template asset이 할당된 액터를 씬에 배치.
3. **디버거 watch** (선택):
   - `UParticleSystemComponent::EmitterInstances.size()` > 0
   - `FParticleEmitterInstance::ActiveParticles`가 시간에 따라 증가 (Rate=20일 때 1초 후 ~20)
4. **화면**: sprite 다수가 액터 위치 근처에 표시 (atlas texture가 nullptr이면 알파 0 discard로 안 보일 수 있음 → atlas 연결 또는 PS 임시 단순화로 확인 가능).
5. **RenderDoc (정밀 검증, 선택)**:
   - GPU Event List에 `"RenderPass.Particle"` 마커
   - `DrawIndexedInstanced(IndexCountPerInstance=6, InstanceCount=ActiveParticles, ...)` 매 프레임 1+회
   - VS Input Layout에 7개 element (POSITION, TEXCOORD, INSTANCE_POSITION/SIZE/COLOR/ROTATION/SUBUV_INDEX)
   - VB slot 0에 quad VB (20 stride × 4 vertex), slot 1에 instance VB (44 stride × N)

### 5.4 화면에 안 보일 때 분기점 좁히기 (중단점 위치)

순서대로 확인:

| 단계 | 중단점 위치 | 확인 사항 |
|------|------------|----------|
| 1 | `PrimitiveDrawCommandBuilder.cpp` `case EPT_ParticleSystem:` | Primitive가 Builder에 들어오는지 |
| 2 | `BuildSpriteInstanceData()` `EmitterCount > 0` 분기 | EmitterInstances가 비지 않은지 |
| 3 | Builder의 `RenderBus.AddCommand(...)` 라인 | Command 발행되는지 |
| 4 | `FParticleRenderPass::DrawCommand` 첫 줄 | Render 루프 도달 |
| 5 | `if (Commands.empty()) return true;` 가드 | Commands 개수 |
| 6 | `EnsureGPUResources(...)` 반환 | quad VB/IB/cbuffer 생성 성공 |
| 7 | `GetSpriteParticleProgram()` 반환 | 셰이더/InputLayout 생성 성공 (실패 시 D3D 디버그 레이어 로그 확인) |
| 8 | `DrawIndexedInstanced(...)` 줄 | 도달 + InstanceCount > 0 |

DrawIndexedInstanced가 호출되는데 화면에 없으면: blend/depth state, view/projection 매트릭스, RTV 바인딩, 알파 discard, NDC 위치 순으로 확인.

---

## 6. 미해결 / 다음 사이클 후보

| 우선순위 | 항목 | 위치 |
|--------|------|------|
| 높음 | `Cmd.ParticleTexture` 채우기 (RequiredModule 또는 SubUVModule의 atlas texture) | `PrimitiveDrawCommandBuilder.cpp` `EPT_ParticleSystem` case |
| 높음 | `Cmd.ParticleSubUVColumns/Rows` 채우기 (SubUVModule) | 위와 같음 |
| 중간 | `FSpriteParticleInstanceData::SubUVIndex` 산출 (USubUVModule 페이로드) | `BuildSpriteInstanceData()` 내부 emitter 루프 |
| 중간 | 거리순 정렬 (alpha blending 정확성) | `Renderer::GetAlignedCommands(Particle)` 추가 |
| 중간 | Material 결합 (UberLit 등) | `SpriteParticleDesc.VertexShaderPath`를 material 시스템과 연동 |
| 낮음 | MeshParticle (Sprite 외 형태) | 별도 VertexFactoryType 추가 |
| 낮음 | 멀티스레드 emit/update | `FParticleEmitterInstance::Tick` 병렬화 |

---

## 7. 알려진 silent bug 함정 (반드시 숙지)

1. **`FVertexFactoryRegistry::Get` default fallback이 `StaticMeshDesc`**
   - 신규 `EVertexFactoryType` 추가 시 명시 case를 안 적으면 잘못된 Desc가 매핑되어 디버깅 어려운 버그가 됨.
   - 위치: [`VertexFactoryTypes.h`](../JSEngine/Source/Engine/Render/Resource/VertexFactoryTypes.h) 라인 200대 switch.

2. **`HashVertexLayout`와 `BuildInputLayoutFromDesc` 동기화 필수**
   - Cycle 1에서 신규 필드 2개 추가 시 둘 다 동시 변경해야 PER_VERTEX/PER_INSTANCE 두 변형이 캐시 키로 충돌하지 않음.

3. **`PickPasses[]`에 `ERenderPass::Particle` 절대 추가 금지**
   - [`Renderer.cpp` 671줄대](../JSEngine/Source/Engine/Render/Renderer/Renderer.cpp): `{Opaque, Translucent, SubUV}`. Particle을 넣으면 ID pick이 Particle을 StaticMesh로 오인하고, ShaderKey 계산이 Particle을 무시함.

4. **`vcxproj`가 VS에 의해 외부 수정이 덮어쓰일 수 있음**
   - Cycle 2/3 작업 중 실제 발생. 신규 cpp/h 추가 시 VS를 닫은 채로 작업하거나, 작업 후 VS에서 "솔루션 다시 로드"를 반드시 거칠 것.

5. **`PrimitiveDrawCommandBuilder.cpp` `EPT_ParticleSystem` case는 명시적 `return true` 종결**
   - v3 시점에 fall-through 버그가 있었고 Cycle 4에서 동시 수정. 이 case가 다시 fall-through로 `default: return false`로 떨어지면 Command 발행이 silent하게 끊김.

6. **`EditorOverlayCollector`의 SupportsOutline 가드**
   - `ParticleSystemComponent::SupportsOutline() == false`라 outline/selection 콜렉터에서 자동 제외. Particle을 outline에 보이게 하려면 이 메서드부터 수정해야 함.

7. **`PassBatchers[Particle]` 등록 안 함**
   - `InitializePassBatchers()`에 Particle을 등록하지 않으면 자동 skip 됨 — 이는 의도된 설계 (ParticleRenderPass가 직접 `GetCommands` 소비). 만약 향후 batcher 패턴이 필요해지면 명시 등록.

---

## 8. 참고 / 선행 문서

- [`Document/VertexFactory_Cascade_Investigation.md`](VertexFactory_Cascade_Investigation.md) — v3 핸드오프 + 함정 분석
- [`Document/RenderDataFlow.md`](RenderDataFlow.md) — RenderBus/PrepareBatchers/RenderPipeline 전체 흐름
- [`Document/particle_class_relation.md`](particle_class_relation.md) — Particle 클래스 관계도
- Plan 파일: `~/.claude/plans/renderdoc-drawindexedinstanced-reflective-charm.md` — Cycle 5 진단 + 단계적 회복 계획

---

## 9. Phase B 진입을 위한 Commit 후보

```
[ParticleRender] Cycle 5: Phase A smoke test verified, revert temp code

- Phase A confirmed ParticleRenderPass D3D path works end-to-end via 4 fake
  instances (R/G/B/Y patches rendered on screen).
- Temporary changes reverted in 4 files:
    Shaders/Particle/SpriteParticle.hlsl (PS body restored)
    Particle/ParticleSystemComponent.{h,cpp} (DebugFakeInstances removed)
    Render/Scene/PrimitiveDrawCommandBuilder.cpp (Template-null branch removed)
- Add Document/Cascade_Porting_Status.md handoff for Phase B.

Next: ParticleSystem asset setup (UParticleSystem + Emitter + LODLevel
+ Required + Spawn) so that EmitterInstances populate and ActiveParticles
grows over time, producing real DrawIndexedInstanced calls.
```
