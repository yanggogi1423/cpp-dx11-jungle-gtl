# Rendering Data Flow — RenderBus / RenderCollector / RenderPass

엔진의 렌더링 데이터는 **수집(Collector) → 운반(Bus) → 소비(RenderPass)** 의 세 단계로 흐른다.
이 문서는 프레임마다 World의 컴포넌트 데이터가 어떻게 GPU draw call까지 도달하는지 추적한다.

---

## 1. 전체 그림 (한눈에)

```
UEngine::Tick(dt)
  └─ UEngine::Render(dt)
       └─ IRenderPipeline::Execute(dt, Renderer)
            ├─ FDefaultRenderPipeline   (런타임/게임)
            └─ FEditorRenderPipeline    (에디터, 4-Viewport)

   Execute() 내부:
     1. Bus.Clear()                                   ← 프레임 데이터 초기화
     2. Bus.SetViewProjection/Settings/Viewport...    ← 카메라/뷰 상태 주입
     3. Collector.CollectWorld(World, ..., Bus, Frustum)
        ├─ PrimitiveDrawCommandBuilder.CollectPrimitive(...)   → Bus.AddCommand(Pass, Cmd)
        ├─ DecalCommandBuilder.CollectDecal(...)                → Bus.AddCommand(Decal, Cmd)
        └─ LightRenderCollector.CollectLight(...)              → Bus.LightInfos / ShadowLightRequests
        (Editor 전용) Collector.CollectGrid/Gizmo/Selection    → EditorOverlayCollector 경유
     4. Renderer.PrepareBatchers(Bus)                  ← Pass별 Lambda Batcher가 Bus에서 커맨드 끌어와 CPU 배치
     5. Renderer.BeginFrame / BeginViewportFrame
     6. Renderer.Render(Bus)
          └─ FRenderPipeline.Render(Context)            ← Context에 Bus/Targets/Batchers/Device 채워서 전달
                └─ for each FBaseRenderPass:
                      Begin → DrawCommand → End          ← 각 Pass가 Context->RenderBus 에서 자기 슬라이스만 읽음
     7. CompositeCurrentSceneToBackBuffer / EndFrame
```

세 컴포넌트의 역할 요약:

| 컴포넌트 | 위치 | 역할 |
|---------|------|------|
| `FRenderBus` | [JSEngine/Source/Engine/Render/Scene/RenderBus.h](JSEngine/Source/Engine/Render/Scene/RenderBus.h) | **프레임 스코프 컨테이너** — Pass별 커맨드 큐 + 라이트/그림자/뷰/뷰포트/포스트프로세스 전역 상태 |
| `FRenderCollector` | [JSEngine/Source/Engine/Render/Scene/RenderCollector.h](JSEngine/Source/Engine/Render/Scene/RenderCollector.h) | **생산자** — World를 순회하며 컴포넌트 → `FRenderCommand` 변환 후 Bus에 push |
| `FBaseRenderPass` 파생 | [JSEngine/Source/Engine/Render/Renderer/RenderFlow/](JSEngine/Source/Engine/Render/Renderer/RenderFlow/) | **소비자** — 자기 담당 `ERenderPass`의 커맨드/라이트 데이터를 Bus에서 읽어 D3D 드로우 콜 실행 |

---

## 2. FRenderBus — 운반자 (Carrier)

`FRenderBus`는 한 프레임 동안 살아 있는 데이터 컨테이너다. `Collector`가 채우고 `RenderPass`가 읽는다.

### 2.1 보유 데이터

#### Pass별 커맨드 큐
[RenderBus.h:123](JSEngine/Source/Engine/Render/Scene/RenderBus.h:123)
```cpp
TArray<FRenderCommand> PassQueues[(uint32)ERenderPass::MAX];
```
`ERenderPass` ([RenderTypes.h:47](JSEngine/Source/Engine/Render/Common/RenderTypes.h:47)) 종류:
`Opaque, Decal, Light, Fog, Sandervistan, FXAA, Font, SubUV, Translucent, SelectionMask, Grid, Editor, EditorOverlay, DepthLess, PostProcessOutline`.

#### 라이트/그림자 데이터 (per-pass 큐가 아닌 직접 멤버)
[RenderBus.h:115-120](JSEngine/Source/Engine/Render/Scene/RenderBus.h:115)
```cpp
FAmbientLightInfo       AmbientLightInfo;
FDirectionalLightInfo   DirectionalLightInfo;
TArray<FLightInfo>      LightInfos;            // Point + Spot
TArray<FShadowLightRequest> ShadowLightRequests;
```

#### GPU 스키닝 본 매트릭스 풀
[RenderBus.h:124](JSEngine/Source/Engine/Render/Scene/RenderBus.h:124)
```cpp
TArray<FBoneMatrixConstants> BoneMatrixConstantsPool;
```
`AllocateBoneMatrixConstants()`로 인덱스 발급 → `FRenderCommand::BoneMatrixConstantsIndex`에 저장. CPU/GPU 스키닝 경로 모두 이 풀에 fallback.

#### 카메라 / 뷰포트 / 에디터 설정
- `View`, `Proj`, `CameraPosition/Forward/Right/Up`, `NearPlane`, `FarPlane`
- `ViewportSize`, `ViewportOrigin`
- `ViewMode`, `ShowFlags`, `LightCullMode`, `ShadowFilterMode`, `WireframeColor`
- `bFXAAEnabled`, `bCascadeVis`
- `BoneWeightHeatmapViewState`

#### 포스트 프로세스 상태
Vignette(Intensity/Radius/Smoothness/Color), CameraFade(Color/Alpha), Letterbox(TargetAspect/Amount), Sandevistan(Enabled/Intensity)

### 2.2 핵심 API

| API | 역할 |
|-----|------|
| `Clear()` | 모든 큐 + 라이트/그림자/본풀/포스트프로세스 상태 초기화 |
| `AddCommand(Pass, Cmd)` | Pass 큐에 푸시 (Collector 쪽 진입점) |
| `GetCommands(Pass)` | Pass 큐 const 참조 (RenderPass 쪽 진입점) |
| `SetViewProjection(...)` | View/Proj 매트릭스 + 카메라 basis 4벡터 갱신 |
| `BuildFrameConstants(bWireframe)` | `FFrameConstants` 빌드 → 셰이더 상수버퍼 업로드용 |
| `AllocateBoneMatrixConstants()` / `GetMutable...` | 스키닝 본 매트릭스 풀 슬롯 발급/접근 |

### 2.3 FRenderCommand 구조

[RenderCommand.h:433](JSEngine/Source/Engine/Render/Scene/RenderCommand.h:433) — Pass에서 1회 draw call 실행에 필요한 모든 정보를 담는 단위.

```cpp
struct FRenderCommand {
    FPerObjectConstants PerObjectConstants;   // World/InvTrans/Color
    UPrimitiveComponent* SourcePrimitive;
    FMeshBuffer* MeshBuffer;                  // VB + IB
    UMaterialInterface* Material;
    EVertexFactoryType VertexFactoryType;     // VS 입력 레이아웃 결정 (Material과 분리)
    uint32 SectionIndexStart / SectionIndexCount;
    bool bUseBoneMatrixConstants;             // GPU 스키닝 경로 표시
    uint32 BoneMatrixConstantsIndex;          // Bus.BoneMatrixConstantsPool 인덱스
    FConstantBuffer* BoneMatrixConstantBuffer;
    FBoundingBox WorldAABB;
    union { ... } Constants;                  // Type별 추가 상수 (Font/SubUV/Billboard/Fog/Light/...)
    ERenderCommandType Type;                  // Primitive/StaticMesh/SkeletalMesh/Billboard/SubUV/...
};
```

핵심 분리 포인트: **`Material` (PS 표면)** 와 **`VertexFactoryType` (VS 입력)** 가 분리되어 같은 Material을 StaticMesh / SkeletalMesh가 공유 가능하다.

---

## 3. FRenderCollector — 생산자 (Producer)

`FRenderCollector`는 여러 sub-builder를 **합성(composition)** 한 façade다.
[RenderCollector.h:32-41](JSEngine/Source/Engine/Render/Scene/RenderCollector.h:32)

```cpp
private:
    FMeshBufferManager           MeshBufferManager;          // VB/IB GPU 리소스 (재사용)
    FDecalCommandBuilder         DecalCommandBuilder;
    FEditorOverlayCollector      EditorOverlayCollector;
    FPrimitiveDrawCommandBuilder PrimitiveDrawCommandBuilder;
    FLightRenderCollector        LightRenderCollector;
    FWorldSpatialIndex::FPrimitiveFrustumQueryScratch FrustumQueryScratch;
    FWorldSpatialIndex::FPrimitiveOBBQueryScratch     OBBQueryScratch;
```

### 3.1 진입점: `CollectWorld`

[RenderCollector.cpp:106](JSEngine/Source/Engine/Render/Scene/RenderCollector.cpp:106)

```
CollectWorld(World, ShowFlags, ViewMode, Bus, ViewFrustum, bIncludeEditorOnlyPrimitives)
├─ ViewFrustum != nullptr:                       (일반 경로 — Frustum culling)
│   └─ CollectWorldWithFrustum(...)
│        ├─ WorldSpatialIndex::FrustumQueryPrimitives(...)  ← BVH 컬링
│        │   └─ for each visible: CollectFromComponent(...)
│        └─ ActorIterator (fallback):
│             ├─ Light 컴포넌트는 무조건 CollectLight(...)
│             └─ Camera-dependent (Billboard/Text/SubUV) + Uncullable primitive 추가 처리
└─ ViewFrustum == nullptr:                       (전체 순회 fallback)
    └─ ActorIterator → CollectFromActor → CollectFromComponent
```

**컬링 통계**는 `FCullingStats { Total / BVHPassed / FallbackPassed }`에 누적되어 디버그 로그/UI에서 사용.

### 3.2 `CollectFromComponent` — 컴포넌트 → RenderCommand 디스패치

[RenderCollector.cpp:269](JSEngine/Source/Engine/Render/Scene/RenderCollector.cpp:269)

`Primitive->GetPrimitiveType()` 기준 분기:
- `EPT_Decal` → `DecalCommandBuilder.CollectDecal(...)`
- 그 외 → `PrimitiveDrawCommandBuilder.CollectPrimitive(...)`

### 3.3 PrimitiveDrawCommandBuilder — 가장 큰 변환기

[PrimitiveDrawCommandBuilder.cpp:194](JSEngine/Source/Engine/Render/Scene/PrimitiveDrawCommandBuilder.cpp:194)

| PrimitiveType | 산출물 | 대상 Pass |
|---------------|--------|-----------|
| `EPT_StaticMesh` | 섹션별 `Cmd`(Type=StaticMesh, VF=StaticMesh) — LOD 선택 후 MeshBuffer 획득 | `ERenderPass::Opaque` |
| `EPT_SkeletalMesh` | 섹션별 `Cmd`(Type=SkeletalMesh, VF=SkeletalMesh) — GPU/CPU 스키닝 분기, 본 매트릭스 풀에 등록 | `ERenderPass::Opaque` |
| `EPT_Text` | `Cmd`(Type=Font, VF=Text) — Font/Text/Scale을 `Constants.Font`에 | `ERenderPass::Font` |
| `EPT_SubUV` | `Cmd`(Type=SubUV, VF=SubUV) — Atlas/Frame/Size 카메라 빌보드 매트릭스 | `ERenderPass::SubUV` |
| `EPT_Billboard` | `Cmd`(Type=Billboard, VF=Billboard) — 카메라 빌보드 매트릭스 | `ERenderPass::SubUV` (Billboard도 SubUVBatcher 공유) |
| `EPT_FOG` | `Cmd`(Type=Primitive) — `Constants.Fog`에 HeightFog 파라미터 | `ERenderPass::Fog` |
| `EPT_ProceduralMesh` | 섹션별 `Cmd`(Type=StaticMesh, VF=ProceduralMesh) — 동적 메시 버퍼 | `ERenderPass::Opaque` |

핵심 부수 효과:
- `MeshBufferManager`에서 정적/스켈레탈/Procedural VB/IB를 **UUID 기반 캐시**로 획득
- SkeletalMesh GPU 스키닝: `MeshBufferManager.GetGPUSkeletalBoneMatrixBuffer(UUID, BoneMatrixConstants, bNeedsUpload)` — 본 버퍼 자체를 GPU에 올림. 실패시 Bus의 `BoneMatrixConstantsPool` 슬롯으로 fallback
- SkeletalMesh 스키닝 통계: `FSkinningStats`에 visible mesh / vertex / influence 누적

### 3.4 LightRenderCollector

[LightRenderCollector.cpp:16](JSEngine/Source/Engine/Render/Scene/LightRenderCollector.cpp:16)

`ULightComponentBase` RTTI 분기:
- `UAmbientLightComponent` → `Bus.AmbientLightInfo` 단일 슬롯 덮어쓰기
- `UDirectionalLightComponent` → `Bus.DirectionalLightInfo` 단일 슬롯 + shadow castor면 `ShadowLightRequests` push (`LightIndex=InvalidShadowIndex`)
- `USpotlightComponent` → `Bus.LightInfos.push_back(Type=0)` + shadow면 `ShadowLightRequests` push (`LightIndex=LightInfos.size()-1`)
- `UPointLightComponent` → `Bus.LightInfos.push_back(Type=1)` + shadow면 `ShadowLightRequests` push

`FRenderLightStats { Directional / Point / Spot / ShadowCasting / Total Count }`에 누적.

### 3.5 DecalCommandBuilder / EditorOverlayCollector (요약)

- **Decal**: OBB로 World Spatial Index를 쿼리해 영향 받는 Primitive를 찾고 `ERenderPass::Decal`에 `Constants.Decal` 채워 push.
- **EditorOverlay**: Selection(AABB/OBB 디버그 박스, 라이트 와이어), Gizmo, Grid, 본 와이어 등 에디터 보조 시각화 → `ERenderPass::Editor`, `EditorOverlay`, `Grid`, `SelectionMask`, `DepthLess`, `PostProcessOutline`, `Translucent`로 분배.

---

## 4. FRenderPass — 소비자 (Consumer)

### 4.1 베이스 클래스와 템플릿 메서드

[RenderPass.h:7](JSEngine/Source/Engine/Render/Renderer/RenderFlow/RenderPass.h:7), [RenderPass.cpp:4](JSEngine/Source/Engine/Render/Renderer/RenderFlow/RenderPass.cpp:4)

```cpp
class FBaseRenderPass {
public:
    bool Render(const FRenderPassContext* Context) {  // override 금지
        if (!Begin(Context))       return false;
        if (!DrawCommand(Context)) return false;
        if (!End(Context))         return false;
        return true;
    }

protected:
    virtual bool Begin(...)       = 0;   // RTV/DSV/State 바인딩
    virtual bool DrawCommand(...) = 0;   // Bus에서 데이터 읽고 Draw
    virtual bool End(...)         = 0;   // SRV/RTV 해제 등

    ID3D11ShaderResourceView* OutSRV;    // 다음 Pass에 넘길 결과
    ID3D11RenderTargetView*   OutRTV;
    ID3D11ShaderResourceView* PrevPassSRV;  // 이전 Pass 결과 (RenderPipeline이 주입)
    ID3D11RenderTargetView*   PrevPassRTV;
};
```

### 4.2 FRenderPassContext — Pass의 입력 채널

[RenderPassContext.h:13](JSEngine/Source/Engine/Render/Renderer/RenderFlow/RenderPassContext.h:13)

```cpp
struct FRenderPassContext {
    const FRenderBus*        RenderBus;            // ← 모든 컬렉트 데이터의 진입점
    const FRenderTargetSet*  RenderTargets;        // Scene Color/Normal/Light/Fog/... RTV+SRV 묶음
    const FPassRenderState*  RenderState;
    ID3D11Device*            Device;
    ID3D11DeviceContext*     DeviceContext;
    FRenderResources*        RenderResources;      // 상수버퍼 풀
    FFontBatcher*            FontBatcher;
    FSubUVBatcher*           SubUVBatcher;
    FLineBatcher*            GridLineBatcher;
    FLineBatcher*            EditorLineBatcher;
    FLineBatcher*            EditorOverlayLineBatcher;
    ID3D11RenderTargetView*   FinalRTV;
    ID3D11ShaderResourceView* FinalSRV;
};
```

`FRenderer::Render(Bus)`가 매 프레임 이 구조체를 채워서 `RenderPipeline.Render(Context)`로 전달한다 ([Renderer.cpp:613](JSEngine/Source/Engine/Render/Renderer/Renderer.cpp:613)).

### 4.3 FRenderPipeline — Pass 시퀀스 실행

[RenderPipeline.cpp:126](JSEngine/Source/Engine/Render/Renderer/RenderFlow/RenderPipeline.cpp:126) — 실행 순서:

```
DepthPre → LightCulling → Shadow → VSMConversion
  → Opaque → Light
  → Fog → Sandervistan → PostProcess → FXAA
  → Font → SubUV → Translucent → SelectionMask
  → Grid → Editor → EditorOverlay → DepthLess → PostProcessOutline
```

[RenderPipeline.cpp:150](JSEngine/Source/Engine/Render/Renderer/RenderFlow/RenderPipeline.cpp:150):
```cpp
for (size_t i = 0; i < RenderPasses.size(); ++i) {
    Pass = RenderPasses[i];
    Pass->SetPrevPassSRV(OutSRV);    // 직전 Pass 결과를 다음 Pass에 chain
    Pass->SetPrevPassRTV(OutRTV);
    Pass->Render(Context);
    OutSRV = Pass->GetOutSRV();
    OutRTV = Pass->GetOutRTV();
}
```
`OutSRV/OutRTV`는 Pass마다 자기 결과 RTV/SRV를 노출하거나, 단순히 `PrevPass*`를 그대로 흘려보낸다 (예: FontPass).

### 4.4 각 Pass가 Bus에서 읽는 슬라이스

| Pass | RenderBus 접근 | 출력 |
|------|---------------|------|
| DepthPre | `GetCommands(Opaque)` ([DepthPrePass.cpp:62](JSEngine/Source/Engine/Render/Renderer/RenderFlow/DepthPrePass.cpp:62)) | DSV에 깊이 prepass |
| LightCulling | `LightInfos`, `CameraPosition` | Tile/Cluster 라이트 인덱스 버퍼 (CS) |
| Shadow | `ShadowLightRequests` + `GetCommands(Opaque)` | Shadow Atlas / Cube map |
| VSMConversion | Shadow 결과 | Variance Shadow Map |
| Opaque | `GetCommands(Opaque)` ([OpaqueRenderPass.cpp:98](JSEngine/Source/Engine/Render/Renderer/RenderFlow/OpaqueRenderPass.cpp:98)) + `ShadowLightRequests` + `GetViewMode` + `GetLightCullMode` + `GetShadowFilterMode` + `GetCascadeVis` | SceneColor / Normal / WorldPos RTV (GBuffer) |
| Light | `LightInfos.size()`, `GetViewMode`, `CameraPosition` ([LightRenderPass.cpp:67](JSEngine/Source/Engine/Render/Renderer/RenderFlow/LightRenderPass.cpp:67)) | SceneLight RTV (풀스크린 라이팅) |
| Fog | `GetCommands(Fog)` ([FogRenderPass.cpp:79](JSEngine/Source/Engine/Render/Renderer/RenderFlow/FogRenderPass.cpp:79)) → `Constants.Fog` 32-layer 패킹 | SceneFog RTV |
| Sandervistan | `bSandevistanEnabled`, `SandevistanIntensity` | Sandervistan RTV |
| PostProcess | Vignette/Fade/Letterbox 상태 | PostProcess RTV |
| FXAA | `GetFXAAEnabled` | FXAA RTV |
| Font | `FontBatcher` (PrepareBatchers에서 `GetCommands(Font)` 소진) | PrevPass에 직접 그림 |
| SubUV | `SubUVBatcher` (`GetCommands(SubUV)` 소진, Atlas 포인터 기준 정렬됨) | PrevPass에 직접 그림 |
| Translucent | `GetCommands(Translucent)` | PrevPass에 직접 그림 |
| SelectionMask | `GetCommands(SelectionMask)` | SelectionMask RTV |
| Grid | `GridLineBatcher` | PrevPass에 직접 |
| Editor | `EditorLineBatcher` | PrevPass에 직접 |
| EditorOverlay | `EditorOverlayLineBatcher` (DSV 무시) | PrevPass에 직접 |
| DepthLess | `GetCommands(DepthLess)` | PrevPass에 직접 |
| PostProcessOutline | `GetCommands(PostProcessOutline)` | PrevPass에 직접 |

---

## 5. Batcher 경로 (Font / SubUV / Line)

Pass 큐에 들어간 일부 커맨드는 **GPU draw 직전 추가 단계**를 거친다.

### 5.1 PrepareBatchers — 큐 → 배쳐

[Renderer.cpp:468](JSEngine/Source/Engine/Render/Renderer/Renderer.cpp:468)
```cpp
void FRenderer::PrepareBatchers(const FRenderBus& InRenderBus) {
    for (uint32 i = 0; i < ERenderPass::MAX; ++i) {
        if (!PassBatchers[i]) continue;
        const auto& Commands = InRenderBus.GetCommands((ERenderPass)i);
        const auto& Aligned  = GetAlignedCommands((ERenderPass)i, Commands);  // SubUV는 Atlas 정렬
        PassBatchers[i].Clear();
        for (const auto& Cmd : Aligned)
            PassBatchers[i].Collect(Cmd, InRenderBus);
    }
}
```

`PassBatchers[ERenderPass::X]`는 `{Clear, Collect, Flush}` 람다 세트 ([Renderer.cpp:1238](JSEngine/Source/Engine/Render/Renderer/Renderer.cpp:1238) InitializePassBatchers):
- **Editor**: DebugBox/OBB/Line/Light → `EditorLineBatcher.AddXxx(...)`
- **EditorOverlay**: DebugLine/Bone → `EditorOverlayLineBatcher.AddXxx(...)` (특히 Bone은 octahedron + 양끝 와이어구)
- **Grid**: Grid 커맨드 → `GridLineBatcher`
- **SubUV**: SubUV + Billboard → `SubUVBatcher.AddSprite(...)` (Atlas SRV 포인터로 사전 정렬되어 같은 텍스처 연속 배치)
- **Font**: Font 커맨드 → `FontBatcher`

### 5.2 Flush — Pass가 호출

각 RenderPass의 `DrawCommand`가 `Context->XxxBatcher->Flush(...)`로 누적된 정점을 1~몇 회의 draw call로 비운다.
예: [FontRenderPass.cpp:36](JSEngine/Source/Engine/Render/Renderer/RenderFlow/FontRenderPass.cpp:36), [SubUVRenderPass.cpp:34](JSEngine/Source/Engine/Render/Renderer/RenderFlow/SubUVRenderPass.cpp:34), [GridRenderPass.cpp:32](JSEngine/Source/Engine/Render/Renderer/RenderFlow/GridRenderPass.cpp:32), [EditorRenderPass.cpp:32](JSEngine/Source/Engine/Render/Renderer/RenderFlow/EditorRenderPass.cpp:32).

---

## 6. 두 Execute 경로의 차이

### 6.1 FDefaultRenderPipeline (런타임)
[DefaultRenderPipeline.cpp:52](JSEngine/Source/Engine/Render/Renderer/DefaultRenderPipeline.cpp:52)

- 단일 카메라 (`World->GetActiveCamera()`)
- `Bus.Clear → SetViewProjection/Settings → Collector.CollectWorld(ViewFrustum)`
- Opaque 커맨드가 0인데 Visible Primitive는 있는 경우 → **Frustum 없이 한 번 더 CollectWorld** (fallback 로그)
- `Camera PostProcess` → Bus의 Vignette/CameraFade/Letterbox에 반영
- `PrepareBatchers → BeginFrame → BeginGameFrame → Render → CompositeCurrentSceneToBackBuffer → RmlUi 렌더 → RenderScreenOverlays → EndFrame`

### 6.2 FEditorRenderPipeline (에디터)
[EditorRenderPipeline.cpp:70](JSEngine/Source/Editor/EditorRenderPipeline.cpp:70), [EditorRenderPipeline.cpp:253](JSEngine/Source/Editor/EditorRenderPipeline.cpp:253)

- 4 Viewport 루프 + 추가 Viewer Viewport (skeletal mesh 미리보기 등)
- 각 Viewport마다 `Bus.Clear`부터 다시 시작 — **Bus는 1개를 매 Viewport 재사용**
- `Collector.CollectWorld(ViewFrustum)` + `CollectGrid / CollectGizmo / CollectSelection` (에디터 헬퍼)
- ViewportCullingStats / DecalStats / LightStats를 Viewport별로 보관
- `Renderer.RenderEditorIdPickBuffer(Bus, ...)` — Picking ID 버퍼는 Opaque/Translucent/SubUV 큐를 다시 한 번 순회해 Actor → uint32 매핑 ([Renderer.cpp:638](JSEngine/Source/Engine/Render/Renderer/Renderer.cpp:638))

---

## 7. 종합 시퀀스 (한 프레임 안 한 Viewport)

```
[Tick]
UEngine::Tick → UEngine::Render → IRenderPipeline::Execute
  │
  ├─ Bus.Clear()
  ├─ Bus.SetViewProjection/Settings/Viewport/PostProcess
  │
  ├─ Collector.CollectWorld(World, ShowFlags, ViewMode, Bus, &Frustum)
  │    ├─ Spatial Index BVH Cull → CollectFromComponent
  │    │    ├─ Static/Skeletal/Procedural Mesh → AddCommand(Opaque)
  │    │    ├─ Text → AddCommand(Font)
  │    │    ├─ SubUV → AddCommand(SubUV)
  │    │    ├─ Billboard → AddCommand(SubUV)
  │    │    ├─ HeightFog → AddCommand(Fog)
  │    │    └─ Decal → DecalCommandBuilder.CollectDecal → AddCommand(Decal)
  │    └─ ActorIterator → CollectLight
  │         ├─ Ambient → Bus.AmbientLightInfo
  │         ├─ Directional → Bus.DirectionalLightInfo + ShadowLightRequests
  │         ├─ Point → Bus.LightInfos + ShadowLightRequests
  │         └─ Spot  → Bus.LightInfos + ShadowLightRequests
  │
  ├─ (Editor only) Collector.CollectGrid / Gizmo / Selection
  │
  ├─ Renderer.PrepareBatchers(Bus)
  │    └─ 각 PassBatcher: Clear → Bus.GetCommands(Pass) 순회 → Batcher.Add 누적
  │
  ├─ Renderer.BeginFrame / BeginViewportFrame
  │
  └─ Renderer.Render(Bus)
       ├─ UpdateUberBuffer / UpdateFrameBuffer (Bus 데이터 → GPU 상수버퍼)
       ├─ RenderPassContext 채움 (Bus + Targets + Batchers + Device)
       └─ RenderPipeline.Render(Context)
            for each Pass:
              Begin (RTV/DSV/State 바인딩)
              DrawCommand:
                 - Mesh Pass: Bus.GetCommands(Pass) 루프 → 셰이더 컴파일/바인딩 → DrawIndexed
                 - Light Pass: Bus.LightInfos/AmbientLightInfo/DirectionalLightInfo 풀스크린
                 - Shadow Pass: Bus.ShadowLightRequests 루프 → Opaque 큐 재드로우
                 - Batcher Pass: Batcher.Flush
              End (SRV/RTV 해제)
              OutSRV/OutRTV → 다음 Pass.PrevPassSRV/PrevPassRTV
```

---

## 8. 한 줄 요약

> **Collector**가 World 컴포넌트를 순회해 `FRenderCommand`를 만들고 → **Bus**의 Pass별 큐와 라이트 슬롯에 모아두면 → **RenderPipeline**이 PassBatcher로 일부를 CPU 배치하고, `FRenderPassContext`에 Bus 포인터를 담아 → **각 FBaseRenderPass**가 `Begin → DrawCommand(GetCommands(Pass)/LightInfos/ShadowLightRequests) → End`로 자기 슬라이스만 GPU에 그린다.
