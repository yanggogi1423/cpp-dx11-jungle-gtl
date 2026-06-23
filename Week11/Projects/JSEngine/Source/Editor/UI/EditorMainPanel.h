#pragma once

#include "ImGui/imgui.h"
#include "Editor/UI/EditorCommandContext.h"
#include "Editor/UI/EditorMainPanelState.h"
#include "Editor/UI/EditorMainPanelWidgetSet.h"
#include "Editor/UI/EditorTabManager.h"
#include "Editor/Viewport/ViewportLayout.h"

#include <functional>

class FRenderer;
class UEditorEngine;
class UMaterialInterface;
class UPrimitiveComponent;
class UActorSequenceComponent;
class UCurveFloatAsset;
class FWindowsWindow;
class FEditorViewer;
class FEditorAnimationSequenceViewerWidget;
struct FAnimSequenceViewerContext;
struct ID3D11Device;

class FEditorMainPanel
{
public:
	// Lifecycle
	void Create(FWindowsWindow* InWindow, FRenderer& InRenderer, UEditorEngine* InEditorEngine);
	void Release();
	void Render(float DeltaTime);
	void Update();

	// Widget access
	FEditorPropertyWidget& GetPropertyWidget();
	FEditorMaterialWidget& GetMaterialWidget();
	FEditorSceneWidget& GetSceneWidget();
	FEditorControlWidget& GetControlWidget();
	void RefreshContentBrowser();

	// Scene and packaging commands
	bool RequestNewScene();
	bool RequestLoadSceneWithDialog();
	bool RequestSaveScene();
	bool RequestSaveSceneAsWithDialog();
	void RequestBuildGame();

	// PIE presentation
	void HideEditorWindowsForPIE();
	void RestoreEditorWindowsAfterPIE();
	bool IsPIEViewportFullscreenEnabled() const;
	void SetPIEViewportFullscreenEnabled(bool bEnabled);

	// Cross-widget commands
	void OpenMaterialAsset(UMaterialInterface* Material);
	void OpenMaterialSlot(UPrimitiveComponent* PrimitiveComp, int32 SlotIndex);
	void OpenCurveAsset(const FString& CurvePath);
	void OpenAnimationStateMachineAsset(const FString& AssetPath);
	void OpenRuntimeUIPreviewAsset(const FString& RmlPath = "");
	void OpenRuntimeUILayoutAsset(const FString& LayoutPath);
	void OpenLuaAnimGraphAsset(const FString& AssetPath);
	void OpenBlueprintAsset(const FString& AssetPath);
	void OpenViewer(FEditorViewer* Viewer);
	void OpenAnimationSequenceViewer(FEditorViewer* Viewer);
	void OpenAnimationSequenceAsset(const FAnimSequenceViewerContext& Context);
	void RequestDockViewer(FEditorViewer* Viewer);
	void RequestDockAnimationSequenceViewer(FEditorViewer* Viewer);
	void RequestDockLuaAnimGraph();
	void RequestDockAnimationStateMachine();
	void RenderViewerToolbarControls(FEditorViewer* Viewer, bool bAllowViewportStats = false);
	FEditorViewportState* ResolveActiveAnimationSequenceViewportState() const;
	ID3D11ShaderResourceView* GetHomeIconResource() const { return IconResources.HomeIcon; }
    void FlushOpenViewerWidgets();
    void CloseViewer(FEditorViewer* Viewer);
    void FlushClosedViewerWidgets();
	void OpenCurveFromActorSequence(
		UCurveFloatAsset* Curve,
		UActorSequenceComponent* SequenceComp,
		const FString& SourceLabel,
		const FString& SourcePath = "",
		int32 InitialSelectedKeyIndex = -1);
	void OpenCurveFromAnimSequence(
		UCurveFloatAsset* Curve,
		const FString& SourceLabel,
		const FString& SourcePath,
		std::function<bool(UCurveFloatAsset*)> SaveCallback,
		int32 InitialSelectedKeyIndex = -1);
	void OpenActorSequencer(UActorSequenceComponent* SequenceComp);
	void PushFooterLog(const FString& Message);
	void RequestPIEViewportInputFocus();
	void OpenProjectSettingsPanel();
	void OpenWorldSettingsPanel();
	void OpenEditorSettingsPanel();
	void ToggleViewportSettingsPanel();
	bool IsViewportSettingsPanelVisible() const;
	bool SpawnPrefabAtOrigin(const FString& PayloadPath);
	bool CanCloseEditor();
	void RestoreLastSceneFromProjectSettings();
	bool IsLevelEditorViewportVisible() const;
	bool IsViewerViewportVisible(FEditorViewer* Viewer) const;
	bool ShouldRenderViewerViewport(FEditorViewer* Viewer) const;

	// Viewport input routing rule:
	// 1. Level viewport input is owned only by the active Level tab.
	// 2. Visible viewer viewports are registered as input targets; hovered/focused window and z-order decide the actual owner.
	// 3. UI widgets never talk to viewport clients directly for input ownership; they only expose visibility/focus state here.
	bool ShouldRouteLevelViewportInput() const;
	bool ShouldRouteViewerViewportInput(FEditorViewer* Viewer) const;
	int32 GetViewerViewportZOrder(FEditorViewer* Viewer) const;

	void ResetWidgetSelections();

private:
	// Bootstrap and frame
	void InitializeImGuiContext();
	void LoadProjectSettings();
	void InitializeImGuiBackend(FWindowsWindow* InWindow, FRenderer& InRenderer);
	void InitializeEditorWidgets(UEditorEngine* InEditorEngine);
	void BindEditorWidgetCallbacks();
	void ConfigureImGuiStyle();
	void LoadEditorFonts();
	void BeginImGuiFrame();
	void HandleContentBrowserShortcut();
	void BuildActiveEditorCommandList(FEditorCommandList& OutCommands);
	bool ExecuteActiveEditorShortcut(const FEditorShortcut& Shortcut);
	bool ExecuteActiveEditorCommand(EEditorCommandId CommandId);
	FEditorViewer* ResolveAnimationSequenceViewerSource() const;
	void OpenAnimationSequenceViewerFromWindowMenu();
	void RenderApplicationChrome(float DeltaTime);
	bool RenderActiveDocumentMainMenu();
	void RenderToolbarAndDock(float DeltaTime);
	void RenderMainViewport(float DeltaTime);
	void RenderEditorPanelWindows(float DeltaTime, bool bDrawEditorPanels);
	float ResolveEffectiveDeltaTime(float DeltaTime) const;
	bool IsLevelEditorTabActive() const;
	FEditorViewerWindowWidget* FindViewerWidgetForTab(const FEditorTabId& TabId) const;
	FEditorAnimationSequenceViewerWidget* FindAnimationSequenceViewerWidgetForTab(const FEditorTabId& TabId) const;
	void RenderActiveViewerDocument(float DeltaTime);
	void RenderActiveAnimationSequenceViewerDocument(float DeltaTime);
	void RenderRuntimeUIPreviewDocument(float DeltaTime);
	void RenderLuaAnimGraphDocument(float DeltaTime);
	void RenderAnimationStateMachineDocument(float DeltaTime);
	void RenderBlueprintDocument(float DeltaTime);
	void UpdateConsoleDrawerAnimation(float EffectiveDeltaTime);
	void RenderLateFrameOverlays(float DeltaTime, float EffectiveDeltaTime, bool bDrawEditorPanels);
	void EndImGuiFrame();
	void ActivateEditorTab(const FEditorTabId& TabId);
	bool RequestCloseEditorTab(const FEditorTabId& TabId);
	void CloseAnimationSequenceViewerTab(const FEditorTabId& TabId);
	void RequestDetachEditorTab(const FEditorTabId& TabId, bool bDetached);
	FEditorTabId GetInputRoutingTabId() const;

	// Layout and viewport rendering
	void RenderEditorTabStrip();
	void RenderEditorToolbar();
	void RenderActiveDocumentToolbar();
	void RenderDockSpace();
	void RenderViewportHostWindow();
	void LoadGameModeSettingsPanelBuffers();
	void LoadWorldGameModeSettingsPanelBuffers();
	void SaveProjectGameModeSettingsPanelBuffers();
	void SaveWorldGameModeSettingsPanelBuffers();
	void RenderProjectSettingsPanel();
	void RenderProjectGameModeSettingsSection();
	void RenderProjectRenderingSettingsSection();
	void RenderEditorSettingsPanel();
	void RenderWorldSettingsPanel();
	void ApplyPendingSettingsWindowViewport(ImGuiID& PendingViewportId);
	void InvalidateSkinningForFocusedWorld();
	FViewportRect GetPIEFixedAspectViewportRect(const FViewportRect& SourceRect) const;
	void ApplyPIEFixedAspectViewportRect();
	void RenderRuntimeUIForPIEViewport(const FViewportRect& ViewportRect, float DeltaTime);
	void QueueRuntimeUIDrawCallback(ImDrawList* DrawList, const FRuntimeUIRenderContext& Context);
	static void RenderRuntimeUIDrawCallback(const ImDrawList* ParentList, const ImDrawCmd* Cmd);
	void ClearRuntimeUIDrawCallbacks();

	// Viewport interactions
	void RenderViewportMenuBarForIndex(int32 ViewportIndex);
	void RenderViewportIconToolbarForIndex(int32 ViewportIndex);
	bool SpawnStaticMeshFromContentPath(const FString& PayloadPath, int32 ViewportIndex, float LocalX, float LocalY);
	bool SpawnSkeletalMeshFromContentPath(const FString& PayloadPath, int32 ViewportIndex, float LocalX, float LocalY);
	bool SpawnPrefabFromContentPath(const FString& PayloadPath, int32 ViewportIndex, float LocalX, float LocalY);
	void HandleContentBrowserViewportDrop();
	bool DrawViewportTextButton(const char* Id, const char* Label, bool bPairFirst = false, bool bPairSecond = false);
	bool DrawViewportIconButton(const char* Id, EEditorMainPanelViewportToolIcon Icon, const char* FallbackLabel, const char* Tooltip, bool bSelected = false, bool bEnabled = true, bool bPairFirst = false, bool bPairSecond = false);
	void LoadViewportToolIcons(ID3D11Device* Device);
	void ReleaseViewportToolIcons();
	void TickViewportContextMenu();
	void RenderViewportContextMenu();

	// Tool windows and overlays
	void RenderConsoleDrawer(float DeltaTime);
	void RenderFooterOverlay(float DeltaTime);
	void RenderEditorDebugPanel(float DeltaTime);
	void RenderUndoHistoryPanel(float DeltaTime);
	void RenderBuildGameModal();
	void TickBuildGameTask();
	void UpdateFooterEventLogs();
	void OpenConsoleDrawer(bool bFocusInput = true);
	void CloseConsoleDrawer();
	void OpenContentBrowser();
	void CloseContentBrowser();
	void ToggleContentBrowser();

	void ApplyPIEViewportFullscreen();
	void RestorePIEViewportLayout();

	FWindowsWindow* Window = nullptr;
	UEditorEngine* EditorEngine = nullptr;

	ImVector<ImWchar> FontGlyphRanges; // Keep alive until the font atlas is built.
	FEditorMainPanelWidgetSet Widgets;
	FEditorTabManager EditorTabs;
    TArray<FEditorViewer*> PendingOpenViewers;

	FEditorMainPanelVisibilityState PanelVisibility;
	FEditorMainPanelBuildGameModalState BuildGameState;
	FEditorMainPanelGameModeSettingsState GameModeSettingsState;
	FEditorMainPanelDebugGridState DebugGridState;
	FEditorMainPanelConsoleDrawerState ConsoleState;
	FEditorMainPanelFooterEventState FooterEventState;
	FEditorMainPanelPIEViewportPresentationState PIEViewportState;
	FEditorMainPanelViewportContextMenuState ViewportContextMenuState;
	FEditorMainPanelRuntimeUIDrawCallbackState RuntimeUIDrawState;
	FEditorMainPanelViewportIconResources IconResources;
	ImGuiID PendingProjectSettingsWindowViewportId = 0;
	ImGuiID PendingWorldSettingsWindowViewportId = 0;
	ImGuiID PendingEditorSettingsWindowViewportId = 0;

};
