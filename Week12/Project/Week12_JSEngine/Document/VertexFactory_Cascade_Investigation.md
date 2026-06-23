 # JSEngine VertexFactory 시스템 조사 보고서

> Cascade 파티클 시스템(UE의 ParticleSystem) 도입을 위한 사전 코드베이스 조사.
> 조사 일자: 2026-05-23. 브랜치: `feature/ParticleRender`.
> 구현은 포함되지 않으며, instancing 기반 렌더링 도입 전 현 구조의 사용 양상을 정리한 문서입니다.

---

## [탐색 1] `FVertexLayoutDesc::Stride` 사용처 전수

### 정의 위치
`JSEngine/Source/Engine/Render/Resource/ShaderTypes.h:74-78` — `TArray<FVertexElementDesc> Elements; uint32 Stride = 0;`

### Read 위치 (프로젝트 내부)
| 위치 | 용도 분류 |
|------|----------|
| `JSEngine/Source/Engine/Core/ShaderResourceCache.cpp:54` | (d) **InputLayout 캐시 해시(`HashVertexLayout`)** 의 시드. `FShaderStageKey::InputLayoutHash`에 흘러들어가 캐시 키 일부가 됨. |

> 이 외 검색된 `.Stride` 매치는 모두 (1) ImGui 서드파티(`imgui_widgets.cpp:8892`, `imgui_impl_dx11.cpp` 일부), (2) 별개 `FVertexBuffer::Stride` 필드, (3) 로컬 `stride` 변수입니다. **GPU 바인딩에는 0회 사용**됩니다 — IASetVertexBuffers의 strides 인자는 모두 `Cmd.MeshBuffer->GetVertexBuffer().GetStride()`로 채워집니다.

### Write 위치 (모두 정적 초기화)
`JSEngine/Source/Engine/Render/Resource/VertexFactoryTypes.h:57-106`의 6개 layout — `sizeof(FNormalVertex)` / `sizeof(FSkeletalMeshVertex)` / `sizeof(FVertex)` / `sizeof(FTextureVertex)` ×2 / `0`(PositionOnly).

### 0인 layout 처리
`PositionOnlyLayout.Stride = 0`은 명시적입니다 (`VertexFactoryTypes.h:101-106`). 의도: DepthPrePass/Shadow에서는 `FMeshBuffer` 쪽 stride(FNormalVertex 등의 풀 stride)를 그대로 쓰면서, InputLayout 캐시 키만 다르게 만들어 별도 캐시 엔트리를 보장하기 위함입니다. 만약 자동 도출하면 PositionOnly는 12byte로 추정되어 캐시 키는 여전히 분리되지만 의미는 바뀝니다.

### 영향 평가
**Stride 필드는 GPU 바인딩과 전혀 무관하며, InputLayout 캐시 해시 구분자로만 기능합니다.** 제거 시 `HashVertexLayout`이 Elements만으로 키를 만들어도 정확합니다 (Elements가 다르면 layout이 다름). 단, Elements의 `AlignedByteOffset`은 일부 layout에서 `D3D11_APPEND_ALIGNED_ELEMENT`(=-1)인 경우는 없고 모두 `offsetof`로 명시되어 있어 자동 도출도 안전합니다.

---

## [탐색 2] Elements 배열 → D3D11 변환 지점

### CreateInputLayout 호출 위치
프로젝트 코드에서 단 **2곳**:
- `JSEngine/Source/Engine/Core/ShaderResourceCache.cpp:206` — `BuildInputLayoutFromReflection`: VS reflection 기반, 외부 layout 무시
- `JSEngine/Source/Engine/Core/ShaderResourceCache.cpp:255` — `BuildInputLayoutFromDesc`: `FVertexLayoutDesc::Elements`를 D3D11 desc로 변환

> 그 외 (`RmlUiRenderInterfaceD3D11.cpp:429`, `imgui_impl_dx11.cpp:491`)는 외부 시스템 자체 layout이라 무관.

### Elements 순회 — `BuildInputLayoutFromDesc` 본문
`ShaderResourceCache.cpp:240-261` — 핵심:
```cpp
ElementDesc.SemanticName = Element.SemanticName.c_str();
ElementDesc.SemanticIndex = Element.SemanticIndex;
ElementDesc.Format = Element.Format;
ElementDesc.InputSlot = Element.InputSlot;            // ← 그대로 전달
ElementDesc.AlignedByteOffset = Element.AlignedByteOffset;
ElementDesc.InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;  // ← 하드코딩
ElementDesc.InstanceDataStepRate = 0;                       // ← 하드코딩
```

**`InputSlot`은 이미 Element가 가지고 있고 전달도 됩니다** — 멀티 슬롯 스키마는 사실상 준비되어 있으나, 현재 모든 layout이 InputSlot=0으로 정의되어 활용되지 않습니다. **`InputSlotClass`와 `InstanceDataStepRate`는 무조건 PER_VERTEX_DATA/0으로 하드코딩**되어 있어 신규 필드 추가가 필요합니다.

`BuildInputLayoutFromReflection`도 동일하게 `ShaderResourceCache.cpp:196-197`에서 PER_VERTEX_DATA / StepRate=0 하드코딩.

### InputLayout 캐시 키
`ShaderResourceCache.cpp:47-65` `HashVertexLayout`은 Stride + 각 Element의 (SemanticName, SemanticIndex, Format, InputSlot, AlignedByteOffset)를 해시. 이 값이 `ShaderTypes.h:13-29` `FShaderStageKey::InputLayoutHash`에 들어가서 같은 VS여도 layout이 다르면 분리 컴파일 — **`InputSlotClass`/`InstanceDataStepRate`는 해시에 미포함** (신규 필드 추가 시 해시 갱신 필수).

### 영향 평가
신규 instance 필드 추가 시 영향 범위:
1. `FVertexElementDesc`에 `InputSlotClass`, `InstanceDataStepRate` 필드 추가
2. `BuildInputLayoutFromDesc`에서 하드코딩 제거 → Element 값 사용
3. `BuildInputLayoutFromReflection`은 그대로 둬도 무방 (instance 경로는 항상 명시적 layout)
4. `HashVertexLayout`에 신규 필드 해시 포함 — 그래야 같은 elements/format인데 슬롯 class만 다른 layout이 캐시 충돌 없음

---

## [탐색 3] `IASetVertexBuffers` 호출 분석

### 호출자 분류 (프로젝트 코드 전수)

**A. NumBuffers=1, FMeshBuffer 기반 (`Cmd.MeshBuffer->GetVertexBuffer().GetStride()`)**

| 위치 | 호출자 |
|------|--------|
| `JSEngine/Source/Engine/Render/Renderer/RenderFlow/OpaqueRenderPass.cpp:276` | `FOpaqueRenderPass::DrawCommand` |
| `JSEngine/Source/Engine/Render/Renderer/RenderFlow/DepthPrePass.cpp:89` | `FDepthPrePass::DrawCommand` |
| `JSEngine/Source/Engine/Render/Renderer/RenderFlow/DepthLessRenderPass.cpp:118` | `FDepthLessRenderPass::DrawCommand` |
| `JSEngine/Source/Engine/Render/Renderer/RenderFlow/DecalRenderPass.cpp:139` | `FDecalRenderPass::DrawCommand` |
| `JSEngine/Source/Engine/Render/Renderer/RenderFlow/TranslucentRenderPass.cpp:129` | `FTranslucentRenderPass::DrawCommand` |
| `JSEngine/Source/Engine/Render/Renderer/RenderFlow/ShadowPass.cpp:114` | `FShadowPass::DrawCommand` |
| `JSEngine/Source/Engine/Render/Renderer/RenderFlow/SelectionMaskRenderPass.cpp:272` | `FSelectionMaskRenderPass::DrawCommand` |
| `JSEngine/Source/Engine/Render/Renderer/Renderer.cpp:278`, `:1583` | `FRenderer::DrawIdPickCommand`, `FRenderer::DrawCommand` |

**B. NumBuffers=1, 배처 내부 하드코딩 stride**

| 위치 | stride 소스 |
|------|------------|
| `JSEngine/Source/Engine/Render/SubUVBatcher.cpp:173` | `sizeof(FTextureVertex)` |
| `JSEngine/Source/Engine/Render/FontBatcher.cpp:313` | `sizeof(FTextureVertex)` |
| `JSEngine/Source/Engine/Render/LineBatcher.cpp:716` | 멤버 `Stride` 필드 |

**C. NumBuffers=0 (full-screen SV_VertexID 패스)**

`Renderer.cpp:784`, `:857`, `:895`, `FXAARenderPass.cpp:70`, `FogRenderPass.cpp:68`, `SandervistanRenderPass.cpp:75`, `PostProcessRenderPass.cpp:85`, `PostProcessOutlineRenderPass.cpp:48`, `LightRenderPass.cpp:101`, `VSMConversionRenderPass.cpp:222`.

### 멀티 슬롯 바인딩
**0회**. NumBuffers가 1 또는 0뿐이며, instance buffer를 추가하려면 슬롯 1을 새로 채우거나 NumBuffers=2 호출로 묶어야 합니다.

### 영향 평가
Instance buffer는 슬롯 1에 별도 호출(`IASetVertexBuffers(1, 1, &InstanceVB, &InstanceStride, &Offset)`)로 끼우는 게 가장 침습성이 낮습니다. 기존 호출들을 NumBuffers=2로 합치는 변경은 불필요.

---

## [탐색 4] FMeshBuffer / Instance Buffer 추상화 현황

### FMeshBuffer 구조 — `JSEngine/Source/Engine/Render/Resource/Buffer.h:73-113`
- 멤버: `FVertexBuffer VertexBuffer; FIndexBuffer IndexBuffer;`
- 메서드: `Create`, `CreateImmutableVertexBuffer`, `CreateDynamicVertexBuffer`, `UpdateDynamicVertexBuffer`, `CreateImmutableVertices<T>`, `CreateDynamicVertices<T>`, `UpdateDynamicVertices<T>`, `IsValid`

### FVertexBuffer는 stride 자체 보유 — `Buffer.h:14-41`
- `VertexCount`, `VertexCapacity`, `Stride` 직접 보유
- `CreateRaw(... bool bDynamic)` 시 D3D11_USAGE_DYNAMIC, CPU_WRITE 자동 설정 (`Buffer.cpp:111-113`)
- `UpdateRaw`는 `D3D11_MAP_WRITE_DISCARD` Map/Unmap 패턴 (`Buffer.cpp:157-171`)

### FInstanceBuffer / InstancedBuffer 검색 결과
**0건**. 코드베이스에 존재하지 않습니다.

### DrawIndexedInstanced / DrawInstanced
프로젝트 코드 내 **0회** 사용. 검색 매치는 모두 `JSEngine/packages/directxtk_...` 서드파티 헤더.

### Dynamic VB 사용 사례 (D3D11_USAGE_DYNAMIC + Map/Unmap)

| 위치 | 패턴 |
|------|------|
| `FontBatcher.cpp:73, 80, 281, 286` | 직접 D3D11_BUFFER_DESC + Map/Unmap |
| `SubUVBatcher.cpp:73, 80, 162, 166` | 동일 |
| `LineBatcher.cpp:154, 686, 694` | 동일 |
| `Buffer.cpp:111, 165` | `FVertexBuffer::CreateRaw(bDynamic=true)` + `UpdateRaw` |
| `Buffer.cpp:186, 203` | `FConstantBuffer` (항상 DYNAMIC) |
| `Buffer.cpp:291, 338` | `FStructuredBuffer` (UAV가 아닌 경우 DYNAMIC) |

### 영향 평가
- **`FVertexBuffer::CreateRaw(bDynamic=true)` + `UpdateRaw`만으로 instance buffer 요구사항은 거의 충족**됩니다. 별도 클래스 없이 활용 가능.
- 의미적 분리가 좋다면 얇은 `FInstanceBuffer` 래퍼(내부적으로 `FVertexBuffer`를 owning) 또는 헬퍼 함수만 추가하는 정도가 적절.
- **`FMeshBuffer` 재사용은 부적합**: FMeshBuffer는 VB+IB 한 쌍의 묶음이고, instance VB는 IB 없이 단독으로 슬롯 1에 묶이는 별개 자원. 의미가 어긋남.

---

## [탐색 5] EVertexFactoryType 사용처

### Switch / 조건 분기 (read)

| 위치 | 패턴 | default 처리 |
|------|------|--------------|
| `VertexFactoryTypes.h:200-220` | `Registry::Get` 마스터 switch | StaticMesh, ProceduralMesh, **default → StaticMeshDesc** |
| `Renderer.cpp:51` | `BindVertexFactoryResources`, `if (Type == SkeletalMesh)` | SkeletalMesh 아니면 no-op (안전) |
| `Renderer.cpp:155-171` | EditorIdPick layout 선택, if/else if | default Primitive layout |
| `Renderer.cpp:705` | `Skeletal ? 3 : 1` for ShaderKey | 비-Skeletal은 1(StaticMesh) |
| `SelectionMaskRenderPass.cpp:39` | `if (== SkeletalMesh)` 단일 분기 | no-op |
| `SelectionMaskRenderPass.cpp:68-85` | layout 선택 if/else if | default Primitive |

### Registry::Get 호출자 (Desc의 사용 필드)
모든 호출자는 다음 중 하나를 읽습니다: `VertexShaderPath`, `BasePassVSEntry`, `DepthPassVSEntry`, `ShadowPassVSEntry`, `SelectionPassVSEntry`, `VertexLayout`, `PositionOnlyLayout`, `SelectionLayout`. — Opaque/Translucent/DepthPre/Shadow/SelectionMask 등 각 패스에서 자기 패스용 entry+layout 한 쌍 조회.

### 영향 평가 (신규 enum값 추가)
- **마스터 switch는 default fallback이 StaticMesh** — 명시 case 없이 추가하면 잘못된 desc가 매핑되어 디버깅 어려운 버그가 됩니다. **반드시 명시적 case 추가** 필요.
- 다른 분기들은 모두 "SkeletalMesh 여부"만 보거나 else-default가 안전 (Primitive layout). 신규 ParticleSprite 추가 시 BindVertexFactoryResources의 bone matrix 바인딩은 자동으로 건너뜀 — **안전**.
- EditorIdPick(`Renderer.cpp:705`)에서 Particle을 ShaderKey=1(StaticMesh)로 잘못 해석할 수 있음 — `ParticleSystemComponent::SupportsOutline() = false`이므로 outline은 영향 없으나, ID pick까지 비활성화하려면 EPT_ParticleSystem을 ID pick 루프 대상 패스(`PickPasses[]`)에 넣지 않거나 별도 분기 추가 필요.

---

## [탐색 6] FRenderCommand 분석

### 전체 정의 — `JSEngine/Source/Engine/Render/Scene/RenderCommand.h:433-479`

핵심 멤버:
- `FPerObjectConstants PerObjectConstants`, `UPrimitiveComponent* SourcePrimitive`
- `FMeshBuffer* MeshBuffer`, `UMaterialInterface* Material`
- `EVertexFactoryType VertexFactoryType`
- `uint32 SectionIndexStart, SectionIndexCount`
- 4개 본 관련 필드 (`bUseBoneMatrixConstants`, `BoneMatrixConstantsIndex`, `BoneMatrixConstantBuffer`, …)
- `FBoundingBox WorldAABB`
- **Union**: `AABB / OBB / DirectionalLight / PointLight / SpotLight / Line / Bone / Grid / Font / SubUV / Billboard / Fog / FXAA / Light / Decal` 13종
- `ERenderCommandType Type`

**InstanceBuffer / InstanceCount / InstanceStride 필드는 존재하지 않음.**

### 기존 SubUV / Billboard Constants
`RenderCommand.h:325-338`:
```cpp
struct FSubUVConstants {
    const FTextureAtlasResource* Atlas = nullptr;
    uint32 FrameIndex = 0;
    float Width = 1.0f, Height = 1.0f;
};
struct FBillboardConstants {
    UTexture* Texture = nullptr;
    float Width = 1.0f, Height = 1.0f;
    FColor Color = FColor::White();
};
```
모두 **per-command** 단일 인스턴스용. **per-particle** 변형 데이터는 없음.

### Stride 사용 시점 — 소비자별
모든 소비 패스는 `Cmd.MeshBuffer->GetVertexBuffer().GetStride()`로 stride를 얻고 `DrawIndexed(Cmd.SectionIndexCount, Cmd.SectionIndexStart, 0)`로 그립니다 (`OpaqueRenderPass.cpp:289`). **`FVertexLayoutDesc::Stride`는 안 씁니다.**

### 기존 TODO 코멘트 (중요)
`PrimitiveDrawCommandBuilder.cpp:535-551`:
```cpp
case EPrimitiveType::EPT_ParticleSystem:
{
    //FRenderCommand를 수정해야할 수 있음
    //FRenderCommand는 Constantbuffer를 처리하지만 instancebuffer에 대한 처리가 일절 없음.
    //Union에서 instancebuffer를 담당하도록 로직을 처리해야할 듯.
    UParticleSystemComponent* ParticleSystemComponent = Cast<UParticleSystemComponent>(Primitive);
    ...
    //LOD로부터 어떤 module을 가져올지 결정해야함.
}
default:
    return false;
```
**`break` 없이 `default`로 흘러 떨어지는 fall-through 버그**도 함께 발견 — switch의 ParticleSystem case는 `RenderBus.AddCommand` 호출이 없고 `return true`도 없어 `default: return false`로 떨어짐. (현재 의도된 미완성 상태.)

### 영향 평가 (신규 필드 추가)
- `InstanceBuffer`는 포인터(자원 참조), `InstanceCount`는 카운트 — **Union이 아닌 struct 멤버**로 추가하는 게 자연스러움 (Union 멤버는 모두 값 타입 constants).
- 영향 받는 코드: 모든 `FRenderCommand Cmd = {}` 초기화는 기본값 nullptr/0이라 안전. Draw 패스가 `if (Cmd.InstanceBuffer)`로 instanced 경로/non-instanced 경로 분기.

---

## [탐색 7] ERenderPass 추가 영향도

### 정의 — `JSEngine/Source/Engine/Render/Common/RenderTypes.h:48-66`
```
Opaque, Decal, Light, Fog, Sandervistan, FXAA, Font, SubUV,
Translucent, SelectionMask, Grid, Editor, EditorOverlay,
DepthLess, PostProcessOutline, MAX
```

### MAX 크기 의존 배열
- `RenderBus.h:123` — `TArray<FRenderCommand> PassQueues[(uint32)ERenderPass::MAX]`
- `Renderer.h:225` — `FPassRenderState PassRenderStates[MAX]`
- `Renderer.h:226` — `FPassBatcherBinding PassBatchers[MAX]`

### MAX 루프
- `RenderBus.cpp:5` — `Clear()`
- `Renderer.cpp:470` — `PrepareBatchers`, **`if (!PassBatchers[i]) continue`** 가드 있음

### Pass 등록 위치
- **enum** 추가 후 → **`RenderPipeline.cpp:126-145`** `RenderPasses.push_back(...)` 시퀀스에 새 RenderPass 객체 추가
- **`RenderPipeline.h`** 헤더에 forward declare 및 shared_ptr 멤버 추가
- **`RenderPipeline.cpp:32-51`** `GetRenderPassPerfName` 배열에 GPU 프로파일러 이름 추가 (안 하면 "RenderPass.Unknown"으로 보임)
- 새 RenderPass에서 `Context->RenderBus->GetCommands(ERenderPass::NewPass)` 직접 조회 — RenderPasses 배열 인덱스와 ERenderPass enum index 사이엔 직접 매핑 없음 (각 패스가 자기 패스 명시)
- `PassRenderStates[NewPass]`는 `Renderer.cpp:1230-1232` `InitializePassRenderStates`에서 명시 설정 안 해도 default `bWireframeAware=false` — 안전

### PassBatcher 안전성
`Renderer.h:136`:
```cpp
explicit operator bool() const { return Flush != nullptr; }
```
**Batcher를 안 쓰는 신규 패스는 `PassBatchers[NewPass]`를 등록하지 않으면 자동 skip** — `PrepareBatchers`의 가드가 안전망. 명시적 등록 불필요.

### 영향 평가
- 신규 `ERenderPass::Particle` 추가는 enum 값 / RenderPipeline에 패스 push_back / 신규 RenderPass 클래스 작성 / `GetRenderPassPerfName` 배열 갱신 정도.
- 다른 패스에서 enum 값에 의존하는 switch는 없으므로 안전.
- 단, ID pick 루프(`Renderer.cpp:671-676 PickPasses[]`)에는 명시 enum이 박혀있어 Particle을 ID pick하려면 추가, 안 하려면 무시.

---

## [탐색 8] UParticleSystemComponent 현재 상태

### 존재 여부
**존재함.** `JSEngine/Source/Engine/Particle/ParticleSystemComponent.h`, `.cpp`. `UPrimitiveComponent` 상속.

### 구현된 시뮬레이션
`ParticleSystemComponent.cpp:141-151`:
- `TickComponent` → 각 `FParticleEmitterInstance::Tick(DeltaTime)` 호출
- `SetTemplate`, `RecreateEmitterInstances`, `ClearEmitterInstances`
- `ComputeEmitterLODDistance` (카메라 거리)
- `QueueCollisionEvent`, `DispatchQueuedParticleEvents` (충돌 이벤트)
- `UpdateWorldAABB` — 활성 파티클 위치로 AABB 확장
- `RaycastMesh` — 항상 false 반환 (미구현)
- `SupportsOutline() = false`

`ParticleEmitterInstance.h`는 `ParticleData/ParticleIndices/InstanceData` 풀 + `ActiveParticles` + `ParticleStride` 보유. 시뮬레이션 데이터 구조는 갖춰져 있음.

### PrimitiveType
**`RenderTypes.h:44` `EPT_ParticleSystem` 존재.** `GetPrimitiveType() → EPT_ParticleSystem` (`ParticleSystemComponent.h:29`).

### CollectFromComponent 처리
`PrimitiveDrawCommandBuilder.cpp:535-551` — **case 자체는 있으나 빈 껍데기**. `Cast<UParticleSystemComponent>` 후 LOG만 찍고, `RenderBus.AddCommand` 호출 없음, return 없음 → `default: return false`로 흘러떨어짐. 위에서 언급한 TODO 코멘트("FRenderCommand 수정 필요, instancebuffer 처리 필요")가 박혀있는 미완성 stub.

### 영향 평가
시뮬레이션은 동작하지만 **렌더링 경로가 0%** — 시뮬레이션 결과가 그려지지 않습니다. 신규 작업의 핵심은 simulation → render command emit → instance buffer 생성 → DrawIndexedInstanced 호출의 풀 파이프라인 연결.

---

# 종합 결론

## 1. Stride: 자동 도출 (4번) vs 명시 (1번)

**권장: 명시 유지 (현행 1번).** 근거:
- Read는 단 1곳(`ShaderResourceCache.cpp:54`)뿐이며, 캐시 키 해시용. 자동 도출/제거 어느 쪽이든 GPU 동작에 영향 없음.
- 자동 도출(max(offset + format_size))은 **vertex struct의 trailing padding**과 어긋날 위험(예: `__declspec(align)`이나 자연 정렬로 sizeof가 가산되는 경우). 현 코드는 `sizeof(FNormalVertex)` 같은 표현이므로 컴파일러 정렬 그대로.
- PositionOnlyLayout은 의도적으로 0이며, "캐시 키만 다르게 만든다"는 미묘한 의도를 갖고 있어 자동 도출 시 의미가 살짝 바뀜.
- 변경 이득 < 변경 위험. **다만 신규 instance layout에서는 InputSlotClass/StepRate 같은 새 필드가 캐시 해시에 포함되도록 `HashVertexLayout`을 갱신해야 함.**

## 2. FInstanceBuffer 신규 클래스 vs 기존 FMeshBuffer 재사용

**권장: 얇은 `FInstanceBuffer` 신규 클래스 (또는 헬퍼 함수)**. 근거:
- `FMeshBuffer`는 VB+IB 한 쌍을 묶는 의미. instance VB는 IB 없는 별개 자원 → 의미 충돌.
- `FVertexBuffer::CreateRaw(bDynamic=true)` + `UpdateRaw`만으로 기능적 요구는 충족됨 (`Buffer.cpp:98-131`, `:157-171`).
- 얇은 래퍼는 인스턴스 갯수/스트라이드를 같이 들고 다니게 만들어 호출처가 깔끔. 헬퍼 함수만으로도 충분하면 굳이 클래스화 안 해도 OK.
- `FStructuredBuffer` 활용(SRV via t-slot, SV_InstanceID로 인덱싱) 옵션도 있으나, Cascade 호환성/표준 IA stream 활용 면에서 일반적인 instance VB가 낫습니다.

## 3. 예상 변경 범위와 주의사항

### 변경 위치 (영향 큰 순)
1. **`FVertexElementDesc` 확장** (`ShaderTypes.h:65-72`) — `InputSlotClass`, `InstanceDataStepRate` 필드 추가. 기존 정의는 default 값 채워지므로 호환.
2. **`BuildInputLayoutFromDesc` 수정** (`ShaderResourceCache.cpp:240-261`) — 하드코딩된 PER_VERTEX_DATA/0을 Element 값으로.
3. **`HashVertexLayout` 갱신** (`ShaderResourceCache.cpp:47-65`) — 신규 두 필드 해시 포함.
4. **`EVertexFactoryType::ParticleSprite`(가칭) 추가** + **Registry switch에 명시 case 추가** (`VertexFactoryTypes.h:200-220`) — default fallback 위험.
5. **신규 `ParticleSpriteLayout`** — InputSlot=0(per-vertex sprite quad) + InputSlot=1(per-instance Position/SubUVIndex/Color 등) Elements 조합.
6. **`FRenderCommand`에 `InstanceBuffer*`, `InstanceCount`, `InstanceStride` 필드 추가** — Union 밖, struct 레벨. Union의 SubUV/Billboard 변형으로는 부적합.
7. **신규 RenderPass / 또는 Translucent 재사용** — `DrawIndexedInstanced` 호출. Translucent에 끼우면 enum 추가 없이도 가능.
8. **`PrimitiveDrawCommandBuilder.cpp:535-551` 완성** — emit + `break` 또는 `return true`. 현재 fall-through 버그 동시 수정.

### 신경 써야 할 특이 케이스
- **`Renderer.cpp:670-776` Editor ID pick 루프**: `PickPasses[] = { Opaque, Translucent, SubUV }`에 박혀 있음. Particle을 Translucent에 흘려보내면 ID pick에 들어옴 → `Renderer.cpp:705`의 `ShaderKey = Skeletal ? 3 : 1`이 Particle을 StaticMesh로 오인. SupportsOutline=false라 outline은 영향 없으나, ID pick에 잡히지 않게 하려면 별도 분기 또는 ParticleSystem 패스 제외 필요.
- **`Renderer.cpp:155-171` EditorIdPick layout 선택**: if/else if 체인. Particle은 default Primitive layout으로 떨어짐 — 실제로 ID pick 안 할거면 무관.
- **`SelectionMaskRenderPass.cpp:68-85`**: Particle은 default Primitive로 fall-through — `SupportsOutline=false`라 SelectionMask 자체에 등록 안 되므로 안전.
- **`BindVertexFactoryResources` (`Renderer.cpp:39-85`)**: SkeletalMesh만 분기. Particle은 no-op로 자동 통과 — 안전.
- **`RenderPipeline.cpp` `GetRenderPassPerfName` 배열** — enum 추가 시 GPU 프로파일러 라벨도 추가 필요.
- **`PassBatchers[NewPass]`는 default-construct 시 `Flush=nullptr`라 PrepareBatchers의 `if (!PassBatchers[i]) continue`에 의해 자동 skip** (`Renderer.h:136`, `Renderer.cpp:472`) — 명시 등록 불필요.
- **InputLayout 캐시 분리 보장**: `FShaderStageKey::InputLayoutHash`가 새 instance layout 필드까지 포함하도록 `HashVertexLayout` 갱신 안 하면, 같은 VS의 PER_VERTEX/PER_INSTANCE 두 변형이 같은 캐시 키로 충돌. 반드시 함께 변경.
- **`PrimitiveDrawCommandBuilder.cpp:535-551` fall-through 버그**: 현재 `case EPT_ParticleSystem` 안에 `return`이나 `break`가 없어 `default: return false`로 흘러떨어짐. Particle 처리 시 동시 수정.
