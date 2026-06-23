# Beam에 PNG/Material 적용 가능성 점검 보고서

## Context

사용자는 Cycle 13a/13b로 구현된 Beam 시스템에 PNG 텍스처와 프로젝트 `.meta` 메타데이터를 활용해 시각적 효과(lightning bolt, 광선 등)를 낼 수 있는지 알고 싶어한다. 이 보고서는 **코드 수정 없이** Cycle 13a/13b 리포트, Beam 구현 파일, Material/Texture 시스템을 점검한 결과와, 만약 추가 기능을 원할 경우의 수정 위치를 정리한 것이다. 사용자 요청에 따라 실제 코드는 수정하지 않는다.

---

## TL;DR — 핵심 결론

| 질문 | 결과 |
|------|------|
| Beam에 PNG로 효과를 낼 수 있는가? | **YES — 코드 수정 0건. 이미 완전히 작동 중.** |
| Beam emitter texture로 PNG를 쓸 수 있는가? | **YES — `.mat` 파일의 `DiffuseMap` 필드에 PNG 경로 직접 참조.** |
| `.meta` 메타데이터를 활용할 수 있는가? | **부분적으로** — 현재 `.meta`의 `Columns/Rows`는 Sprite SubUV에서만 사용. Beam에선 무시됨. |
| 코드 수정이 필요한가? | **기본 사용은 불필요.** Additive blend / SubUV 애니메이션을 원하면 옵션 B/C. |

---

## 1. 현재 시스템 검증 결과

### 1.1 이미 작동하는 PNG → Beam 흐름 (코드 수정 불필요)

데이터 흐름:

```
PNG (Asset/.../foo.png)
  ↓  ResourceManager::LoadTexture → UTexture (D3D11 SRV)
.mat ({ "DiffuseMap": "Asset/.../foo.png", ... })
  ↓  MaterialLoadService → UMaterial (MaterialParams["DiffuseMap"] = UTexture*)
.particlesystem ({ "Class":"UBeamTypeData", "Material":"Asset/.../Beam.mat" })
  ↓  Reflection picker → UBeamTypeData::Material
PrimitiveDrawCommandBuilder.cpp:665-666
  → Cmd.Material = BeamTD->GetMaterial()
PrimitiveDrawCommandBuilder.cpp:708-723 (Mesh||Ribbon||Beam 분기)
  → Material->GetParam("DiffuseMap") → Cmd.ParticleTexture
ParticleRenderPass.cpp:604-613 (RenderBeamEmitter)
  → DeviceContext->PSSetShaderResources(0, 1, &TextureSRV)
BeamParticle.hlsl:52
  → return BeamAlbedo.Sample(BeamSampler, input.TexCoord) * input.Color;
```

핵심 검증 위치:

- [ParticleModuleTypeDataBeam.h:62-63](JSEngine/Source/Engine/Particle/ParticleModuleTypeDataBeam.h:62) — `UPROPERTY(..., ReferenceKind = Asset) UMaterialInterface* Material` 이미 존재 (에디터 picker 자동 노출)
- [PrimitiveDrawCommandBuilder.cpp:659-668](JSEngine/Source/Engine/Render/Scene/PrimitiveDrawCommandBuilder.cpp:659) — Beam case에서 Material 추출
- [PrimitiveDrawCommandBuilder.cpp:708-723](JSEngine/Source/Engine/Render/Scene/PrimitiveDrawCommandBuilder.cpp:708) — `Mesh||Ribbon||Beam`로 DiffuseMap 추출 분기 (Cycle 13a §2에서 확장 완료)
- [ParticleRenderPass.cpp:560-639](JSEngine/Source/Engine/Render/Renderer/RenderFlow/ParticleRenderPass.cpp:560) — `RenderBeamEmitter`
- [ParticleRenderPass.cpp:604-613](JSEngine/Source/Engine/Render/Renderer/RenderFlow/ParticleRenderPass.cpp:604) — `ParticleTexture->GetSRV()` 바인딩 (없으면 `DefaultWhiteSRV` fallback)
- [BeamParticle.hlsl:50-54](JSEngine/Shaders/Particle/BeamParticle.hlsl:50) — `Sample * input.Color` (텍스처 × vertex color 합성)
- [ParticleBeamEmitterInstance.cpp:278, 384-386](JSEngine/Source/Engine/Particle/ParticleBeamEmitterInstance.cpp:278) — `TextureTile` / `TextureTileDistance` UV 반복 이미 계산
- 실제 PNG 자원: `Asset/Cube/Cube.png`, `Asset/Cube/Projectile.png`, `Asset/Mesh/.../*.png` (150+개)
- `.mat` 형식 실증: `Asset/Material/Auto/Dice_Mat_0.mat:21-23` `"DiffuseMap" : "Asset/Mesh/Dice/Dice_Diffuse.png"`

### 1.2 현재의 제약 (미구현)

| 제약 | 위치 | 영향 |
|------|------|------|
| Additive blend 미지원 | [RenderResources.h:66-71](JSEngine/Source/Engine/Render/Resource/RenderResources.h:66) — `EBlendType { Opaque, AlphaBlend, NoColor }` | Lightning bolt 같은 발광 표현은 AlphaBlend로만 가능 (자연스럽지 못함) |
| Beam blend 하드코딩 | [ParticleRenderPass.cpp:580](JSEngine/Source/Engine/Render/Renderer/RenderFlow/ParticleRenderPass.cpp:580) — `EBlendType::AlphaBlend` 고정 | Material의 BlendType이 무시됨 |
| SubUV/애니메이션 미지원 | `BeamParticle.hlsl`이 단일 텍스처만 샘플 | `.meta`의 `Columns/Rows`가 Beam에선 사용되지 않음 (Sprite에서만 [Builder.cpp:705-706](JSEngine/Source/Engine/Render/Scene/PrimitiveDrawCommandBuilder.cpp:705)에서 채워짐) |

> Cycle 13a §11 `"Additive blend (EBlendType에 Additive 값 없음 — Material/RenderResources 별도 cycle)"` — 13a 작성자 본인이 명시한 후속 작업.

---

## 2. 옵션 A — 기본 워크플로우 (코드 수정 0건, 즉시 사용 가능)

추천 사용 절차 (사용자가 에디터/스크립트로 실행):

1. **PNG 준비**: 예) `Asset/Particle/lightning_purple.png` 배치
2. **`.meta` 자동 생성**: `FTextureAssetMetaService::LoadOrCreate`가 없는 경우 자동 생성 (`{"Columns":1,"Rows":1,"Type":"None"}`)
3. **`.mat` 생성** (에디터 또는 JSON 직접 작성):
   ```json
   {
     "Name": "BeamLightning",
     "Params": [
       { "Name":"DiffuseMap", "Type":"Texture", "Value":"Asset/Particle/lightning_purple.png" },
       { "Name":"bHasDiffuseMap", "Type":"Bool", "Value":true }
     ],
     "ShaderType": "SurfaceLit"
   }
   ```
   주의: `ShaderType`은 Beam 렌더링에서 사용되지 않음 (Beam은 자체 `BeamParticle.hlsl` 강제). 의미상 채워두기만 하면 됨.
4. **.particlesystem 의 `UBeamTypeData.Material`**에 `.mat` 경로 지정 (에디터 picker 사용 권장)
5. **추가 조정**:
   - `TextureTile = N` → 텍스처가 strip 전체에 N회 반복
   - `TextureTileDistance > 0` → 거리 기반 누적 타일링 (스크롤 효과의 기반)
   - 파티클 `Color` → 텍스처 위에 곱셈 합성 → tint/페이드 가능

검증 방법은 §5 참조.

---

## 3. 옵션 B — Additive Blend 지원 (코드 수정 필요, 권장 범위)

### 3.1 변경 위치 (3개 파일)

| 파일 | 변경 |
|------|------|
| [RenderResources.h:66-71](JSEngine/Source/Engine/Render/Resource/RenderResources.h:66) | `enum class EBlendType`에 `Additive` 추가 |
| `FResourceManager::GetOrCreateBlendState` (구현부) | `Additive` 케이스 추가 — `SrcBlend=SRC_ALPHA, DestBlend=ONE, BlendOp=ADD` (또는 더 단순한 `ONE,ONE`) |
| [ParticleRenderPass.cpp:580](JSEngine/Source/Engine/Render/Renderer/RenderFlow/ParticleRenderPass.cpp:580) | 하드코딩된 `AlphaBlend`를 Material의 BlendType으로 조회하도록 변경. 대안: TypeData에 `EBlendType BeamBlendMode` UPROPERTY 추가하고 Builder가 Cmd로 전달 |

### 3.2 권장 설계 (가장 작은 변경)

`UBeamTypeData`에 `UPROPERTY(...) EBlendType BlendMode = EBlendType::AlphaBlend;` 추가 → Builder가 `Cmd.BeamBlendMode`로 복사 → `RenderBeamEmitter`가 그 값으로 `GetOrCreateBlendState` 호출.

Material 자체의 BlendType 필드를 활용하는 안은 Material 시스템이 Lit shader 기준이라 부작용 위험이 더 큼 — Beam은 자체 ShaderType이 없어 Material 상태가 그대로 적용되지 않는 구조라서, **TypeData 멤버 추가가 더 결합도가 낮다**.

### 3.3 기존 Cycle 13b 영향 없음

NoiseSamples/payload는 그대로 유지. Visual 결과만 lightning bolt가 진짜 발광체로 보임.

---

## 4. 옵션 C — SubUV 텍스처 애니메이션 (코드 수정 필요, 큰 변경)

### 4.1 변경 위치 (5~6개 파일)

| 파일 | 변경 |
|------|------|
| [Builder.cpp:659-668](JSEngine/Source/Engine/Render/Scene/PrimitiveDrawCommandBuilder.cpp:659) | Beam case에 `Cmd.ParticleSubUVColumns/Rows` 채움 (`FTextureAssetMetaService`로 `.meta` 조회 또는 TypeData UPROPERTY로 명시) |
| `ParticleBeamEmitterInstance.cpp` (`BuildVertexBuffer`) | 각 vertex의 `TexCoordU` 계산을 frame index 기반으로 변환. V 좌표도 frame row를 반영하려면 vertex에 별도 필드 추가 필요 |
| [BeamParticleVertex 구조](JSEngine/Source/Engine/Particle/ParticleBeamTypes.h) | 현재 `TexCoordU`만 있고 V는 SV_VertexID로 결정 → SubUV 행을 반영하려면 `TexCoordV` 필드 추가 또는 frame index payload 추가 (vertex stride 변경) |
| `BeamParticle.hlsl` | V coordinate 계산을 frame row 반영하도록 수정. 또는 frame index를 CB로 받아 PS에서 UV 변환 |
| [ParticleModuleTypeDataBeam.h](JSEngine/Source/Engine/Particle/ParticleModuleTypeDataBeam.h) | UPROPERTY `int32 SubUVColumns/Rows`, `float FramesPerSecond`, `bool bRandomStartFrame` 추가 (Sprite의 `SubImagesHorizontal/Vertical` 패턴 재사용) |
| Vertex Factory `BeamParticleLayout` | TexCoordV 추가 시 layout 갱신 (`VertexFactoryTypes.h` 근처) |

### 4.2 권장 단순화

전체 strip이 같은 frame을 쓰면 충분 (per-vertex frame 보간 불필요). 이 경우 PS의 constant buffer에 `frameIdx`만 추가하면 vertex 구조 변경 없이 구현 가능 → 가장 작은 코드 변경 경로.

### 4.3 옵션 B/C 비교

| 항목 | B (Additive) | C (SubUV) |
|------|-------------|----------|
| 변경 파일 수 | 3개 | 5~6개 (vertex stride 변경 시 더 많음) |
| Beam의 lightning 효과 향상 | 즉시 큼 (발광감) | 작거나 중간 (정적 PNG로 충분한 경우가 많음) |
| 13a/13b payload/stride 영향 | 없음 | 있음 (vertex layout/stride 변경 가능성) |
| Sprite 기존 코드 재사용 | 거의 없음 | 많음 (`SubImagesHorizontal/Vertical` 패턴) |

옵션 B를 먼저 구현하고, SubUV는 실제 정적 PNG로 충분하지 않을 때 추가 cycle로 분리하는 것을 권장.

---

## 5. 검증 방법 (각 옵션별 end-to-end)

### 5.1 옵션 A — 기본 워크플로우 검증

```
1. 새 Beam Material 작성:
   - PNG 배치 (예: Asset/Cube/Projectile.png 재사용)
   - .mat 작성 후 ResourceManager.LoadMaterial 로드 성공 확인
2. .particlesystem 의 UBeamTypeData.Material 에 위 .mat 지정
3. 인게임 실행:
   a. Strip 위에 텍스처가 보이면 PNG 흐름 OK
   b. TextureTile 변경 → strip을 따라 반복 회수 변화 확인
   c. 파티클 Color 변경 → 텍스처에 tint 적용 확인
   d. Material 미지정 시 흰색 strip (DefaultWhiteSRV fallback) 확인
4. Cycle 13b Noise 활성 → lightning bolt 형태에 PNG가 따라가는지 확인
```

### 5.2 옵션 B — Additive blend 검증

```
1. EBlendType::Additive 케이스 D3D11 BLEND_DESC 정상 생성 확인 (RenderDoc capture)
2. 동일 PNG/.mat 로 BlendMode=AlphaBlend vs Additive 비교 — Additive 시 배경과 가산되어 밝게 빛남
3. 어두운 배경에서 가장 효과가 큼 (밝은 배경에선 saturate)
4. depth-write 없는 상태(이미 DepthReadOnly)이므로 깊이 오정렬 없음 확인
```

### 5.3 옵션 C — SubUV 검증

```
1. 6×6 sprite sheet PNG 준비 (예: 36-frame lightning loop)
2. .meta 가 {"Columns":6,"Rows":6,"Type":"SubUV"} 로 자동/수동 생성됨 확인
3. 인게임 strip 표면에 frame이 시간에 따라 진행되는지 확인
4. FramesPerSecond 변경에 따른 속도 변화 확인
5. 13b Noise 와 함께 사용 시 lightning bolt가 살아 움직이는 효과 확인
```

---

## 6. 변경 권장 사항 요약

| 사용자가 원하는 효과 | 권장 옵션 |
|---|---|
| 단순히 PNG 텍스처 입힌 광선/빔/레이저 | **A (코드 수정 0건)** |
| Lightning bolt 발광체 느낌 | **A + B** |
| 살아 움직이는 lightning loop, 텍스처 애니메이션 광선 | **A + B + C** |

옵션 B/C는 실제 구현 시 별도 Cycle (예: 13c 또는 14a)로 분리하는 것이 13a/13b의 회귀 위험을 최소화함. 본 보고서는 **점검** 단계이며, 코드는 수정하지 않았다.