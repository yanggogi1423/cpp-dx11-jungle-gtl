# Cycle 11 재진단 — Pre-Entry md 코드 대조 검증

**작성일**: 2026-05-25
**대상 브랜치**: `feature/ParticleRender`
**모드**: diagnose only (코드 변경 0, 보고서 작성만)
**baseline**:
- [ParticleEmitter_InfraCheck.md](ParticleEmitter_InfraCheck.md) (Cycle 7→8 baseline, 참조 전용)
- [ParticleEmitter_Cycle11_PreEntry.md](ParticleEmitter_Cycle11_PreEntry.md) (Cycle 10d 완료 후 갱신본, 본 검증 대상)

**범위**: §2.1 (Pre-Entry §0/§A 주장 검증) + §2.2 (이슈 4건 집중) + §2.3 (Cycle 11 plan 파일 존재/위치)

---

## §2.1 Pre-Entry md §0 / §A 검증

### §0 Cycle 10d 변경 요약 표 — 7행 검증

- [OK] **Stride source-of-truth → container 이전** — `int32 ParticleStride` 멤버가 container 내부.
  근거: [ParticleTypes.h:57](../JSEngine/Source/Engine/Particle/ParticleTypes.h:57) `int32 ParticleStride = 0;`
- [OK] **Stride read → `ParticleStorage.GetStride()` 위임 (4곳)** — grep 결과 정확히 4곳.
  근거: [ParticleEmitterInstance.cpp:179](../JSEngine/Source/Engine/Particle/ParticleEmitterInstance.cpp:179), [224](../JSEngine/Source/Engine/Particle/ParticleEmitterInstance.cpp:224), [247](../JSEngine/Source/Engine/Particle/ParticleEmitterInstance.cpp:247), [260](../JSEngine/Source/Engine/Particle/ParticleEmitterInstance.cpp:260). `* ParticleStride` 직접 잔존 0건 (grep `ParticleStride` 결과 모두 container 내부 멤버 또는 RuntimeView 필드).
- [OK] **Stride 계산 → `Allocate(MaxParticles, ParticleSize + PayloadBytes)` 내부 가산 + align** — Allocate 호출 1회, 인자에 PayloadBytes 포함.
  근거: [ParticleEmitterInstance.cpp:53](../JSEngine/Source/Engine/Particle/ParticleEmitterInstance.cpp:53) `ParticleStorage.Allocate(MaxActiveParticles, ParticleSize + PayloadBytes)`. 내부 align [ParticleTypes.h:79-80](../JSEngine/Source/Engine/Particle/ParticleTypes.h:79).
- [OK] **container 할당 → 단일 `Allocate()` 만** — redundant `new uint8/uint16` 라인 부재.
  근거: [ParticleEmitterInstance.cpp:19-65](../JSEngine/Source/Engine/Particle/ParticleEmitterInstance.cpp:19) Init 전체에 `new` 호출 0건. [ParticleTypes.h:86-87](../JSEngine/Source/Engine/Particle/ParticleTypes.h:86) container.Allocate에 `new uint8[MemBlockSize]` 1회 + ParticleIndices는 reinterpret_cast 포인터 산술로 동일 블록 끝에 배치.
- [OK] **silent bug ν 해소** — container 이중 할당 + leak 패턴 제거.
  근거: Init에 `new` 0건 + Reset [ParticleTypes.h:106-115](../JSEngine/Source/Engine/Particle/ParticleTypes.h:106) `delete[] ParticleData` 1회로 단일 블록 free.
- [OK] **silent bug ξ 해소** — Stride payload-aware 자동 반영.
  근거: [ParticleEmitterInstance.cpp:44-46](../JSEngine/Source/Engine/Particle/ParticleEmitterInstance.cpp:44) PayloadBytes 계산 후 [cpp:53](../JSEngine/Source/Engine/Particle/ParticleEmitterInstance.cpp:53) Allocate 인자 가산.
- [OK] **ι/κ 해소 + λ/μ 미해결** — ι: TypeDataModule UPROPERTY, κ: GetEffectiveRenderMode, λ: BuildInstanceData clear+reserve, μ: 미확인.
  근거: [ParticleSystem.h:46-47](../JSEngine/Source/Engine/Particle/ParticleSystem.h:46) UPROPERTY 마크. [ParticleSystem.cpp:57-68](../JSEngine/Source/Engine/Particle/ParticleSystem.cpp:57) TypeData 우선. [ParticleEmitterInstance.cpp:318, 324](../JSEngine/Source/Engine/Particle/ParticleEmitterInstance.cpp:318) clear()+reserve() 잔존.

### §A.1 Container 정체 — 멤버/메서드 목록

- [OK] **이름 + 위치** — `FParticleDataContainer` at [ParticleTypes.h:46](../JSEngine/Source/Engine/Particle/ParticleTypes.h:46)
- [OK] **멤버 목록** — `MemBlockSize`, `ParticleDataNumBytes`, `ParticleIndicesNumShorts`, `ParticleData*`, `ParticleIndices*`, `ParticleStride` 모두 존재 + `DefaultParticleAlignment` 상수.
  근거: [ParticleTypes.h:48-59](../JSEngine/Source/Engine/Particle/ParticleTypes.h:48)
- [OK] **메서드 목록** — `Allocate(MaxParticles, Stride, Alignment)` / `Reset()` / `GetMemoryBytes()` / `AlignSize()` static / `GetStride()` 모두 존재.
  근거: [ParticleTypes.h:62-115](../JSEngine/Source/Engine/Particle/ParticleTypes.h:62)
- [OK] **`GetStride()` Cycle 10d 추가** — `int32 GetStride() const { return ParticleStride; }`
  근거: [ParticleTypes.h:98-101](../JSEngine/Source/Engine/Particle/ParticleTypes.h:98)

### §A.2 EmitterInstance 측 변화

- [OK] **`ParticleStride` 멤버 삭제** — instance 측 ParticleStride 멤버 부재.
  근거: [ParticleEmitterInstance.h:79-103](../JSEngine/Source/Engine/Particle/ParticleEmitterInstance.h:79) private 영역에 `ParticleStride` 필드 없음. [.h:93-94](../JSEngine/Source/Engine/Particle/ParticleEmitterInstance.h:93) 삭제 주석.
- [OK] **`GetParticleStride()` container 위임** — `return ParticleStorage.GetStride();`
  근거: [ParticleEmitterInstance.h:53](../JSEngine/Source/Engine/Particle/ParticleEmitterInstance.h:53)
- [OK] **Init 재구성: PayloadBytes 계산이 Allocate 앞** — line 44-48 PayloadBytes/InstancePayloadSize/PayloadOffset 셋팅, line 53 Allocate.
  근거: [ParticleEmitterInstance.cpp:44-53](../JSEngine/Source/Engine/Particle/ParticleEmitterInstance.cpp:44)
- [OK] **Allocate 인자 `ParticleSize + PayloadBytes` 형태** — 일치.
  근거: [ParticleEmitterInstance.cpp:53](../JSEngine/Source/Engine/Particle/ParticleEmitterInstance.cpp:53)
- [OK] **redundant `new uint8[]` / `new uint16[]` 2라인 삭제** — Init/Reset 본문에 `new` 호출 0건.
  근거: Init [cpp:19-65](../JSEngine/Source/Engine/Particle/ParticleEmitterInstance.cpp:19), Reset [cpp:70-83](../JSEngine/Source/Engine/Particle/ParticleEmitterInstance.cpp:70) 전체 read 확인.

### §A.5 결정 1~5 확정 상태

- [OK] **결정 1 (`UParticleModuleRequired::RenderMode` 잔존 + UPROPERTY + NoEdit)** — 4가지 속성 모두 일치 (§2.2 이슈 (b) 참조).
  근거: [ParticleModules.h:29, 31, 58-59](../JSEngine/Source/Engine/Particle/ParticleModules.h:58)
- [OK] **결정 2 (RenderCommand 옵션 i — 별도 슬롯)** — Mesh/Ribbon/Beam 슬롯 3종 별도 추가, void* 미사용.
  근거: [RenderCommand.h:497-505](../JSEngine/Source/Engine/Render/Scene/RenderCommand.h:497) `MeshParticleInstances`, `MeshParticleInstanceCount`, `RibbonVertices`, `RibbonVertexCount`, `BeamVertices`, `BeamVertexCount` 별도 필드.
- [OK] **결정 3 (단일 Pass + procedural switch)** — ParticleRenderPass에서 단일 Pass 안에서 `Cmd.VertexFactoryType` switch dispatch.
  근거: [ParticleRenderPass.cpp:120-145](../JSEngine/Source/Engine/Render/Renderer/RenderFlow/ParticleRenderPass.cpp:120) `switch (Cmd.VertexFactoryType)` 안에서 4-way dispatch (Sprite/Mesh/Ribbon/Beam).
- [N/A] 결정 4 / 결정 5 — 본 cycle 진입 결정 항목 (사용자 결정 미완), §2.2 이슈 (d) 참조.

### §A.6 silent bug 상태 (ι/κ/λ/μ/ν/ξ)

- [OK] **ι 해소 (Cycle 8)** — `TypeDataModule` UPROPERTY 마크 + 직렬화 자동.
  근거: [ParticleSystem.h:46-47](../JSEngine/Source/Engine/Particle/ParticleSystem.h:46) `UPROPERTY(DisplayName = "TypeData Module")`
- [OK] **κ 해소 (Cycle 8)** — `GetEffectiveRenderMode` TypeData 우선 + RequiredModule fallback + Sprite default.
  근거: [ParticleSystem.cpp:57-68](../JSEngine/Source/Engine/Particle/ParticleSystem.cpp:57)
  ```
  if (TypeDataModule) return TypeDataModule->GetRenderMode();
  if (RequiredModule) return RequiredModule->GetRenderMode();
  return EParticleEmitterRenderMode::Sprite;
  ```
- [OK] **λ 미해결** — `BuildInstanceData` 매 frame `clear() + reserve()` 패턴 유지.
  근거: [ParticleEmitterInstance.cpp:316-324](../JSEngine/Source/Engine/Particle/ParticleEmitterInstance.cpp:316)
  ```
  SpriteInstanceDataBuffer.clear();
  ...
  SpriteInstanceDataBuffer.reserve(ActiveParticles);
  ```
- [UNKNOWN] **μ 미확인** — MeshBuffer cache UUID 충돌은 본 진단 권한 범위 외 (런타임 측정 필요).
- [OK] **ν 해소 (Cycle 10d)** — Allocate 1회만 + redundant new 삭제.
  근거: [ParticleEmitterInstance.cpp:53](../JSEngine/Source/Engine/Particle/ParticleEmitterInstance.cpp:53) Allocate 1회. cpp:52 주석 "silent bug ν 원인이므로 제거됨".
- [OK] **ξ 해소 (Cycle 10d)** — Stride 계산이 `ParticleSize + PayloadBytes` 형태.
  근거: [ParticleEmitterInstance.cpp:53](../JSEngine/Source/Engine/Particle/ParticleEmitterInstance.cpp:53) + cpp:42 주석 "silent bug ξ 해소".

---

## §2.2 이슈 4건 집중 검증

### 이슈 (a) silent bug ν/ξ 의 최초 식별 시점

- **결론**: ν/ξ 라벨은 **Cycle 10d 구현 단계의 cpp 주석에서 신규 도입**. 별도 진단 md(예: Cycle 10b/10c 진단 문서)는 부재. `Cycle11_PreEntry.md`가 retroactive하게 ν/ξ 라벨을 §0/§A.6/§B.1에 기록.
- **근거**:
  - `InfraCheck.md §4.7` 에 ι/κ/λ/μ 4건만 존재 (lines 230-233 — 본 진단에서 확인). ν/ξ 라벨 부재.
  - `Document/` 디렉터리 전체 grep 결과 (ν 또는 ξ): `ParticleEmitter_Cycle11_PreEntry.md` 1건만 hit.
  - `JSEngine/Source` 전체 grep 결과 (ν 또는 ξ): `ParticleEmitterInstance.cpp:42, 52` 2줄 hit — "silent bug ξ 해소" / "silent bug ν 원인이므로 제거됨".
  - `ClaudeCode_Cycle10_ImplementationPrompt.md` grep 결과 (ν/ξ/nu/xi): 0건.
  - `Document/` 내 md 목록에서 Cycle 10b/10c/10d 명칭 진단 md 부재 (Glob `**/Cycle10*.md` 결과 0).
- **영향**: implement plan에 직접 영향 없음 — ν/ξ가 어디서 발견됐든 Cycle 10d에서 해소된 결과가 사실로 검증됨.
- **권고**: 사용자 결정 불필요. 향후 silent bug 라벨링 일관성을 위해 진단 md에 새 라벨 도입 시 baseline §4.7 표 갱신 (또는 별도 부록) 권장.

### 이슈 (b) 결정 1 (`UParticleModuleRequired::RenderMode`) 의 현재 상태

- **결론**: 4가지 속성 모두 일치. (i) 잔존 / (ii) UPROPERTY / (iii) NoEdit / (iv) TypeData 우선 source.
- **근거**:
  - (i) **잔존**: [ParticleModules.h:59](../JSEngine/Source/Engine/Particle/ParticleModules.h:59) `EParticleEmitterRenderMode RenderMode = EParticleEmitterRenderMode::Sprite;` 멤버 존재. line 29 getter / line 31 setter 잔존.
  - (ii) **UPROPERTY 마크**: [ParticleModules.h:58](../JSEngine/Source/Engine/Particle/ParticleModules.h:58) `UPROPERTY(DisplayName = "Emitter Type", Category = "TypeData", NoEdit)`
  - (iii) **NoEdit 속성**: line 58 `NoEdit` 인자 포함.
  - (iv) **TypeData 우선 source**: [ParticleSystem.cpp:57-68](../JSEngine/Source/Engine/Particle/ParticleSystem.cpp:57)
    ```
    EParticleEmitterRenderMode UParticleLODLevel::GetEffectiveRenderMode() const
    {
        if (TypeDataModule)
            return TypeDataModule->GetRenderMode();
        if (RequiredModule)
            return RequiredModule->GetRenderMode();
        return EParticleEmitterRenderMode::Sprite;
    }
    ```
    호출처 [ParticleEmitterInstance.cpp:231](../JSEngine/Source/Engine/Particle/ParticleEmitterInstance.cpp:231) + [PrimitiveDrawCommandBuilder.cpp:600](../JSEngine/Source/Engine/Render/Scene/PrimitiveDrawCommandBuilder.cpp:600).
- **영향**: implement plan 영향 없음.
- **권고**: 사용자 결정 불필요. 결정 1은 완전히 확정 상태.

### 이슈 (c) `EVertexFactoryType::MeshParticle` enum entry 존재 여부

- **결론**: enum entry는 **이미 Cycle 10a 에서 추가됨**. Cycle 11 에서 추가 작업 불필요. §B.1 파일 목록의 "수정 — `MeshParticleLayout` + `MeshParticleDesc` 본문" 표현은 정확 (enum 확장이 아니라 본문 채우기).
- **근거**:
  - [VertexFactoryTypes.h:31-37](../JSEngine/Source/Engine/Render/Resource/VertexFactoryTypes.h:31)
    ```
    SpriteParticle,
    // 본 enum entry 추가는 silent bug §7-1 회피의 필수 절반.
    MeshParticle,
    RibbonParticle,
    BeamParticle,
    ```
  - Registry switch case도 명시 존재: [VertexFactoryTypes.h:262-265](../JSEngine/Source/Engine/Render/Resource/VertexFactoryTypes.h:262) `case EVertexFactoryType::MeshParticle: ... return EmptyParticleDesc;`
  - `EmptyParticleDesc` 정의: [VertexFactoryTypes.h:239](../JSEngine/Source/Engine/Render/Resource/VertexFactoryTypes.h:239) `static const FVertexFactoryDesc EmptyParticleDesc = {};` (빈 Desc fallback)
- **영향**: Cycle 11 Mesh plan은 enum 확장 작업 0. case body의 `return EmptyParticleDesc;` 를 `return MeshParticleDesc;` 로 교체 + `MeshParticleLayout`/`MeshParticleDesc` 정의 추가만 필요.
- **권고**: 사용자 결정 불필요.

### 이슈 (d) 결정 4 (Mesh payload 0 vs MeshRotation) 와 §B.1 회귀 안전 장치 (A) 전제의 일관성

- **결론**: **MISMATCH 아님, 단 결정이 명시적으로 lock-in 되지 않은 상태**. Pre-Entry md §B.1 line 133 "UMeshTypeData::RequiredPayloadBytes() = 0 유지" 가 (A) 전제로 작성됨. Pre-Entry md Part C 결정 4 line 233 도 "(A) 유지" 권고. 두 진술이 사실상 일관되지만 결정 4 가 unchecked 항목으로 남아 있어 implement 진입 시 명시 확정 필요.
- **근거**:
  - [Cycle11_PreEntry.md:133](ParticleEmitter_Cycle11_PreEntry.md:133) — `UMeshTypeData::RequiredPayloadBytes() = 0 유지`
  - [Cycle11_PreEntry.md:233](ParticleEmitter_Cycle11_PreEntry.md:233) — `(결정 4) ... 본 진단 권고: (A) 유지`
- **영향**: 결정 4의 lock-in 상태가 implement plan 의 핵심 회귀 안전 조건 (Sprite Stride 불변) 과 직결.
- **권고**: 사용자 결정 필요.
  - **(A) 채택 (권고)**:
    - plan 그대로 진입.
    - `UMeshTypeData::RequiredPayloadBytes() = 0`
    - FBaseParticle 의 `float Rotation` 단일 축만 사용.
    - 단일 issue 원칙에 부합 (Cycle 11 형식적 파생만으로 마무리).
  - **(B) 채택 (대안)**:
    - plan 변경 필요 항목:
      - `UMeshTypeData::RequiredPayloadBytes() = sizeof(FMeshRotationPayload)` 로 변경 — Cycle 10d 의 ξ 해소 덕분에 Stride 자동 가산, **Sprite Stride 불변 보장 유지** (회귀 0).
      - payload struct 정의 위치: 별도 `ParticleMeshTypes.h` 신규 또는 `ParticleTypes.h` 확장 — Cycle 11 에서 신규 파일 1개 추가 시 vcxproj 항목 +1.
      - 회귀 안전 장치 §B.1 line 133 문구 변경: `UMeshTypeData::RequiredPayloadBytes() = sizeof(FMeshRotationPayload) — 단, USpriteTypeData::RequiredPayloadBytes() = 0 유지가 Sprite 불변 보장`
      - FParticleMeshEmitterInstance 의 Spawn override 도입 (initial orientation 설정), BuildInstanceData override 가 payload 영역 read (Rotation FVector 3축 → instance VB 의 Transform/Quat 빌드).
  - Claude Code 는 plan 작성 시 (A) 를 기본 전제로 진행. 사용자가 (B) 를 선호하면 implement plan 의 G 항목 (추측/미결정) 에서 명시 후 진입.

---

## §2.3 Cycle 11 (Mesh) 구현 plan 검증

### §B.1 변경 대상 파일 8건 — 존재/위치 확인

- [OK] **`Engine/Particle/ParticleModuleTypeDataMesh.h/.cpp`** — 신규.
  근거: Glob `**/ParticleModuleTypeDataMesh*` 0 hits.
- [OK] **`Engine/Particle/ParticleMeshEmitterInstance.h/.cpp`** — 신규.
  근거: Glob `**/ParticleMeshEmitterInstance*` 0 hits.
- [OK] **`Engine/Render/Resource/VertexTypes.h`** — 수정. 현재 Sprite 정점만 정의.
  근거: [VertexTypes.h:59-76](../JSEngine/Source/Engine/Render/Resource/VertexTypes.h:59) `FSpriteParticleVertex` (20B) + `FSpriteParticleInstanceData` (44B) 만. `FMeshParticleInstanceData` 정의 부재 (forward decl만 [RenderCommand.h:28](../JSEngine/Source/Engine/Render/Scene/RenderCommand.h:28), [ParticleEmitterInstance.h:12](../JSEngine/Source/Engine/Particle/ParticleEmitterInstance.h:12)).
- [OK] **`Engine/Render/Resource/VertexFactoryTypes.h`** — 수정. 현재 `EmptyParticleDesc` fallback.
  근거: [VertexFactoryTypes.h:239, 262-265](../JSEngine/Source/Engine/Render/Resource/VertexFactoryTypes.h:239) `EmptyParticleDesc = {};` + case `MeshParticle: return EmptyParticleDesc;`. `MeshParticleLayout` / `MeshParticleDesc` 정의 부재.
- [OK] **`Engine/Render/Renderer/RenderFlow/ParticleRenderPass.cpp` line 253-258 NOP** — 확인.
  근거: [ParticleRenderPass.cpp:253-258](../JSEngine/Source/Engine/Render/Renderer/RenderFlow/ParticleRenderPass.cpp:253)
  ```
  void FParticleRenderPass::RenderMeshEmitter(const FRenderCommand& Cmd, const FRenderPassContext& Context)
  {
      (void)Cmd;
      (void)Context;
      // Cycle 11 (Mesh emitter)에서 본문 채움.
  }
  ```
- [MISMATCH] **`Engine/Render/Scene/PrimitiveDrawCommandBuilder.cpp` line 622-627** — Pre-Entry md `§B.1` 표현 "NOP 교체" 부정확.
  - 실제 상태: Mesh switch case 가 **이미 일부 구현됨** (Cycle 10c).
  - 근거: [PrimitiveDrawCommandBuilder.cpp:622-627](../JSEngine/Source/Engine/Render/Scene/PrimitiveDrawCommandBuilder.cpp:622)
    ```
    case EParticleEmitterRenderMode::Mesh:
        Cmd.MeshParticleInstances = Instance->GetMeshInstanceData(Count);
        Cmd.MeshParticleInstanceCount = Count;
        Cmd.VertexFactoryType = EVertexFactoryType::MeshParticle;
        bHasData = (Cmd.MeshParticleInstances != nullptr && Count > 0);
        break;
    ```
  - 현재 누락된 부분: `Cmd.MeshBuffer` 미세팅 + Mesh asset(`UStaticMesh*`) 조회 + `Cmd.SectionIndexStart/Count` 미세팅 + `Cmd.Material` Mesh asset 의 머티리얼 적용 분기 미세팅.
  - 어느 쪽이 stale 인지 단정 보류 — 사실만 기록.
- [OK] **`Shaders/Particle/MeshParticle.hlsl`** — 신규.
  근거: Glob `**/MeshParticle*.hlsl` 0 hits. 기존 `Shaders/Particle/SpriteParticle.hlsl` 만 존재.
- [OK] **`Engine/Render/Resource/ShaderPaths.h`** — 수정. 현재 `ParticleSprite` 만.
  근거: [ShaderPaths.h:34](../JSEngine/Source/Engine/Render/Resource/ShaderPaths.h:34) `inline constexpr const char* ParticleSprite = "Shaders/Particle/SpriteParticle.hlsl";` `ParticleMesh` 항목 부재.
- [OK] **`JSEngine.vcxproj` + `.filters`** — 존재.
  근거: Glob `**/JSEngine.vcxproj*` 결과 3 hits (`.vcxproj`, `.vcxproj.filters`, `.vcxproj.user`).

### §B.1 silent bug 매칭 처리 plan 항목

- [OK] **§7-1** — MeshParticle case 본문 채움 (EmptyParticleDesc → MeshParticleDesc 교체) plan 에 명시.
- [OK] **§7-4** — vcxproj 신규 파일 다수 등록 plan 에 명시.
- [OK] **§7-5** — `EPT_ParticleSystem` case `return true` 보장. 현재 보장됨 ([PrimitiveDrawCommandBuilder.cpp:675](../JSEngine/Source/Engine/Render/Scene/PrimitiveDrawCommandBuilder.cpp:675) `return true;`).
- [OK] **ν / ξ** — Cycle 10d 사전 해소. Cycle 11 추가 작업 0.

### §B.1 에 빠진 항목 — 신규 식별

- [MISMATCH] **`UParticleModuleTypeDataBase::CreateInstance()` override** — `UMeshTypeData::CreateInstance() → new FParticleMeshEmitterInstance` 필요.
  근거: [ParticleModuleTypeData.h:27](../JSEngine/Source/Engine/Particle/ParticleModuleTypeData.h:27) `virtual FParticleEmitterInstance* CreateInstance(...)`. `USpriteTypeData::CreateInstance()` 가 base instance 반환 ([cpp/h:40](../JSEngine/Source/Engine/Particle/ParticleModuleTypeData.h:40))처럼 `UMeshTypeData::CreateInstance()` 도 정의 필수. §B.1 파일 목록 자체에는 `ParticleModuleTypeDataMesh.h/.cpp` 신규로 포함되어 있어 암묵적이지만, plan body 의 "container 상호작용" 섹션에는 명시 없음 → implement plan 에 명시 추가 권장.
- [MISMATCH] **Mesh asset 참조 형태 (UPROPERTY) 미명시** — `UMeshTypeData` 의 `UStaticMesh*` 멤버를 어떤 UPROPERTY 속성으로 정의할지 (ReferenceKind = Asset 등) 명시 부재. baseline [InfraCheck.md §3.1 (a)](ParticleEmitter_InfraCheck.md:71) 에 "ReferenceType = Asset" 언급. implement plan 에 패턴 명시 권장.
- [MISMATCH] **D3D state setup (BlendState / DepthStencilState / RasterizerState)** — Mesh path 의 state 가 Sprite 와 다른지 미확정.
  - Sprite ([ParticleRenderPass.cpp:186-191](../JSEngine/Source/Engine/Render/Renderer/RenderFlow/ParticleRenderPass.cpp:186)): AlphaBlend + DepthReadOnly + SolidNoCull.
  - Mesh 는 일반적으로 Opaque + DepthTestWrite + DepthBackfaceCull 이 자연 — implement plan G 항목 (추측) 으로 표기 필요.
- [MISMATCH] **per-instance Transform constant 전달 방식** — Sprite 는 `b8 SpriteParticleCB` (SubUV cols/rows) + per-instance Position/Size/Color/Rotation/SubUVIndex 만 instance VB 슬롯에 전달. Mesh 는 instance 마다 `FMatrix Transform` (또는 Position + Quat + Scale) 이 instance VB 슬롯에 필요 → `FMeshParticleInstanceData` 정의 상세 미명시. baseline [§3.1 (d)](ParticleEmitter_InfraCheck.md:87) 가 "추측 sizeof ≈ 64–80 bytes" 로만 표현. implement plan 에 정확한 layout 명시 권장.
- [MISMATCH] **에디터 UI 분기** — `EditorParticleSystemWidget.cpp` 의 emitter UI 에서 TypeData 선택 / Mesh asset 선택이 가능한지 미확인. baseline `[InfraCheck.md §1.2](ParticleEmitter_InfraCheck.md:31)` 에서 "에디터에서 변경 불가" 언급. RenderMode 가 NoEdit 인 점은 TypeData 가 single source 이므로 OK 이지만, Mesh asset 선택 UI 가 자동 생성되는지 (UPROPERTY ReferenceKind = Asset 자동 처리?) implement plan G 항목 표기 필요.

---

## 재진단 종합 판정

> **(B) 이슈 1건 사용자 결정 필요, 결정 후 Cycle 11 진입.**

- **사용자 결정 필요**: 이슈 (d) — 결정 4 의 (A) vs (B) 명시 확정. Pre-Entry md 가 사실상 (A) 로 작성됐으나 unchecked 항목으로 남아 있어 implement 진입 시 lock-in 명시 권장.
- **자동 해소 가능**: 이슈 (a) / (b) / (c) — 추가 결정 불필요. §2.1 모든 항목 OK. §2.3 의 PrimitiveDrawCommandBuilder line 622-627 표현 부정확은 implement plan body 가 "이미 dispatch 구현, MeshBuffer/SectionIndex/Material 추가 wire-up 만 필요" 로 정정하면 해소.

이슈 (d) 의 결정만 lock-in 되면 Cycle 11 즉시 진입 가능. 추가 선행 fix cycle 불필요.
