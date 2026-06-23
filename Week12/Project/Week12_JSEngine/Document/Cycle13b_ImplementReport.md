# Cycle 13b (Beam Emitter — Noise) 구현 결과 보고서

**작성일**: 2026-05-26
**대상 브랜치**: `feature/ParticleRender`
**모드**: implement 완료 (빌드 검증 통과 — 오류/경고 0)
**선행 문서**:
- [Cycle13a_ImplementReport.md](Cycle13a_ImplementReport.md) — Beam 본체 (Noise 제외)
- [Cycle13_ReDiagnose.md](Cycle13_ReDiagnose.md) — Beam 전체 진단
- [Cycle12_ImplementReport.md](Cycle12_ImplementReport.md) — Ribbon 패턴 / 디버그 분리 원칙

---

## §0 한 줄 요약

> Cycle 13a (Beam 본체) 위에 per-particle 영구 NoiseSamples + Beam-local 좌표 perturbation 구현 완료. Debug x64 빌드 통과 (**오류 0, 경고 0**). silent bug 7건 (위험 1/5/6/7/8/9/11) 모두 명시 방어. random source cascade 의 actual choice = **local `std::mt19937`** (FEngineRandom singleton 의 SetSeed 전역 race 회피). container 자동 가산 패턴 **네 번째 실측 검증** (Beam-13b payload = 100B, Stride = **208B**). 13c 이관 1건 (LineBatcher 디버그 시각화). 인게임 verify 만 남음.

---

## §1 적용된 사용자 결정 (lock-in)

| 분기 | 옵션 | 코드 위치 |
| --- | --- | --- |
| 1 (Noise 데이터 모델) | B-2 — Per-particle 영구, `BeamNoiseMaxFrequency=8` 컴파일 타임 max | [ParticleBeamTypes.h:10](../JSEngine/Source/Engine/Particle/ParticleBeamTypes.h:10) (상수) + [ParticleBeamTypes.h:24](../JSEngine/Source/Engine/Particle/ParticleBeamTypes.h:24) (payload 멤버) + [ParticleBeamEmitterInstance.cpp:125-135](../JSEngine/Source/Engine/Particle/ParticleBeamEmitterInstance.cpp:125) (Spawn hook) |
| 2 (Frequency UPROPERTY) | A — Default=4, Min=1, Max=`BeamNoiseMaxFrequency`=8 | [ParticleModuleBeamNoise.h:36-37](../JSEngine/Source/Engine/Particle/ParticleModuleBeamNoise.h:36) |
| 3 (Noise 좌표) | B — Beam-local (Tangent + Perp1 + Perp2) | [ParticleBeamEmitterInstance.cpp:37-50](../JSEngine/Source/Engine/Particle/ParticleBeamEmitterInstance.cpp:37) (ComputeBeamLocalAxes) + [cpp:265-269](../JSEngine/Source/Engine/Particle/ParticleBeamEmitterInstance.cpp:265) (WorldOffset 계산) |
| 4 (bTargetNoise UPROPERTY) | A — Default=false | [ParticleModuleBeamNoise.h:44-45](../JSEngine/Source/Engine/Particle/ParticleModuleBeamNoise.h:44) + [ParticleBeamEmitterInstance.cpp:238-239](../JSEngine/Source/Engine/Particle/ParticleBeamEmitterInstance.cpp:238) (분기 조건) |
| 5 (LineBatcher 디버그) | B — 13c 분리 | 본 cycle 미구현 — §11 13c 이관 항목 list |
| 6 (random source) | A — 본 엔진 우선, std::mt19937 fallback | **actual choice = std::mt19937 (cascade fallback)**. `FEngineRandom` 존재하나 singleton + 전역 `SetSeed` → per-particle deterministic seed 적용 시 다른 시스템 race. 따라서 local `std::mt19937` 채택 — §12 상세. [ParticleBeamEmitterInstance.cpp:74-81](../JSEngine/Source/Engine/Particle/ParticleBeamEmitterInstance.cpp:74) |

---

## §2 변경 파일 목록

### 신규 파일 (2 + 1 .gen.cpp)

| 파일 | 역할 | 라인 |
| --- | --- | --- |
| [ParticleModuleBeamNoise.h](../JSEngine/Source/Engine/Particle/ParticleModuleBeamNoise.h) | `UParticleModuleBeamNoise` UCLASS — 4 UPROPERTY (Frequency / NoiseRange / bTargetNoise / bSmooth) | 50 |
| [ParticleModuleBeamNoise.cpp](../JSEngine/Source/Engine/Particle/ParticleModuleBeamNoise.cpp) | (단순 데이터 컨테이너 — Spawn/Update 무) | 4 |
| [UParticleModuleBeamNoise.gen.cpp](../JSEngine/Intermediate/Reflection/Particle/UParticleModuleBeamNoise.gen.cpp) | `GenerateReflection.py` 자동 생성 — 4 property 등록 (Frequency Min=1.0 Max=8.0 확인) | (auto) |

### 수정 파일 (3)

| 파일 | 변경 내용 |
| --- | --- |
| [ParticleBeamTypes.h](../JSEngine/Source/Engine/Particle/ParticleBeamTypes.h) | `BeamNoiseMaxFrequency = 8` constexpr 추가. `FParticleBeamPayload` 에 `FVector NoiseSamples[BeamNoiseMaxFrequency]` 멤버 추가 (4B → **100B**). `static_assert(sizeof == 4)` → `static_assert(sizeof == 100)` 갱신. |
| [ParticleBeamEmitterInstance.cpp](../JSEngine/Source/Engine/Particle/ParticleBeamEmitterInstance.cpp) | `#include <random>`, `<cmath>`, `Math/Utils.h`, `ParticleModuleBeamNoise.h` 추가. `BeamAxisParallelDot` 상수 (위험 11). `ComputeBeamLocalAxes` helper (Beam-local 좌표축, 위험 11 방어). `GenerateNoiseSamples` helper (분기 1 B-2 + 분기 6 A). `SpawnParticles` 의 BeamIndex 분배 루프에 Noise hook 추가 (`Particle->ParticleId` seed). `BuildVertexBuffer` 의 segment 루프에 perturbation 분기 (Source/Target 끝점 보호 + bTargetNoise + bSmooth + Beam-local → World offset). |
| [JSEngine.vcxproj](../JSEngine/JSEngine.vcxproj) + [.filters](../JSEngine/JSEngine.vcxproj.filters) | 3 신규 파일 등록 (.h / .cpp / .gen.cpp) — 총 6 ItemGroup 항목 × 2 파일 |

### 0건 변경 보장 (Cycle 13a 본체)

- `ParticleBeamEmitterInstance.h` — 0건 (멤버 / signature 변경 없음. 본 cycle 의 추가 로직은 모두 .cpp 내부 helper 또는 기존 함수 본문에 흡수)
- `ParticleModuleTypeDataBeam.h/.cpp` — 0건
- `ParticleModuleBeamSource.h/.cpp` — 0건
- `ParticleModuleBeamTarget.h/.cpp` — 0건
- `BeamParticle.hlsl` (VS/PS) — 0건 (vertex layout 동일, 위치만 CPU 측 perturb)
- `ParticleRenderPass` / `VertexFactoryTypes` / `ShaderPaths` / `PrimitiveDrawCommandBuilder` — 0건

---

## §3 작업 순서 vs 실제 진행 결과

| Phase | 계획 (prompt §2) | 실제 진행 | 비고 |
| --- | --- | --- | --- |
| 1 | 데이터 구조 (NoiseSamples) | 완료 | `BeamNoiseMaxFrequency=8` constexpr + payload 100B + `static_assert` 갱신 |
| 2 | UCLASS (NoiseModule) | 완료 | 4 UPROPERTY (Frequency / NoiseRange / bTargetNoise / bSmooth) + GenerateReflection.py 실행 |
| 3 | SpawnParticles hook + random source 결정 | 완료 | actual choice = local `std::mt19937` (cascade fallback). seed = `Particle->ParticleId` (instance-wide unique). `GenerateNoiseSamples` helper 작성 + Spawn 루프 hook |
| 4 | BuildVertexBuffer perturbation | 완료 | `ComputeBeamLocalAxes` helper (위험 11 방어) + segment 루프에 noise 분기 (Source/Target 끝점 + bTargetNoise + bSmooth + Beam-local → World) |
| 5 | 빌드 verify | 완료 | Debug x64 통과, 오류/경고 0. `static_assert` 3건 모두 통과. JSEngine.exe 45.17 MB (13a 의 45.16 MB 에서 ~10KB 증가). |

### 빌드 진행 중 발견된 이슈

없음. `GenerateReflection.py` 실행 → `UParticleModuleBeamNoise.gen.cpp` 자동 생성 확인 → vcxproj 등록 → 첫 빌드 통과 (Cycle 12 의 LNK2019 사전 인지 학습 효과).

---

## §4 silent bug 방어 매핑

### 본 cycle 신규 방어 (2건)

| 위험 | 내용 | 방어 코드 위치 |
| --- | --- | --- |
| **6 (Noise determinism)** | random source 가 frame-rate 의존이면 머신/replay 간 다른 결과 | spawn 시 1회 random 호출 + per-particle 영구 캡처 → frame-rate 비종속. seed = `Particle->ParticleId` (base SpawnParticles 의 `++ParticleCounter` 결정성). [ParticleBeamEmitterInstance.cpp:74-95](../JSEngine/Source/Engine/Particle/ParticleBeamEmitterInstance.cpp:74) (GenerateNoiseSamples) + [cpp:144](../JSEngine/Source/Engine/Particle/ParticleBeamEmitterInstance.cpp:144) (Spawn 호출 — particle 당 1회) |
| **11 (perp axis singular)** | Tangent 가 WorldUp 과 거의 평행 → `Cross(WorldUp) ≈ 0` → 정규화 NaN | `ComputeBeamLocalAxes` 의 `if (\|Tangent.Dot(WorldUp)\| > 0.99f) RefAxis = WorldRight;` 자동 전환 [ParticleBeamEmitterInstance.cpp:42-49](../JSEngine/Source/Engine/Particle/ParticleBeamEmitterInstance.cpp:42) |

### 기존 Cycle 13a 의 방어 유지 (5건, 변경 0건)

| 위험 | 방어 위치 |
| --- | --- |
| 1 (SlotIndex 참조) | `GetBeamPayload` 의 `SlotIndex < 0 \|\| SlotIndex >= GetMaxActiveParticleCount()` 검사 ([ParticleBeamEmitterInstance.cpp:115-118](../JSEngine/Source/Engine/Particle/ParticleBeamEmitterInstance.cpp:115)) |
| 5 (dangling pointer) | `BuildVertexBuffer` 의 SourceComp/TargetComp nullptr → fallback ([cpp:223-225, 257-263](../JSEngine/Source/Engine/Particle/ParticleBeamEmitterInstance.cpp:223)) |
| 7 (zero-length beam) | `BeamLength < BeamSmallNumber` 시 continue ([cpp:267-271](../JSEngine/Source/Engine/Particle/ParticleBeamEmitterInstance.cpp:267)) |
| 8 (interp overflow) | `clamp(InterpolationPoints, 0, BeamInterpolationPointsMax=64)` ([cpp:241-242](../JSEngine/Source/Engine/Particle/ParticleBeamEmitterInstance.cpp:241)) + TypeData UPROPERTY Min/Max(0..64) 1차 방어 |
| 9 (MaxBeamCount 초과) | `NextBeamIndex = (NextBeamIndex + 1) % MaxBeams` ([cpp:163](../JSEngine/Source/Engine/Particle/ParticleBeamEmitterInstance.cpp:163)) |

### 위험 8 (payload mismatch) 의 추가 방어 메커니즘

본 cycle 의 `static_assert(sizeof(FParticleBeamPayload) == 100)` 갱신 (`== 4` 에서) 자체가 silent bug 8 의 방어 — 만약 NoiseSamples 멤버 추가가 align 오차 등으로 예상 100B 가 아니면 빌드 즉시 실패. **빌드 통과 = sizeof 정확성 검증**.

### 위험 9 (NoiseSamples buffer overrun) 의 다층 방어

- UPROPERTY `Max=8` (1차) — Editor 가 8 초과 입력 차단.
- `GenerateNoiseSamples` 의 `Clamp(Frequency, 0, BeamNoiseMaxFrequency)` (2차) — 사용자가 reflection 우회 시점에도 buffer overrun 0.
- `BuildVertexBuffer` 의 `Clamp(NoiseModule->GetFrequency(), 0, BeamNoiseMaxFrequency)` (3차) — 같은 정책.

**제외**: 위험 10 (race — 단일 thread 가정 유지).

---

## §5 회귀 안전 점검 (변경 0건 보장)

| 항목 | 결과 |
| --- | --- |
| `USpriteTypeData` / `UMeshTypeData` / `URibbonTypeData` | 0건 변경 |
| `UBeamTypeData` / `UParticleModuleBeamSource` / `UParticleModuleBeamTarget` (Cycle 13a) | 0건 변경 (멤버 / 메서드 무변) |
| base `FParticleEmitterInstance::Init` / `SpawnParticles` / `KillParticle` / `Tick` | 0건 변경 |
| Sprite/Mesh/Ribbon case in `PrimitiveDrawCommandBuilder` | 0건 변경 |
| `RenderSpriteEmitter` / `RenderMeshEmitter` / `RenderRibbonEmitter` / `RenderBeamEmitter` body | 0건 변경 |
| `FRenderCommand` sizeof | **464B 유지** (Beam payload 확장은 RenderCommand 와 무관 — Cycle 10a 의 베이스라인 보존) |
| `EParticleEmitterRenderMode` / `EVertexFactoryType` enum | 0건 변경 |
| `BeamParticleDesc` / `BeamParticleLayout` | 0건 변경 |
| `BeamParticle.hlsl` (VS/PS) | 0건 변경 (vertex layout 동일 — 위치만 CPU 측 perturb) |
| Cycle 13a 의 SpawnParticles BeamIndex round-robin / Tick / GetBeamVertexData / EnsureBeamState | **본문 일부 변경 (Noise hook 삽입)**, 단 외부 동작 (NoiseModule 미존재 시) 0건 변경 — 회귀 안전 |
| EnsureBeamState / KillParticle 미override 정책 | 유지 (KillParticle base swap-pop 안전) |

### 회귀 안전의 핵심 — NoiseModule 미존재 시 13a 동작 유지

- `SpawnParticles`: `NoiseModule==nullptr` → `NoiseFrequency=0` → `GenerateNoiseSamples` 가 NoiseSamples 모두 zero-init 만 (garbage 노출 0) — payload 다른 멤버 (BeamIndex) 영향 0.
- `BuildVertexBuffer`: `NoiseModule==nullptr` → `bApplyNoise=false` → perturbation 분기 자체 skip → 13a 의 strip 정점 생성 그대로.
- **검증 시나리오**: NoiseModule 없는 emitter (Cycle 13a 의 default Beam asset) → 13a 와 동일 픽셀 출력 보장.

---

## §6 container 자동 가산 네 번째 실측 검증

- **Stride 산식**: `AlignSize(sizeof(FBaseParticle) + sizeof(FParticleBeamPayload), 16)` = `AlignSize(108 + 100, 16)` = `AlignSize(208, 16)` = **208B** (208 / 16 = 13, exact multiple).
- **PayloadOffset**: `sizeof(FBaseParticle)` = 108. payload 영역은 `[108, 208)` 범위에 인터리브 배치 — 100B (4B BeamIndex + 96B NoiseSamples).
- **자동 가산 검증**: Cycle 10d 의 container.Allocate(`ParticleSize + PayloadBytes`) 패턴이 큰 payload 에서도 무사함 확인 — **네 번째 실측 검증**.

| Emitter | Payload bytes | Stride (align 16B) | 차이 |
| --- | --- | --- | --- |
| Sprite | 0 | 112B | baseline |
| Mesh | 36 | 144B | +32B |
| Ribbon | 32 | 144B | +32B |
| Beam (13a) | 4 | 112B | 0B |
| **Beam (13b)** | **100** | **208B** | **+96B (Sprite 대비)** |

- **base `Init` 의 PayloadBytes 가산**: [ParticleEmitterInstance.cpp:44-46](../JSEngine/Source/Engine/Particle/ParticleEmitterInstance.cpp:44) — `UBeamTypeData::RequiredPayloadBytes() = sizeof(FParticleBeamPayload) = 100` 반환 → `ParticleSize + PayloadBytes = 108 + 100 = 208` → container.Allocate(208) → align(16) → 208B stride.
- **SlotIndex 안전성**: Beam 은 chain 의존 없음 → base `KillParticle` 의 swap-pop 직접 사용 → SlotIndex 불변 → payload 의 NoiseSamples 도 swap-pop 영향 0.

---

## §7 Cycle 13a vs 13b 차이 실측

| 항목 | Cycle 13a | Cycle 13b |
| --- | --- | --- |
| 모듈 수 | 3 (TypeData + Source + Target) | **4** (+ Noise) |
| Payload | 4B (BeamIndex) | **100B** (+ NoiseSamples[8] = 96B) |
| Stride | 112B (Sprite 와 동일) | **208B** (Sprite 대비 +96B) |
| `SpawnParticles` override | BeamIndex round-robin 분배 | + **NoiseSamples 생성 hook** (per-particle 영구, ParticleId seed) |
| `BuildVertexBuffer` | Source/Target/Tangent + strip 정점 | + **Perp1/Perp2 + Noise perturb (Beam-local → World offset)** |
| silent bug 방어 | 5건 (위험 1/5/7/8/9) | **+2건** (위험 6/11) — 총 7건 |
| Beam-local 좌표 변환 | 없음 (Tangent + 단일 Perp 만 — strip 폭용) | **있음** (Tangent + Perp1 + Perp2 — noise XYZ 채널 매핑) |
| Visual 효과 | 직선 또는 직선 interp | **lightning bolt 모양** (Frequency=4 default) |
| 결정성 | random 호출 없음 | **spawn 시 1회 random + per-particle 영구 → 결정성 보장** |
| 신규 파일 | 10 (전체 Beam) | 2 (Noise UCLASS .h/.cpp) + 1 .gen.cpp |
| 신규 라인 | 554 | ~100 추가 (Spawn hook + perturbation + helpers + Noise UCLASS) |

---

## §8 빌드 verify 결과

- **명령**: `MSBuild JSEngine.sln /p:Configuration=Debug /p:Platform=x64 /m /v:normal`
- **결과**: `JSEngine.exe` 생성 (`C:\GitDirectory12\JSEngine\Bin\Debug\JSEngine.exe`, **45.17 MB** — 13a 의 45.16 MB 에서 ~10KB 증가)
- **컴파일 오류**: **0** (정확 검증: 전체 `MSBuild ... /v:normal` 출력에서 `: error` 매치 0건)
- **컴파일 경고**: **0** (`: warning` 매치 0건)
- **링커 오류**: **0**
- **경과 시간**: 22초 (incremental — Cycle 13a 기반 대부분 cached)
- **빌드 진행 중 추가 fix**: 0건

### Cycle 13b 의 vcxproj 신규 등록 항목 (총 3건)

```xml
<!-- ClCompile (2) -->
Source\Engine\Particle\ParticleModuleBeamNoise.cpp
Intermediate\Reflection\Particle\UParticleModuleBeamNoise.gen.cpp

<!-- ClInclude (1) -->
Source\Engine\Particle\ParticleModuleBeamNoise.h
```

---

## §9 sizeof 확인

| 구조체 | 예상 | 실측 (static_assert 통과) | 위치 |
| --- | --- | --- | --- |
| `FParticleBeamPayload` (13a) | 4B | (변경됨) | — |
| `FParticleBeamPayload` (13b) | **100B** (4B BeamIndex + 8×12B NoiseSamples) | **100B** | [ParticleBeamTypes.h:27-28](../JSEngine/Source/Engine/Particle/ParticleBeamTypes.h:27) |
| `FBeamParticleVertex` | 48B (Cycle 13a 유지) | 48B | [ParticleBeamTypes.h:47-48](../JSEngine/Source/Engine/Particle/ParticleBeamTypes.h:47) |
| `FRenderCommand` | 464B (Cycle 10a baseline 유지) | 464B | [ParticleRenderPass.cpp:15](../JSEngine/Source/Engine/Render/Renderer/RenderFlow/ParticleRenderPass.cpp:15) |

세 assert 모두 컴파일 통과 — 한 개라도 실패했으면 빌드 오류 즉시 감지. **갱신된 `sizeof(FParticleBeamPayload) == 100` 자체가 silent bug 8 (payload mismatch) 방어 메커니즘**.

---

## §10 인게임 verify 항목 (사용자 후속 — 본 cycle 범위 외)

prompt §9 의 verify 항목 그대로:

### 회귀 안전
- [ ] NoiseModule 미존재 emitter → Cycle 13a 의 직선 beam 동작 그대로 (회귀 안전)
- [ ] Sprite/Mesh/Ribbon/Beam-13a 회귀 동일 동작

### Noise 동작
- [ ] NoiseModule 추가 + Frequency=4 + NoiseRange=(0, 30, 30) → lightning bolt 모양 (perp 방향 흔들림 30 단위)
- [ ] Frequency=1 → 중간 1점만 흔들림 (꺽인 직선)
- [ ] Frequency=8 → 정밀 lightning bolt
- [ ] NoiseRange=(50, 0, 0) → tangent 축으로만 흔들림 (beam 길이 약간 변동)
- [ ] bTargetNoise=false → Target 끝점 정확히 TargetComponent 위치
- [ ] bTargetNoise=true → Target 끝점도 흔들림
- [ ] bSmooth=false → 꺽인 직선 (nearest sample)
- [ ] bSmooth=true → 부드러운 곡선 (linear interp)

### 위험 방어 검증
- [ ] beam 이 수직 방향 (예: 하늘 → 땅 lightning, Tangent ≈ WorldUp) → 위험 11 방어 작동 (NaN 없이 정상 lightning, RefAxis 가 WorldRight 로 자동 전환)
- [ ] 같은 ParticleSystem 을 두 머신에서 동시 실행 → 동일 결과 (위험 6 결정성 — `ParticleId` 기반 seed)
- [ ] 한 particle 의 lifetime 동안 sample 변동 없음 → beam 모양 고정 (per-particle 영구 확인)
- [ ] particle 이 새로 spawn 되면 새 모양 (다른 ParticleId → 다른 seed)

### Render 확인
- [ ] RenderDoc capture — Cycle 13a 와 동일 topology (TRIANGLESTRIP) + slot 사용 (slot 0 only), vertex 위치만 perturb

---

## §11 13c 이관 항목 (본 cycle 범위 외)

prompt §1 의 "제외 (DON'T)" + §10 그대로:

### 디버그 시각화 (분기 5 B 채택 결과)
- LineBatcher 디버그 시각화 통합 — Cycle 12 의 디버그 시각화 (분리됨) 와 통합 가능성 검토
- `bDrawDebug` UPROPERTY 또는 console command 토글
- 각 noise sample + interpolation point 위치를 line/sphere 로 draw
- 가능 시 Cycle 12 / 13a 의 디버그 cycle 전체 통합

### Noise 추가 옵션 (분기 1 B-2 결과)
- `bNoiseLock` UPROPERTY — per-frame 모드 (분기 1 옵션 C 확장 시)
- `NoiseSpeed` UPROPERTY — per-frame 갱신 속도 (per-particle 영구 모드에 무의미)
- `EBeamNoiseSpace` UPROPERTY — World/Local 사용자 선택 (분기 3 B 단일 채택 결과 본 cycle 외)

### 본 emitter 의도 외
- InterpolationPoints 와 Frequency 의 매핑 알고리즘 고도화 (현재 linear interp 만 — Catmull-Rom 등은 후속)
- Additive blend (`EBlendType` 에 Additive 값 없음 — Material 측 BlendType 시스템 별도 cycle)
- BeamMethod 다중 (Distance/Emitter/Particle/Branch — 본 cycle 외, Cycle 13a 결정 15 B 유지)
- Speed (beam 전파 속도)
- Sheets (strip 두께 분할) — 본 cycle 1 고정

---

## §12 random source 채택 결과

**Phase 3.1 의 사전 점검 결과** (prompt §0 분기 6 A — cascade 전략):

| 후보 | 발견 여부 | 사용 가능성 | 채택 |
| --- | --- | --- | --- |
| `FEngineRandom` (singleton) | ✅ — [EngineRandom.h:8-24](../JSEngine/Source/Engine/Core/Random/EngineRandom.h:8) | `SetSeed(uint32)` + `RandomFloat(min, max)` 가능 | ❌ — **singleton 의 `SetSeed` 가 전역 영향** → 다른 시스템 random 호출과 race 우려 |
| `FMath::RandRange` / `FRandomStream` | ❌ — grep 0건 | — | — |
| `std::mt19937` + `std::uniform_real_distribution` (local 인스턴스) | ✅ — `<random>` | per-spawn 새 인스턴스 + seed 결정성 | ✅ **채택** |

**actual choice = local `std::mt19937` (cascade fallback)**.

**근거**:
1. `FEngineRandom` 은 singleton 으로 `SetSeed(Seed)` 호출 시 다른 모든 random 호출에 영향 — 같은 frame 의 다른 spawn 모듈 (Lifetime / Location / Velocity 등) 결과가 예측 불가능하게 바뀜.
2. per-particle deterministic noise 의 안전한 구현은 **local RNG 인스턴스**가 더 단순 (per-spawn 신규 생성, 결정적 seed).
3. `std::mt19937` 의 per-spawn instantiation 비용은 미미 (state 5KB, 64bit 초기화).

**seed source 채택 결과**: `Particle->ParticleId` (uint32). 출처:
- base `SpawnParticles` 의 [ParticleEmitterInstance.cpp:185](../JSEngine/Source/Engine/Particle/ParticleEmitterInstance.cpp:185): `Particle->ParticleId = ++ParticleCounter;`
- instance 의 `ParticleCounter` 가 spawn 순서대로 증가 → 같은 spawn 순서면 같은 seed → 머신 간 결정성 보장.
- fallback: `Particle` 가 nullptr 인 비정상 경로에서 `static_cast<uint32>(SlotIndex)` 사용 (defensive).

---

## §13 prompt 추측과 실제 코드의 차이

진단 §3.3 의 Noise 멤버 후보 6개 중 본 cycle 채택:
- `Frequency` ✅ (분기 2 A)
- `NoiseRange` ✅ (분기 3 B — Beam-local 좌표 의미)
- `bTargetNoise` ✅ (분기 4 A)
- `bSmooth` ✅
- `NoiseSpeed` ❌ (per-particle 영구 모드에 무의미 — 13c 이관)
- `bNoiseLock` ❌ (분기 1 B-2 가 항상 lock 됨 → UPROPERTY 미도입, 13c 의 옵션 C 확장 시 도입)

진단 §3.4.2 의 payload 추정 (4B BeamIndex + NoiseSamples[N]):
- N = `BeamNoiseMaxFrequency` = 8 채택 ✅ (분기 1 B-2 의 컴파일 타임 max 결과)
- payload = 4 + 96 = **100B** (예상 그대로, align 무영향)
- Stride = 108 + 100 = **208B** (align(16) 결과 그대로)

진단 §5 위험 6 방어 메커니즘:
- spawn 시 1회 random + per-particle 영구 → 자동 결정성 ✅
- random source cascade actual choice = local `std::mt19937` (FEngineRandom singleton 회피)

진단 §5 위험 11 (perp axis singular) 사전 식별:
- `BeamAxisParallelDot = 0.99f` 임계값 채택 ✅
- RefAxis 자동 전환 (WorldUp ↔ WorldRight) ✅

prompt §1 (포함 DO) 의 모든 항목 구현 완료. 다른 차이 없음.

---

## §14 결론 한 줄

> Cycle 13b (Beam Emitter — Noise) 구현 완료. 6 파일 변경 (신규 2 + 수정 3 + vcxproj 1 pair) + 1 신규 `.gen.cpp` 자동 생성. Debug x64 빌드 통과 (오류/경고 0). 회귀 안전 12 항목 모두 충족 (NoiseModule 미존재 시 13a 동작 그대로 보장). silent bug 7건 (위험 1/5/6/7/8/9/11) 모두 명시 방어. container 자동 가산 패턴 **네 번째 실측 검증** (Sprite 0B → Mesh 36B → Ribbon 32B → Beam-13a 4B → **Beam-13b 100B**, Stride **208B**). random source cascade actual choice = **local `std::mt19937`** (FEngineRandom singleton 의 전역 SetSeed race 회피, ParticleId 기반 결정성 보장). 13c 이관 5건 (LineBatcher 디버그 + bNoiseLock + NoiseSpeed + EBeamNoiseSpace + InterpolationPoints/Frequency 매핑 고도화) 명시. 인게임 verify 만 다음 단계.
