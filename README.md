# CPP DX11 Jungle GTL

C++ / Direct3D 11 기반 자체 게임 엔진과 에디터를 개발한 14주 프로젝트입니다.

**크래프톤 정글 GameTech Lab 3기**에서 진행한 주차별 팀 프로젝트의 소스를 모았습니다. 빈 `main` 함수에서 시작해 렌더링, 씬 편집, 공간 질의, 애니메이션과 물리 기능을 확장하고, 1·9·14주차 게임잼에서 자체 엔진으로 게임을 제작했습니다.

- 진행 기간 : 2026.03 ~ 2026.06 (14주)
- 개발 방식 : 주차별 팀 협업, 기능 구현 및 게임잼
- 개인 기여 : 양현석 (`yanggogi1423`, `an_yan_yang`, `HyunSuk YANG`)
- 주요 기술 : C++, Direct3D 11, Win32 API, HLSL, Lua, ImGui, RmlUi
- 관련 도구 및 라이브러리 : Visual Studio, Git, FBX SDK, PhysX, NvCloth

[포트폴리오](https://anyanyang.notion.site/2e9c8404c9328051ab45c80805366ec1) · [엔진 상세](https://anyanyang.notion.site/3c0c8404c9328166809ff6638824b7f9)

## 프로젝트 진행 흐름

주차별로 엔진 기능을 구현하고, 에디터에서 데이터를 만들고 조작하는 흐름과 실제 게임 실행을 함께 다뤘습니다. 게임잼에서는 입력, 상태 전환, HUD와 연출을 연결해 제작한 엔진을 게임 개발에 사용했습니다.

| 주차 | 팀 프로젝트 주제 | 개인 기여 요약 |
| --- | --- | --- |
| [W01](./Week01/) | SPACE.EXE 게임잼 | UI, 상태 전환, 리소스·사운드 관리, 랭킹 화면 |
| [W02](./Week02/) | 3D 엔진 코어 및 렌더러 | RenderBus·RenderCollector, 렌더 명령과 패스 실행 구조 |
| [W03](./Week03/) | 씬·에디터 통합 | 엔진 실행 루프, 뷰포트 카메라, 선택과 Gizmo 조작 |
| [W04](./Week04/) | OBJ·Material 및 다중 뷰포트 | StaticMesh 로딩, 바이너리 캐시, 리소스 관리, 에디터 도구 |
| [W05](./Week05/) | 50,000 StaticMesh 최적화잼 | BVH·Octree, Ray/ID Picking, 컬링 및 PIE 연결 |
| [W06](./Week06/) | Projection Decal 및 후처리 | Decal 컴포넌트, 후처리 패스 분리, 에디터·피킹 보완 |
| [W07](./Week07/) | Forward Lighting | Light Actor/Component, 조명 연결, Mesh Decal, PIE 모드 |
| [W08](./Week08/) | Shadow Mapping | Local Shadow Atlas Scheduling, 그림자 필터 설정·디버그 |
| [W09](./Week09/) | CyberPsycho 게임잼 | 입력 소유권, 선택 서비스, UI·HUD·게임 상태 연결 |
| [W10](./Week10/) | Skeletal Mesh 및 CPU Skinning | SkeletalMesh Viewer, Bone 선택, GameMode·PIE 입력 연결 |
| [W11](./Week11/) | Animation 및 GPU Skinning | AnimSequence Viewer, Timeline·Curve·Notify 편집 도구 |
| [W12](./Week12/) | Particle System | Symbol Server, 충돌 질의, Bloom, 반투명·파티클 통합 |
| [W13](./Week13/) | PhysX 및 Cloth | NvCloth 런타임, DOF 렌더 패스, 카메라 디버그 |
| [W14](./Week14/) | Snajper 게임잼 | Lua 상태·HUD, 킬캠, 피격 Decal, 라디오·결과 연출 |

## 주차별 개인 기여

아래는 팀 전체 구현 중 제가 담당한 범위입니다. 개인 기여는 Notion에 보존한 원본 저장소의 commit·diff·blame 조사 기록을 기준으로 정리했습니다. 각 주차의 공동 작업은 구현과 연결·수정 범위를 구분했습니다.

### W01. SPACE.EXE — UI 및 게임 상태 흐름

- `StateMachine`을 통한 Main → Stage Selection → Loading → InGame → Ending 화면 전환 구성
- `UIFrame` / `UIManager` 기반 버튼, 이미지, HUD와 반복 UI 요소 구현
- `UResourceManager`와 사운드 매니저를 통한 화면 자산 및 BGM·SFX 연결
- 엔딩 랭킹 표시·정렬 및 점수 목록 범위 오류 수정

엔딩과 전역 Manager는 공동 작업 파일이며, 개인 기여는 UI와 상태 연결, 랭킹 화면 및 안정화 작업입니다.

[원본 저장소](https://github.com/jungle-gametechlab3-w1-3/cpp-gametechlab-project-w01) · [UI Base 커밋](https://github.com/jungle-gametechlab3-w1-3/cpp-gametechlab-project-w01/commit/bf7f357) · [기여 기록](https://www.notion.so/3c0c8404c93281358464fcfc68f29294)

### W02. Renderer — 렌더 명령 수집과 패스 실행

- `FRenderCollector → FRenderBus → FRenderer` 렌더링 흐름 설계 및 구현
- Component, DepthLess, Editor, Outline 등 패스별 명령 수집과 실행 분리
- D3D11 Buffer·Shader·Render Resource 및 HLSL 입력 연결
- 월드 객체가 렌더 명령을 제출하고 Renderer가 GPU 실행을 담당하도록 구성

[원본 저장소](https://github.com/JHCard7872/Week2) · [RenderBus 커밋](https://github.com/JHCard7872/Week2/commit/d66310c) · [기여 기록](https://www.notion.so/3c0c8404c93281fcbb57e5efb5c774b0)

### W03. Editor — 실행 루프와 뷰포트 조작

- `EditorEngineLoop`에서 Window, Input, Renderer, Asset, Editor 초기화 및 갱신 흐름 구성
- `EditorViewportClient` 기반 카메라 이동, Orbit, Dolly, Pan, 선택 객체 Focus 구현
- AABB·Triangle·Quad Picking과 드래그 영역 선택 연결
- 팀원의 Gizmo 기반 위에 다중 선택, 중심 변환, 누적 Snapping과 회전 조작 보완

[원본 저장소](https://github.com/DDing-Ho/KraftonJungle3_Week3_Team2) · [실행 루프 커밋](https://github.com/DDing-Ho/KraftonJungle3_Week3_Team2/commit/9992591) · [기여 기록](https://www.notion.so/3c0c8404c932817d9e8cf2ca3d9c0136)

### W04. Asset Pipeline — 모델 로딩과 에디터 연결

- `FObjLoader`에서 OBJ 데이터를 Vertex·Index·Material Slot과 StaticMesh로 변환
- `BinarySerializer`를 통한 cooked asset 캐시 및 Little-Endian·Padding 처리
- `ResourceManager`의 캐시 유효성 확인, Material·Texture·GPU Resource 로딩 연결
- 뷰포트 카메라 조작, Toolbar의 씬 Load/Save 메뉴, Selection Mask·Outline 후처리 구현

[원본 저장소](https://github.com/keonwookang0914/W4_Jungle_Team2) · [바이너리 캐시 커밋](https://github.com/keonwookang0914/W4_Jungle_Team2/commit/f0191bd) · [기여 기록](https://www.notion.so/3c0c8404c932810f94c4e6b44d540e30)

### W05. 최적화잼 — 공간 질의와 선택 시스템

- 인스턴싱 없이 50,000개 StaticMesh를 다루는 팀 과제에서 공간 질의와 피킹 담당
- World BVH·StaticMesh BVH 기반 후보 탐색 및 Triangle Intersection 구성
- ID Buffer Picking과 Ray Picking을 `PickingService`로 연결
- `WorldRenderProxy`·Octree 구현, Frustum Culling 공동 구현 및 후보 캐시 연결
- `EditorEngine`의 PIE 전환과 `GameViewportClient`의 플레이어 입력 연결

50,000개는 과제의 씬 조건이며, 성능 개선 수치를 의미하지 않습니다.

[원본 저장소](https://github.com/lin-ion/Jungle_Week5_Team2) · [Picking Service 커밋](https://github.com/lin-ion/Jungle_Week5_Team2/commit/6311d76) · [기여 기록](https://www.notion.so/3c0c8404c9328126aa85c4caaf6d1ba2)

### W06. Decal 및 Post Process — 에디터에서 렌더링 기능 조작

- `ProjectionDecalComponent`와 에디터·SceneProxy 연결
- Post Process Ping-Pong 구조와 후처리 패스 분리 구현
- 팀의 Fog·FXAA·Scene Depth 기능을 렌더러에 통합하고 FXAA 설정 UI 추가
- 다중 뷰포트 메뉴, ID/Billboard Picking, Gizmo·입력 오류 수정

[원본 저장소](https://github.com/lsh-Brecht/Jungle_Week6_Team3) · [후처리 구조 커밋](https://github.com/lsh-Brecht/Jungle_Week6_Team3/commit/906c219) · [기여 기록](https://www.notion.so/3c0c8404c93281688ab8f14323b56b84)

### W07. Lighting — Light 모델과 Mesh Decal

- Ambient·Directional·Point·Spot Light Actor/Component 계층 구현
- 에디터 속성 변경, 직렬화, 렌더 패킷과 UberLit Shader 입력 연결
- Bounds·BVH Triangle Query로 표면 후보를 좁히는 Mesh Decal 구현
- PIE의 Ejected/Possessed 모드, Player Controller 연결 및 ID Picking 수정

최종 Compute Light Culling과 Normal Mapping·Bloom 등은 팀 구현이며, 개인 기여는 Light 모델과 렌더링 연결, Mesh Decal 및 에디터 조작입니다.

[원본 저장소](https://github.com/wwonnn/KraftonJungle_Week7_Team5) · [Light Actor/Component 커밋](https://github.com/wwonnn/KraftonJungle_Week7_Team5/commit/1dfac18a) · [기여 기록](https://www.notion.so/3c0c8404c93281bfabe4e081523eb5b3)

### W08. Shadow — Local Shadow Atlas Scheduling

- `LocalShadowRequestPlanner`에서 카메라 관련도, Caster 유무, 우선순위와 해상도 정책으로 그림자 요청 구성
- Point Light의 Cube Face와 Spot Light 요청을 공통 구조로 처리
- Atlas 할당·실행과 `ShadowResourceManager`를 연결해 Light의 Shadow View에 반영
- PCF·VSM·ESM 설정 UI 및 Light Perspective Override 디버그 기능 구현

[원본 저장소](https://github.com/lois505/Jungle_week8_Team7) · [Shadow Atlas Scheduling 커밋](https://github.com/lois505/Jungle_week8_Team7/commit/2b55fb3) · [기여 기록](https://www.notion.so/3c0c8404c932810fba0bfba7b73ea365)

### W09. CyberPsycho — 입력 소유권과 게임 실행 흐름

- Editor·PIE·Runtime·ImGui·RmlUi의 Focus/Capture에 따른 마우스·키보드 입력 처리 기준 구성
- `EditorInputRouter`와 `EditorPickingService`를 통해 입력과 선택 경로 정리
- ID Buffer 우선 선택과 Ray/공간 인덱스 기반 Fallback 연결
- UI·HUD·Game Management와 게임 상태 전환, Lua·RmlUi 실행 연결 및 오류 수정

[원본 저장소](https://github.com/esc10946/W9_Team3_Engine) · [선택 서비스 커밋](https://github.com/esc10946/W9_Team3_Engine/commit/7565f529) · [기여 기록](https://www.notion.so/3c0c8404c93281858974eb6f25fc8ad1)

### W10. SkeletalMesh Viewer — 모델 확인과 플레이 설정

- SkeletalMesh Viewer의 탭, Bone 선택 및 Bone·Socket 편집 화면 연결
- Editor Picking과 뷰포트 입력을 Viewer 조작에 연동
- GameMode 설정 UI, Pawn·Input 설정 및 PIE 실행 흐름 구성
- CPU Skinning 파이프라인의 초기 구현과 에디터·렌더러 연결에 기여

FBX Importer와 최종 Skinning 컴포넌트는 팀원의 구현·리팩터링이 함께 반영된 영역입니다.

[원본 저장소](https://github.com/jskim-research/W10_Team1_Engine) · [Viewer UI·Bone Picking 커밋](https://github.com/jskim-research/W10_Team1_Engine/commit/223017a8) · [기여 기록](https://www.notion.so/3c0c8404c9328105a773dc98ebda4030)

### W11. AnimSequence Viewer — 애니메이션 편집 도구

- `EditorAnimationSequenceViewerWidget`의 Timeline, 재생, Frame Step과 Seek 구현
- Curve·Notify 편집 UI, Skeleton Panel 및 애니메이션 자산 저장 연결
- Preview Instance의 갱신·동기화와 Viewer 책임 분리
- Bone Weight View Mode, 디버그 Overlay와 World/Local Gizmo 조작 보완

Animation Runtime·AnimGraph·FSM·GPU Skinning Core는 팀 구현이며, 개인 대표 기여는 애니메이션 제작·확인을 위한 Viewer입니다.

[원본 저장소](https://github.com/yanggogi1423/W11_Team1_Engine) · [Viewer Timeline 커밋](https://github.com/yanggogi1423/W11_Team1_Engine/commit/1ff8f72) · [기여 기록](https://www.notion.so/3c0c8404c932817590e0c5d3fccec159)

### W12. Particle 주차 — 충돌 질의와 진단 도구

- Symbol Server의 PDB·Source Snapshot 게시, Source Index, HTTP 서비스와 Dashboard 구성
- `CollisionQueryUtils`의 OBB·Sphere·Capsule Raycast 및 Sweep 구현
- `BloomRenderPass`의 Threshold → Blur → Composite와 리소스 관리 구현
- 반투명 패스의 거리 정렬·렌더 상태 처리 수정 및 ParticleDataContainer·Emitter 연결에 기여

Particle Runtime Core, DynamicData와 Sprite/Mesh/Ribbon/Beam 렌더링은 팀 구현입니다.

[원본 저장소](https://github.com/mathison0/Jungle_Week12_Team3) · [Symbol Server 커밋](https://github.com/mathison0/Jungle_Week12_Team3/commit/73a7124) · [기여 기록](https://www.notion.so/3c0c8404c93281d79be8faedceb17b38)

### W13. Cloth 및 DOF — 물리·렌더링 기능 통합

- NvCloth의 Solver 생명주기, Cloth Instance·Component·RenderProxy 구현
- PhysX 충돌 연결과 CUDA/DX11/CPU Backend Fallback 구성
- CoC 계산, Near/Far 분리, Blur, Composite 단계의 DOF 렌더 패스·HLSL 구현
- 카메라의 Focus Distance·Focal Length·F-stop 전달 및 선택 카메라 뷰포트 디버그 연결

[원본 저장소](https://github.com/mathison0/Jungle_Week13_Team1) · [Cloth 통합 커밋](https://github.com/mathison0/Jungle_Week13_Team1/commit/77f1d1c) · [기여 기록](https://www.notion.so/3c0c8404c93281ae955af3b04a8bb6d4)

### W14. Snajper — 상태·HUD와 킬캠 연출

- Lua 관리 계층의 Main → PreInGame → Loading → InGame → Victory/Defeat 상태 흐름 구현
- HUD·UI Binding, BGM·SFX, 라디오 피드백 및 엔딩·결과 화면 구성
- `SniperKillCamDirector`·Rail Camera·CameraManager를 연결한 킬캠 연출 구현
- Bullet Hole·Blood Impact Decal 및 카메라·입력·연출 복귀 흐름 통합

무기·스코프·탄도·영점의 핵심 기반은 팀원 기여이며, 개인 담당은 게임 상태·UI, 킬캠·피격 표현과 통합입니다.

[원본 저장소](https://github.com/lsh-Brecht/Jungle_Week14_Team7) · [킬캠 연출 커밋](https://github.com/lsh-Brecht/Jungle_Week14_Team7/commit/43d2373) · [기여 기록](https://www.notion.so/3c0c8404c93281aabec4ca7a1ba35c49)

## 자체 엔진으로 제작한 게임

| 주차 | 작품 | 플레이 및 배포 페이지 | 개인 담당 |
| --- | --- | --- | --- |
| W01 | SPACE.EXE | [itch.io](https://yanggogi1423.itch.io/spaceexe) | 상태 전환, UI·HUD, 랭킹 |
| W09 | CyberPsycho | [itch.io](https://yanggogi1423.itch.io/cyberpsycho) | 입력 처리, UI·HUD, 게임 관리 |
| W14 | Snajper | [itch.io](https://yanggogi1423.itch.io/snajper) | 상태·HUD, 킬캠, Decal, 라디오·결과 연출 |

## 저장소 구성 및 실행 참고

이 저장소는 원본 주차별 저장소를 `Week01/` ~ `Week14/`로 평탄화한 **소스 스냅샷 저장소**입니다. 원본 Git 이력은 각 주차의 원본 저장소 링크에서 확인할 수 있습니다. 이 저장소의 반입 커밋은 개인 기능의 작성 이력을 나타내지 않습니다.

빌드 출력, 로컬 IDE 캐시, 생성한 Release와 Resource 폴더는 버전 관리에서 제외했습니다. 따라서 이 저장소만으로 모든 주차를 바로 실행할 수 있는 형태는 아니며, 주차별 프로젝트 설정과 필요한 리소스·라이브러리를 함께 확인해야 합니다.

### 버전 관리 제외 항목

- `Release/`
- `Resource/` and `Resources/`
- downloaded/reference folders such as `Others/`, `Clone/`, and `Videos/`
- local root asset/document folders outside `Week01/` through `Week14/`
- Visual Studio state such as `.vs/`
- Build/debug outputs such as `Build/`, `Bin/`, `Debug/`, `Intermediate/`, `Saved/`, and crash dumps
- Archives and diagnostic captures

추적 중인 대용량 소스 자산은 Git LFS를 사용합니다.
