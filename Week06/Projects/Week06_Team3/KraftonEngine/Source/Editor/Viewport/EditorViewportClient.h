#pragma once

#include "Viewport/ViewportClient.h"
#include "Render/Types/RenderTypes.h"
#include "Render/Types/ViewTypes.h"
#include "Editor/Input/EditorViewportInputContexts.h"
#include "Editor/Input/EditorViewportController.h"

#include "UI/SWindow.h"
#include <string>
#include <memory>
#include "Core/RayTypes.h"
#include "Core/CollisionTypes.h"
class UWorld;
class UCameraComponent;
class UGizmoComponent;
class FEditorSettings;
class FWindowsWindow;
class FSelectionManager;
class FViewport;
class FOverlayStatSystem;
class FEditorViewportController;
class FEditorViewportCommandTool;
class FEditorNavigationTool;
class FEditorGizmoTool;
class FEditorSelectionTool;
class AActor;

class FEditorViewportClient : public FViewportClient
{
public:
	friend class FEditorViewportCommandContext;
	friend class FEditorViewportGizmoContext;
	friend class FEditorViewportSelectionContext;
	friend class FEditorViewportNavigationContext;

	void Initialize(FWindowsWindow* InWindow);
	void SetOverlayStatSystem(FOverlayStatSystem* InOverlayStatSystem) { OverlayStatSystem = InOverlayStatSystem; }
	// World는 더 이상 저장하지 않는다 — GetWorld()는 GEngine->GetWorld()를 경유하여
	// ActiveWorldHandle을 따르므로 PIE 전환 시 자동으로 올바른 월드를 반환한다.
	UWorld* GetWorld() const;
	void SetGizmo(UGizmoComponent* InGizmo) { Gizmo = InGizmo; }
	void SetSettings(const FEditorSettings* InSettings) { Settings = InSettings; }
	void SetSelectionManager(FSelectionManager* InSelectionManager) { SelectionManager = InSelectionManager; }
	UGizmoComponent* GetGizmo() { return Gizmo; }

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
	void SyncNavigationCameraTargetFromCurrent();
	UCameraComponent* GetCamera() const { return Camera; }

	void Tick(float DeltaTime);

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
	const FRect& GetViewportScreenRect() const { return ViewportScreenRect; }

	// ImDrawList에 자신의 SRV를 SWindow Rect 위치에 렌더 (활성 테두리 포함)
	void RenderViewportImage(bool bIsActiveViewport, bool bDrawActiveOutline = true);
	void TriggerPIEStartOutlineFlash(float HoldSeconds = 1.0f, float FadeSeconds = 2.0f);
	void ClearPIEStartOutlineFlash();
	bool ProcessInput(FViewportInputContext& Context) override;
	bool WantsRelativeMouseMode(const FViewportInputContext& Context, POINT& OutRestoreScreenPos) const override;
	bool WantsAbsoluteMouseClip(const FViewportInputContext& Context, RECT& OutClipScreenRect) const override;
	FEditorViewportController* GetInputController();
	bool SetInteractionMode(EEditorViewportModeType InModeType);
	EEditorViewportModeType GetInteractionMode() const;
	bool CycleInteractionMode();
	const FViewportInputContext& GetRoutedInputContext() const { return RoutedInputContext; }
	FSelectionManager* GetSelectionManager() const { return SelectionManager; }
	const FEditorSettings* GetSettings() const { return Settings; }
	UWorld* GetInteractionWorld() const { return ResolveInteractionWorld(); }
	bool ConvertScreenToViewportLocal(const POINT& InScreenPos, POINT& OutLocal, bool bClampToViewport = true) const;
	bool PickActorByIdAtLocalPoint(const POINT& InLocalPoint, AActor*& OutActor) const;
	float GetWindowWidth() const { return WindowWidth; }
	float GetWindowHeight() const { return WindowHeight; }

private:
	bool HandleCommandInput(float DeltaTime);
	bool HandleNavigationInput(float DeltaTime);
	bool HandleGizmoInput(float DeltaTime);
	bool HandleSelectionInput(float DeltaTime);
	void EnsureInputContextStack();
	UWorld* ResolveInteractionWorld() const;

private:
	FViewport* Viewport = nullptr;
	SWindow* LayoutWindow = nullptr;
	FWindowsWindow* Window = nullptr;
	FOverlayStatSystem* OverlayStatSystem = nullptr;
	UCameraComponent* Camera = nullptr;
	UGizmoComponent* Gizmo = nullptr;
	const FEditorSettings* Settings = nullptr;
	FSelectionManager* SelectionManager = nullptr;
	FViewportRenderOptions RenderOptions;

	float WindowWidth = 1920.f;
	float WindowHeight = 1080.f;

	bool bIsActive = false;
	// 뷰포트 슬롯의 스크린 좌표 (ImGui screen space = 윈도우 클라이언트 좌표)
	FRect ViewportScreenRect;
	bool bHasRoutedInputContext = false;
	FViewportInputContext RoutedInputContext;
	std::unique_ptr<FEditorViewportController> InputController;
	bool bInputContextStackInitialized = false;
	TArray<IEditorViewportInputContext*> InputContextStack;
	std::unique_ptr<IEditorViewportInputContext> CommandInputContext;
	std::unique_ptr<IEditorViewportInputContext> GizmoInputContext;
	std::unique_ptr<IEditorViewportInputContext> SelectionInputContext;
	std::unique_ptr<IEditorViewportInputContext> NavigationInputContext;
	bool bPIEOutlineFlashActive = false;
	float PIEOutlineFlashElapsed = 0.0f;
	float PIEOutlineFlashHoldDuration = 0.5f;
	float PIEOutlineFlashFadeDuration = 1.0f;
};
