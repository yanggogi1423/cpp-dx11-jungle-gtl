#pragma once

#include "Viewport/ViewportClient.h"
#include "Engine/Input/InputTypes.h"
#include "Editor/Input/EditorViewportController.h"
#include "Render/Types/RenderTypes.h"
#include "Render/Types/ViewTypes.h"
#include "Editor/Viewport/ViewportCamera.h"

#include "UI/SWindow.h"
#include <string>
#include <memory>
class UWorld;
class FTransformGizmo;
class FEditorSettings;
class FWindowsWindow;
class FSelectionManager;
class FViewport;

class FEditorViewportClient : public FViewportClient
{
	friend class FEditorViewportController;
	friend class FEditorViewportCommandTool;
	friend class FEditorGizmoTool;
	friend class FEditorSelectionTool;
	friend class FEditorNavigationTool;
	friend class FViewportCommandContext;
	friend class FEditorGizmoInputContext;
	friend class FEditorSelectionInputContext;
	friend class FEditorNavigationInputContext;

public:
	void Initialize(FWindowsWindow* InWindow);
	void SetWorld(UWorld* InWorld);
	void SetGizmo(FTransformGizmo* InGizmo) { Gizmo = InGizmo; }
	void SetSettings(const FEditorSettings* InSettings) { Settings = InSettings; }
	void SetSelectionManager(FSelectionManager* InSelectionManager) { SelectionManager = InSelectionManager; }
	FTransformGizmo* GetGizmo() { return Gizmo; }

	// 뷰포트별 렌더 옵션
	FViewportRenderOptions& GetRenderOptions() { return RenderOptions; }
	const FViewportRenderOptions& GetRenderOptions() const { return RenderOptions; }

	// 뷰포트 타입 전환 (Perspective / Ortho 방향)
	void SetViewportType(ELevelViewportType NewType);
	void SetViewportSize(float InWidth, float InHeight);

	// Camera lifecycle
	void CreateCamera();
	void DestroyCamera();
	void ResetCamera();
	FViewportCamera* GetCamera() const { return Camera.get(); }

	void Tick(float DeltaTime);
	void TickPIEOutlineFlashOnly(float DeltaTime);
	bool ProcessInput(FViewportInputContext& Context) override;
	bool WantsRelativeMouseMode(const FViewportInputContext& Context, POINT& OutRestoreScreenPos) const override;
	FEditorViewportController* GetInputController() { EnsureInputController(); return InputController.get(); }

	// 활성 상태 — 활성 뷰포트만 입력 처리
	void SetActive(bool bInActive) { bIsActive = bInActive; }
	bool IsActive() const { return bIsActive; }

	// FViewport 소유
	void SetViewport(FViewport* InViewport) { Viewport = InViewport; }
	FViewport* GetViewport() const { return Viewport; }

	// SWindow 레이아웃 연결 — SSplitter 리프 노드
	void SetLayoutWindow(SWindow* InWindow) { LayoutWindow = InWindow; }
	SWindow* GetLayoutWindow() const { return LayoutWindow; }

	// SWindow Rect → ViewportScreenRect 갱신 + FViewport 리사이즈 요청
	void UpdateLayoutRect();

	// ImDrawList에 자신의 SRV를 SWindow Rect 위치에 렌더 (활성 테두리 포함)
	void RenderViewportImage(bool bIsActiveViewport, bool bDrawActiveOutline = true);
	const FRect& GetViewportScreenRect() const { return ViewportScreenRect; }
	void TriggerPIEStartOutlineFlash(float HoldSeconds = 1.0f, float FadeSeconds = 2.0f);
	void ClearPIEStartOutlineFlash();

private:
	void EnsureInputController();
	void EnsureInputContextStack();
	bool TryCycleGizmoMode();
	void BeginDeferredSpatialIndexInvalidation();
	void EndDeferredSpatialIndexInvalidation();

public:
	void ResetIdPickingState();
	void BeginSelectionMarquee(const POINT& InLocalStart, bool bInAdditive);
	void UpdateSelectionMarquee(const POINT& InLocalCurrent);
	void EndSelectionMarquee();
	bool HasSelectionMarquee() const { return bSelectionMarqueeActive; }
	bool IsSelectionMarqueeAdditive() const { return bSelectionMarqueeAdditive; }
	const POINT& GetSelectionMarqueeStartLocal() const { return SelectionMarqueeStartLocal; }
	const POINT& GetSelectionMarqueeCurrentLocal() const { return SelectionMarqueeCurrentLocal; }

	FViewport* Viewport = nullptr;
	SWindow* LayoutWindow = nullptr;
	FWindowsWindow* Window = nullptr;
	UWorld* World = nullptr;
	std::unique_ptr<FViewportCamera> Camera;
	FTransformGizmo* Gizmo = nullptr;
	const FEditorSettings* Settings = nullptr;
	FSelectionManager* SelectionManager = nullptr;
	FViewportRenderOptions RenderOptions;

	float WindowWidth = 1920.f;
	float WindowHeight = 1080.f;

	bool bIsActive = false;
	bool bDeferredSpatialIndexInvalidation = false;

	// 뷰포트 슬롯의 스크린 좌표 (ImGui screen space = 윈도우 클라이언트 좌표)
	FRect ViewportScreenRect;
	bool bHasInputContext = false;
	FViewportInputContext InputContext;
	float DispatchDeltaTime = 0.0f;
	std::unique_ptr<FEditorViewportController> InputController;
	bool bInputContextStackInitialized = false;
	TArray<IInputContext*> InputContextStack;
	std::unique_ptr<IInputContext> ViewportCommandContext;
	std::unique_ptr<IInputContext> GizmoInputContext;
	std::unique_ptr<IInputContext> SelectionInputContext;
	std::unique_ptr<IInputContext> NavigationInputContext;

	bool bSelectionMarqueeActive = false;
	bool bSelectionMarqueeAdditive = false;
	POINT SelectionMarqueeStartLocal = { 0, 0 };
	POINT SelectionMarqueeCurrentLocal = { 0, 0 };
	bool bPIEOutlineFlashActive = false;
	float PIEOutlineFlashElapsed = 0.0f;
	float PIEOutlineFlashHoldDuration = 0.5f;
	float PIEOutlineFlashFadeDuration = 1.0f;
};
