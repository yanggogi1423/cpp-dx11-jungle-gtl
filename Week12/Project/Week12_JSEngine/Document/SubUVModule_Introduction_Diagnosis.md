# USubUVModule 신규 도입 가능성 진단

작성일: 2026-05-24
대상 브랜치: `feature/ParticleRender`
선행 문서: [Particle_ControlFlow_Diagnosis.md](Particle_ControlFlow_Diagnosis.md), [Particle_NextCycle_Plan.md](Particle_NextCycle_Plan.md), [Cascade_Porting_Status.md](Cascade_Porting_Status.md)

진단 대상: 별도 `USubUVModule` 신설 (선행 plan의 A안 폐기, C안 채택 검토)
모드: **진단(diagnose only) — 코드 변경 없음**

---

## 0. 진단 중 발견한 중대 사실 (Plan 변경 영향)

선행 plan 작성 후 본 진단에서 새로 확정된 사실 2가지. 이로 인해 C안의 형태가 달라짐:

1. **`FTextureAtlasResource` 인프라가 이미 존재** ([ResourceTypes.h:23-34](JSEngine/Source/Engine/Core/ResourceTypes.h:23))
   - 멤버: `FName Name`, `FString Path`, `UTexture* Texture`, `uint32 Columns`, `uint32 Rows`, `bool IsLoaded()`
   - **builder가 atlas Cmd 3 필드 (Texture / Columns / Rows) 모두 1번 조회로 충족 가능**
   - 등록 entrypoint: `FResourceManager::RegisterSubUV(FName, Path, Columns, Rows)`, 조회: `FResourceManager::FindSubUV(FName)` — [ResourceManager.h:109-112](JSEngine/Source/Engine/Core/ResourceManager.h:109)

2. **`USubUVComponent` 선례가 이미 존재** ([SubUVComponent.h/.cpp](JSEngine/Source/Engine/Component/SubUVComponent.h))
   - 패턴: `FName SubUVName` UPROPERTY + `FTextureAtlasResource* CachedSubUV` 비-UPROPERTY 런타임 캐시
   - `Serialize(Ar) override` 의 IsLoading 분기에서 `SetSubUV(SubUVName)` 호출로 캐시 자동 재구축 — [SubUVComponent.cpp:30-38](JSEngine/Source/Engine/Component/SubUVComponent.cpp:30)
   - `PostEditProperty("SubUVName")` 에서도 같은 메서드로 캐시 갱신 — [SubUVComponent.cpp:51-59](JSEngine/Source/Engine/Component/SubUVComponent.cpp:51)
   - **USubUVModule의 직접적인 모방 대상**

→ 이 두 사실로 USubUVModule의 자연스러운 형태는 "`FName` 1개 필드 + atlas 캐시 포인터"로 굳어진다. `UTexture* + int32 Columns + int32 Rows` 3 필드를 직접 들고 있는 형태(선행 plan의 A안)는 atlas 등록 mechanism 우회/중복이 됨.

---

## 1. 결정/검증 요약 표 (hop별 매트릭스)

| Hop | Has | Missing | 결정 필요 | 위험 |
|-----|-----|---------|----------|------|
| **1. CacheModuleLists 분류** | bSpawnModule/bUpdateModule → SpawnModules/UpdateModules 배열 push 규칙 ([ParticleSystem.cpp:8-41](JSEngine/Source/Engine/Particle/ParticleSystem.cpp:8)). Spawn+Update 둘 다 true 선례 2건 (`Color`, `Size`) | (없음) | **USubUVModule을 `bUpdateModule=true` only로 할지, Spawn도 true로 할지** | 잘못 설정 시 module이 캐시에 안 잡혀 silent하게 동작 안 함 |
| **2. FBaseParticle 레이아웃** | `FBaseParticle` 13 멤버 ([ParticleTypes.h:19-34](JSEngine/Source/Engine/Particle/ParticleTypes.h:19)). `FSpriteParticleInstanceData::SubUVIndex` (offset 40) + 7-element layout 슬롯 이미 존재 ([VertexFactoryTypes.h:118](JSEngine/Source/Engine/Render/Resource/VertexFactoryTypes.h:118)) | **`FBaseParticle::SubUVIndex` 필드 부재**. `Rotation`/`RotationRate`만 있고 SubUV 영구 저장 자리 없음 | **per-particle SubUVIndex를 영구 필드로 저장할지 vs 매 프레임 RelativeTime×FrameCount로 동적 계산할지** | 영구 저장 시 `sizeof(FBaseParticle)` 변경 → `ParticleSize/Stride` 영향 (캐시 자동 재산정이라 큰 문제 아님) |
| **3. 신규 클래스 절차** | 신규 위치 `JSEngine/Source/Engine/Particle/` 하위 결정 가능. 모방 대상 `UParticleModuleColor` (Spawn+Update 둘 다) 명확 | vcxproj/filters에 신규 .h/.cpp **수동 등록** 필요 (silent bug §7-4) | **신규 파일을 `ParticleModules.h/.cpp`에 합칠지(파일 추가 없음) vs `ParticleModuleSubUV.h/.cpp`로 분리할지** | 분리 시 vcxproj 자동 갱신 함정. 합치면 함정 회피 (강력 권장) |
| **4. 직렬화 자동성** | `FProperty::VisitReferences`는 `ReferenceKind != Asset`일 때만 그래프 traverse ([Property.cpp:178-183](JSEngine/Source/Engine/Object/Property.cpp:178)). `ReferenceKind == Asset`이면 path 직렬화로 자동 처리 ([Property.cpp:586-598](JSEngine/Source/Engine/Object/Property.cpp:586)). `UPROPERTY(ReferenceType = Asset)` 마크 선례 3건 ([DecalComponent.h:46](JSEngine/Source/Engine/Component/DecalComponent.h:46), [MeshComponent.h:25](JSEngine/Source/Engine/Component/MeshComponent.h:25), [ProceduralMeshComponent.h:54](JSEngine/Source/Engine/Component/ProceduralMeshComponent.h:54)) | (없음 — 인프라 완비) | **USubUVModule에 어떤 필드 추가할지: (a) `FName SubUVName` 만 (USubUVComponent 패턴) vs (b) `UPROPERTY(ReferenceType=Asset) UTexture* SubUVTexture` + `int32 Columns/Rows`** | (a)는 자동 직렬화 + atlas 인프라 재활용. (b)는 atlas 시스템 우회 → 중복 |
| **5. Builder 조회 전략** | Builder 본문 ([PrimitiveDrawCommandBuilder.cpp:535-573](JSEngine/Source/Engine/Render/Scene/PrimitiveDrawCommandBuilder.cpp:535)). `Instance->GetCurrentLODLevel()` 이미 cached ([ParticleEmitterInstance.h:30](JSEngine/Source/Engine/Particle/ParticleEmitterInstance.h:30)). `Cast<UParticleModuleSpawn>` 선례 ([ParticleSystem.cpp:26](JSEngine/Source/Engine/Particle/ParticleSystem.cpp:26)) | LODLevel에 `CachedSubUVModule` 슬롯 없음 | **옵션 A(매 프레임 linear search) vs B(LODLevel 캐시 슬롯 + CacheModuleLists 수정) vs C(emitter 1개 가정 캐시 1슬롯, 사실상 B와 동일)** | B 채택 시 `CacheModuleLists()` 수정 영향 (이미 SpawnModule 슬롯 패턴 선례 있어 위험 낮음) |
| **6. Mesh/Beam/Ribbon 확장 정합성** | `EParticleEmitterRenderMode` enum 정의 ([ParticleTypes.h:11-17](JSEngine/Source/Engine/Particle/ParticleTypes.h:11)). 사용처 grep 결과: **builder 분기 없음**, EditorParticleSystemWidget의 UI label 변환만 ([EditorParticleSystemWidget.cpp:282-294](JSEngine/Source/Editor/UI/EditorParticleSystemWidget.cpp:282)). `UParticleModuleTypeDataBase` forward만, 정의 부재 ([ParticleSystem.h:6,40](JSEngine/Source/Engine/Particle/ParticleSystem.h:6)) | TypeDataBase 정의, builder의 RenderMode 분기 | (본 cycle 결정 X — 식별만) | 본 cycle에서 C안 채택 시 Mesh/Beam/Ribbon emitter는 SubUVModule을 안 가지면 됨 → 코드 friction 없음 |

---

## 2. Hop 1: Module 분류 시스템 (CacheModuleLists)

### 2.1 실측 결과

`UParticleLODLevel::CacheModuleLists()` 본문 ([ParticleSystem.cpp:8-41](JSEngine/Source/Engine/Particle/ParticleSystem.cpp:8)):

- [x] 라인 14-17: `RequiredModule` 이 활성이면 무조건 `SpawnModules` 에 push (Required는 module 배열 외부의 의무 슬롯)
- [x] 라인 19-40: `Modules` 배열 순회
  - 라인 26-30: `Cast<UParticleModuleSpawn>` 으로 캐스트 성공 → `SpawnModule` 슬롯에 저장 후 **continue** (SpawnModules 배열에는 안 들어감 — `bSpawnModule=false`)
  - 라인 32-35: `IsSpawnModule()` true → `SpawnModules.push_back`
  - 라인 36-39: `IsUpdateModule()` true → `UpdateModules.push_back`
  - **둘 다 true면 양쪽에 들어감** (if 두 개 독립)

### 2.2 기존 9개 module 플래그 표

[ParticleModules.cpp](JSEngine/Source/Engine/Particle/ParticleModules.cpp) 생성자 직접 확인:

| Module | bSpawnModule | bUpdateModule | 위치 |
|--------|--------------|---------------|------|
| `UParticleModuleRequired` | **true** | false | [ParticleModules.cpp:36](JSEngine/Source/Engine/Particle/ParticleModules.cpp:36) |
| `UParticleModuleSpawn` | **false** | false | [ParticleModules.cpp:57](JSEngine/Source/Engine/Particle/ParticleModules.cpp:57) (의도된 분리, §7 silent bug γ 후보) |
| `UParticleModuleLifetime` | true | false | [ParticleModules.cpp:77](JSEngine/Source/Engine/Particle/ParticleModules.cpp:77) |
| `UParticleModuleLocation` | true | false | [ParticleModules.cpp:95](JSEngine/Source/Engine/Particle/ParticleModules.cpp:95) |
| `UParticleModuleVelocity` | true | false | [ParticleModules.cpp:115](JSEngine/Source/Engine/Particle/ParticleModules.cpp:115) |
| **`UParticleModuleColor`** | **true** | **true** | [ParticleModules.cpp:134-135](JSEngine/Source/Engine/Particle/ParticleModules.cpp:134) — **둘 다 true 선례 1** |
| **`UParticleModuleSize`** | **true** | **true** | [ParticleModules.cpp:168-169](JSEngine/Source/Engine/Particle/ParticleModules.cpp:168) — **둘 다 true 선례 2** |
| `UParticleModuleCollision` | false | true | [ParticleModules.cpp:202](JSEngine/Source/Engine/Particle/ParticleModules.cpp:202) |
| `UParticleModuleEventGenerator` | false | true | [ParticleModules.cpp:262](JSEngine/Source/Engine/Particle/ParticleModules.cpp:262) |

### 2.3 USubUVModule 권고

**권고: `bSpawnModule = true; bUpdateModule = true;`** (둘 다 true, Color/Size 패턴).

근거:
- Spawn 시: `Particle.SubUVIndex = 0` 초기화 (또는 Random first frame 옵션)
- Update 시: `RelativeTime × TotalFrames` → SubUVIndex 진행
- 두 시점 모두 동작해야 atlas 첫 프레임 깜빡임 없이 자연스러운 재생

대안: `bUpdateModule = true` only (RequiredModule이 Spawn 시 `Particle.RelativeTime=0`으로 초기화하므로 Update만으로도 첫 프레임 0 보장됨). 단 Spawn에서 SubUVIndex 명시 초기화가 안전.

---

## 3. Hop 2: FBaseParticle 데이터 레이아웃

### 3.1 실측 결과

- [x] `FBaseParticle` 정의 위치: [ParticleTypes.h:19-34](JSEngine/Source/Engine/Particle/ParticleTypes.h:19)
- [x] 멤버 전수 (13개):
  - `FVector Location, OldLocation, Velocity, BaseVelocity` (4×12=48B, 정렬 무시)
  - `float RelativeTime, Lifetime` (8B)
  - `FVector Size` (12B)
  - `FColor Color` (16B, 4×float)
  - `float Rotation, RotationRate` (8B)
  - `uint32 ParticleId, Flags` (8B)
  - `int32 CollisionCount` (4B)
  - 합계 약 104B (정렬 padding 무시한 추정)
- [x] **`SubUVIndex` 필드 없음** — 영구 저장 자리 부재
- [x] `FSpriteParticleInstanceData::SubUVIndex` (offset 40, uint32) — [VertexTypes.h:74](JSEngine/Source/Engine/Render/Resource/VertexTypes.h:74)
- [x] `SpriteParticleLayout` 7 elements 중 `INSTANCE_SUBUV_INDEX` slot 존재 (DXGI_FORMAT_R32_UINT) — [VertexFactoryTypes.h:118](JSEngine/Source/Engine/Render/Resource/VertexFactoryTypes.h:118)

### 3.2 결정 필요

**Per-particle SubUVIndex 저장 전략 두 옵션:**

| 옵션 | FBaseParticle 변경 | BuildSpriteInstanceData | 비고 |
|------|---------------------|------------------------|------|
| **(α) 영구 저장** | `uint32 SubUVIndex = 0` 추가 (4B) | `Data.SubUVIndex = Particle->SubUVIndex` | sizeof 변경 → ParticleSize/Stride 영향 (CacheEmitterModuleInfo 자동 재산정이라 OK). USubUVModule.Update가 매 프레임 모든 particle 순회하며 갱신 |
| **(β) 동적 계산** | 변경 없음 | `Data.SubUVIndex = static_cast<uint32>(Particle->RelativeTime × TotalFrames) % TotalFrames` | sizeof 변경 없음. 단 builder 시점에 SubUVModule(또는 LOD에서 Columns/Rows)을 알아야 TotalFrames 계산 가능 |

권고: **(α) 영구 저장**. Color/Size가 이미 영구 저장 방식이므로 일관성. 메모리 영향 4B/particle은 무시 가능.

### 3.3 정렬/sizeof 영향 (옵션 α 채택 시)

- 현재 `sizeof(FBaseParticle)` 추정 ≈ 96-112B (정렬 padding 포함, 컴파일러 의존)
- `uint32 SubUVIndex` 추가 시 +4B (CollisionCount 뒤에 자연 padding 차지 가능성 있음 → 실제 sizeof 증가 0~4B)
- 영향처: `UParticleEmitter::ParticleSize = sizeof(FBaseParticle)` ([ParticleSystem.cpp:48](JSEngine/Source/Engine/Particle/ParticleSystem.cpp:48)), `ParticleStride = ParticleSize`. 자동 재산정.
- 영향 안 받음: GPU 측 (`FSpriteParticleInstanceData` 별도 struct), shader (instance struct만 봄)

---

## 4. Hop 3: USubUVModule 신규 클래스 도입 절차

### 4.1 파일 위치 결정

옵션 A: `JSEngine/Source/Engine/Particle/ParticleModules.h/.cpp` 에 **추가** (신규 파일 없음)
옵션 B: `JSEngine/Source/Engine/Particle/ParticleModuleSubUV.h/.cpp` **분리**

**권고: 옵션 A**. 이유: silent bug §7-4 (vcxproj 자동 갱신 함정) 완전 회피. 9개 기존 module 모두 한 파일에 있는 컨벤션 유지. 분리 가치 낮음 (USubUVModule 본문도 작음).

### 4.2 vcxproj/filters 갱신 절차 (옵션 B 채택 시에만 필요)

[Cascade_Porting_Status.md §7-4](Cascade_Porting_Status.md) + Cycle 2/3에서 실제 발생한 사례 근거:

- 신규 파일 추가 시 `JSEngine.vcxproj` + `JSEngine.vcxproj.filters` 두 파일 **수동 편집** 필요
- 패턴 (`ParticleModules` 등록 예시 — [JSEngine.vcxproj:909, 1306](JSEngine/JSEngine.vcxproj:909) / [JSEngine.vcxproj.filters:1296, 2392](JSEngine/JSEngine.vcxproj.filters:1296)):
  - `.vcxproj`: `<ClCompile Include="Source\Engine\Particle\ParticleModuleSubUV.cpp" />` + `<ClInclude Include="...\.h" />` 2줄
  - `.vcxproj.filters`: 동일 2줄 + `<Filter>Source\Engine\Particle</Filter>` 자식 태그
- **VS가 외부 수정을 덮어쓸 수 있음** (Cycle 2에서 실제 발생) — VS를 닫고 편집하거나, 편집 후 VS에서 "솔루션 다시 로드"
- 검증: 빌드 통과 + Solution Explorer에 신규 파일 노출 확인

### 4.3 override 후보 가상 함수

[ParticleModule.h:20-27](JSEngine/Source/Engine/Particle/ParticleModule.h:20):
- `virtual void Spawn(FParticleEmitterInstance*, FBaseParticle&, float SpawnTime)` — 옵션 α 채택 시 `Particle.SubUVIndex = 0` 초기화
- `virtual void Update(FParticleEmitterInstance*, float DeltaTime)` — 매 프레임 active particle 순회하며 SubUVIndex 진행

추가 가상 hook 없음 — base는 이 2개와 `bEnabled/bSpawnModule/bUpdateModule` 플래그뿐.

### 4.4 모방 대상

**`UParticleModuleColor`** ([ParticleModules.h:106-122](JSEngine/Source/Engine/Particle/ParticleModules.h:106), [ParticleModules.cpp:132-164](JSEngine/Source/Engine/Particle/ParticleModules.cpp:132)):
- 생성자에서 `bSpawnModule = true; bUpdateModule = true;`
- Spawn override: `Particle.Color = StartColor`
- Update override: 모든 active particle 순회, RelativeTime 기반 lerp

USubUVModule도 동일 구조:
- Spawn: `Particle.SubUVIndex = 0`
- Update: `Particle.SubUVIndex = floor(RelativeTime × TotalFrames) % TotalFrames`

### 4.5 신규 UPROPERTY 필드 (본 진단에서 결정 필요)

선행 prompt §"신규 UPROPERTY 필드 확정" 의 3가지 후보:
- `SubUVTexture: UTexture*`
- `SubUVColumns: int32`
- `SubUVRows: int32`
- `InterpolationMethod: enum` (후속)

→ §0의 새 발견으로 인해 다음 두 형태로 재정렬:

**옵션 (a) — FName 1개 (USubUVComponent 패턴 모방)**:
```
UPROPERTY(DisplayName = "SubUV")
FName SubUVName;
// 비-UPROPERTY 런타임 캐시
FTextureAtlasResource* CachedSubUV = nullptr;
```
- Texture/Columns/Rows는 `CachedSubUV->Texture/Columns/Rows`로 얻음
- 직렬화 자동 (FName UPROPERTY)
- Editor 셀렉터 자동 (EditorAssetService의 SubUVNames 활용 가능 — [EditorAssetService.h:47](JSEngine/Source/Editor/Asset/EditorAssetService.h:47))

**옵션 (b) — 3 필드 직접 보유 (선행 plan의 A안 형태)**:
```
UPROPERTY(DisplayName = "Sub UV Texture", ReferenceType = Asset)
UTexture* SubUVTexture = nullptr;
UPROPERTY(DisplayName = "Sub UV Columns", Min = 1)
int32 SubUVColumns = 1;
UPROPERTY(DisplayName = "Sub UV Rows", Min = 1)
int32 SubUVRows = 1;
```
- 직렬화 자동 (Hop 4 참조)
- Atlas 시스템과 별도 — atlas 등록 인프라를 우회

**권고: 옵션 (a)**. 근거: (1) `FTextureAtlasResource`가 이미 우리 필요(Texture+Columns+Rows)를 1번 조회로 충족, (2) USubUVComponent 직접 선례, (3) Editor에 SubUVNames 셀렉터 이미 있음, (4) "Columns/Rows를 atlas 등록 시 1번, particle module에서 또 1번 입력"하는 중복 제거.

---

## 5. Hop 4: 직렬화 자동성 검증

### 5.1 그래프 직렬화 mechanism

- [x] `FParticleSystemObjectGraphResolver` ([ResourceManager.cpp:85-149](JSEngine/Source/Engine/Core/ResourceManager.cpp:85))
- [x] `CollectParticleSystemObjectGraph` ([ResourceManager.cpp:151-187](JSEngine/Source/Engine/Core/ResourceManager.cpp:151)) — BFS로 root부터 `Property->VisitReferences`를 따라 그래프 객체 수집
- [x] `IsParticleSystemGraphObject` ([ResourceManager.cpp:67-74](JSEngine/Source/Engine/Core/ResourceManager.cpp:67)) — `UParticleSystem`/`UParticleEmitter`/`UParticleLODLevel`/**`UParticleModule`** 의 자식만 그래프에 포함. **USubUVModule이 `UParticleModule` 상속이면 자동 인지** ✅
- [x] 직렬화 시 `Object->GetClass()->GetAllProperties()` 순회 + `Property->VisitReferences` 사용 ([ResourceManager.cpp:167-185](JSEngine/Source/Engine/Core/ResourceManager.cpp:167))

### 5.2 `FProperty::VisitReferences` 본문 두 분기

[Property.cpp:169-211](JSEngine/Source/Engine/Object/Property.cpp:169):
- `EPropertyType::ObjectPtr` + `ReferenceKind != Asset` → 그래프 자식 객체로 처리 (UUID 매핑)
- `EPropertyType::ObjectPtr` + `ReferenceKind == Asset` → traverse 안 함 (외부 asset)
- `EPropertyType::Array` → element 별 recurse
- `EPropertyType::Struct` → child property recurse

### 5.3 `SerializeObjectPtrValue` 두 분기

[Property.cpp:579-633](JSEngine/Source/Engine/Object/Property.cpp:579):
- `ReferenceKind == Asset` → `GetAssetObjectPath` 경로를 FString으로 저장, 로드 시 `ResolveAssetObject(Path, Class)`
- `ReferenceKind != Asset` → `Resolver->GetObjectId(Object)` UUID 저장, 로드 시 `Resolver->ResolveObjectId`

→ **둘 다 자동**. 사용자 코드에서 직접 직렬화할 것 없음.

### 5.4 `UPROPERTY(ReferenceType = Asset)` 선례

- [x] [DecalComponent.h:46](JSEngine/Source/Engine/Component/DecalComponent.h:46): `UPROPERTY(DisplayName = "Materials", ReferenceType = Asset) TArray<UMaterialInterface*> Materials;`
- [x] [MeshComponent.h:25](JSEngine/Source/Engine/Component/MeshComponent.h:25): 동일 패턴
- [x] [ProceduralMeshComponent.h:54](JSEngine/Source/Engine/Component/ProceduralMeshComponent.h:54): 동일 패턴

→ 옵션 (b) 채택 시 `UTexture* SubUVTexture`도 같은 마커로 자동 직렬화에 잡힌다.

### 5.5 `UBillboardComponent::Texture` 패턴 (선행 plan §4 패턴 일관성)

- [x] [BillboardComponent.h:59-62](JSEngine/Source/Engine/Component/BillboardComponent.h:59):
  - `UPROPERTY(DisplayName = "Texture") FName TextureName;` ← 직렬화되는 것은 FName
  - `UTexture* Texture = nullptr;` ← 비-UPROPERTY, 런타임 캐시
- → 옵션 (a) 패턴과 동일. **선행 plan §4 "패턴 일관성"의 근거는 사실은 FName 패턴이었다** (UTexture* 직접 직렬화 패턴 아님)

### 5.6 검증 방법 (본 진단에서는 추론만, 코드 작성 금지)

- 옵션 (a) 채택 시: `Asset->Emitters[0]->LODLevels[0]->Modules.push_back(NewSubUVModule)` → `SaveParticleSystem` → JSON에 SubUVModule UCLASS 노드 + FName 필드 직렬화 확인
- 옵션 (b) 채택 시: 같은 흐름, JSON에 SubUVTexture path 문자열 + Columns/Rows 정수 확인

---

## 6. Hop 5: Builder의 USubUVModule 조회 전략

### 6.1 Builder 현재 상태

[PrimitiveDrawCommandBuilder.cpp:535-573](JSEngine/Source/Engine/Render/Scene/PrimitiveDrawCommandBuilder.cpp:535) — `case EPT_ParticleSystem:` 본문:
- emitter 루프 안에서 `InstanceData` 채워 Cmd 발행
- 라인 566-568: `Cmd.ParticleTexture = nullptr`, `SubUVColumns = 1`, `SubUVRows = 1` 하드코딩 (TODO)

### 6.2 옵션 A — 매 프레임 linear search

```
// Pseudo (실제 코드 작성 금지 — 진단의 식별용)
for (UParticleModule* M : LOD->GetModules())
    if (USubUVModule* S = Cast<USubUVModule>(M)) { /* use S */; break; }
```
- 장점: builder만 수정 (좁은 변경). LODLevel 손대지 않음.
- 단점: 매 프레임 N 모듈 순회 (N ≤ 10 추정, 무시 가능)
- 비고: `Cast<UParticleModuleSpawn>` 선례 ([ParticleSystem.cpp:26](JSEngine/Source/Engine/Particle/ParticleSystem.cpp:26))로 패턴 일관

### 6.3 옵션 B — LODLevel에 `CachedSubUVModule` 슬롯 + `CacheModuleLists` 채움

```
// Pseudo
// ParticleSystem.h: private USubUVModule* CachedSubUVModule = nullptr; + getter
// ParticleSystem.cpp CacheModuleLists():
//   if (USubUVModule* S = Cast<USubUVModule>(Module)) { CachedSubUVModule = S; /* and/or push */; }
```
- 장점: 명료. `SpawnModule` 별도 슬롯 선례와 일관 ([ParticleSystem.h:43](JSEngine/Source/Engine/Particle/ParticleSystem.h:43), [ParticleSystem.cpp:26-30](JSEngine/Source/Engine/Particle/ParticleSystem.cpp:26))
- 단점: `CacheModuleLists()` + LODLevel 헤더 + getter 3 영향처
- 비고: SubUVModule이 일반 SpawnModules/UpdateModules에 들어가야 Tick에서 Spawn/Update 호출됨 — **별도 슬롯과 + 일반 배열 양쪽에 들어가도록 코드 작성 필요** (continue 안 함)

### 6.4 옵션 C — emitter 1개 가정 캐시 1슬롯

옵션 B와 사실상 동일 (LODLevel당 SubUVModule 최대 1개 가정). 별도 옵션으로 다룰 가치 낮음.

### 6.5 권고

**옵션 B 권고**. 근거:
- 코드 일관성: 기존 `SpawnModule` 별도 캐시 슬롯 패턴
- 명료성: builder가 `LOD->GetCachedSubUVModule()` 1줄로 조회
- 위험 낮음: `CacheModuleLists()` 수정 영역 작음 (continue 빼고 if 1줄 추가)

대안 옵션 A는 LODLevel 변경 회피가 강하게 필요할 때 (예: hop 5의 변경을 minimize하고 builder만 손대고 싶을 때) 합리적.

### 6.6 Hardcoded 3줄 교체 패턴 초안 (옵션 B + (a) 가정)

코드 작성 금지 원칙에 따라 의사 코드로만 표시:

```
// PrimitiveDrawCommandBuilder.cpp case EPT_ParticleSystem 본문 (Cmd 채우는 시점):
UParticleLODLevel* LOD = Instance->GetCurrentLODLevel();
USubUVModule* SubUV = LOD ? LOD->GetCachedSubUVModule() : nullptr;
const FTextureAtlasResource* Atlas =
    (SubUV && SubUV->IsEnabled())
    ? FResourceManager::Get().FindSubUV(SubUV->GetSubUVName())
    : nullptr;

Cmd.ParticleTexture       = (Atlas && Atlas->IsLoaded()) ? Atlas->Texture : nullptr;
Cmd.ParticleSubUVColumns  = Atlas ? Atlas->Columns : 1;
Cmd.ParticleSubUVRows     = Atlas ? Atlas->Rows    : 1;
```

---

## 7. Hop 6: Mesh/Beam/Ribbon emitter 확장 정합성

### 7.1 EParticleEmitterRenderMode 현재 사용처

전체 grep 결과:
- [x] [ParticleTypes.h:11-17](JSEngine/Source/Engine/Particle/ParticleTypes.h:11) — enum 정의
- [x] [ParticleTypes.h:83](JSEngine/Source/Engine/Particle/ParticleTypes.h:83) — `FParticleEmitterRuntimeView::RenderMode` 멤버
- [x] [ParticleModules.h:20,38](JSEngine/Source/Engine/Particle/ParticleModules.h:20) — RequiredModule getter + private 멤버
- [x] [EditorParticleSystemWidget.cpp:282-294](JSEngine/Source/Editor/UI/EditorParticleSystemWidget.cpp:282) — UI label 변환 switch (CPU Sprites / Mesh Particles / Beam / Ribbon)
- [x] **Builder 분기 없음** — `case EPT_ParticleSystem` 안에서 RenderMode를 분기하지 않음. 모든 emitter를 Sprite로 처리

### 7.2 UParticleModuleTypeDataBase 부재 재확인

- [x] [ParticleSystem.h:6](JSEngine/Source/Engine/Particle/ParticleSystem.h:6) — `class UParticleModuleTypeDataBase;` forward only
- [x] [ParticleSystem.h:40](JSEngine/Source/Engine/Particle/ParticleSystem.h:40) — `UParticleModuleTypeDataBase* TypeDataModule = nullptr;` 비-UPROPERTY 슬롯
- [x] **정의 부재** — grep 결과 정의 없음. UE Cascade 정통의 Mesh/Beam/Ribbon 분기 메커니즘이 미포팅

### 7.3 C안의 emitter 확장 친화성

- USubUVModule은 `LODLevel.Modules` 배열에 들어가는 일반 module
- **Mesh/Beam/Ribbon emitter는 SubUVModule을 안 가지면 그만**. Sprite 전용 module로 자연 분리됨
- 만약 A안 (RequiredModule에 SubUV 박기)으로 갔다면, Mesh/Beam/Ribbon emitter도 RequiredModule을 갖기 때문에 무관한 SubUV 필드를 짊어졌을 것 → C안이 **emitter 확장 친화적인 것이 코드 레벨에서 확인됨** ✅

### 7.4 Mesh/Beam/Ribbon emitter 확장 시 예상 변경 hop 목록 (본 진단에서 결정 X, 식별만)

| hop | 작업 | 영향 영역 |
|-----|------|----------|
| 1 | `UParticleModuleTypeDataBase` 정의 (`USpriteTypeData`/`UMeshTypeData`/...) | Particle/ 하위 신규 .h/.cpp |
| 2 | `UParticleLODLevel.TypeDataModule` 사용 활성화 (현재는 슬롯만 있고 미사용) | LODLevel 사용처 전반 |
| 3 | Builder의 `case EPT_ParticleSystem`에 `TypeDataModule` 기반 VertexFactoryType 분기 | PrimitiveDrawCommandBuilder.cpp |
| 4 | Mesh용 `EVertexFactoryType::MeshParticle` + layout 등록 | VertexFactoryTypes.h, hlsl 신규 |
| 5 | 기존 `EVertexFactoryType::SpriteParticle` Desc 보호 (Default fallback이 StaticMesh) | VertexFactoryTypes.h §7-1 함정 회피 |
| 6 | `FParticleRenderPass` 가 Sprite/Mesh/Beam/Ribbon 다 처리할지, 별도 RenderPass 분리할지 결정 | RenderPipeline.cpp |
| 7 | `BuildSpriteInstanceData`를 RenderMode별 분기 (or rename to `BuildInstanceData`) | ParticleSystemComponent.cpp |

→ 본 C안 cycle은 위 hop들과 **간섭 없음**. SubUVModule이 Sprite 모듈로 한정되므로 Mesh/Beam/Ribbon 도입 시 추가 변경 없음.

---

## 8. C안 진행을 위해 사용자 결정이 필요한 항목 (요약)

각 항목에 사용자가 답해야 본 진단 다음의 implementation cycle plan 문서 작성 가능:

1. **(Hop 1) USubUVModule 플래그**: `bSpawnModule=true; bUpdateModule=true;` (Color/Size 패턴) vs `bUpdateModule=true` only? — *권고: 둘 다 true*
2. **(Hop 2) Per-particle SubUVIndex 저장**: 옵션 α (FBaseParticle에 `uint32 SubUVIndex` 추가) vs 옵션 β (영구 저장 X, 매 프레임 RelativeTime×FrameCount로 동적 계산)? — *권고: 옵션 α*
3. **(Hop 3) 파일 분리**: 옵션 A (`ParticleModules.h/.cpp`에 합침, vcxproj 무영향) vs 옵션 B (`ParticleModuleSubUV.h/.cpp` 분리)? — *권고: 옵션 A*
4. **(Hop 3/4) UPROPERTY 필드 형태**: 옵션 (a) `FName SubUVName` + 비-UPROPERTY `FTextureAtlasResource* CachedSubUV` 캐시 (USubUVComponent 패턴) vs 옵션 (b) `UTexture* + int32 Columns + int32 Rows` 3 필드 직접 보유? — *권고: 옵션 (a)*
5. **(Hop 5) Builder 조회 전략**: 옵션 A (linear search) vs 옵션 B (LODLevel에 캐시 슬롯 추가 + `CacheModuleLists()` 수정)? — *권고: 옵션 B*
6. **(Hop 3) InterpolationMethod enum**: 본 cycle 포함 vs 후속 cycle? — *권고: 후속 (본 cycle 최소 필드만)*
7. **(Hop 3) USubUVModule이 RequiredModule의 `SubUVName`을 deprecation 처리할지**: 둘 다 두면 어느 쪽 우선? — *결정 필요 (`UParticleModuleRequired::SubUVName` 의 의도/현재 사용처 재확인 필요)*

---

## 9. silent bug 함정 매칭

`Cascade_Porting_Status.md §7` 의 7개 함정 중 본 도입에서 충돌 가능한 것:

| § | 함정 | 본 C안에서의 위험 |
|---|------|------------------|
| 7-1 | `FVertexFactoryRegistry::Get` default StaticMeshDesc fallback | ❌ 무관 — 본 cycle에서 `EVertexFactoryType` 추가 없음 |
| 7-2 | `HashVertexLayout` 동기화 | ❌ 무관 — vertex layout 변경 없음 |
| 7-3 | `PickPasses[]`에 Particle 추가 금지 | ❌ 무관 — 본 cycle에서 안 건드림 |
| **7-4** | **`vcxproj` 외부 수정 시 VS 덮어쓰기** | ⚠️ **옵션 B (파일 분리) 채택 시 직접 충돌**. 옵션 A로 회피 가능 |
| **7-5** | `EPT_ParticleSystem` case 명시 `return true` 종결 | ⚠️ **본 cycle은 이 case 본문을 수정하므로 직접 위험**. Hardcoded 3줄 교체 후 case 출구가 `return true`로 유지되는지 확인 |
| 7-6 | `EditorOverlayCollector` SupportsOutline | ❌ 무관 |
| 7-7 | `PassBatchers[Particle]` 미등록 | ❌ 무관 |

진단 §7 신규 silent bug 후보 (선행 진단 §7) 중 본 cycle 관련:
- **β (PSSetShaderResources(0, nullptr) → discard)**: SubUVName이 설정되지 않거나 atlas가 등록 안 됐을 때 여전히 nullptr → 화면 안 보임. 본 cycle 적용 후에도 atlas 등록 + Module에 이름 설정 두 단계가 다 필요. fallback white 1×1 텍스처는 별도 cycle에서 처리 가능.
- **γ (`UParticleModuleSpawn::bSpawnModule = false` 의도된 분리)**: SubUVModule은 일반 module이므로 `bSpawnModule = true`로 두면 됨. SpawnModule 슬롯과 혼동 위험 없음.

새 silent bug 후보 (본 진단에서 발견):
- **ζ** : Hop 5 옵션 B 채택 시 `CacheModuleLists()`에서 SubUVModule을 별도 슬롯 캐시만 하고 일반 `SpawnModules`/`UpdateModules` 배열에 push 안 하면 → Tick에서 Spawn/Update 호출 안 됨 → SubUVIndex 갱신 silent하게 안 됨. `UParticleModuleSpawn`의 `continue`와 헷갈리지 않도록 코드 작성 시 주의 필요.
- **η** : Hop 3 옵션 (a) 채택 시 `SetSubUV(SubUVName)` 호출 시점 (Load 직후, PostEditProperty)이 누락되면 `CachedSubUV`가 nullptr → builder의 `FindSubUV(SubUVName)` 직접 호출 fallback이 필요하거나 캐시 누락 silent. USubUVComponent와 동일하게 Serialize override 필수.

---

## 10. 다음 구현 사이클 진입 전 사전 작업 목록

사용자가 implementation cycle plan 문서를 작성하기 전에 해소돼야 할 의문점:

- [ ] §8의 7개 결정 항목에 답
- [ ] **`UParticleModuleRequired::SubUVName`** 의 현재 의도/사용처 재확인 — USubUVModule이 같은 필드를 갖게 되면 RequiredModule의 기존 필드를 deprecate할지, 남길지, 또는 RequiredModule이 기본 atlas role을 하고 SubUVModule이 override role을 할지 정책 결정
- [ ] **`FResourceManager::RegisterSubUV(FName, Path, Columns, Rows)`** 가 어디서 호출되는지 (Resource.ini 자동 로드인지 / 코드 명시 호출인지) — atlas 등록 entry 흐름 파악되어야 사용자가 testing 시 atlas 셋업 가능
- [ ] **EditorParticleSystemWidget**가 `UParticleLODLevel::Modules` 배열에 신규 USubUVModule 추가 UI를 자동으로 제공하는지 실측 (UCLASS 등록만으로 Inspector "+" 버튼에 노출되는지) — 안 되면 별도 cycle 필요
- [ ] **`EditorAssetService::GetSubUVNames()`** 셀렉터 ([EditorAssetService.h:47](JSEngine/Source/Editor/Asset/EditorAssetService.h:47))가 SubUVModule의 FName UPROPERTY에 자동 연결되는지 ([EditorPropertyWidget.cpp:2332](JSEngine/Source/Editor/UI/EditorPropertyWidget.cpp:2332) 참조) — 자동이면 옵션 (a)가 더욱 유리
- [ ] Hop 2 옵션 α 채택 시 `sizeof(FBaseParticle)` 정확한 변화량 측정 (정렬 padding 포함) — 메모리 영향 정량화

---

## 11. Inference / 가정 (사실과 분리)

> 이 섹션은 코드 직접 확인이 아닌 추론. 구현 전 사용자 확인 권장.

- **추론 1**: §0의 `FTextureAtlasResource` 발견은 선행 plan의 형태 (RequiredModule에 3 필드 박기)를 무의미하게 만든다. 단, RequiredModule의 기존 `SubUVName` UPROPERTY가 이미 atlas를 가리킬 의도였으나 builder에 연결이 안 된 상태일 가능성이 있다. 그렇다면 사실 "SubUVModule 신설 없이 builder만 수정"하는 더 작은 옵션도 존재한다 — 이는 본 prompt 범위 밖이지만 §8 항목 7에 묶어 사용자가 결정해야 함.
- **추론 2**: Hop 2 옵션 α 채택 시 `sizeof(FBaseParticle)` 증가는 0 또는 4B (정렬 padding 흡수 가능). MaxActiveParticles=128 기준 +512B/emitter — 무시 가능.
- **추론 3**: `UPROPERTY(ReferenceType = Asset)` 마커는 코드베이스 reflection 매크로 처리기가 인식하는 것으로 보임 (Property.h의 ReferenceKind enum + Decal/Mesh/ProceduralMesh 3건 동일 사용). 실제 매크로 처리기 코드는 안 봤으므로 정확한 작동은 컴파일 후 확인 필요.
- **추론 4**: Hop 5 옵션 B의 ζ silent bug 후보(별도 슬롯만 두고 SpawnModules/UpdateModules 미push) 는 의도된 동작이라면 `CacheModuleLists()` 본문에서 SubUVModule 처리 시 `continue` 빼고 if 분기 둘 다 평가하도록 명시 작성 필요. 본 진단에서는 의사 코드 수준만 식별.
- **추론 5**: Hop 6의 "Mesh/Beam/Ribbon emitter 확장 시 7-hop"은 본 진단의 범위 밖이라 식별만 함. 실제 hop 수와 순서는 별도 진단 cycle 필요.

---

## 12. 결론 한 줄

> C안 (별도 USubUVModule 신설)은 코드 레벨에서 **`FTextureAtlasResource` 인프라 + `USubUVComponent` 패턴 모방**으로 일관성 있게 도입 가능하다. 변경 영역은 좁고 (옵션 A+a+B 조합 시 4 파일 ~50줄), silent bug §7-5만 직접 충돌 위험이며 회피 절차 명확하다. **§8의 7개 결정 항목**에 사용자가 답한 후 implementation cycle plan 문서로 진입한다.
