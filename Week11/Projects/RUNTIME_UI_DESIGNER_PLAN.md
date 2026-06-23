# Runtime UI Designer 구현 계획

## 목적

현재 Runtime UI 작업 흐름은 `.rml`, `.rcss`를 직접 수정하고 Runtime UI Previewer에서 Reload하면서 확인하는 방식이다. 이 방식은 작은 수정에도 컨텍스트 전환이 많고, 위치/크기/부모-자식 관계/스타일을 눈으로 맞추기 어렵다.

목표는 Runtime UI Previewer를 단순 미리보기 도구에서 한 단계 확장하여, 엔진 에디터 안에서 UI를 배치하고 저장할 수 있는 Runtime UI Designer로 발전시키는 것이다.

다만 처음부터 UMG Designer 수준을 목표로 잡으면 범위가 크게 터진다. 따라서 사이드 이펙트 없이 작업 속도를 크게 올리는 수준까지 MVP를 깎고, 이후 기능을 단계적으로 올리는 방향이 좋다.

## 핵심 방향

RML/RCSS를 원본 데이터로 삼지 않는다.

대신 에디터가 조작하는 별도의 Runtime UI Layout Asset을 원본으로 삼고, RML/RCSS는 생성 결과물로 취급한다.

```text
Runtime UI Layout Asset
  -> Editor Designer에서 시각 편집
  -> RML / RCSS 생성
  -> Runtime Preview 또는 Game에서 로드
  -> Lua는 action 이름으로 로직 연결
```

이 방향을 택하는 이유는 RML/RCSS를 직접 역파싱해서 Anchor, Pivot, Parent Transform 같은 엔진식 의미로 복원하는 작업이 매우 불안정하기 때문이다. 반대로 엔진 전용 UI Asset을 원본으로 두면 에디터 조작, 저장, Undo/Redo, validation, export가 훨씬 명확해진다.

## 현재 엔진 구조와의 대응

현재 Runtime UI는 RmlUi 기반이다.

- `FRmlUiSystem`은 `.rml` document load/reload/show/hide를 담당한다.
- `EditorRuntimeUIPreviewWidget`은 `.rml` preview, reload, 해상도, zoom, 입력 전달, action event 확인을 제공한다.
- `LuaUIAPI`는 element text/style/attribute/action event 제어를 제공한다.
- Lua 쪽 로직 연결은 `data-action` 또는 action event polling 구조와 잘 맞는다.

따라서 새 Designer는 기존 Runtime UI backend를 대체하지 않고, 그 앞단에 시각 편집용 asset/export layer를 추가하는 형태가 가장 안전하다.

## 원본 데이터 모델 초안

```cpp
enum class ERuntimeUIWidgetType
{
    Canvas,
    Panel,
    Text,
    Image,
    Button
};

struct FRuntimeUIWidgetNode
{
    FString Id;
    FString DisplayName;
    ERuntimeUIWidgetType Type;

    int32 ParentIndex;
    TArray<int32> Children;

    FVector2D AnchorMin;
    FVector2D AnchorMax;
    FVector2D Pivot;
    FVector2D Position;
    FVector2D Size;
    float Rotation;
    FVector2D Scale;

    FString Text;
    FString ImagePath;

    bool bUseNineSlice;
    FMargin NineSliceBorder;

    FString OnClickAction;
};
```

이 asset은 에디터용 원본이다. Runtime에서 꼭 이 구조를 직접 읽어도 되고, 1차에서는 RML/RCSS export 결과만 Runtime에서 사용해도 된다.

## Lua Binding 정책

디자인과 로직을 분리한다.

- Designer: 버튼, 이미지, 텍스트, 위치, 크기, 부모-자식 관계, 스타일을 저장한다.
- Lua: 실제 게임 로직을 처리한다.
- 연결점: Widget의 action 이름.

예시:

```text
Button: StartButton
OnClick Action: StartGame
```

Export 결과:

```html
<button id="StartButton" data-action="StartGame">Start</button>
```

Lua는 기존 방식대로 action event를 polling하거나 dispatch 받아 처리한다.

## Batch 0: 조사와 정책 고정

목표:

- 기존 Runtime UI Previewer, `FRmlUiSystem`, `LuaUIAPI`, asset 저장 방식을 확인한다.
- Runtime UI Layout Asset을 원본으로 삼는 정책을 확정한다.
- RML/RCSS export 범위와 naming rule을 정한다.

산출물:

- 데이터 모델 초안
- 저장 경로 정책
- export 파일 경로 정책
- 기존 `.rml/.rcss`와의 공존 정책

예상 작업량:

- 반나절 ~ 1일

유용성:

- 바로 기능이 늘지는 않지만, 이후 작업의 흔들림을 줄인다.

사이드 이펙트:

- 거의 없음.

## Batch 1: UI Layout Asset과 Widget Tree

목표:

- Designer가 편집할 원본 asset을 만든다.

구현:

- `URuntimeUILayoutAsset` 또는 동등한 asset class 추가
- `FRuntimeUIWidgetNode` 추가
- 부모/자식 관계 저장
- 기본 widget type 지원
  - Canvas
  - Panel
  - Text
  - Image
  - Button
- 위치/크기/Anchor/Pivot 저장
- asset 저장/로드

예상 작업량:

- 1~2일

유용성:

- 아직 시각 편집은 약하지만, 이후 모든 기능의 기반이 된다.

사이드 이펙트:

- 기존 Runtime UI에는 영향을 주지 않도록 새 asset으로만 격리 가능하다.

## Batch 2: Designer 기본 화면

목표:

- Previewer를 Designer로 확장한다.
- 사용자가 텍스트 파일을 열지 않고 UI 구조를 볼 수 있게 한다.

구현:

- Canvas View
- Hierarchy Panel
- Details Panel
- Widget 선택
- Add/Delete/Duplicate
- Parent 변경
- 선택 outline 표시
- Details에서 Position/Size/Anchor/Pivot 수정

예상 작업량:

- 2~3일

유용성:

- 이 단계부터 실제로 작업 속도가 빨라진다.
- 부모/자식 관계와 기본 배치를 눈으로 확인할 수 있다.

사이드 이펙트:

- 기존 Previewer와 별도 Design Mode로 두면 낮다.

## Batch 3: Canvas 조작

목표:

- 엔진 Editor에서 Actor를 배치하듯이 UI를 직접 움직일 수 있게 한다.

구현:

- 마우스 Drag Move
- Resize Handle
- Pivot 표시
- Anchor preset
- Parent local coordinate 계산
- Grid/Snap 옵션
- 최소 Undo/Redo

예상 작업량:

- 2~4일

유용성:

- 작업 속도 향상이 가장 크게 체감되는 단계다.
- `.rml/.rcss` 직접 수정 후 Reload하는 흐름을 상당 부분 제거한다.

사이드 이펙트:

- 좌표계 정책을 잘못 잡으면 이후 수정 비용이 커진다.
- 따라서 처음에는 rotation/scale보다 position/size/anchor/pivot 위주로 제한하는 편이 안전하다.

## Batch 4: RML/RCSS Export와 즉시 Preview

목표:

- Designer에서 만든 UI를 Runtime UI backend가 읽을 수 있는 형태로 출력한다.
- Reload 중심 워크플로우를 없앤다.

구현:

- Widget Tree -> `.rml` 생성
- Widget Tree -> `.rcss` 생성
- `id`, `class`, `data-action` 출력
- 저장 후 Preview에 즉시 반영
- Content Browser asset 갱신
- missing asset fallback

예상 작업량:

- 1~2일

유용성:

- MVP의 핵심 완성 지점이다.
- 이 단계까지 오면 “Runtime UI를 에디터에서 배치한다”고 말할 수 있다.

사이드 이펙트:

- 기존 손작성 `.rml/.rcss`와 충돌하지 않도록 export 파일은 별도 경로 또는 명확한 generated marker를 두는 편이 좋다.

권장 정책:

```text
Asset/UI/Layouts/MainMenu.uiasset
Asset/UI/Generated/MainMenu.rml
Asset/UI/Generated/MainMenu.rcss
```

generated 파일은 사람이 직접 수정하지 않는 것으로 규칙을 둔다.

## Batch 5: 디자인 기능 확장

목표:

- 실제 UI 제작에 필요한 스타일 기능을 추가한다.

구현:

- Text color/font/align
- Background color
- Background image
- Opacity
- Border radius
- Button normal/hover/pressed style
- Image picker
- NineSlice border 설정

예상 작업량:

- 2~4일

유용성:

- 툴이 실전 제작에 가까워진다.
- 특히 Image/NineSlice가 들어가면 버튼, 패널, 팝업 제작 속도가 크게 오른다.

사이드 이펙트:

- 스타일 속성이 많아질수록 asset schema와 export code가 무거워진다.
- 처음에는 RmlUi backend에서 확실히 지원하는 속성만 노출하는 게 좋다.

## Batch 6: UI Animation

목표:

- UI 열림/닫힘, hover, popup 등장 같은 애니메이션을 시각적으로 다룬다.

구현:

- Widget별 animation clip
- Property track
  - position
  - scale
  - opacity
  - color
- easing/curve
- preview playback
- Lua에서 `PlayAnimation("OpenMenu")` 형태로 호출

예상 작업량:

- 3~7일

유용성:

- 완성되면 매우 강력하지만, MVP에는 넣지 않는 편이 좋다.

사이드 이펙트:

- Runtime playback, serialization, editor timeline, Lua binding까지 범위가 넓어진다.
- Animation은 별도 기능으로 분리해서 후순위로 두는 것이 안전하다.

## Batch 7: Production Polish

목표:

- 팀원이 장시간 써도 불편하지 않은 툴로 다듬는다.

구현:

- Multi-select
- Align tools
- Copy/Paste
- Lock/Hide
- Rename validation
- Missing asset warning
- Export diff 안정화
- Hot reload 안정화
- Shortcut

예상 작업량:

- 계속

유용성:

- 작업량을 계속 줄여준다.

사이드 이펙트:

- 기능 추가보다 UX 정리 성격이 강하다.

## MVP 범위 제안

작업 속도를 폭발적으로 올리면서 사이드 이펙트를 낮게 유지하려면 MVP는 여기까지가 적당하다.

```text
Batch 0
Batch 1
Batch 2
Batch 3 일부
Batch 4
Batch 5 일부
```

MVP에 포함:

- UI Layout Asset
- Widget Tree
- Parent/Child 관계
- Panel/Text/Image/Button
- Anchor/Pivot/Position/Size
- Canvas에서 선택/이동/리사이즈
- Details 수정
- RML/RCSS export
- Runtime Preview 즉시 반영
- Lua action 이름 바인딩
- Image path 지정
- 기본 color/text style

MVP에서 제외:

- UI Animation
- 복잡한 CSS 역파싱
- 기존 손작성 RML을 완전한 Designer asset으로 import
- Multi-select
- 고급 state style
- full Undo/Redo
- responsive layout 자동화

## 가장 많이 깎은 실전형 MVP

시간이 부족하면 더 줄일 수 있다.

```text
Canvas
Panel
Text
Image
Button
Parent/Child
Position/Size
Pivot
OnClick Action
RML/RCSS Export
Preview 반영
```

Anchor도 처음에는 preset만 제공한다.

```text
Top Left
Top Center
Top Right
Middle Center
Bottom Left
Bottom Center
Bottom Right
Stretch
```

이 정도만 있어도 `.rml/.rcss`를 계속 열고 Reload하는 작업은 크게 줄어든다.

## 추천 구현 순서

1. 새 asset class와 widget node serialization을 만든다.
2. Runtime UI Previewer에 Design Mode를 추가한다.
3. Hierarchy와 Details를 붙인다.
4. Canvas에서 선택 outline을 그린다.
5. Position/Size를 Details에서 수정하게 한다.
6. 마우스 drag move를 붙인다.
7. RML/RCSS export를 붙인다.
8. Button `data-action`을 Lua action 이름으로 export한다.
9. Image/Text/Button 스타일을 조금씩 추가한다.

## 중요한 설계 원칙

기존 Runtime UI는 깨지지 않아야 한다.

- 기존 `.rml/.rcss` 로드 방식은 유지한다.
- 새 Designer asset은 선택 기능으로 추가한다.
- generated RML/RCSS는 별도 경로에 둔다.
- Runtime은 처음에는 기존 RmlUiSystem을 그대로 사용한다.

RML/RCSS 역파싱은 MVP에서 하지 않는다.

- 기존 손작성 RML을 Designer로 가져오는 기능은 후순위다.
- 처음에는 Designer에서 만든 asset만 Designer가 책임진다.

Lua는 로직만 담당한다.

- 버튼 클릭 시 어떤 함수를 실행할지는 Lua 쪽에서 처리한다.
- Designer는 action 이름만 저장한다.

## 결론

이 기능은 완성되면 매우 유용하다. 특히 현재처럼 RML/RCSS를 직접 고치고 Reload하는 반복을 줄여주기 때문에 UI 작업 속도는 크게 올라간다.

다만 NineSlice, Animation, State Style, Responsive Layout까지 한 번에 잡으면 범위가 크다. 처음에는 Runtime UI Designer의 MVP를 다음 수준으로 깎는 것이 가장 좋다.

```text
Widget Tree + Parent/Child + Pivot/Position/Size + Canvas 조작 + RML/RCSS Export + Lua Action Binding
```

이 정도가 사이드 이펙트 없이 작업 속도를 가장 크게 올리는 핵심 지점이다.
