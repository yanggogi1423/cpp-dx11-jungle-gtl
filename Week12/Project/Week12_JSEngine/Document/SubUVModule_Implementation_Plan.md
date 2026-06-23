# USubUVModule 신설 Implementation Plan

작성일: 2026-05-25
대상 브랜치: `feature/ParticleRender`
작업 형태: 단일 cycle — `USubUVModule` 신규 클래스 + per-particle `SubUVIndex` 영구 저장 + builder 연결
상태: **plan-only — 구현 대기 (사용자 승인 후 별도 prompt에서 구현 진입)**

---

## 0. 선행 문서

- [Particle_ControlFlow_Diagnosis.md](Particle_ControlFlow_Diagnosis.md) — Phase B 진입 전 control flow 진단 (atlas → builder → GPU path)
- [SubUVModule_Introduction_Diagnosis.md](SubUVModule_Introduction_Diagnosis.md) — 본 plan의 직접 근거. 7개 결정 도출
- [Particle_NextCycle_Plan.md](Particle_NextCycle_Plan.md) — 폐기된 A안 (RequiredModule에 3 필드 박기) 참조용. silent bug §7-5 매칭 형식 차용
- [Cascade_Porting_Status.md](Cascade_Porting_Status.md) §7 — 7개 silent bug 함정 목록

---

## 1. 목표

> `USubUVModule` 신규 클래스를 도입하고 `FBaseParticle`에 per-particle `SubUVIndex`를 추가하여, sprite emitter가 atlas의 frame을 시간에 따라 순환 재생하는 cycle을 완성한다.

**검증 가능한 성공 기준**: atlas가 등록된 PS asset 셋업 → 액터 배치 → 런타임에 sprite particle 다수가 atlas grid를 시간 진행에 따라 순환 재생 (frame 0 → 1 → ... → N-1 → 0 → ...).

---

## 2. 범위

### 포함
- [ ] `USubUVModule` 신규 UCLASS (`ParticleModules.h/.cpp`에 추가, 별도 파일 없음 — 결정 4)
- [ ] `FBaseParticle::SubUVIndex: uint32 = 0` 필드 추가 (`ParticleTypes.h` — 결정 3)
- [ ] `UParticleModuleRequired::SubUVName` UPROPERTY + getter 제거 (결정 7, USubUVModule로 이관)
- [ ] `BuildSpriteInstanceData`에서 `Data.SubUVIndex = Particle->SubUVIndex` 전달 (결정 3)
- [ ] `PrimitiveDrawCommandBuilder::case EPT_ParticleSystem` 본문에서 USubUVModule **linear search** (결정 5) → atlas 조회 → `Cmd.ParticleTexture` / `SubUVColumns` / `SubUVRows` 채움

### 명시적 제외 (후속 cycle)
- ❌ `InterpolationMethod` enum (Linear blend / Random first frame / Custom curve) — 결정 6
- ❌ `ParticleTexture == nullptr` 시 white 1×1 fallback texture (진단 §9 후보 β)
- ❌ Rotation / RotationRate 동적 갱신 (별도 cycle D)
- ❌ Material 결합 (별도 cycle E)
- ❌ 거리순 정렬 (별도 cycle F)
- ❌ Mesh / Beam / Ribbon TypeData 도입 + emitter 확장 (별도 hop 7개, 진단 §7.4 참조)
- ❌ EditorParticleSystemWidget의 module add 버튼 UI 별도 손질 — UCLASS 등록만으로 자동 노출되는지는 빌드 후 실측 (자동 노출 못 하면 별도 cycle)

---

## 3. 1순위 cycle 선정 근거

진단 보고서 [§0](SubUVModule_Introduction_Diagnosis.md) 의 발견 + [§12 결론](SubUVModule_Introduction_Diagnosis.md) 채택.

### 활용 핵심 인프라
- **`FTextureAtlasResource` 이미 존재** ([ResourceTypes.h:23-34](../JSEngine/Source/Engine/Core/ResourceTypes.h:23)) — `Name / Path / Texture / Columns / Rows / IsLoaded()` 1회 조회로 builder의 3 필드 모두 충족
- **`USubUVComponent` 선례 이미 존재** ([SubUVComponent.h/.cpp](../JSEngine/Source/Engine/Component/SubUVComponent.h)) — `FName SubUVName` UPROPERTY + `FTextureAtlasResource* CachedSubUV` 캐시 + `Serialize` IsLoading 분기에서 `SetSubUV(SubUVName)` 자동 재구축 패턴 그대로 모방
- **`FResourceManager::FindSubUV(FName)`** 조회 entry 존재 ([ResourceManager.cpp:1047](../JSEngine/Source/Engine/Core/ResourceManager.cpp:1047))
- **자동 atlas 등록 경로**: `FResourceManager` 에셋 스캔 시 `EAssetMetaType::SubUV` meta가 발견되면 자동 `RegisterSubUV()` 호출 ([ResourceManager.cpp:508](../JSEngine/Source/Engine/Core/ResourceManager.cpp:508)) — `.png + .meta` 페어가 있으면 별도 코드 없이 등록됨

### prompt 4규칙 매칭

| 규칙 | 점수 | 근거 |
|------|------|------|
| (1) 가장 상류의 끊김 | ✅ 강 | atlas/SubUV 메타데이터 없이는 PS sample zero → discard → 화면 공백. Rotation(D)/Material(E)/Sort(F)는 모두 이 path에 dependent |
| (2) 변경 영역 좁음 | ✅ | 5 파일, +50~80 라인 / -10 라인. 신규 .h/.cpp 0개 → vcxproj 무변경 (silent bug §7-4 회피) |
| (3) silent bug 함정 비충돌 | ✅ | §7-4 회피(파일 합침). §7-5는 본 cycle이 case 본문 수정하므로 직접 위험 — `return true` 종결 유지 방법 명시. §7 신규 ζ/η 회피 방법 명시 |
| (4) use case 양쪽 공통 | ✅ | (2) 코드 spawn: `Module->SetSubUVName(N)` 한 줄로 atlas 지정. (3) 직렬화: FName UPROPERTY 자동, 로드 시 Serialize override가 CachedSubUV 자동 재구축 |

---

## 4. 변경 파일 목록

각 파일별 **현재 상태 → 변경 후 형태**, 추정 라인 변동, 의존 결정 번호.

### 4.1 `JSEngine/Source/Engine/Particle/ParticleTypes.h` (결정 3)

**현재** ([ParticleTypes.h:19-34](../JSEngine/Source/Engine/Particle/ParticleTypes.h:19)): `FBaseParticle` 13 멤버. SubUVIndex 자리 없음.

**변경 후**:
```cpp
struct FBaseParticle
{
    // ... 기존 13 멤버 ...
    int32 CollisionCount = 0;
    uint32 SubUVIndex = 0;   // 신규
};
```

- 추가 라인: +1
- 영향: `sizeof(FBaseParticle)` 변화는 0 또는 +4B (정렬 padding 흡수 가능). `UParticleEmitter::ParticleSize/Stride`는 `CacheEmitterModuleInfo()`가 `sizeof(FBaseParticle)`로 매 init마다 재산정 ([ParticleSystem.cpp:48](../JSEngine/Source/Engine/Particle/ParticleSystem.cpp:48)) — 자동 안전 (진단 §3.3)

### 4.2 `JSEngine/Source/Engine/Particle/ParticleModule.h` (변경 없음)

base 클래스 그대로 — Spawn / Update virtual hook 이미 존재.

### 4.3 `JSEngine/Source/Engine/Particle/ParticleModules.h` (결정 1 / 2 / 4 / 7)

**현재** ([ParticleModules.h:6-39](../JSEngine/Source/Engine/Particle/ParticleModules.h:6)):
- `UParticleModuleRequired`에 line 19 getter + line 35-36 UPROPERTY `SubUVName` 있음
- USubUVModule 없음

**변경 후**:

(a) `UParticleModuleRequired`에서 SubUVName 관련 2줄 **제거**:
```cpp
// 제거 (line 19):
const FName& GetSubUVName() const { return SubUVName; }

// 제거 (line 35-36):
UPROPERTY(DisplayName = "SubUV")
FName SubUVName;
```

(b) 파일 하단 (혹은 EventGenerator 뒤)에 USubUVModule UCLASS 신규 선언:
```cpp
class FTextureAtlasResource; // forward (헤더 상단 또는 forward 블록)

UCLASS()
class USubUVModule : public UParticleModule
{
public:
    GENERATED_BODY(USubUVModule, UParticleModule)

    USubUVModule();
    void Spawn(FParticleEmitterInstance* Owner, FBaseParticle& Particle, float SpawnTime) override;
    void Update(FParticleEmitterInstance* Owner, float DeltaTime) override;
    void Serialize(FArchive& Ar) override;
    void PostEditProperty(const char* PropertyName) override;

    void SetSubUVName(const FName& InName);
    const FName& GetSubUVName() const { return SubUVName; }
    const FTextureAtlasResource* GetCachedSubUV() const { return CachedSubUV; }

private:
    UPROPERTY(DisplayName = "SubUV")
    FName SubUVName;

    FTextureAtlasResource* CachedSubUV = nullptr; // ResourceManager 소유, 참조만
};
```

- 추정 라인 변동: -3 (RequiredModule) / +24 (USubUVModule)

### 4.4 `JSEngine/Source/Engine/Particle/ParticleModules.cpp` (결정 1 / 2 / 3 / 7)

**현재**: 9개 module 본문. USubUVModule 없음. RequiredModule 본문에는 SubUVName 사용처 없음 (Spawn 본문은 RelativeTime/Lifetime/Size/Color 초기화만, [ParticleModules.cpp:34-53](../JSEngine/Source/Engine/Particle/ParticleModules.cpp:34))

**변경 후**:

(a) RequiredModule 본문 변경 **없음** (SubUVName UPROPERTY는 헤더에서만 사용됐음).

(b) 파일 하단에 USubUVModule 본문 추가 — `UParticleModuleColor` 패턴 모방:
```cpp
// 생성자 — 결정 1/2
USubUVModule::USubUVModule()
{
    bSpawnModule  = true;  // SubUVIndex = 0 초기화
    bUpdateModule = true;  // RelativeTime → SubUVIndex 진행
}

// Spawn — 결정 3
void USubUVModule::Spawn(FParticleEmitterInstance*, FBaseParticle& Particle, float)
{
    Particle.SubUVIndex = 0;
}

// Update — 결정 3
void USubUVModule::Update(FParticleEmitterInstance* Owner, float)
{
    if (!CachedSubUV || !Owner) return;
    const uint32 TotalFrames = CachedSubUV->Columns * CachedSubUV->Rows;
    if (TotalFrames == 0) return;

    for (int32 i = 0; i < Owner->GetActiveParticleCount(); ++i)
    {
        FBaseParticle* P = Owner->GetParticle(i);
        if (!P) continue;
        const float Clamped = std::clamp(P->RelativeTime, 0.0f, 0.9999f);
        P->SubUVIndex = static_cast<uint32>(Clamped * TotalFrames) % TotalFrames;
    }
}

// Serialize override — η silent bug 회피 (USubUVComponent 패턴)
void USubUVModule::Serialize(FArchive& Ar)
{
    UParticleModule::Serialize(Ar);
    if (Ar.IsLoading())
    {
        SetSubUVName(SubUVName);
    }
}

// PostEditProperty — η silent bug 회피 (USubUVComponent 패턴)
void USubUVModule::PostEditProperty(const char* PropertyName)
{
    UParticleModule::PostEditProperty(PropertyName);
    if (PropertyName && strcmp(PropertyName, "SubUVName") == 0)
    {
        SetSubUVName(SubUVName);
    }
}

void USubUVModule::SetSubUVName(const FName& InName)
{
    SubUVName = InName;
    CachedSubUV = FResourceManager::Get().FindSubUV(InName);
}
```

(c) include 추가 (파일 상단):
```cpp
#include "Core/ResourceManager.h"
#include "Core/ResourceTypes.h"  // FTextureAtlasResource
#include <cstring>               // strcmp (이미 포함되어 있을 수 있음)
#include <algorithm>             // std::clamp
```

- 추정 라인 추가: +60

### 4.5 `JSEngine/Source/Engine/Particle/ParticleSystemComponent.cpp` (결정 3)

**현재** ([ParticleSystemComponent.cpp:217-223](../JSEngine/Source/Engine/Particle/ParticleSystemComponent.cpp:217)):
```cpp
FSpriteParticleInstanceData Data;
Data.Position   = Particle->Location;
Data.Size       = FVector2(Particle->Size.X, Particle->Size.Y);
Data.Color      = Particle->Color;
Data.Rotation   = Particle->Rotation;
Data.SubUVIndex = 0; // TODO: USubUVModule 포팅 후 페이로드에서 추출
```

**변경 후**:
```cpp
Data.SubUVIndex = Particle->SubUVIndex;
```

TODO 주석 제거. include는 ParticleTypes.h 이미 포함되어 있어 추가 없음.

- 라인 변동: -1 (TODO 주석 줄 제거) / +1 → 순 0

### 4.6 `JSEngine/Source/Engine/Render/Scene/PrimitiveDrawCommandBuilder.cpp` (결정 1 / 5)

**현재** ([PrimitiveDrawCommandBuilder.cpp:535-573](../JSEngine/Source/Engine/Render/Scene/PrimitiveDrawCommandBuilder.cpp:535)): `case EPT_ParticleSystem:` 본문, line 566-568에서 `Cmd.ParticleTexture = nullptr; SubUVColumns = 1; SubUVRows = 1;` 하드코딩.

**변경 후** — emitter 루프 진입 직전 (또는 첫 emitter 처리 시점) USubUVModule **emitter당 1회 linear search** → frame 내 reuse:

```cpp
// emitter 루프 안, Cmd 채우기 직전
UParticleLODLevel* LOD = Instance->GetCurrentLODLevel();
const USubUVModule* SubUV = nullptr;
if (LOD)
{
    for (UParticleModule* M : LOD->GetModules())
    {
        if (USubUVModule* S = Cast<USubUVModule>(M))
        {
            SubUV = S;
            break;  // emitter당 1개만 허용 (LODLevel.Modules 순회 1회)
        }
    }
}
const FTextureAtlasResource* Atlas = SubUV ? SubUV->GetCachedSubUV() : nullptr;

Cmd.ParticleTexture      = (Atlas && Atlas->IsLoaded()) ? Atlas->Texture : nullptr;
Cmd.ParticleSubUVColumns = Atlas ? Atlas->Columns : 1;
Cmd.ParticleSubUVRows    = Atlas ? Atlas->Rows    : 1;

// ... 기존 Cmd 발행 ...
// case 출구는 기존 `return true;` 종결 유지 (silent bug §7-5)
```

- include 추가: `#include "Particle/ParticleModules.h"` (USubUVModule 타입), `#include "Core/ResourceTypes.h"` (FTextureAtlasResource)
- 라인 변동: -3 (하드코딩 3줄 제거) / +18 (linear search 블록) → 순 +15

### 4.7 vcxproj / vcxproj.filters

**변경 없음** — 신규 .h/.cpp 0개 (결정 4 — ParticleModules에 합침)

### 변경 안 함 (명시)

- `ParticleModule.h` (base)
- `ParticleEmitterInstance.h/.cpp` (Tick은 그대로 SpawnModules/UpdateModules 순회 → USubUVModule.Spawn/Update 자동 호출)
- `ParticleSystem.h/.cpp` (`CacheModuleLists()` 무변경 → silent bug ζ 회피, 결정 5)
- `RenderCommand.h` (필드 이미 존재)
- `ParticleRenderPass.h/.cpp` (GPU 경로 이미 동작)
- `SpriteParticle.hlsl` (셰이더 무변경)
- `VertexFactoryTypes.h` / `VertexTypes.h` (instance layout 이미 SubUVIndex 슬롯 보유)

---

## 5. 단계별 작업 체크리스트

의존 그래프: `FBaseParticle` → USubUVModule 본문 → `BuildSpriteInstanceData` → builder. 각 step 후 `Debug|x64` 빌드 통과 단위로 commit 단위 분할 가능.

- [ ] **Step 1**: `FBaseParticle::SubUVIndex: uint32 = 0` 추가 ([ParticleTypes.h:19-34](../JSEngine/Source/Engine/Particle/ParticleTypes.h:19))
  - 검증: 빌드 통과. `sizeof(FBaseParticle)` 변화는 자동 재산정으로 흡수.

- [ ] **Step 2**: USubUVModule **클래스 선언만** 추가 ([ParticleModules.h](../JSEngine/Source/Engine/Particle/ParticleModules.h) 하단). 본문 비어 있음 (생성자만).
  - 검증: 빌드 통과 (UCLASS reflection 등록 확인).

- [ ] **Step 3**: USubUVModule **본문 추가** ([ParticleModules.cpp](../JSEngine/Source/Engine/Particle/ParticleModules.cpp)) — 생성자 (`bSpawnModule = bUpdateModule = true`), Spawn (`Particle.SubUVIndex = 0`), Update (RelativeTime → SubUVIndex 진행), Serialize/PostEditProperty override (CachedSubUV 자동 재구축, η 회피), SetSubUVName 본문
  - 검증: 빌드 통과. Tick에서 `OutputDebugString`으로 `Particle->SubUVIndex` 값 시간 진행 확인 (atlas 셋업 전이라도 SubUVIndex 자체는 0 고정).

- [ ] **Step 4**: `UParticleModuleRequired::SubUVName` UPROPERTY + getter **제거** ([ParticleModules.h:19, 35-36](../JSEngine/Source/Engine/Particle/ParticleModules.h:19))
  - 검증: 빌드 통과. 외부 호출처 0건 (grep 확인 완료).
  - 직렬화 영향: §6.6 참조 (기존 .particlesystem 파일 마이그레이션).

- [ ] **Step 5**: `BuildSpriteInstanceData` 의 `Data.SubUVIndex = 0` → `Data.SubUVIndex = Particle->SubUVIndex` ([ParticleSystemComponent.cpp:222](../JSEngine/Source/Engine/Particle/ParticleSystemComponent.cpp:222))
  - 검증: 빌드 통과.

- [ ] **Step 6**: `PrimitiveDrawCommandBuilder.cpp` 의 `case EPT_ParticleSystem` 본문에서 USubUVModule linear search + atlas 조회 → Cmd 3 필드 채움 ([PrimitiveDrawCommandBuilder.cpp:566-568](../JSEngine/Source/Engine/Render/Scene/PrimitiveDrawCommandBuilder.cpp:566))
  - 검증: 빌드 통과. case 출구 `return true;` 종결 유지 확인 (silent bug §7-5).

- [ ] **Step 7**: 코드 spawn 셋업 + 화면 확인 (§6.2)

권장 commit 단위: Step 1-2 / Step 3 / Step 4 / Step 5-6 / Step 7 (5 commit), 또는 Step 1-6 일괄 / Step 7 (2 commit). 본 plan은 단일 cycle이므로 일괄 1 commit도 허용 (§8 참조).

---

## 6. 검증 시퀀스

### 6.1 빌드 검증
- [ ] `Debug|x64` 빌드 통과 — 경고 0, 오류 0
- [ ] Cycle 1의 `HashVertexLayout` 영향 없음 (vertex layout 변경 없음)
- [ ] vcxproj/filters 외부 자동 갱신 없음 (신규 파일 0개)

### 6.2 코드 spawn 검증 (use case 2)
- [ ] atlas 등록: `JSEngine/Asset/plasma.png` + `plasma.meta` 페어 존재 시 자동 `RegisterSubUV` 됨 (기존 `ResourceManager.cpp:508` 경로). meta가 없다면 코드 명시:
  ```cpp
  FResourceManager::Get().RegisterSubUV(FName("plasma"), "Asset/plasma.png", Columns, Rows);
  ```
- [ ] PS 셋업 (예: `BeginPlay` 또는 임시 helper):
  ```cpp
  UParticleSystem* PS = UParticleSystem::CreateDefaultSpriteSystem();
  USubUVModule* SubUV = UObjectManager::Get().CreateObject<USubUVModule>();
  SubUV->SetSubUVName(FName("plasma"));
  PS->Emitters[0]->LODLevels[0]->Modules.push_back(SubUV);
  PS->Emitters[0]->LODLevels[0]->CacheModuleLists();  // SubUVModule이 SpawnModules/UpdateModules에 push되도록
  SetTemplate(PS);
  ```
- [ ] 액터 배치 → 실행
- [ ] **확인 사항**: 화면에 sprite 다수가 atlas 첫 frame (cell 0,0) → 시간 진행 따라 다음 cell 순환 재생. 안 보이면 §6.5 분기점 좁히기.

### 6.3 디버거 watch
| 위치 | 값 | 의미 |
|------|----|----|
| `USubUVModule::Update` 안, `P->SubUVIndex` | 0 → 1 → 2 → ... → TotalFrames-1 → 0 wrap | per-particle 진행 |
| `BuildSpriteInstanceData` 안, `Data.SubUVIndex` | `Particle->SubUVIndex`와 일치 | step 5 연결 확인 |
| `PrimitiveDrawCommandBuilder` builder 출구, `Cmd.ParticleTexture` | `!= nullptr` (atlas 잡힌 경우) | step 6 연결 확인 |
| 동, `Cmd.ParticleSubUVColumns / Rows` | atlas 등록값 | atlas 매핑 확인 |
| `USubUVModule::SetSubUVName` 직후, `CachedSubUV` | `!= nullptr` | η 회피 확인 |

### 6.4 RenderDoc (선택)
- [ ] `t0` SRV 슬롯에 atlas 텍스처 binding (PS 단계)
- [ ] `b8` cbuffer에 `SubUVColumns / SubUVRows` 정확한 값 (VS 단계)
- [ ] instance buffer 의 `SUBUV_INDEX` slot이 frame별로 변화하는지

### 6.5 화면 안 보일 때 분기점 좁히기

진단 §7.5 표 + USubUVModule 단계 추가:

| 단계 | 위치 | 확인 |
|------|------|------|
| 1 | `USubUVModule::SetSubUVName` | `CachedSubUV != nullptr` (nullptr이면 atlas 미등록 — RegisterSubUV 또는 .meta 누락) |
| 2 | `CacheModuleLists()` 후 LOD | USubUVModule이 `SpawnModules` + `UpdateModules` 양쪽에 push 됐는가 (둘 다 true이므로) |
| 3 | `USubUVModule::Update` 진입 | 호출되는가 (호출 안 되면 step 2 실패) |
| 4 | `Particle->SubUVIndex` | 시간 따라 갱신되는가 |
| 5 | `Data.SubUVIndex` (BuildSpriteInstanceData) | `Particle->SubUVIndex`와 동일한가 |
| 6 | `Cmd.ParticleTexture` (builder 출구) | nullptr이면 linear search 실패 — LOD nullptr, Cast 실패, CachedSubUV nullptr 중 하나 |
| 7 | `Cmd.ParticleSubUVColumns / Rows` | atlas 등록값과 일치? 1로 떨어졌다면 Atlas nullptr |
| 8 | `ParticleRenderPass.cpp:168` `TextureSRV` | nullptr이면 Cmd.ParticleTexture nullptr 또는 GetSRV() nullptr |
| 9 | RenderDoc PS sample | 검은 픽셀이면 atlas 자체가 검정 또는 UV 좌표 오류 |
| 10 | alpha discard | 화면 안 보임 ↔ atlas alpha 채널 0 가능성 |

### 6.6 직렬화 검증 (use case 3)
- [ ] **SaveParticleSystem**: SubUVModule 포함된 PS asset 저장 → JSON에 USubUVModule UCLASS 노드 + `SubUVName` FName 필드 직렬화 확인
- [ ] **LoadParticleSystem**: 위 JSON 로드 → USubUVModule 객체 복원 → `Serialize(Ar)` IsLoading 분기 → `SetSubUVName(SubUVName)` 자동 호출 → `CachedSubUV` 자동 재구축. 디버거로 로드 직후 `CachedSubUV != nullptr` 확인 (η 회피 검증)
- [ ] **기존 .particlesystem 파일 마이그레이션**:
  - `UParticleModuleRequired::SubUVName` 제거 → 기존 직렬화 파일에 `RequiredModule` 노드 안에 `SubUVName` 필드가 있을 때 로드 동작 확인
  - 예상: reflection 기반 직렬화는 unknown property를 무시하는 것이 일반적이나, ResourceManager 본문에서 strict 모드라면 경고/에러 가능 — 빌드 후 기존 PS asset 로드 1건 실측 필요
  - 영향 클 시: 본 cycle에서 `SubUVName` 필드를 `RequiredModule`에 transient로 1 cycle 유지 + 마이그레이션 helper 추가 검토 (현재는 비포함)

---

## 7. 회피해야 할 silent bug

| § | 함정 | 본 cycle 위험 | 회피 방법 |
|---|------|--------------|----------|
| 7-1 | `FVertexFactoryRegistry::Get` default StaticMeshDesc fallback | ❌ 무관 | (`EVertexFactoryType` 추가 없음) |
| 7-2 | `HashVertexLayout` 동기화 | ❌ 무관 | (vertex layout 변경 없음) |
| 7-3 | `PickPasses[]`에 Particle 추가 금지 | ❌ 무관 | (안 건드림) |
| **7-4** | `vcxproj` 외부 수정 시 VS 덮어쓰기 | ✅ **회피** | 결정 4 — 신규 .h/.cpp 0개 → vcxproj 무변경 |
| **7-5** | `EPT_ParticleSystem` case `return true` 종결 | ⚠️ **직접 위험** | Step 6 수정 후 case 출구가 `return true`로 종결되는지 명시 확인. fall-through로 default 진입 시 다른 EPT가 처리됨 (silent bug) |
| 7-6 | `EditorOverlayCollector` SupportsOutline | ❌ 무관 | (안 건드림) |
| 7-7 | `PassBatchers[Particle]` 미등록 | ❌ 무관 | (안 건드림) |
| 진단 §9 β | atlas nullptr 시 PS sample zero → discard | ⚠️ 잔존 함정 | atlas 등록 누락 또는 `SetSubUVName(잘못된이름)` 시 검은 화면 — 본 cycle 범위 밖. fallback white texture는 후속 cycle |
| 진단 §9 ζ | `CacheModuleLists` 잘못된 continue | ✅ **회피** | 결정 5 — `CacheModuleLists()` 수정 안 함. USubUVModule은 일반 SpawnModules/UpdateModules에 자연 push됨 (둘 다 true) |
| 진단 §9 η | `CachedSubUV` 자동 재구축 누락 | ⚠️ **직접 위험** | Step 3에서 Serialize override의 IsLoading 분기 + PostEditProperty 둘 다 구현 (USubUVComponent 패턴 그대로). §6.3 디버거 watch 검증 |
| 진단 §7 후보 γ | `UParticleModuleSpawn::bSpawnModule = false` 의도된 분리 | ❌ 무관 | USubUVModule은 별도 슬롯 캐시 없음, 일반 module 패턴 |

### Step 6 회피 절차 (§7-5)
1. `case EPT_ParticleSystem:` 본문 안에서만 작업
2. emitter 루프 종료 후 기존 `return true;` 위치 유지
3. 새로 추가하는 linear search/Cmd 채움 코드를 case 본문 안에 위치시키고, **case 출구 전에** 모든 로직 종결
4. 빌드 후 코드 spawn 검증에서 화면에 sprite 보이면 case 정상 종결 확인됨 (fall-through라면 default가 ParticleSystem을 다른 EPT로 처리하지 못해 안 보임)

---

## 8. Commit 후보 메시지

### 단일 cycle 일괄 commit
```
[ParticleRender] Cycle 7: Introduce USubUVModule for sprite atlas playback

- New USubUVModule UCLASS (bSpawnModule=bUpdateModule=true) added to
  ParticleModules.{h,cpp}. FName SubUVName UPROPERTY + non-UPROPERTY
  FTextureAtlasResource* CachedSubUV cache (USubUVComponent pattern).
- Serialize / PostEditProperty overrides auto-rebuild CachedSubUV on
  load and on property edit (η silent bug avoidance).
- FBaseParticle gains uint32 SubUVIndex = 0. ParticleSize/Stride auto
  recomputed by CacheEmitterModuleInfo (no manual sync).
- UParticleModuleRequired::SubUVName UPROPERTY + getter removed,
  ownership transferred to USubUVModule. No external callers (grep 0).
- BuildSpriteInstanceData now reads Particle->SubUVIndex instead of 0.
- PrimitiveDrawCommandBuilder EPT_ParticleSystem case performs a one-
  pass linear search over LOD->GetModules() per emitter, looks up the
  atlas resource via FResourceManager, and fills Cmd.ParticleTexture /
  SubUVColumns / SubUVRows. Existing `return true` terminator preserved
  (silent bug §7-5).
- No new files, no vcxproj change, no shader change (Cycle 3 GPU path
  already consumes SUBUV_INDEX slot + b8 columns/rows).

Verification:
- Build Debug|x64, 0 warnings, 0 errors.
- Register atlas (e.g. plasma.png + .meta) → SetSubUVName on module →
  sprites cycle through atlas grid over time.
- Load existing serialized .particlesystem asset → CachedSubUV rebuilds
  via Serialize IsLoading branch.

Out of scope (next cycle candidates):
- InterpolationMethod enum (Linear/Random/Curve)
- Atlas nullptr fallback (white 1x1 texture, β)
- Rotation/RotationRate dynamic update (D)
- Material binding (E), distance sort (F)
- Mesh/Beam/Ribbon TypeData
```

### Step별 분할 commit (선택)
```
1) Particle: add FBaseParticle::SubUVIndex
2) Particle: introduce USubUVModule (skeleton)
3) Particle: USubUVModule Spawn/Update/Serialize/PostEditProperty bodies
4) Particle: remove SubUVName from UParticleModuleRequired
5) ParticleRender: feed Particle->SubUVIndex into FSpriteParticleInstanceData
6) ParticleRender: wire USubUVModule → Cmd atlas/SubUV grid in EPT_ParticleSystem
```

---

## 9. 추정 규모

| 항목 | 추정 |
|------|------|
| 변경 파일 수 | 5 (`ParticleTypes.h` / `ParticleModules.h` / `ParticleModules.cpp` / `ParticleSystemComponent.cpp` / `PrimitiveDrawCommandBuilder.cpp`) |
| 추가 라인 | +90 ~ +110 |
| 삭제 라인 | -7 (`RequiredModule::SubUVName` 2줄 + getter 1줄 + 하드코딩 3줄 + TODO 1줄) |
| 신규 .h/.cpp | 0 |
| vcxproj 변경 | 없음 |
| 셰이더 변경 | 없음 |
| 의존 cycle | 없음 (Cycle 1–5 인프라 + 진단 §0의 atlas 인프라 위) |
| 검증 시간 추정 | 45분 (빌드 + 코드 spawn 셋업 + 화면 확인 + 직렬화 라운드트립) |

---

## 10. 사용자 승인 요청

본 plan 대로 구현 cycle 진입해도 되는지 확인 부탁드립니다.

### 대안 / 조정 후보 (사용자 선택 가능)
- **(대안 1)** Step 4 (RequiredModule.SubUVName 제거)를 본 cycle에서 제외 → SubUVName을 RequiredModule에 transient로 1 cycle 유지하여 기존 .particlesystem asset 마이그레이션 무영향. 다음 cycle에서 제거. (현재 plan은 결정 7대로 본 cycle에서 제거.)
- **(대안 2)** Step 7의 atlas 등록 부분에서 코드 명시 `RegisterSubUV` 호출을 빌드 검증 helper로 임시 추가 (`.meta` 페어 없는 환경 대비). 정리는 다음 commit.
- **(대안 3)** Step 6의 linear search를 진단 §6.6 의사 코드 그대로 (`SubUV->IsEnabled()` 가드 포함)로 채택할지 — 현재 plan은 base `UParticleModule::bEnabled` 필드 가드를 linear search 단계가 아닌 Update/Spawn 본문에서 처리하도록 단순화. 명시 가드 원하시면 builder에 `&& SubUV->IsEnabled()` 한 줄 추가.
- **(조정)** 단일 commit vs 6개 분할 commit 선택 (§8).

### 검토 체크리스트 (prompt 명시 점검 항목)
- [x] §4 변경 파일 목록이 7개 결정을 모두 반영 (결정 1: §4.3/4.4 / 결정 2: §4.4 생성자 / 결정 3: §4.1/4.4/4.5 / 결정 4: §4.7 / 결정 5: §4.6 / 결정 6: §2 제외 명시 / 결정 7: §4.3/4.4)
- [x] §5 step 순서 의존 그래프 정합 (FBaseParticle → USubUVModule → RequiredModule 정리 → BuildSpriteInstanceData → builder)
- [x] §6 검증 시퀀스가 use case (2) 코드 spawn + (3) 직렬화 양쪽 다룸 (§6.2 + §6.6)
- [x] §7 silent bug §7-5 (EPT_ParticleSystem return true) 회피 방법 구체 (§7 표 + Step 6 회피 절차)
- [x] §7 ζ / η 신규 함정 처리 plan 반영 (§7 표)
- [x] 결정 7 (RequiredModule.SubUVName 제거) 영향처 grep 확인 — 외부 호출처 0건 (`GetSubUVName` 호출은 USubUVComponent 자체뿐), §4.3 / §4.4 반영
- [x] 기존 .particlesystem 마이그레이션 영향 §6.6 + §10 대안 1에 언급
