# Cycle 11 (Mesh Emitter) Implement Plan + Ribbon/Beam Outline

**작성일**: 2026-05-25
**대상 브랜치**: `feature/ParticleRender`
**모드**: plan only (코드 변경 0)
**선행 문서**:
- [ParticleEmitter_Cycle11_PreEntry.md](ParticleEmitter_Cycle11_PreEntry.md) — Cycle 10d 완료 진단
- [Cycle11_ReDiagnose.md](Cycle11_ReDiagnose.md) — 본 plan 의 직접 근거 (코드 대조 검증)

**전제 (재논의 금지)**: TypeData 패턴, `EParticleEmitterRenderMode` 라우팅 키, container 책임 승격 완료 (Cycle 10d), 공통 infra → Mesh → Ribbon → Beam 순서

---

## 0. 사전 해소 필요 항목 (Cycle 11 진입 전 사용자 결정)

`Cycle11_ReDiagnose.md §2.2` 에서 식별된 이슈 중 plan 진입 전 사용자 결정이 필요한 항목.

- [ ] **이슈 (d) — 결정 4 (Mesh payload 0 vs MeshRotation) lock-in**

  - **옵션 A (권고)**: `UMeshTypeData::RequiredPayloadBytes() = 0`. Sprite 처럼 FBaseParticle 의 `float Rotation` 단일 축만 사용. Cycle 11 단일 issue 원칙 부합.
    - 영향: 본 plan 그대로 진입. payload struct 정의 0건. Spawn override 0건. BuildInstanceData override 가 `Particle->Rotation` 단일 float 로 instance VB 의 Transform 빌드.
  - **옵션 B**: `UMeshTypeData::RequiredPayloadBytes() = sizeof(FMeshRotationPayload)` (≈36B).
    - 영향: 본 plan 의 다음 항목 변경:
      - 신규 파일 `Engine/Particle/ParticleMeshTypes.h` (또는 `ParticleTypes.h` 확장) 에 `FMeshRotationPayload { FVector InitialOrientation, FVector Rotation, FVector RotRate }` 정의 추가.
      - `FParticleMeshEmitterInstance::SpawnParticles()` override — Spawn 시 payload 영역 초기화 (`(uint8*)Particle + PayloadOffset` 위치에 `FMeshRotationPayload` 셋팅).
      - `FParticleMeshEmitterInstance::BuildInstanceData()` override — payload read 후 instance VB 의 Transform 에 3축 회전 적용.
      - 회귀 안전: Cycle 10d 의 container Stride 자동 가산 덕분에 Sprite Stride 불변 보장 — Sprite 회귀 0.
  - **권고**: (A) — Cycle 11 의 형식적 파생 원칙에 부합 + 디버깅/회귀 표면적 최소화. (B) 는 Cycle 11 완료 후 별도 cycle 로 분리하는 편이 안전.

이 외 이슈 (a) / (b) / (c) 는 사용자 결정 불필요 (재진단 §2.2 참조).

---

## Part A. Cycle 11 (Mesh Emitter) 상세 plan

### A. 변경 대상 파일 (신규 / 수정)

#### 신규 파일

- [ ] **`Engine/Particle/ParticleModuleTypeDataMesh.h/.cpp`** — `UMeshTypeData` 클래스.
  의존: `Particle/ParticleModuleTypeData.h` (base `UParticleModuleTypeDataBase`).
  silent bug 매칭: §7-4 (vcxproj 등록 필요).
  주요 멤버:
  ```
  UPROPERTY(DisplayName = "Static Mesh", Category = "Mesh", ReferenceKind = Asset)
  UStaticMesh* Mesh = nullptr;
  UPROPERTY(DisplayName = "Override Material", Category = "Mesh")
  bool bOverrideMaterial = false;
  UPROPERTY(DisplayName = "Material Override", Category = "Mesh", ReferenceKind = Asset)
  UMaterialInterface* OverrideMaterial = nullptr;
  ```
  override:
  ```
  int32 RequiredPayloadBytes() const override { return 0; }  // 옵션 A (결정 4)
  EParticleEmitterRenderMode GetRenderMode() const override { return EParticleEmitterRenderMode::Mesh; }
  FParticleEmitterInstance* CreateInstance(UParticleSystemComponent* Component, int32 EmitterIndex) const override;
  ```

- [ ] **`Engine/Particle/ParticleMeshEmitterInstance.h/.cpp`** — `FParticleMeshEmitterInstance` (base `FParticleEmitterInstance` 파생).
  의존: `ParticleEmitterInstance.h`, `Render/Resource/VertexTypes.h` (FMeshParticleInstanceData).
  silent bug 매칭: §7-4.
  주요 override:
  ```
  void BuildInstanceData() override;  // MeshInstanceDataBuffer 채움
  const FMeshParticleInstanceData* GetMeshInstanceData(uint32& OutCount) const override;
  ```
  추가 멤버:
  ```
  TArray<FMeshParticleInstanceData> MeshInstanceDataBuffer;  // base 의 SpriteInstanceDataBuffer 대응
  ```
  옵션 A 채택 시 Spawn/Kill override 불필요. 옵션 B 채택 시 SpawnParticles override 추가 + payload 초기화 로직 도입.

- [ ] **`Shaders/Particle/MeshParticle.hlsl`** — Mesh particle 전용 VS/PS.
  의존: `Shaders/Common/*.hlsli` (View/Projection 행렬, lighting 등 기존 공유 헤더 사용 권장).
  silent bug 매칭: §7-4.
  VS 입력: slot 0 (per-vertex `FNormalVertex` — POSITION/COLOR/NORMAL/TEXCOORD/TANGENT), slot 1 (per-instance — Transform 4x3 또는 Position+Quat+Scale + InstanceColor).
  PS 출력: Albedo × InstanceColor (단순). lit/unlit 분기는 본 cycle 결정 사항 (G 항목 참조).

#### 수정 파일

- [ ] **`Engine/Render/Resource/VertexTypes.h`** — `FMeshParticleInstanceData` struct 정의 추가.
  의존: `Math/Matrix.h`, `Math/Vector.h`.
  silent bug 매칭: §7-1 의 절반 — 본 struct 정의가 layout 의 stride/offset 기준이 됨. tight-packed `static_assert` 필수.
  잠정 layout:
  ```
  struct FMeshParticleInstanceData
  {
      FVector  InstancePosition;   // 12 (offset 0)
      float    InstanceRotation;   //  4 (offset 12) — radians (옵션 A)
      FVector  InstanceScale;      // 12 (offset 16)
      uint32   _Padding0;          //  4 (offset 28)
      FColor   InstanceColor;      // 16 (offset 32)
  };                               // 48 bytes
  static_assert(sizeof(FMeshParticleInstanceData) == 48, "...");
  ```
  옵션 B 채택 시 FMatrix Transform 형태 (64B) 로 확장 또는 Quat+Position+Scale (32B 압축).

- [ ] **`Engine/Render/Resource/VertexFactoryTypes.h`** — `MeshParticleLayout` + `MeshParticleDesc` 본문 추가 + switch case 본문 교체.
  의존: 위 `FMeshParticleInstanceData` 정의 + `FNormalVertex` (이미 존재 line 17-24).
  silent bug 매칭: §7-1 (case body 의 `EmptyParticleDesc` → `MeshParticleDesc` 교체 필수).
  잠정 layout:
  ```
  static const FVertexLayoutDesc MeshParticleLayout = {
      {
          // Slot 0: per-vertex (FNormalVertex)
          { "POSITION",  0, DXGI_FORMAT_R32G32B32_FLOAT,    0, offsetof(FNormalVertex, Position), D3D11_INPUT_PER_VERTEX_DATA, 0 },
          { "COLOR",     0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, offsetof(FNormalVertex, Color),    D3D11_INPUT_PER_VERTEX_DATA, 0 },
          { "NORMAL",    0, DXGI_FORMAT_R32G32B32_FLOAT,    0, offsetof(FNormalVertex, Normal),   D3D11_INPUT_PER_VERTEX_DATA, 0 },
          { "TEXCOORD",  0, DXGI_FORMAT_R32G32_FLOAT,       0, offsetof(FNormalVertex, UVs),      D3D11_INPUT_PER_VERTEX_DATA, 0 },
          { "TANGENT",   0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, offsetof(FNormalVertex, Tangent),  D3D11_INPUT_PER_VERTEX_DATA, 0 },
          // Slot 1: per-instance (FMeshParticleInstanceData)
          { "INSTANCE_POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 1, offsetof(FMeshParticleInstanceData, InstancePosition), D3D11_INPUT_PER_INSTANCE_DATA, 1 },
          { "INSTANCE_ROTATION", 0, DXGI_FORMAT_R32_FLOAT,       1, offsetof(FMeshParticleInstanceData, InstanceRotation), D3D11_INPUT_PER_INSTANCE_DATA, 1 },
          { "INSTANCE_SCALE",    0, DXGI_FORMAT_R32G32B32_FLOAT, 1, offsetof(FMeshParticleInstanceData, InstanceScale),    D3D11_INPUT_PER_INSTANCE_DATA, 1 },
          { "INSTANCE_COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, offsetof(FMeshParticleInstanceData, InstanceColor), D3D11_INPUT_PER_INSTANCE_DATA, 1 },
      },
      sizeof(FMeshParticleInstanceData)
  };
  static const FVertexFactoryDesc MeshParticleDesc = {
      FShaderPaths::ParticleMesh, FShaderPaths::ParticleMesh, FShaderPaths::ParticleMesh, FShaderPaths::ParticleMesh,
      "MeshParticleVS", "MeshParticleVS", "MeshParticleVS", "MeshParticleVS",
      MeshParticleLayout, MeshParticleLayout, MeshParticleLayout
  };
  ```
  case 본문 교체 (line 262-265):
  ```
  case EVertexFactoryType::MeshParticle:
      return MeshParticleDesc;
  case EVertexFactoryType::RibbonParticle:
  case EVertexFactoryType::BeamParticle:
      return EmptyParticleDesc;  // Cycle 12+/13+ 에서 교체
  ```

- [ ] **`Engine/Render/Renderer/RenderFlow/ParticleRenderPass.cpp`** — `RenderMeshEmitter` 본문 채움 (line 253-258).
  의존: `MeshParticleDesc` (위 항목), `Cmd.MeshBuffer` (Builder 에서 세팅), `FMeshBuffer::GetVertexBuffer/GetIndexBuffer`.
  silent bug 매칭: §7-5 (case `return true` 가 EPT_ParticleSystem dispatch 측에서 이미 보장 — RenderMeshEmitter 자체는 void, 별도 처리 0).
  잠정 본문 (Sprite 의 RenderSpriteEmitter 패턴 + Mesh asset path):
  ```
  void FParticleRenderPass::RenderMeshEmitter(const FRenderCommand& Cmd, const FRenderPassContext& Context)
  {
      if (!Cmd.MeshParticleInstances || Cmd.MeshParticleInstanceCount == 0 || !Cmd.MeshBuffer) return;
      // Shader binding (MeshParticleVS/PS), state setup (BlendOpaque/DepthTestWrite/SolidBackface — 결정 G 항목),
      // PerObject CB Update (Mesh asset 의 World 행렬은 instance VB 가 대신, 여기서는 frame/view CB 만 바인딩),
      // VB 0: Cmd.MeshBuffer 의 VertexBuffer, VB 1: 별도 InstanceBuffer (MeshInstanceBuffer 신규)
      // IB: Cmd.MeshBuffer 의 IndexBuffer
      // DrawIndexedInstanced(IndexCount, Cmd.MeshParticleInstanceCount, 0, 0, 0)
  }
  ```
  추가 멤버 (h 파일): `FInstanceBuffer MeshInstanceBuffer;` — Sprite 의 InstanceBuffer 대응. EnsureGPUResources 에서 Create.

- [ ] **`Engine/Render/Scene/PrimitiveDrawCommandBuilder.cpp`** — Mesh switch case 본문 확장 (line 622-627).
  현재 상태: dispatch 골격 (MeshParticleInstances/Count/VertexFactoryType) 완료. 누락 부분만 추가.
  추가 본문 (line 622-627 직후):
  ```
  case EParticleEmitterRenderMode::Mesh:
      Cmd.MeshParticleInstances = Instance->GetMeshInstanceData(Count);
      Cmd.MeshParticleInstanceCount = Count;
      Cmd.VertexFactoryType = EVertexFactoryType::MeshParticle;
      // 신규: TypeDataMesh 의 Mesh asset 조회 + MeshBuffer 세팅
      if (const UMeshTypeData* MeshTD = Cast<const UMeshTypeData>(LOD->GetTypeDataModule()))
      {
          if (UStaticMesh* Mesh = MeshTD->GetMesh())
          {
              Cmd.MeshBuffer = MeshBufferManager.GetMesh(Mesh->GetUUID());
              Cmd.SectionIndexStart = 0;
              Cmd.SectionIndexCount = Cmd.MeshBuffer ? Cmd.MeshBuffer->GetIndexBuffer().GetCount() : 0;
              Cmd.Material = MeshTD->GetEffectiveMaterial();  // bOverrideMaterial ? OverrideMaterial : Mesh 의 디폴트
          }
      }
      bHasData = (Cmd.MeshParticleInstances != nullptr && Count > 0 && Cmd.MeshBuffer != nullptr);
      break;
  ```
  silent bug 매칭: §7-1 (Cmd.MeshBuffer nullptr 일 때 bHasData=false → 그리지 않음, silent 회피).

- [ ] **`Engine/Render/Resource/ShaderPaths.h`** — `ParticleMesh` 경로 추가 (line 34 옆).
  추가 한 줄:
  ```
  inline constexpr const char* ParticleMesh = "Shaders/Particle/MeshParticle.hlsl";
  ```

- [ ] **`JSEngine.vcxproj` + `JSEngine.vcxproj.filters`** — 신규 .h/.cpp/.hlsl 5개 (TypeDataMesh.h/.cpp, MeshEmitterInstance.h/.cpp, MeshParticle.hlsl) 등록.
  silent bug 매칭: §7-4. VS 닫고 수동 편집 후 reload 권장. `GenerateProjectFiles.py` 가 있다면 그것 실행.

### B. 작업 순서 (sub-step)

- [ ] **1.** 결정 4 (이슈 d) 사용자 lock-in 확인. **완료 기준**: 사용자가 옵션 A 또는 B 선택 명시.
- [ ] **2.** `Engine/Particle/ParticleModuleTypeDataMesh.h/.cpp` 신규 작성 + `Engine/Particle/ParticleMeshEmitterInstance.h/.cpp` 신규 작성. **완료 기준**: 빌드 통과 (다른 변경 0건 상태에서 신규 파일만 컴파일 성공).
- [ ] **3.** `VertexTypes.h` 에 `FMeshParticleInstanceData` 정의 + tight-packed `static_assert`. **완료 기준**: 빌드 통과. forward decl ([RenderCommand.h:28](../JSEngine/Source/Engine/Render/Scene/RenderCommand.h:28), [ParticleEmitterInstance.h:12](../JSEngine/Source/Engine/Particle/ParticleEmitterInstance.h:12)) 이 실제 정의로 해소.
- [ ] **4.** `Engine/Particle/ParticleMeshEmitterInstance.cpp` 의 `BuildInstanceData()` / `GetMeshInstanceData()` 본문 채움 — `ParticleStorage.GetParticle(i)` 순회 → `Particle->Location/Size/Rotation/Color` 로 `FMeshParticleInstanceData` 채워 `MeshInstanceDataBuffer` 에 push. **완료 기준**: 빌드 통과 + 단위 verify: cube mesh asset + UMeshTypeData 로 교체한 ParticleSystem asset 에서 `GetMeshInstanceData(Count)` 호출 시 ActiveParticleCount 와 동일한 Count 반환.
- [ ] **5.** `ShaderPaths.h` 추가 + `Shaders/Particle/MeshParticle.hlsl` 신규 작성 (VS: World 행렬 = Translation(InstancePosition) × Rotation(InstanceRotation) × Scale(InstanceScale), PS: 단순 albedo × InstanceColor). **완료 기준**: 빌드 통과 + Shader 컴파일 통과.
- [ ] **6.** `VertexFactoryTypes.h` 에 `MeshParticleLayout` + `MeshParticleDesc` 정의 + switch case 본문 교체 (EmptyParticleDesc → MeshParticleDesc). **완료 기준**: 빌드 통과 + `FVertexFactoryRegistry::Get(MeshParticle)` 호출 시 채워진 Desc 반환 (디버거 watch).
- [ ] **7.** `ParticleRenderPass.cpp` 의 `RenderMeshEmitter` 본문 채움 + `FParticleRenderPass` 에 `MeshInstanceBuffer` 멤버 + `EnsureGPUResources` 에서 Create. **완료 기준**: 빌드 통과.
- [ ] **8.** `PrimitiveDrawCommandBuilder.cpp` 의 Mesh case 본문 확장 (Cmd.MeshBuffer 세팅 + Material). **완료 기준**: 빌드 통과 + 디버거: Mesh emitter asset 실행 시 `Cmd.MeshBuffer != nullptr` 확인.
- [ ] **9.** vcxproj + .filters 등록. **완료 기준**: VS 재시작 후 모든 신규 파일이 솔루션에 보임.
- [ ] **10.** 실행 verify — cube mesh asset 1개 + UMeshTypeData 로 교체한 ParticleSystem asset. **완료 기준**: 화면에 N개 mesh particle 표시 + Sprite asset 회귀 0.

### C. container 상호작용

- **read**: derived `BuildInstanceData()` override 가 base 의 `GetParticle(i)` 사용 — 내부적으로 `ParticleStorage.ParticleData + ParticleStorage.ParticleIndices[i] * ParticleStorage.GetStride()` 산식. payload 영역 직접 access 0건 (옵션 A 채택 시).
- **write**: 없음 — Mesh 는 base 의 SpawnParticles / KillParticle 동작 그대로 사용. derived 는 BuildInstanceData / GetMeshInstanceData 만 override.
- **init**: `UMeshTypeData::RequiredPayloadBytes() = 0` (옵션 A) → `FParticleEmitterInstance::Init` ([cpp:53](../JSEngine/Source/Engine/Particle/ParticleEmitterInstance.cpp:53)) 의 `ParticleSize + PayloadBytes` 가 그대로 ParticleSize → Sprite 와 동일 Stride.
- **handoff**: derived 의 `MeshInstanceDataBuffer` → `GetMeshInstanceData()` override 로 노출 → `PrimitiveDrawCommandBuilder` 가 `Cmd.MeshParticleInstances` 슬롯에 매핑.

### D. 완료 기준

- [ ] 빌드 통과 (모든 변경 적용 후 incremental + full 빌드).
- [ ] cube 또는 sphere mesh asset 1개 + UMeshTypeData 로 교체한 ParticleSystem asset 실행 → 화면에 N개 mesh particle 표시.
- [ ] RenderDoc: `DrawIndexedInstanced(IndexCount, ActiveParticles, 0, 0, 0)` event 발생, slot 0 mesh VB / slot 1 instance VB.
- [ ] 회귀: 기존 Sprite asset 동일 동작 (Sprite particle 표시).

### E. 회귀 안전 장치

- [ ] **USpriteTypeData 변경 0 확인** — `USpriteTypeData::RequiredPayloadBytes() = 0` 유지 ([ParticleModuleTypeData.h:38](../JSEngine/Source/Engine/Particle/ParticleModuleTypeData.h:38)). 본 cycle 에서 `USpriteTypeData` 정의 손대지 않음.
- [ ] **UMeshTypeData::RequiredPayloadBytes() = 0 (옵션 A)** 또는 **sizeof(FMeshRotationPayload) (옵션 B)** — Cycle 10d 의 container 자동 가산 덕분에 어느 쪽이든 Sprite Stride 불변.
- [ ] **VertexFactoryRegistry::Get 의 Sprite case 미접근** — switch 본문에서 Sprite case ([VertexFactoryTypes.h:259-260](../JSEngine/Source/Engine/Render/Resource/VertexFactoryTypes.h:259)) 변경 0건.
- [ ] **EPT_ParticleSystem case `return true` 보장** — Mesh 분기 확장이 break 로 끝나고 함수 끝 line 675 `return true` 도달 ([PrimitiveDrawCommandBuilder.cpp:675](../JSEngine/Source/Engine/Render/Scene/PrimitiveDrawCommandBuilder.cpp:675)).
- [ ] **base `FParticleEmitterInstance::BuildInstanceData` 변경 0건** — Sprite path 동작 보존.
- [ ] **ParticleRenderPass 의 `RenderSpriteEmitter` 변경 0건** — Mesh helper 추가가 Sprite helper 영향 없음.

### F. silent bug 매칭

- **§7-1** (VertexFactoryRegistry default fallback): Mesh case 본문을 `EmptyParticleDesc` 에서 `MeshParticleDesc` 로 교체 (위 작업 6) — 명시 case 가 채워지므로 silent 회피.
- **§7-4** (vcxproj 자동 덮어쓰기): 신규 .h/.cpp/.hlsl 5개 등록 (위 작업 9) — VS 닫고 수동 편집 후 reload.
- **§7-5** (EPT_ParticleSystem case `return true`): Mesh 분기 확장 후에도 함수 끝의 `return true` ([cpp:675](../JSEngine/Source/Engine/Render/Scene/PrimitiveDrawCommandBuilder.cpp:675)) 가 도달 가능 — 본 cycle 에서 추가 보장 작업 0.
- **ν / ξ**: Cycle 10d 사전 해소. 본 cycle 추가 작업 0.

### G. 추측 / 미결정 (Cycle 11 진입 시 사용자 결정 또는 추가 진단 필요)

- **[추측]** D3D state setup (BlendState / DepthStencilState / RasterizerState) — Mesh path 가 Sprite 와 다를 가능성 매우 높음 (Sprite: AlphaBlend+DepthReadOnly+SolidNoCull, Mesh: 일반적으로 Opaque+DepthTestWrite+DepthBackfaceCull). **권고**: 초기 구현은 Opaque+DepthTestWrite+SolidBackface 로 시작, 빌드 후 실측 조정. 실제 사용 자산이 알파 마스킹 필요하면 Material 단에서 결정.
- **[추측]** `FMeshParticleInstanceData` 의 정확한 layout — 옵션 A 기준 48B 잠정. 실제 instance VB stride 가 cache line align 됐는지 RenderDoc 측정 권장.
- **[추측]** Mesh asset 의 lit/unlit — 본 cycle 의 단일 issue 원칙상 unlit 단순 albedo × InstanceColor 시작. lit (PerObject CB 의 light 정보 + 노멀 변환) 은 후속 cycle.
- **[추측]** PerObjectConstants 의 사용 — Sprite 는 ParticleRenderPass 에서 `Cmd.PerObjectConstants` 를 b1 에 바인딩 ([cpp:225-229](../JSEngine/Source/Engine/Render/Renderer/RenderFlow/ParticleRenderPass.cpp:225)). Mesh 는 instance 마다 World 행렬이 다르므로 PerObject CB 의 Model 행렬은 Identity 로 두고 instance VB 의 InstancePosition/Rotation/Scale 이 World 합성을 담당해야 함.
- **[추측]** μ (MeshBuffer cache UUID 충돌) — `MeshBufferManager.GetMesh(UUID)` 가 staticmesh actor 와 particle emitter 사이 race 없는지 미확인. 본 cycle 진입 시 측정.
- **[미결정]** 에디터 UI 분기 — UMeshTypeData 의 Mesh asset UPROPERTY 가 자동으로 에디터에 노출되는지. `EditorParticleSystemWidget` 의 emitter UI 가 TypeDataModule 선택 + 그 안의 UPROPERTY 자동 노출을 지원하는지 미확인.

---

## Part B. Ribbon outline (Cycle 12a + 12b)

`Cycle11_PreEntry.md §B.2` 의 핵심만 outline. Cycle 11 (Mesh) 결과 검증 후 Ribbon plan 진입 시 상세화.

### (a) TypeData class — `UParticleModuleTypeDataRibbon`
- 멤버: `int32 MaxTrailCount`, `int32 MaxParticleInTrailCount`, `float SheetsPerTrail`, `float TangentSpawningScalar`, `bool bRenderGeometry/bRenderSpawnPoints/bRenderTangents`.
- override: `RequiredPayloadBytes() → sizeof(FRibbonParticlePayload)`, `CreateInstance() → new FParticleRibbonEmitterInstance`, `GetRenderMode() → Ribbon`.

### (b) Particle payload — `FRibbonParticlePayload` (FBaseParticle 뒤에 byte payload)
- 멤버: `int32 NextIndex` (linked list, -1 = tail), `int32 PrevIndex`, `FVector Tangent`, `float SpawnedTangentStrength`, `int32 TrailIndex`, `float Distance`.
- 추측 sizeof ≈ 32–40B.
- **회귀 안전 (핵심)**: payload 의 NextIndex/PrevIndex 는 반드시 **물리 SlotIndex** (container 의 ParticleData 위치) 저장. swap-pop ([ParticleEmitterInstance.cpp:213](../JSEngine/Source/Engine/Particle/ParticleEmitterInstance.cpp:213)) 가 `ParticleIndices` 만 swap 하므로 SlotIndex 불변 → linked list 안전.

### (c) Instance override 목록 — `FParticleRibbonEmitterInstance`
- `KillParticle(Index)` override — base swap-pop 호출 전에 NextIndex/PrevIndex 재연결.
- `SpawnParticles` override — 신규 particle 을 head 로 prepend + tangent 계산.
- `Tick` override (선택) — particle update 후 head 위치 변화로 tangent 재계산.
- 추가 멤버: `TArray<int32> HeadIndices` (trail별 head SlotIndex, MaxTrailCount 만큼).

### (d) 렌더 어댑터 핵심 1줄
- `FRibbonParticleVertex` (strip 정점, dynamic VB per-frame 생성) + `MeshParticleLayout` 과 다른 `RibbonParticleLayout` (slot 0 per-vertex only, no instancing) + `D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP` + ParticleRenderPass 의 RenderRibbonEmitter 본문 채움 (Cycle 12b).

### (e) Cycle 11 (Mesh) 결과에 의존하는 항목
- **MeshInstanceBuffer 패턴 검증** — Cycle 11 에서 `FInstanceBuffer` 의 Mesh path 재사용이 검증되면 Ribbon 의 dynamic VB 패턴도 동일 클래스 (`FInstanceBuffer` 또는 별도 `FDynamicVertexBuffer`) 재사용 가능 여부 판단.
- **payload-aware Stride 검증** — Cycle 11 에서 옵션 B 가 선택되면 (사용자 결정), Ribbon payload 도입 시 검증된 패턴 재사용. 옵션 A 가 선택되면 Ribbon 이 payload 도입의 첫 사례 — container 자동 가산이 처음으로 실측됨.
- **TypeData 의 derived CreateInstance() 패턴 검증** — Cycle 11 의 UMeshTypeData → FParticleMeshEmitterInstance 매핑이 작동하면 Ribbon 도 동일 패턴.

---

## Part C. Beam outline (Cycle 13a + 13b)

`Cycle11_PreEntry.md §B.3` 의 핵심만 outline. Ribbon 진입 전 사용자 결정 후 상세화.

### (a) TypeData class — `UParticleModuleTypeDataBeam2`
- 멤버: `int32 MaxBeamCount`, `EBeam2Method BeamMethod`, `float TextureTile/TextureTileDistance`, `int32 Sheets`, `int32 Frequency`.
- override: `RequiredPayloadBytes() → sizeof(FBeamParticlePayload)`, `CreateInstance() → new FParticleBeamEmitterInstance`, `GetRenderMode() → Beam`.

### (b) Particle payload — `FBeamParticlePayload`
- 멤버: `FVector SourcePoint`, `FVector TargetPoint`, `FVector SourceTangent`, `FVector TargetTangent`, `int32 InterpolationSteps`.
- 추측 sizeof ≈ 56B (noise 제외).

### (c) Instance override 목록 — `FParticleBeamEmitterInstance`
- `Tick` override **의미 변경** — base Tick 의 `RelativeTime >= 1.0f → KillParticle` 우회 (Beam 은 sustained 모드 또는 Source/Target 갱신만으로 lifetime 관리).
- `SpawnParticles` override — Source/Target 모듈 조회 후 payload 초기화.
- `KillParticle` override 불필요 (Beam 은 linked list 아님, swap-pop 안전).
- 신규 모듈 3종: `UParticleModuleBeamSource`, `UParticleModuleBeamTarget`, `UParticleModuleBeamNoise` (Cycle 13b 또는 별도 13c — 결정 5 참조).

### (d) 렌더 어댑터 핵심 1줄
- `FBeamParticleVertex` (strip 정점, Source→Target curvature 보간 unroll) + `BeamParticleLayout` (slot 0 per-vertex only) + `D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP` + `RenderBeamEmitter` 본문 (Cycle 13b).

### (e) Cycle 11 (Mesh) + 12 (Ribbon) 결과에 의존하는 항목
- **dynamic VB 패턴 검증** — Cycle 12b 에서 Ribbon 의 dynamic strip VB 가 검증되면 Beam 은 동일 패턴 재사용 (실제로 Ribbon/Beam 둘 다 strip + dynamic VB + TRIANGLESTRIP 이므로 코드 공유 가능 — 단 의미상 다른 instance 상태).
- **Tick 의미 변경 패턴 검증** — base Tick 의 RelativeTime kill 로직을 override 하는 첫 사례 (Mesh/Ribbon 은 KillParticle 만 override). derived Tick 이 base 동작을 얼마만큼 호출/우회할지 패턴 정립 필요.

---

## Part D. Cycle 12 진입 전 재진단 권고

Cycle 11 (Mesh) 완료 후 Ribbon (Cycle 12) 진입 전 다시 진단해야 할 항목:

- [ ] **Cycle 11 회귀**: Sprite asset 실행 시 frame time / GPU profile 이 Cycle 10d baseline 과 동일한지 측정.
- [ ] **container payload-aware Stride 실측** (옵션 B 채택했을 때만) — Sprite Stride vs Mesh Stride 디버거 watch 로 비교. Cycle 10d 의 가설 (container 자동 가산) 이 실제로 작동했는지 검증.
- [ ] **MeshInstanceBuffer 패턴 재사용 검토** — Ribbon 의 dynamic VB 가 동일 클래스로 충분한지 vs 별도 dynamic VB 클래스 신규 도입 필요한지.
- [ ] **silent bug μ (MeshBuffer cache UUID 충돌)** — Cycle 11 실행 시 measurement 결과 반영. Ribbon 도 mesh asset 사용 안 함 (strip 정점 직접 생성) 이므로 영향 0 일 가능성 — 그러나 Cycle 11 측정 결과 따라 다음 cycle plan 보강.
- [ ] **silent bug λ (BuildInstanceData 매 frame rebuild)** — Cycle 11 후 emitter 수가 늘면 frame time 영향 측정. 결정 8 (캐시 시점) 의 재논의 시점.
- [ ] **결정 5 (Beam Noise 포함 시점)** — Ribbon 완료 후 Beam 진입 시 lock-in.

---

## 결론 한 줄

> Cycle 10d 가 container 책임 승격으로 silent bug ν/ξ 동시 해소 → Cycle 11 (Mesh) 즉시 진입 가능 상태. 단 결정 4 (Mesh payload 0 vs MeshRotation) 의 lock-in 만 사용자 결정으로 명시 후 진입 권장. plan body 는 (A) 채택을 전제로 작성됐고 (B) 채택 시 본 plan §0 의 변경 항목으로 정리 가능. Cycle 11 의 critical path = (1) 신규 .h/.cpp 5개 작성, (2) `FMeshParticleInstanceData` + `MeshParticleLayout/Desc` 정의, (3) `RenderMeshEmitter` 본문 + Builder Mesh 분기 의 MeshBuffer 세팅 추가.
