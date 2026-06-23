# Bloom / HDR Tone Mapping Batch Plan

## Goal

Week07 engine의 Bloom과 HDR Tone Mapping 흐름을 Week12 JSEngine 렌더 파이프라인에 맞게 정식 기능으로 이식한다.

최종 목표는 단순 MVP가 아니라 다음 조건을 만족하는 기능이다.

- SceneColor를 HDR color buffer로 운용한다.
- Bloom threshold, blur, composite 단계를 분리한다.
- Tone Mapping은 PostProcess 단계에서 노출값과 mapping mode를 기준으로 수행한다.
- Editor UI에서 Bloom / Tone Mapping 값을 조절할 수 있다.
- Particle, translucent object, static mesh가 같은 scene color 흐름 안에서 자연스럽게 후처리된다.
- 리사이즈, SRV/UAV unbind, resource lifetime이 안정적으로 관리된다.

## Non Goal

이번 배치에서 바로 하지 않는 항목은 다음과 같다.

- Auto Exposure
- Lens Dirt Texture
- Multi mip bloom pyramid
- Camera별 post process volume blending
- 물리 기반 camera response curve

이 항목들은 Bloom / Tone Mapping의 기본 파이프라인이 안정화된 뒤 확장한다.

## Batch 1. Investigation / Plan

목표:

- Week07 Bloom / Tone Mapping 구현 구조 확인
- Week12 RenderTarget, PostProcess, RenderPipeline 구조 확인
- 현재 SceneColor format과 resource binding 방식 확인
- 작업 범위와 위험 요소 문서화

산출물:

- 이 문서
- Week12에 맞춘 구현 순서

위험 요소:

- 현재 PostProcess pass 위치가 translucent / particle보다 앞에 있으면 Bloom 대상이 누락될 수 있다.
- SceneColor가 LDR format이면 Bloom threshold가 의미 있게 동작하기 어렵다.
- Compute shader composite를 쓰려면 UAV resource 관리가 필요하다.

## Batch 2. HDR Render Target / UAV Groundwork

목표:

- SceneColor를 HDR format으로 변경한다.
- RenderTargetBuilder와 FRenderTarget에 UAV 생성 옵션을 추가한다.
- Bloom temporary target이 SRV/UAV로 안정적으로 사용될 수 있게 한다.

예상 변경:

- `FRenderTarget`
- `FRenderTargetBuilder`
- `RenderTargetFactory`

완료 기준:

- SceneColor가 `DXGI_FORMAT_R16G16B16A16_FLOAT`로 생성된다.
- 필요한 render target은 RTV/SRV/UAV 조합을 선택적으로 만들 수 있다.
- 기존 렌더 패스가 빌드 깨짐 없이 동작한다.

## Batch 3. Tone Mapping

목표:

- PostProcess shader에 Tone Mapping을 추가한다.
- Exposure, mode, enable flag를 constant buffer로 전달한다.
- 기존 gamma correction과 충돌하지 않게 정리한다.

지원 모드:

- Linear
- Reinhard
- ACES
- Hable

예상 변경:

- `RenderCommand.h`
- `PostProcessRenderPass`
- `PostProcess.hlsl`
- Editor settings / UI

완료 기준:

- Tone Mapping enable/disable이 가능하다.
- Exposure 조절이 화면에 반영된다.
- ACES를 기본 모드로 사용할 수 있다.

## Batch 4. Bloom Pass

목표:

- Bloom 전용 render pass를 추가한다.
- Threshold, blur, composite compute shader를 추가한다.
- Bloom 결과를 HDR scene color 흐름에 합성한다.

단계:

1. Threshold: 밝은 픽셀 추출
2. Blur: ping-pong blur
3. Composite: 원본 scene color + bloom color

예상 변경:

- `BloomRenderPass.h`
- `BloomRenderPass.cpp`
- `BloomThresholdCS.hlsl`
- `BloomBlurCS.hlsl`
- `BloomCompositeCS.hlsl`
- `Renderer.cpp` shader load

완료 기준:

- Bloom enable/disable이 가능하다.
- Bloom threshold/intensity/blur iteration이 동작한다.
- Resource resize와 unbind가 안정적이다.

## Batch 5. Pipeline Integration

목표:

- Bloom과 Tone Mapping이 scene rendering 이후, editor overlay 이전에 적용되도록 pass 순서를 정리한다.
- Particle과 translucent object가 Bloom 대상에 포함되게 한다.

권장 순서:

```text
Opaque
Light
Fog
Sandevistan
Translucent
Particle
Bloom
PostProcess / ToneMapping
FXAA
Editor UI / Overlay
```

완료 기준:

- Particle emissive color가 Bloom에 기여할 수 있다.
- UI / editor overlay는 불필요하게 tone mapping되지 않는다.
- 기존 selection / outline pass가 깨지지 않는다.

## Batch 6. Verification / Tuning

목표:

- Debug x64 빌드 통과
- 간단한 scene에서 HDR / Bloom / Tone Mapping 동작 확인
- Particle demo scene에서 Bloom 효과 확인

체크리스트:

- Build success
- Resize 후 crash 없음
- Bloom off 시 기존 화면과 유사
- Bloom on 시 밝은 particle / material 주변 glow 확인
- Exposure 값 변경 시 화면 밝기 변화 확인
- Editor UI / gizmo / collider debug draw가 정상 표시

