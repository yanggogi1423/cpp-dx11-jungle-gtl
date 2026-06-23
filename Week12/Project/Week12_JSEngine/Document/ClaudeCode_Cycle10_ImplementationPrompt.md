# Claude Code Implementation Prompt — Cycle 10a + 10b

## Particle Emitter 공통 Infra 구현: 렌더 파이프라인 type-agnostic 화

---

## 0. 컨텍스트 및 재논의 금지 영역

본 작업은 자체 엔진 particle system의 **공통 infra 마지막 단계**다. Cycle 8 (TypeData 체계) + Cycle 9 (Instance virtualize) 가 완료된 상태에서, **렌더 파이프라인 (VertexFactory · RenderCommand · Builder · RenderPass · Component · Instance)** 의 Sprite 고정 path를 type-agnostic 으로 일반화한다. Cycle 10 완료 후 Phase 2 (Cycle 11 Mesh emitter) 진입.

선행 문서 (필독):
- `ParticleEmitter_InfraCheck.md` — 진단서 (§2 9개 인프라 항목, §3 emitter type별 추가 infra, §4 충돌·리스크, §7 사용자 결정)
- `Cascade_Porting_Status.md` §7 — silent bug 매뉴얼 (§7-1 / §7-4 / §7-5 + 신규 ι/κ/λ/μ)
- `RenderDataFlow.md` — 변경 대상 아님, 전제로만 사용

**재논의 금지 (확정 결정)**:
- TypeData 패턴 채택 + `EParticleEmitterRenderMode` 라우팅 키 사용
- Cycle 8/9 결과물 (UParticleModuleTypeDataBase, USpriteTypeData, `LODLevel::TypeDataModule` UPROPERTY, `GetEffectiveRenderMode()` helper, `FParticleEmitterInstance` virtual 화, payload-aware ParticleStride) 그대로 사용. 재설계 금지
- DynamicData 계층 (`FDynamicEmitterDataBase` 등 9종 class) — **Track B로 분리됨**. 본 cycle (Track A) 범위 밖이며, **Track A 완료 후 후속 cycle에서 도입 예정**. 본 cycle 안에서 손대지 말 것. (사전 결정 기록: 소유자=SceneProxy 지향, 생명주기=매 프레임 new/delete, 생성=TypeDataModule->BuildDynamicData)
- Mesh/Ribbon/Beam 구체 구현 금지 — Cycle 11+ 작업
- "over-engineering" 메타 코멘트 금지

**본 cycle의 4가지 확정 설계 결정** (사용자 사전 결정, Cycle 10 내부에서 재논의 금지):
1. **FRenderCommand 슬롯 방식 = 옵션 (i) 별도 슬롯**. `MeshParticleInstances` / `RibbonVertices` / `BeamVertices` 3종 슬롯을 `FSpriteParticleInstanceData* ParticleInstances` 옆에 나란히 추가. generic void* 방식 사용 금지
2. **RenderPass 구조 = 단일 Pass + 내부 helper 함수 분기**. `FParticleRenderPass` 1개 클래스 안에서 RenderMode별 helper 함수로 분기. 별도 Pass 클래스 (FRibbonParticleRenderPass 등) 생성 금지
3. **BuildInstanceData 소유자 = `FParticleEmitterInstance` (instance가 build)**. virtual 메서드로 instance가 자기 type의 data를 빌드. Component는 dispatch 역할만
4. **비대칭 구조 인지**: 데이터 생성은 polymorphism (instance virtual), 렌더 분기는 procedural switch (RenderPass helper). 의도된 비대칭이므로 일관성 위해 한쪽으로 통일 시도 금지

본 cycle 범위:
- **Cycle 10a**: VertexFactory enum 확장 + Registry case + RenderCommand 슬롯 + ParticleRenderPass helper 분기 wire-up (모두 NOP path)
- **Cycle 10b**: Component build/getter type-agnostic 화 + Instance virtual BuildInstanceData 도입

본 cycle 범위 밖 (다음 cycle):
- Cycle 11: Mesh emitter 구체 구현 (UParticleModuleTypeDataMesh + FMeshParticleInstanceData + MeshParticle VertexFactory Layout/Desc + Mesh helper 구현)
- Cycle 12a/b: Ribbon emitter
- Cycle 13a/b: Beam emitter

---

## 1. 본 작업의 목적

진단 §2 표의 다음 항목들을 **[없음/Sprite 고정] → [있음/type-agnostic]** 으로 전환:
- 항목 6 (`RecreateEmitterInstances` TypeData 기반 분기) — **Cycle 9에서 이미 처리**, 본 cycle은 확인만
- 항목 7 (`BuildSpriteInstanceData` 의 RenderMode 분기) — **Cycle 10b**
- 항목 8 (`EmitterInstanceData` 컨테이너 type-agnostic) — **Cycle 10b**
- §2.1 추가 발견 1 (`FRenderCommand`의 type-agnostic 슬롯) — **Cycle 10a**
- §2.1 추가 발견 2 (`FParticleRenderPass`의 RenderMode 분기) — **Cycle 10a**
- §2.1 추가 발견 3 (`EVertexFactoryType`의 Mesh/Beam/Ribbon entry) — **Cycle 10a**

각 단계 끝마다 **Sprite 동작 동일성** smoke test gate.

---

## 2. Cycle 10a — VertexFactory · RenderCommand · RenderPass wire-up (모두 NOP path)

### 2.1 작업 항목

#### (a) `EVertexFactoryType` enum 확장

- 위치: `JSEngine/Source/Engine/Render/Resource/VertexFactoryTypes.h:19-32`
- 현재: `SpriteParticle` 만 존재
- 추가:
  - `MeshParticle`
  - `RibbonParticle`
  - `BeamParticle`
- enum 순서: 기존 entry 뒤에 추가 (직렬화 영향 회피)

#### (b) `FVertexFactoryRegistry::Get` 명시 case 추가 — **silent bug §7-1 회피**

- 위치: 진단서 `Cascade_Porting_Status.md §7-1` 참조
- 작업: `Registry::Get` switch에 `MeshParticle` / `RibbonParticle` / `BeamParticle` 명시 case 추가
- **본 cycle에서는 layout/desc 비워둠 (Cycle 11+ 에서 채움)**. 즉:
  - case body는 `return nullptr;` 또는 명시적으로 빈 Desc 반환 + 어딘가 로깅 ("Mesh/Ribbon/Beam VertexFactory not yet implemented, returning empty desc")
  - **default 분기로 떨어져 StaticMesh layout이 채택되는 silent bug 차단이 본 case 추가의 목적**
- **빠지면 Cycle 11 진입 시 silent 하게 StaticMesh layout으로 그려져 디버깅 지옥** — §7-1 직접 충돌

#### (c) `FRenderCommand` 슬롯 추가 — 옵션 (i)

- 위치: `JSEngine/Source/Engine/Render/Scene/RenderCommand.h:482-488`
- 현재: `const FSpriteParticleInstanceData* ParticleInstances` + `ParticleTexture` / `SubUVColumns` / `Rows`
- 추가:
  - `const FMeshParticleInstanceData* MeshParticleInstances = nullptr;` (struct 자체는 Cycle 11에서 정의, 본 cycle은 **forward declaration**만)
  - `const FRibbonParticleVertex* RibbonVertices = nullptr;` (forward decl)
  - `const FBeamParticleVertex* BeamVertices = nullptr;` (forward decl)
  - 각각 `uint32 MeshInstanceCount` / `uint32 RibbonVertexCount` / `uint32 BeamVertexCount` 카운터 추가
- 기존 Sprite 슬롯 (`ParticleInstances` 등)은 손대지 말 것 — 회귀 위험
- **forward declaration 위치 주의**: RenderCommand.h에 직접 `struct FMeshParticleInstanceData;` 등 forward decl만 두고, 실제 정의는 Cycle 11+ 각 emitter cycle에서

#### (d) `FParticleRenderPass` 내부 helper 분기 wire-up — **단일 Pass 유지**

- 위치: `JSEngine/Source/Engine/Render/Renderer/RenderFlow/ParticleRenderPass.cpp`
- 현재: `Render()` 또는 동등 진입 함수가 모든 emitter를 Sprite 가정으로 처리 (Quad VB(4) · IB(6) · b8 · DrawIndexedInstanced(6, ...))
- 변경:
  - **클래스 자체 분리하지 말 것**. 단일 `FParticleRenderPass` 유지
  - 진입 함수 안에서 `Cmd.VertexFactoryType` (또는 emitter의 RenderMode) 으로 4-way switch:
    - `case SpriteParticle`: 기존 Sprite 렌더 코드를 `RenderSpriteEmitter(Cmd)` private helper로 추출하여 호출
    - `case MeshParticle`: `RenderMeshEmitter(Cmd)` helper 호출 (본 cycle에서는 NOP, 빈 함수)
    - `case RibbonParticle`: `RenderRibbonEmitter(Cmd)` helper 호출 (NOP)
    - `case BeamParticle`: `RenderBeamEmitter(Cmd)` helper 호출 (NOP)
    - `default`: 로그 + skip
- NOP helper 시그니처: `void RenderXxxEmitter(const FRenderCommand& Cmd) {}` (cpp에서 빈 구현)
- **목적**: switch 분기와 helper 함수 4개가 **wire-up은 되어 있으나 Sprite만 실제 동작**. Cycle 11+ 각 emitter cycle은 해당 helper 함수 본문만 채우면 됨

#### (e) `PrimitiveDrawCommandBuilder::case EPT_ParticleSystem` 의 RenderMode 분기 — **silent bug §7-5 회피**

- 위치: `JSEngine/Source/Engine/Render/Scene/PrimitiveDrawCommandBuilder.cpp:547` 근처 (진단 §4.2 호출처)
- 현재: `ParticleSystemComponent->BuildSpriteInstanceData();` 직접 호출 (Sprite 가정)
- 변경:
  - 현재 호출을 `ParticleSystemComponent->BuildInstanceData();` (Cycle 10b에서 추가되는 type-agnostic 진입점) 로 교체
  - case 본문 안에 emitter 별 분기를 두지 말 것 — 분기는 instance virtual + RenderPass helper에서 처리. **Builder는 일괄 호출만**
  - **case 본문의 모든 경로 끝에서 `return true;` 명시** — §7-5 직접 충돌. 빠지면 다음 case로 fall-through

### 2.2 Cycle 10a silent bug 점검

- [§7-1] (b) Registry case 추가 완료 확인 — 명시 case 누락 시 StaticMesh layout으로 silent fallback
- [§7-4] (a)/(c) 신규 enum entry · 신규 forward decl이지만 신규 파일은 없음 → vcxproj 영향 적음. 단 RenderCommand.h 수정량이 크면 `GenerateProjectFiles.py` 1회 실행 안전
- [§7-5] (e) `case EPT_ParticleSystem` 본문의 모든 분기에서 `return true` 명시 확인
- [§7-3 무관] PickPasses 손대지 말 것
- [§7-7 무관] PassBatchers 손대지 말 것

### 2.3 Cycle 10a 완료 기준 (smoke test gate)

- [ ] 빌드 통과 (warning 없음, 특히 forward declaration 관련)
- [ ] 기본 Sprite asset 실행 → 화면 결과 Cycle 9 직후와 동일 (회귀 0)
- [ ] `FParticleRenderPass` 진입 시 디버거에서 `RenderSpriteEmitter()` helper 경로를 타는지 확인
- [ ] `FVertexFactoryRegistry::Get(EVertexFactoryType::MeshParticle)` 호출 시 default StaticMesh 가 아닌 명시 case의 nullptr/empty 반환 확인
- [ ] `FRenderCommand` sizeof 측정 + 기록 (포인터 3개 + 카운터 3개 추가분 대략 +36~48 bytes 예상)
- [ ] `PrimitiveDrawCommandBuilder` 의 case EPT_ParticleSystem 모든 경로 끝 `return true` 검토 (코드 리뷰)

**위 모든 항목 통과 후 Cycle 10b 진입.** 한 항목이라도 실패 시 Cycle 10a 안에서 fix 후 재검증.

---

## 3. Cycle 10b — Component build/getter type-agnostic + Instance virtual BuildInstanceData

### 3.1 작업 항목

#### (a) `FParticleEmitterInstance::BuildInstanceData` virtual 메서드 추가

- 위치: `ParticleEmitterInstance.h` (Cycle 9에서 virtual 화된 클래스)
- 시그니처 (참고, 정확한 형태는 RenderCommand 구조에 맞춰 조정):
  ```
  // type별 instance data를 빌드해 FRenderCommand 슬롯에 채워줌
  // 본 cycle base 구현: Sprite path 그대로
  virtual void BuildInstanceData(FRenderCommand& OutCmd) const;
  ```
- base 구현 (=Sprite path):
  - 위치: `ParticleEmitterInstance.cpp`
  - 현재 `UParticleSystemComponent::BuildSpriteInstanceData()` 안의 emitter 1개 처리 루프를 **이 메서드로 이동**
  - 각 active particle을 순회 → `FSpriteParticleInstanceData` 생성 → 누적 버퍼에 push → `OutCmd.ParticleInstances` 와 카운터 셋
- **버퍼 owner 결정 필요**: per-instance `TArray<FSpriteParticleInstanceData> InstanceDataBuffer` 멤버를 instance가 보유하고, virtual BuildInstanceData가 그것을 채우고 OutCmd 슬롯에 raw pointer를 노출. (Cycle 8/9 에서 Component가 보유하던 `EmitterInstanceData[EmitterIdx]` 를 instance 내부로 이전)
- **추측**: 위 버퍼 이전이 가장 자연스러움. Component의 `EmitterInstanceData` 멤버는 **삭제** 또는 **포인터 컨테이너로 축소** (구조 결정은 본 cycle에서 확정)
- 참고: Mesh/Ribbon/Beam derived instance는 Cycle 11+ 에서 이 메서드를 override (각 type별 InstanceDataBuffer 타입이 다름 — 별도 멤버 또는 union 형태)

#### (b) `UParticleSystemComponent` 메서드 일반화

- 위치: `ParticleSystemComponent.h/.cpp`
- 작업:
  - 신규 진입점: `void BuildInstanceData();` — 모든 emitter instance를 순회하며 각자의 `Instance->BuildInstanceData(FRenderCommand 생성/전달)` 호출
  - 기존 `BuildSpriteInstanceData()` — **deprecate + 내부에서 새 `BuildInstanceData()` 호출** 후 deprecation log, 또는 완전 제거 (호출처는 진단 §4.2에서 1건만 확인됨 — `PrimitiveDrawCommandBuilder.cpp:547`)
  - 호출처 1건이므로 **완전 제거 + 새 함수로 일괄 교체** 권장. Cycle 10a (e) 에서 이미 교체했으므로 본 작업은 그 변경의 후속
- 기존 `GetEmitterInstanceData(int32 EmitterIndex)` 반환 타입 처리:
  - 현재: `const TArray<FSpriteParticleInstanceData>&`
  - 변경 옵션 A: Sprite 전용 getter는 그대로 유지하되 deprecation 명시. Cycle 11+에서 type별 getter (`GetMeshEmitterInstanceData(...)` 등) 추가
  - 변경 옵션 B: 본 cycle에서 함수명을 `GetSpriteInstanceData(EmitterIndex)` 로 명시 변경 + type별 getter 시그니처만 추가 (Cycle 11+ 구현)
  - **추측: 옵션 B가 명시적이고 호출처 변경 부담 적음** (호출처가 적다는 가정). 호출처 5건 이상이면 옵션 A
  - **Claude Code 결정**: 먼저 호출처 grep으로 개수 확인 후 사용자에게 보고하고 GO 받기

#### (c) Component의 `EmitterInstanceData` 멤버 거취 결정

- 위치: `ParticleSystemComponent.h:65` (현재 `TArray<TArray<FSpriteParticleInstanceData>>`)
- (a) 에서 버퍼가 instance 내부로 이전된다면 Component의 이 멤버는 **불필요**:
  - 옵션 X: 완전 삭제
  - 옵션 Y: 호환을 위해 deprecation 주석 + 다음 cycle에서 삭제
- **추측: 옵션 X가 깔끔**. 단 `GetEmitterInstanceData` 가 이 멤버를 참조한다면 함께 처리
- **Claude Code 결정**: 의존성 grep 후 사용자에게 보고

#### (d) `FParticleEmitterRuntimeView` payload offset 노출 (선택, 진단 §2 항목 9)

- 위치: `ParticleTypes.h:83-93`
- 현재: `RenderMode` 노출하지만 `PayloadOffset` / `InstancePayloadSize` 노출 없음
- 변경: 두 필드를 view에 추가
- **본 cycle 사용처가 없으므로** 추가는 보류 가능. 단 진단 §2-9의 결손 해소를 본 cycle에 포함시키려면 추가
- **사용자 결정 사항** (본 prompt §6에 명시): RuntimeView 에 payload 정보 노출을 본 cycle에 포함할지, Ribbon cycle (12a) 에 미룰지

### 3.2 Cycle 10b silent bug 점검

- [신규 위험 1] `BuildSpriteInstanceData` 제거/이전 시 호출처 누락 — `PrimitiveDrawCommandBuilder.cpp:547` 1건 외에 다른 곳에서 호출하는지 grep 확인
- [신규 위험 2] Component의 `EmitterInstanceData` 멤버 삭제 시 외부에서 직접 참조하는 코드 — grep 확인
- [κ] `RenderMode` single source 원칙: instance의 BuildInstanceData가 `Instance->GetTemplate()->GetLODLevel(0)->GetEffectiveRenderMode()` 또는 동등 helper로 type 결정. `Required.RenderMode` 직접 참조 금지 (fallback은 GetEffectiveRenderMode 내부에서)
- [λ] BuildInstanceData가 매 viewport마다 재계산되는 구조 유지 시 성능 영향 누적 — 본 cycle은 캐싱 도입하지 않음. 캐싱 검토는 별도 cycle (진단 §4.7-λ 추측). 단 BuildInstanceData의 호출 빈도가 늘지 않도록 호출 위치 유지 (PrimitiveDrawCommandBuilder 1회만)

### 3.3 Cycle 10b 완료 기준 (smoke test gate)

- [ ] 빌드 통과
- [ ] 기본 Sprite asset 실행 → 화면 결과 Cycle 10a 직후와 동일 (회귀 0)
- [ ] 디버거에서 `FParticleEmitterInstance::BuildInstanceData()` (base/Sprite 구현) 가 호출되는지 확인
- [ ] `Cmd.ParticleInstances` 가 Sprite 데이터로 채워지고, `Cmd.MeshParticleInstances/RibbonVertices/BeamVertices` 는 nullptr 유지 확인
- [ ] `BuildSpriteInstanceData` 호출처 0건 (grep 결과 보고)
- [ ] Component의 `EmitterInstanceData` 멤버 거취 결정·문서화
- [ ] Sprite particle 다수 (예: rate 100, lifetime 2초) 실행 → 1분간 비정상 종료 없음 (instance 내부 버퍼 관리 회귀 검증)

---

## 4. 출력물 및 진행 방식 지시

### 4.1 작업 순서 (엄격 준수)

1. **Self-check 응답** (§6) 먼저 → 사용자 GO 신호 대기
2. Cycle 10a 의 (a) → (b) → (c) → (d) → (e) 순서대로 구현
3. Cycle 10a §2.3 smoke test 모두 통과 → 보고
4. **사용자 승인 후** Cycle 10b 진입
5. Cycle 10b 진입 직후, `BuildSpriteInstanceData` / `EmitterInstanceData` 호출처/참조처 grep 결과 보고 후 **§3.1-(b) 옵션 A/B 및 §3.1-(c) 옵션 X/Y 결정 받기**
6. Cycle 10b 의 (a) → (b) → (c) → (d 결정 시) 순서대로 구현
7. Cycle 10b §3.3 smoke test 모두 통과 → 보고

### 4.2 보고 형식 (각 cycle 종료 시)

- 변경 파일 목록 + 각 파일 변경 위치 (줄 번호)
- §2.3 또는 §3.3 smoke test 결과 (체크박스별 PASS/FAIL + 근거)
- silent bug 회피 결과 (§2.2 또는 §3.2 각 항목)
- `FRenderCommand` sizeof 측정값 (10a)
- 호출처 grep 결과 (10b 진입 시 + 종료 시)
- 발견된 의외 / 추측 / 신규 silent bug 후보

### 4.3 작업 중 금지 사항

- 별도 RenderPass class 생성 금지 (`FRibbonParticleRenderPass` 등) — 단일 Pass 유지 결정
- generic void* 슬롯 도입 금지 — 옵션 (i) 별도 슬롯 결정
- Mesh/Ribbon/Beam helper 함수 본문 채우기 금지 — Cycle 11+ 작업
- `FMeshParticleInstanceData` / `FRibbonParticleVertex` / `FBeamParticleVertex` struct 정의 금지 — Cycle 11+ 작업 (본 cycle은 forward declaration만)
- VertexFactory Layout/Desc 채우기 금지 — Cycle 11+ 작업 (본 cycle은 case 명시 + empty 반환만)
- DynamicData 계열 (`FDynamicEmitterDataBase` 등) 도입 금지 — **Track B 영역**, 본 cycle 범위 밖. (영구 폐기 아님, 후속 cycle에서 도입 예정)
- Cycle 8/9 결과물 재설계 금지
- 진단서 §6 추측 영역 본 cycle에서 결정 금지 (해당 cycle에서)
- "비대칭 구조 (instance polymorphism + RenderPass procedural switch)" 를 한쪽으로 통일 시도 금지 — 의도된 설계

### 4.4 막힘 시 처리

- 진단 문서·본 prompt 에 명시되지 않은 결정 필요 시: **구현 중단 + 사용자에게 질문**
- 신규 silent bug 후보 발견 시: **즉시 보고**
- 빌드 실패 시 자가 fix 시도 1회만, 실패 시 보고
- §3.1-(b)/(c)/(d) 의 옵션 결정 필요 시: 본 prompt §4.1 step 5 에서 명시적으로 사용자 질문

---

## 5. 사용자 측 사전 결정 사항 정리

본 cycle 진입 전 확정 (재논의 금지):

- **결정 2** (FRenderCommand 슬롯 방식): **옵션 (i) 별도 슬롯**
- **결정 3** (RenderPass 구조): **단일 Pass + 내부 helper 함수 분기**
- **결정 추가** (BuildInstanceData 소유자): **Instance가 build (instance-side virtual)**
- **결정 추가** (Cycle 10 세분화): **10a / 10b 로 분할**

본 cycle 내부 미결 사항 (작업 중 사용자 질문 필요):

- §3.1-(b): `GetEmitterInstanceData` 처리 방식 (옵션 A: deprecation 유지, 옵션 B: 함수명 변경) — 호출처 grep 후 결정
- §3.1-(c): Component의 `EmitterInstanceData` 멤버 거취 (옵션 X: 삭제, 옵션 Y: deprecation) — 의존성 grep 후 결정
- §3.1-(d): `FParticleEmitterRuntimeView` payload offset 노출을 본 cycle에 포함할지, Ribbon cycle (12a) 로 미룰지

---

## 6. 작업 시작 전 self-check

본 prompt 를 읽고 시작 전, 다음을 응답으로 보낼 것:

1. 본 cycle (10a + 10b) 작업 범위 요약 (3줄 이내)
2. Cycle 10a 와 10b 사이 smoke test gate 존재함을 인지함 확인
3. **§0 의 4가지 확정 설계 결정** (RenderCommand 슬롯 별도, RenderPass 단일+helper, Instance가 build, 비대칭 구조 인지) 모두 이해함 확인 — 각 항목별로 1줄 요약
4. Cycle 8/9 결과물 (TypeData base, USpriteTypeData, instance virtual 화, payload-aware stride, GetEffectiveRenderMode) 코드 위치 확인 — 각 항목 파일·줄번호 보고
5. 본 cycle 범위 밖 항목 (Cycle 11+ Mesh/Ribbon/Beam 구체 구현, Track B DynamicData 계층) 손대지 않음 확인 — Track B는 후속 cycle에서 도입 예정임을 인지
6. §3.1-(b)/(c)/(d) 의 미결 사항을 본 prompt §4.1 step 5 에서 사용자에게 질문할 예정임 확인
7. 의문점 (있으면)

위 응답 후 사용자 GO 신호 받고 작업 시작.
