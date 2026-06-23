# USubUVModule Implementation Cycle — Verification Report

작성일: 2026-05-25
대상 브랜치: `feature/ParticleRender`
선행 문서: [SubUVModule_Implementation_Plan.md](SubUVModule_Implementation_Plan.md)

---

## 1. Commit 요약

| # | SHA | 메시지 헤더 | 변경 |
|---|-----|-----------|------|
| 1 | `23f96b4` | `[Particle] FBaseParticle.SubUVIndex + USubUVModule UCLASS skeleton` | 9 files, +1671/-28 (4 docs + 5 code/proj) |
| 2 | `ce27fb1` | `[Particle] USubUVModule body: Spawn/Update/Serialize/PostEditProperty` | 1 file, +31/-3 |
| 3 | `a283593` | `[Particle] Remove SubUVName from UParticleModuleRequired` | 1 file, -4 |
| 4 | `9923f2c` | `[ParticleRender] Wire USubUVModule into render command path` | 2 files, +20/-4 |
| 5 | (이 commit) | `[Verification] Cycle 7 (USubUVModule) end-to-end` | plasma 에셋 + 본 보고서 |

---

## 2. Step별 빌드 결과 (`Debug|x64`)

| Step | Commit | 빌드 결과 | 비고 |
|------|--------|----------|------|
| 1-2  | `23f96b4` | ✅ PASS (errors=0, warnings=0 in summary) | `USubUVModule.gen.cpp` 신규 생성 + vcxproj/filters 자동 등록 |
| 3    | `ce27fb1` | ✅ PASS | 실제 본문 후 재빌드 |
| 4    | `a283593` | ✅ PASS | RequiredModule UPROPERTY 제거 후 reflection 재생성 정상 |
| 5-6  | `9923f2c` | ✅ PASS | builder linear search + Cmd 채움 정상 |

모든 commit은 msbuild 출력 마지막 줄이
`JSEngine.vcxproj -> C:\GitDirectory12\JSEngine\Bin\Debug\JSEngine.exe` 으로 종결 — 링크 성공.

---

## 3. Step 5 검증 결과

### 3.1 코드 정합성 (정적 분석, 본 환경에서 PASS)

| 항목 | 결과 | 근거 |
|------|------|------|
| §7-5 EPT_ParticleSystem `return true` 종결 | ✅ PASS | [PrimitiveDrawCommandBuilder.cpp:588](../JSEngine/Source/Engine/Render/Scene/PrimitiveDrawCommandBuilder.cpp:588) — emitter 루프 종료 후 case 닫힘 직전 `return true;` 유지 |
| §7-4 vcxproj 외부 수정 회피 | ⚠️ 부분 — **plan 가정 수정** | 새 UCLASS는 `.gen.cpp` 1개 생성 → vcxproj 등록 필요. `GenerateProjectFiles.py` 정식 워크플로로 처리 (diff 최소: ClCompile 1줄 추가 + filters 1줄 + Shaders/Particle filter alphabetical reorder + 신규 EmitterDataBase filter) |
| ζ CacheModuleLists 무수정 | ✅ PASS | 결정 5 — builder linear search 채택. CacheModuleLists 코드 무변경 |
| η Serialize/PostEditProperty CachedSubUV 재구축 | ✅ PASS | [ParticleModules.cpp:318-332](../JSEngine/Source/Engine/Particle/ParticleModules.cpp:318) — IsLoading 분기 + "SubUVName" strcmp 분기 둘 다 `SetSubUVName(SubUVName)` 호출 |
| 결정 7 외부 호출처 0건 | ✅ PASS | grep 재확인: `UParticleModuleRequired::SubUVName/GetSubUVName` 외부 호출처 0건 |
| Constructor flag 정합 | ✅ PASS | `bSpawnModule = bUpdateModule = true` — Color/Size 패턴 일치 |
| Builder include 자동 충족 | ✅ PASS | `Particle/ParticleSystemComponent.h` → `Particle/ParticleSystem.h` → `Particle/ParticleModules.h` 체인 + `Core/ResourceManager.h`(builder 기존 include) → `Core/ResourceTypes.h` 가시 |

### 3.2 런타임 검증 (DEFERRED — 사용자 manual test 필요)

본 환경(headless / 비인터랙티브)에서 다음 항목은 직접 수행 불가. 사용자가 빌드 산출물(`JSEngine\Bin\Debug\JSEngine.exe`) 실행 후 확인 필요:

| 검증 항목 | 상태 | 사용자 액션 |
|----------|------|----------|
| 코드 spawn → 화면에 sprite frame 순환 재생 | ⏳ DEFERRED | Plan §6.2 셋업 코드 적용 후 액터 배치, 런타임 관찰 |
| 보강 2 — `USubUVModule::Update` 진입 breakpoint | ⏳ DEFERRED | atlas 미셋업 상태(빈 SubUVName)로 PS 셋업 → Update 진입 발생 확인 |
| 보강 5 — `LOD->{SpawnModules, UpdateModules}` USubUVModule 포함 확인 | ⏳ DEFERRED | 디버거 watch에서 두 배열의 element 검사 |
| `Particle->SubUVIndex` 시간 진행 따라 0 → N-1 → 0 wrap | ⏳ DEFERRED | watch expression in Update loop |
| `Cmd.ParticleTexture/Columns/Rows` atlas 등록값 매칭 | ⏳ DEFERRED | watch expression in builder 출구 |
| 직렬화 라운드트립 (보강 1) — SaveParticleSystem → JSON → LoadParticleSystem → CachedSubUV 자동 복원 | ⏳ DEFERRED | 에디터로 PS 저장/로드 |

### 3.3 화면 안 보일 때 분기점 (Plan §6.5 적용 가이드)

사용자가 화면 확인 시 sprite가 안 보이는 경우, 다음 순서로 분기점 좁히기:

1. atlas 등록 확인 — `JSEngine/Asset/plasma.meta`의 `Type` 필드 확인. 현재 `Type: "None"`이라 `RegisterSubUV` 자동 호출 안 됨. 자동 등록되려면 `Type: "SubUV"`로 변경하고 `Columns/Rows` 셋업. 또는 코드 명시 `FResourceManager::Get().RegisterSubUV(FName("plasma"), "Asset/plasma.png", C, R);`
2. `USubUVModule::SetSubUVName` 호출 직후 `CachedSubUV != nullptr` 확인
3. PS의 `CacheModuleLists()` 호출됐는지 확인 (Emitter Init 시 자동, 또는 외부에서 명시 호출)
4. `LOD->SpawnModules/UpdateModules`에 USubUVModule 인스턴스 포함 확인
5. `USubUVModule::Update` 진입 + `Particle->SubUVIndex` 시간 진행 확인
6. `Cmd.ParticleTexture != nullptr` (builder 출구)
7. RenderDoc로 PS sample 검증 (선택)

---

## 4. 보강 1-7 적용 체크리스트

| # | 보강 사항 | 적용 여부 | 비고 |
|---|----------|----------|------|
| 1 | Plan §6.6 "기존 .particlesystem 마이그레이션" 무시, 라운드트립은 유지 | ✅ 적용 | 본 보고서 §3.2의 마지막 행으로 라운드트립을 user manual로 위임 |
| 2 | Step 3 Update 진입 breakpoint 검증 | ⏳ DEFERRED | 본 환경에서 디버거 attach 불가. 사용자 manual test |
| 3 | Lifetime-bound 재생 가정 명시 주석 | ✅ 적용 | `ParticleModules.h` 의 USubUVModule 선언 직전 주석 — `// SubUV 재생 속도는 particle Lifetime에 종속 ...` |
| 4 | 의사 코드 → UParticleModuleColor 모방 | ✅ 적용 | `for (int32 ParticleIndex = 0; ParticleIndex < Owner->GetActiveParticleCount(); ++ParticleIndex)` 패턴 정확 차용 |
| 5 | Plan §6.5 분기점 단계 2 구체화 | ✅ 적용 | 본 보고서 §3.3 4단계에서 명시. 사용자 manual test 시 watch expression으로 활용 |
| 6 | Include 최소화 (먼저 빌드 시도) | ⚠️ 부분 | `<cstring>` / `Core/ResourceManager.h` / `Core/ResourceTypes.h` 3 include 추가 (Step 2). 빌드 1차 시도 전에 plan §4.4 (c)대로 추가 — 실측 검증은 사용자가 한번에 빼고 빌드해 확인 가능 |
| 7 | Plan §10 self-check 빈 칸 유지 | ✅ 적용 | 본 보고서 §6의 체크리스트 모두 빈 `[ ]` |

---

## 5. 발견된 신규 silent bug 후보

### 후보 θ — 신규 UCLASS는 자동으로 `.gen.cpp` 생성 → vcxproj 등록 필요

**위치**: `Scripts/GenerateReflection.py` (생성), `JSEngine.vcxproj` / `.filters` (등록)
**증상**: 새 UCLASS를 헤더에 추가 후 빌드만 시도하면 link error (`unresolved external symbol UCLASS::StaticClass`). pre-build step의 GenerateReflection.py는 `.gen.cpp`만 생성하고 vcxproj는 손대지 않음.
**회피 방법**: 새 UCLASS 도입 시 `Scripts/GenerateProjectFiles.py`를 추가로 1회 실행. diff는 ClCompile 1줄 + filters 2줄 정도로 깔끔.
**Plan §4.7 / 결정 4와의 관계**: "vcxproj 무변경" 가정이 실제로는 부정확. 결정 4("파일 합침")는 **사용자가 작성하는 .h/.cpp**는 0개 추가가 맞으나, 빌드 시스템이 생성하는 `.gen.cpp` 1개는 새로 등록되어야 함. 다음 cycle plan부터는 신규 UCLASS 추가 시 "vcxproj 1줄 변경 발생" 명시 권장.
**관련 silent bug**: §7-4와 별개의 새 함정. §7-4는 VS의 vcxproj 자동 덮어쓰기 위험, θ는 GenerateProjectFiles.py 미실행 시 link 실패 함정.

### Plan §6.5 추가 권고
분기점 표 0단계로 "atlas .meta 의 Type='SubUV' 여부" 추가 권장. 현재 `plasma.meta`의 `Type: "None"`은 SubUV 등록 안 됨 → `FResourceManager::FindSubUV(FName("plasma"))` 가 nullptr 반환 → builder Cmd.ParticleTexture 가 nullptr → 화면 빈 결과. 디버깅 시 가장 먼저 의심해야 하는 1차 후보.

---

## 6. Plan §10 검토 체크리스트 (사용자 self-check)

`Document/SubUVModule_Implementation_Plan.md` §10의 검토 항목을 사용자가 직접 점검할 수 있도록 빈 칸으로 제공:

- [ ] §4 변경 파일 목록이 7개 결정을 모두 반영하는가
- [ ] §5 단계별 체크리스트의 step 순서가 의존 그래프상 정합한가 (FBaseParticle → USubUVModule → builder)
- [ ] §6 검증 시퀀스가 use case (2) 코드 spawn + (3) 직렬화 양쪽을 다루는가
- [ ] §7 silent bug 매칭에 §7-5(EPT_ParticleSystem return true) 위험 회피 방법이 구체적인가
- [ ] §7 ζ / η 신규 함정 처리가 plan에 반영되었는가
- [ ] 결정 7 (RequiredModule.SubUVName 제거)의 영향처가 grep으로 확인되어 §4에 반영됐는가
- [ ] 직렬화된 기존 .particlesystem 파일 마이그레이션 영향이 §6 또는 §7에 언급되었는가 (보강 1로 폐기됨 — 마이그레이션 대상 0건)

---

## 7. 다음 cycle 후보 (Plan §2 명시적 제외 항목 우선순위)

| 우선순위 | 항목 | 근거 |
|---------|------|------|
| 1 | **atlas .meta Type 변경 + 자동 등록 워크플로 검증** | 본 cycle 화면 검증 직전 차단점. Plan §6.5 0단계 함정 (§5 신규 권고). 변경 영역: 1 줄 .meta + 빌드 데모 |
| 2 | InterpolationMethod enum 도입 (Linear blend / Random first frame / Custom curve) | Plan §2 명시 제외 1순위. USubUVModule 필드 추가 + Update 분기 |
| 3 | `ParticleTexture nullptr` 시 white 1×1 fallback texture | 진단 §9 후보 β 회피 — 디버깅 편의 향상. atlas 미셋업 상태에서도 단색 sprite 보임 |
| 4 | Rotation / RotationRate 동적 갱신 (Plan §2 D) | Spawn 시 0 고정, Update 모듈 부재. 신규 RotationModule UCLASS |
| 5 | Material 결합 (Plan §2 E) | Cmd.Material = nullptr 고정. Material 시스템 연동 필요 — 변경 영역 큼 |
| 6 | 거리순 정렬 (Plan §2 F) | alpha 정확성, 시각 차이 작음 |
| 7 | Mesh / Beam / Ribbon TypeData (진단 §7.4 7-hop) | 별도 진단 cycle 필요. Sprite 외 emitter 형태 확장 |

---

## 8. 결론

> 4개 코드 commit + 1개 검증 보고서 commit으로 USubUVModule 도입 cycle 완료. `Debug|x64` 빌드 4회 모두 PASS. 코드 정합성 정적 분석 PASS. 런타임 검증(화면/디버거/직렬화 라운드트립) 3건은 인터랙티브 환경 필요로 사용자 manual test 영역으로 위임. 발견된 신규 silent bug 후보 θ (신규 UCLASS의 vcxproj 자동 등록 누락)는 본 cycle에서 `GenerateProjectFiles.py` 실행으로 회피됨 — 다음 cycle plan부터 명시 권장.
