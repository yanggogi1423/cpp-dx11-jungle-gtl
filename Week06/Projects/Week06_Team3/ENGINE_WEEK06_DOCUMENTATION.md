# KraftonEngine 개요 및 Week06 기능 문서

## 1. 엔진 전체 구조

이 엔진은 다음과 같은 런타임 흐름을 중심으로 구성되어 있다.

1. `UEngine::Tick`
2. `InputSystem::Tick`
3. `UEngine::WorldTick`
4. `UEngine::Render`
5. `IRenderPipeline::Execute`

핵심은 월드 시뮬레이션과 렌더링을 명확히 분리해두었다는 점이다.

- `UEngine`는 윈도우, 렌더러, 월드 컨텍스트, 활성 렌더 파이프라인을 소유한다.
- `UWorld`는 액터, 씬 등록, 공간 분할, 가시성 갱신, 피킹 가속 구조, 틱 매니저를 소유한다.
- `FScene`은 렌더 측 프리미티브 프록시와 안개, 로컬 이펙트 같은 씬 전역 효과 소스를 소유한다.
- `FRenderer`는 실제 렌더 패스를 실행하고, 상수 버퍼를 갱신하며, 후처리를 수행하는 저수준 렌더러이다.
- `FDefaultRenderPipeline`, `FEditorRenderPipeline`은 하나 이상의 `FRenderBus`를 구성해서 `FRenderer`에 전달한다.

정리하면 다음 관점으로 보면 이해가 쉽다.

- 게임플레이 측 데이터는 `Actor / Component / World`에 존재한다.
- 렌더 측 데이터는 `PrimitiveSceneProxy / RenderBus / Renderer`에 존재한다.
- 두 계층의 동기화는 `CreateRenderState`, `DestroyRenderState`, `MarkProxyDirty`를 통해 일어난다.

## 2. Tick 및 World 흐름

`UEngine::WorldTick`은 현재 월드 타입을 `ELevelTick`으로 변환한 뒤 각 월드를 틱한다.

- `Editor` 월드는 `LEVELTICK_ViewportsOnly`
- `PIE`, `Game` 월드는 `LEVELTICK_All`
- PIE 월드가 존재하면 에디터 월드는 중복 시뮬레이션을 막기 위해 스킵된다.

이후 `UWorld::Tick`은 대략 다음 순서로 수행된다.

1. `Partition.FlushPrimitive()`
2. `UpdateVisibleProxies()`
3. 디버그 드로우 갱신
4. `TickManager.Tick(...)`

즉 이 문서에서 설명하는 기능들은 다음 세 갈래 중 하나로 참여한다.

- 일반적인 액터/컴포넌트 틱 경로로 동작한다.
- 렌더 프록시 또는 씬 효과 데이터를 `FScene`에 등록한다.
- 이후 `FRenderer`에서 별도 렌더 패스나 후처리로 평가된다.

## 3. 렌더링 아키텍처

렌더러는 패스 기반으로 동작한다. `FRenderer::Render`는 고정된 순서로 패스를 실행한다.

- `Opaque`
- `Decal`
- `ProjectionDecal`
- `Translucent`
- `Grid`
- `SubUV`
- `Billboard`
- `Editor`
- `GizmoOuter`
- `GizmoInner`
- `Font`
- `PostProcess`
- `OverlayFont`

각 패스의 상태는 `FPassRenderState`가 단일 기준점 역할을 한다.

중요한 특징은 다음과 같다.

- `Decal`은 장면 깊이를 셰이더에서 직접 복원하므로 `NoDepth + Alpha Blend`를 사용한다.
- `ProjectionDecal`은 실제 투영용 지오메트리를 렌더링하므로 `DepthReadOnly + Alpha Blend`를 사용한다.
- `Fog`, `Outline`, `FXAA`는 후처리 체인 안에서 실행된다.
- `SceneDepth`는 일반 머티리얼 패스가 아니라 전용 시각화 모드이다.

`FScene`은 월드와 렌더러를 연결하는 다리 역할을 한다.

- 프리미티브 컴포넌트는 `FPrimitiveSceneProxy`를 생성한다.
- Fog 컴포넌트는 Fog 리스트에 등록된다.
- FireBall 같은 로컬 씬 이펙트는 `ISceneEffectSource`로 등록된다.
- 매 프레임 렌더 전에 dirty 프록시가 갱신된다.

## 4. Deferred Decal

관련 파일:

- `KraftonEngine/Source/Engine/Components/DecalComponent.*`
- `KraftonEngine/Source/Engine/Render/Proxy/DecalSceneProxy.*`
- `KraftonEngine/Shaders/Decal.hlsl`

### 4.1 데이터 모델

`UDecalComponent`는 `UPrimitiveComponent`를 상속하며, 데칼 볼륨 자체를 표현한다.

주요 데이터는 다음과 같다.

- `DecalSize`
- `DecalColor`
- `DecalMaterial` 또는 텍스처 경로
- Fade In/Out 파라미터
- Sort Order

이 컴포넌트는 `FMeshBufferManager`의 큐브 메시를 볼륨 메시로 사용한다. 월드 AABB는 `DecalSize`와 컴포넌트의 월드 변환으로부터 계산된다.

### 4.2 렌더링 경로

`CreateSceneProxy`는 `FDecalSceneProxy`를 반환한다.

이 프록시는 다음과 같은 역할을 한다.

- `EShaderType::Decal` 바인딩
- `ERenderPass::Decal`에서 렌더링
- 볼륨 큐브를 입력 지오메트리로 사용

셰이더는 전형적인 Screen-space / Deferred Decal 방식이다.

1. 버텍스 셰이더가 데칼 볼륨을 그린다.
2. 픽셀 셰이더가 `DepthTex`를 읽는다.
3. `InverseViewProjection`으로 월드 위치를 복원한다.
4. 그 위치를 `InverseDecalModel`로 데칼 로컬 공간으로 변환한다.
5. 복원된 픽셀이 단위 박스 내부인지 검사한다.
6. 로컬 YZ를 이용해 데칼 UV를 계산한다.
7. 데칼 텍스처를 샘플하고 필요하면 알파 기준으로 `discard`한다.

즉 실제 데칼이 붙는 표면은 버텍스 버퍼로 전달되지 않는다. 이미 렌더된 장면의 깊이로부터 표면을 복원하는 구조이다.

### 4.3 Fade 지원

`UDecalComponent`는 시간 기반 Fade 시스템을 포함하고 있다.

- `SetFadeIn`
- `SetFadeOut`
- `BeginPlay`에서 설정된 Fade 시퀀스 시작
- `TickComponent`에서 알파 갱신
- 갱신된 색은 dirty 마킹을 통해 렌더 프록시에 반영

즉 Deferred Decal은 단순 렌더링 기능을 넘어, 간단한 시간 기반 이펙트 시스템의 역할도 겸하고 있다.

### 4.4 Spot Light와의 재사용 관계

현재 `ASpotLightActor`는 `ADecalActor`를 상속해서 Deferred Decal 볼륨을 재사용한다.

- 액터를 전방으로 향하게 회전
- 데칼 텍스처를 `"light"`로 설정
- 스포트라이트 아이콘 빌보드 사용

따라서 현재 엔진의 `Spot Light`는 물리 기반 라이팅 시스템이라기보다, 구조적으로는 특수한 Deferred Decal Actor에 가깝다.

## 5. Projection Decal

관련 파일:

- `KraftonEngine/Source/Engine/Components/ProjectionDecalComponent.*`
- `KraftonEngine/Source/Engine/Mesh/ProjectionDecalMeshBuilder.*`
- `KraftonEngine/Source/Engine/Render/Proxy/ProjectionDecalSceneProxy.*`
- `KraftonEngine/Shaders/ProjectionDecal.hlsl`

### 5.1 개념

Projection Decal은 대상 표면을 깊이에서 복원하지 않는다. 대신 CPU에서 실제 대상 메시를 잘라서, 이미 데칼 로컬 공간에 들어와 있는 전용 지오메트리를 만든다.

즉 고전적인 Deferred Decal보다는, 투영용 메시 오버레이에 더 가깝다.

### 5.2 CPU 메시 빌드

`FProjectionDecalMeshBuilder::BuildRenderableMesh`는 월드의 액터를 순회하며 `UStaticMeshComponent` 수신 대상을 필터링한다.

필터링 단계는 다음과 같다.

1. 비가시 액터/메시 스킵
2. 필요 시 동일 Owner 제외
3. 월드 AABB 기반 1차 충돌 판정
4. OBB vs OBB SAT 기반 2차 정밀 판정
5. 대상 메시 버텍스를 ProjectionDecal 로컬 공간으로 복사

빌더는 각 월드 버텍스를 `WorldToProjectionDecal`로 변환하여 다음 데이터를 저장한다.

- 로컬 위치
- 로컬 노멀
- 컬러
- UV

### 5.3 셰이더 경로

Projection Decal 셰이더는 Deferred Decal보다 훨씬 단순하다.

입력 버텍스가 이미 데칼 로컬 공간에 있으므로, 픽셀 셰이더는 다음 정도만 수행하면 된다.

1. `abs(localPos) <= 0.5` 검사
2. 로컬 YZ로 UV 계산
3. 텍스처 샘플
4. 알파가 낮으면 `discard`
5. 로컬 노멀을 이용한 약한 facing fade 적용

Deferred Decal과의 본질적인 차이는 다음이다.

- Deferred Decal은 장면 깊이에서 월드 위치를 복원한다.
- Projection Decal은 CPU가 만든 전용 버텍스 버퍼를 사용한다.

즉 셰이더 입장에서는 Projection Decal이 훨씬 단순하지만, 대신 CPU 측 메시 구성 비용이 존재한다.

### 5.4 Fade 지원

현재 `ProjectionDecalComponent`는 기존 MeshDecal 스타일의 Fade 경로를 이식받은 상태이다.

지원 항목:

- `ProjectionDecalColor`
- `SetFadeIn`
- `SetFadeOut`
- `BeginPlay`
- `TickComponent`

프록시는 이 색을 `PerObjectConstants.Color`에 기록하므로, 알파 페이드가 실제 렌더 결과에 반영된다.

## 6. Exponential Height Fog

관련 파일:

- `KraftonEngine/Source/Engine/Components/ExponentialHeightFogComponent.*`
- `KraftonEngine/Source/Engine/Render/Proxy/FScene.*`
- `KraftonEngine/Source/Engine/Render/Fog/FogRenderTypes.h`
- `KraftonEngine/Shaders/Common/FogCommon.hlsl`
- `KraftonEngine/Shaders/FogPostProcess.hlsl`

### 6.1 컴포넌트 계층

`UExponentialHeightFogComponent`는 `UPrimitiveComponent`를 상속하지만, 일반 프리미티브 프록시를 만들지는 않는다.

대신 다음 방식으로 동작한다.

- `CreateRenderState`에서 `FScene`에 등록
- `DestroyRenderState`에서 `FScene`에서 해제
- `BuildFogUniformParameters`로 `FFogUniformParameters` 생성

주요 속성:

- Density
- Height Falloff
- Fog Base Height
- Start Distance
- Cutoff Distance
- Max Opacity
- Fog Inscattering Color

### 6.2 씬 등록 방식

`FScene`은 `FogComponents` 리스트를 소유하고, `GetFogPostProcessConstants()`를 제공한다.

현재 구현은 활성 Fog 중 첫 번째 항목을 대표 Fog로 사용하여 `FFogPostProcessConstants`에 패킹한다.

즉 구조는 다중 Fog를 받을 수 있게 열어두었지만, 실제 현재 경로는 첫 활성 Fog 하나를 쓰는 단순화된 형태이다.

### 6.3 후처리 경로

Fog는 메시 단위 Forward Shading이 아니라 후처리 효과로 구현되어 있다.

`FRenderer`는 Fog를 `EPostEffectType::Fog`로 등록하고 `DrawPostProcessFog`를 호출한다.

셰이더는 다음 순서로 동작한다.

1. Scene Color와 Depth를 읽는다.
2. 깊이로부터 월드 위치를 복원한다.
3. 카메라 거리와 높이 감쇠를 이용해 안개 밀도를 계산한다.
4. 원래 색을 안개 색 쪽으로 보간한다.

추가 처리:

- 깊이가 far plane이면 하늘 또는 원거리 배경으로 간주
- 이 경우 고정된 fallback 거리로 안개 가중치를 계산

즉 전형적인 Deferred Post-process Fog 구조라고 볼 수 있다.

## 7. Spot Light

관련 파일:

- `KraftonEngine/Source/Engine/GameFramework/SpotLightActor.*`
- `KraftonEngine/Source/Engine/Components/DecalComponent.*`

현재 Spot Light는 의도적으로 가볍게 구현되어 있다.

구현 요약:

- `ASpotLightActor`는 `ADecalActor`를 상속
- 액터를 `(90, 0, 0)` 만큼 회전
- 데칼 컴포넌트에 `"light"` 텍스처 지정
- 빌보드에는 `"SpotLightIcon"` 스프라이트 지정

즉 현재 엔진의 Spot Light는 별도의 그림자 기반 광원이나 BRDF 라이팅 패스가 아니라, Deferred Decal 시스템을 재사용한 시각 효과이다.

따라서 현재 구조에서 Spot Light는 사실상 다음 세 요소의 조합이다.

- 에디터에서 보이는 액터
- 아이콘 빌보드
- 광원처럼 보이게 하는 특수 데칼 투영

간단하고 싸며 편집하기 쉽지만, 물리 기반 스포트라이트는 아니다.

## 8. FireBall

관련 파일:

- `KraftonEngine/Source/Engine/Components/FireBallComponent.*`
- `KraftonEngine/Source/Engine/Components/SceneEffectSource.h`
- `KraftonEngine/Source/Engine/Render/Proxy/FScene.*`
- `KraftonEngine/Source/Engine/Render/Pipeline/Renderer.cpp`

### 8.1 역할

`UFireBallComponent`는 일반 프리미티브 프록시로 렌더링되지 않는다.

대신 `ISceneEffectSource`를 구현하며, 씬 전역에 로컬 효과 입력을 제공하는 소스로 동작한다.

### 8.2 등록 경로

렌더 상태 생성 시점에 다음이 수행된다.

- `FScene::SceneEffectSources`에 자기 자신을 등록

이후 매 프레임 다음 흐름을 탄다.

- `FScene::GetSceneEffectConstants()`가 활성 씬 이펙트 목록 수집
- `FRenderer::UpdateSceneEffectBuffer()`가 이를 상수 버퍼 `b5`에 업로드

FireBall 하나당 기록되는 데이터는 대략 다음과 같다.

- 위치와 반경
- 색상
- 강도
- 반경 감쇠 값

### 8.3 의미

이 기능은 메시 렌더러라기보다 로컬 틴트 또는 로컬 글로우 기여 시스템으로 보는 편이 맞다.

이 방식의 장점은 다음과 같다.

- 별도 프록시가 필요 없다.
- 파라미터 업로드만으로 동작해 가볍다.
- 여러 이펙트를 하나의 씬 상수 버퍼로 묶기 쉽다.

즉 FireBall은 구조적으로 일반 메시보다는 씬 이펙트 볼륨에 더 가깝다.

## 9. SceneDepth 뷰 모드

관련 파일:

- `KraftonEngine/Shaders/SceneDepthVisualize.hlsl`
- `KraftonEngine/Source/Engine/Render/Pipeline/Renderer.cpp`
- `KraftonEngine/Source/Engine/Render/Resource/ShaderManager.cpp`
- `KraftonEngine/Source/Engine/Render/Types/ViewTypes.h`

### 9.1 목적

SceneDepth는 깊이 값을 그레이스케일로 시각화하는 디버그 뷰 모드이다.

### 9.2 셰이더 로직

셰이더는 다음 순서로 동작한다.

1. Depth Texture를 읽는다.
2. Device Z를 View-space Depth로 변환한다.
3. Near/Far Plane을 이용해 정규화한다.
4. 로그 스케일과 대비 보정을 적용한다.
5. 최종적으로 그레이스케일 값을 출력한다.

의도한 목표는 UE 스타일의 가독성이다.

- 가까운 깊이는 어둡게
- 먼 깊이는 밝게
- 아무것도 그려지지 않은 배경은 검게 유지

### 9.3 렌더러 동작

렌더러는 `SceneDepth` 모드를 일반 후처리 토글처럼 다루지 않는다.

- 일반 후처리 체인 대신 깊이 시각화를 직접 그린다.
- Fog와 FXAA는 SceneDepth 모드에서 스킵된다.
- Outline은 패스 정책에 따라 일부 허용될 수 있다.

즉 SceneDepth는 단순한 디버그 오버레이가 아니라, 프레임 최종 합성 방식 자체를 바꾸는 모드이다.

## 10. FXAA

관련 파일:

- `KraftonEngine/Shaders/FXAA.hlsl`
- `KraftonEngine/Source/Engine/Render/Pipeline/Renderer.*`
- `KraftonEngine/Source/Editor/Settings/EditorSettings.*`
- `KraftonEngine/Source/Editor/UI/EditorMainPanel.cpp`

### 10.1 파이프라인 위치

FXAA는 `FRenderer::RegisterPostEffect`를 통해 등록되는 후처리 패스이다.

- Outline 뒤에서 실행
- 활성화되어 있을 때만 동작
- `SceneDepth` 뷰 모드에서는 스킵

### 10.2 셰이더 로직

FXAA 셰이더는 전형적인 단일 패스 FXAA 구현이다.

1. Scene Color와 주변 픽셀의 휘도 샘플
2. 로컬 대비 계산
3. 의미 있는 에지가 없으면 Early-out
4. 에지 방향이 수평인지 수직인지 추정
5. 그 방향으로 샘플 탐색
6. 서브픽셀 오프셋 계산
7. 보정된 UV로 다시 샘플링

### 10.3 에디터 연동

에디터에서는 다음 항목을 조절할 수 있다.

- 활성/비활성
- Preset Stage
- Edge Threshold
- Edge Threshold Min
- Search Steps

설정은 `EditorSettings`에 저장되고, 이후 렌더러 상수 버퍼로 전달된다.

즉 셰이더 코드를 수정하지 않아도 런타임에서 FXAA를 조정할 수 있다.

## 11. RotatingMovementComponent

관련 파일:

- `KraftonEngine/Source/Engine/Components/RotatingMovementComponent.*`

`URotatingMovementComponent`는 매 틱마다 대상 Scene Component를 지속적으로 회전시키는 이동 계열 컴포넌트이다.

전형적인 흐름은 다음과 같다.

1. Updated Component를 찾는다.
2. 회전 속도와 DeltaTime으로 각도 변화량을 계산한다.
3. Scene Component에 회전을 적용한다.

이 컴포넌트의 핵심 가치는 반복적인 회전 로직을 액터 클래스에서 빼내어 재사용 가능한 정책으로 분리했다는 점이다.

즉 액터는 얇게 유지하고, 실제 움직임 규칙은 컴포넌트가 담당하는 현재 엔진의 구성 철학과 잘 맞는다.

## 12. ProjectileMovementComponent

관련 파일:

- `KraftonEngine/Source/Engine/Components/ProjectileMovementComponent.*`

### 12.1 목적

`UProjectileMovementComponent`는 단순한 투사체 이동을 담당하는 컴포넌트이다.

보유 데이터:

- `Velocity`
- `InitialSpeed`
- `MaxSpeed`

### 12.2 런타임 동작

런타임에서는 대략 다음 순서로 동작한다.

1. Updated Component를 찾는다.
2. 실제 사용할 속도를 계산한다.
3. 명시적 속도가 없으면 Owner의 Forward 방향에서 속도를 유도한다.
4. `Velocity * DeltaTime` 만큼 위치를 이동시킨다.

기본 충돌 처리 동작은 현재 `Stop`이다.

### 12.3 에디터 시각화

이 컴포넌트는 `CollectEditorVisualizations`를 통해 에디터용 시각화도 지원한다.

즉 단순 이동 로직일 뿐 아니라, 디버깅 의도까지 렌더 버스에 직접 전달하는 구조라고 볼 수 있다.

## 13. FireBall + Projectile + Rotation의 관계

이 세 요소는 현재 엔진의 조합형 설계를 잘 보여주는 예시이다.

- `ProjectileMovementComponent`는 이동을 담당
- `RotatingMovementComponent`는 지속 회전을 담당
- `FireBallComponent`는 로컬 시각 효과 기여를 담당
- 필요하면 `SubUVComponent`가 애니메이션 스프라이트 표현을 담당

즉 하나의 거대한 액터 클래스에 모든 기능을 몰아넣지 않고, 역할별 컴포넌트를 조합해 이펙트 액터를 완성하는 구조이다.

## 14. SubUV와 FireBall 표현

관련 파일:

- `KraftonEngine/Source/Engine/Components/SubUVComponent.*`
- `KraftonEngine/Source/Engine/Render/Proxy/SubUVSceneProxy.*`
- `KraftonEngine/Source/Engine/Render/Batcher/SubUVBatcher.*`
- `KraftonEngine/Shaders/ShaderSubUV.hlsl`

주차 주제는 FireBall이지만, 실제 화면에 보이는 애니메이션 표현은 SubUV 스프라이트 파이프라인을 기반으로 구성된다.

### 14.1 컴포넌트

`USubUVComponent`는 `UBillboardComponent`를 확장하며 다음 정보를 가진다.

- 파티클 리소스 이름
- 아틀라스 열/행 수
- 시작/끝 프레임
- 재생 속도
- 루프 여부
- 현재 프레임

### 14.2 Tick

매 틱마다 `PlayRate`와 프레임 지속 시간을 이용해 `FrameIndex`를 갱신한다.

지원 기능:

- 루프 애니메이션
- 원샷 재생
- 에디터에서 조절 가능한 프레임 범위

### 14.3 렌더링 경로

`FSubUVSceneProxy`는 일반 프리미티브 드로우 경로로 직접 렌더링되지 않는다.

대신 다음 흐름을 따른다.

- `bBatcherRendered = true` 설정
- `FSubUVEntry`를 RenderBus에 기록
- `FRenderer::PrepareBatchers`가 이를 수집 및 정렬
- `FSubUVBatcher`가 `SubUV` 패스에서 최종 렌더링

셰이더는 현재 프레임에 해당하는 UV로 아틀라스를 샘플하고, 보이지 않는 픽셀은 `discard`한다.

즉 FireBall 같은 애니메이션 이펙트의 실제 시각 표면은 SubUV가 담당하는 구조라고 이해하면 된다.

## 15. 설계 요약

이번 주 기능들을 관통하는 중요한 구조적 특징은, 엔진이 모든 기능을 하나의 렌더링 방식에 억지로 몰아넣지 않는다는 점이다.

대신 다음과 같이 병렬적인 전략을 사용한다.

- 일반 프리미티브는 프록시 기반 직접 패스
- Deferred Decal, Fog는 깊이 기반 후처리 또는 스크린 공간 처리
- Projection Decal은 CPU에서 만든 전용 지오메트리 사용
- FireBall 계열은 씬 이펙트 상수 버퍼 사용
- SubUV는 배처 기반 스프라이트 렌더링 사용
- 회전/투사체 이동은 재사용 가능한 Movement Component로 분리

이 분리는 장점이 크다. 각 기능이 가장 자연스럽고 비용이 맞는 경로를 선택할 수 있기 때문이다.

## 16. 추천 코드 읽기 순서

팀원이 엔진 구조를 빠르게 파악하려면 다음 순서로 읽는 것이 효율적이다.

1. `Source/Engine/Runtime/Engine.cpp`
2. `Source/Engine/GameFramework/World.cpp`
3. `Source/Engine/Render/Proxy/FScene.*`
4. `Source/Engine/Render/Pipeline/Renderer.*`
5. `Source/Engine/Components/DecalComponent.*`
6. `Source/Engine/Components/ProjectionDecalComponent.*`
7. `Source/Engine/Mesh/ProjectionDecalMeshBuilder.*`
8. `Source/Engine/Components/ExponentialHeightFogComponent.*`
9. `Source/Engine/Components/ProjectileMovementComponent.*`
10. `Source/Engine/Components/RotatingMovementComponent.*`
11. `Source/Engine/Components/FireBallComponent.*`
12. `Source/Engine/Components/SubUVComponent.*`
13. `Shaders/Decal.hlsl`
14. `Shaders/ProjectionDecal.hlsl`
15. `Shaders/FogPostProcess.hlsl`
16. `Shaders/FXAA.hlsl`
17. `Shaders/SceneDepthVisualize.hlsl`

## 17. 현재 한계 및 주의점

- `SpotLightActor`는 현재 데칼 기반의 가짜 스포트라이트이며, 물리 기반 광원 패스는 아니다.
- Fog 상수 레이아웃은 여러 엔트리를 지원할 수 있지만, 현재 실제 사용 경로는 첫 번째 활성 Fog 위주이다.
- `ProjectionDecal`은 CPU 메시 추출에 의존하므로, Deferred Decal보다 빌드/갱신 비용이 더 클 수 있다.
- `SceneDepth`는 의도적으로 일반 후처리 체인의 일부를 우회한다.
- `FireBallComponent`는 씬 이펙트 소스이므로, 눈에 보이는 지오메트리와 로컬 틴트 기여를 분리해서 이해해야 한다.
