#include "Editor/UI/EditorMainPanel.h"

#include "Editor/EditorEngine.h"
#include "Editor/Viewer/EditorViewer.h"

#include "ImGui/imgui.h"
#include "ImGui/imgui_impl_dx11.h"
#include "ImGui/imgui_impl_win32.h"

#include <algorithm>

void FEditorMainPanel::Render(float DeltaTime)
{
    ClearRuntimeUIDrawCallbacks();
    BeginImGuiFrame();
    TickBuildGameTask();
    HandleContentBrowserShortcut();
    RenderToolbarAndDock(DeltaTime);
    RenderMainViewport(DeltaTime);

    const bool bDrawEditorPanels = !PIEViewportState.bHideEditorWindows;
    RenderEditorPanelWindows(DeltaTime, bDrawEditorPanels);
    RenderBuildGameModal();
    Widgets.ViewportOverlayWidget.RenderFloatingOverlays(DeltaTime);

    const float EffectiveDeltaTime = ResolveEffectiveDeltaTime(DeltaTime);
    UpdateConsoleDrawerAnimation(EffectiveDeltaTime);
    RenderLateFrameOverlays(DeltaTime, EffectiveDeltaTime, bDrawEditorPanels);
    EndImGuiFrame();

    ClearRuntimeUIDrawCallbacks();
}

void FEditorMainPanel::BeginImGuiFrame()
{
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
}

void FEditorMainPanel::HandleContentBrowserShortcut()
{
    const ImGuiIO& IO = ImGui::GetIO();
    if (!IO.WantTextInput && IO.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Space, false))
    {
        ToggleContentBrowser();
    }
    else
    {
        Widgets.ContentBrowserWidget.SetVisible(PanelVisibility.bShowContentBrowser);
    }
}

void FEditorMainPanel::RenderToolbarAndDock(float DeltaTime)
{
    const bool bContentBrowserVisibleBeforeMenu = PanelVisibility.bShowContentBrowser;
    RenderApplicationChrome(DeltaTime);
    if (!bContentBrowserVisibleBeforeMenu && PanelVisibility.bShowContentBrowser)
    {
        OpenContentBrowser();
    }
    else if (bContentBrowserVisibleBeforeMenu && !PanelVisibility.bShowContentBrowser)
    {
        CloseContentBrowser();
    }
    RenderEditorTabStrip();
    RenderEditorToolbar();
    RenderDockSpace();
}

void FEditorMainPanel::RenderMainViewport(float DeltaTime)
{
	FlushOpenViewerWidgets();

    if (IsLevelEditorTabActive())
    {
        RenderViewportHostWindow();
        Widgets.ViewportOverlayWidget.RenderViewportFrameOverlays(DeltaTime);
        return;
    }

    if (EditorTabs.GetActiveTabKind() == EEditorTabKind::RuntimeUIPreview)
    {
        RenderRuntimeUIPreviewDocument(DeltaTime);
        return;
    }

    if (EditorTabs.GetActiveTabKind() == EEditorTabKind::LuaAnimGraphEditor)
    {
        RenderLuaAnimGraphDocument(DeltaTime);
        return;
    }

    if (EditorTabs.GetActiveTabKind() == EEditorTabKind::AnimationStateMachineEditor)
    {
        RenderAnimationStateMachineDocument(DeltaTime);
        return;
    }

    if (EditorTabs.GetActiveTabKind() == EEditorTabKind::BlueprintEditor)
    {
        RenderBlueprintDocument(DeltaTime);
        return;
    }

	if (EditorTabs.GetActiveTabKind() == EEditorTabKind::AnimationSequenceViewer)
	{
		RenderActiveAnimationSequenceViewerDocument(DeltaTime);
		return;
	}

    RenderActiveViewerDocument(DeltaTime);
}

void FEditorMainPanel::RenderEditorPanelWindows(float DeltaTime, bool bDrawEditorPanels)
{
    const bool bLevelEditorTabActive = IsLevelEditorTabActive();

    if (bDrawEditorPanels && bLevelEditorTabActive && PanelVisibility.bShowControl)
    {
        Widgets.ControlWidget.Render(DeltaTime);
    }
    if (bDrawEditorPanels && bLevelEditorTabActive && PanelVisibility.bShowMaterialEditor)
    {
        Widgets.MaterialWidget.Render(DeltaTime);
    }
    if (bDrawEditorPanels && bLevelEditorTabActive && PanelVisibility.bShowProperty)
    {
        Widgets.PropertyWidget.Render(DeltaTime);
    }
    if (bDrawEditorPanels && bLevelEditorTabActive && PanelVisibility.bShowSceneManager)
    {
        Widgets.SceneWidget.Render(DeltaTime);
    }
    if (bDrawEditorPanels && bLevelEditorTabActive && PanelVisibility.bShowStatProfiler)
    {
        Widgets.StatWidget.Render(DeltaTime);
    }
    if (bDrawEditorPanels && bLevelEditorTabActive)
    {
        RenderEditorDebugPanel(DeltaTime);
    }
    if (bDrawEditorPanels && bLevelEditorTabActive)
    {
        RenderUndoHistoryPanel(DeltaTime);
    }
    if (bDrawEditorPanels && PanelVisibility.bShowProjectSettings)
    {
        RenderProjectSettingsPanel();
    }
    if (bDrawEditorPanels && PanelVisibility.bShowWorldSettings)
    {
        RenderWorldSettingsPanel();
    }
    if (bDrawEditorPanels && PanelVisibility.bShowEditorSettings)
    {
        RenderEditorSettingsPanel();
    }
    if (bDrawEditorPanels && Widgets.CurveEditorWidget.IsVisible())
    {
        Widgets.CurveEditorWidget.Render(DeltaTime);
    }
    if (bDrawEditorPanels && bLevelEditorTabActive && Widgets.ActorSequencerWidget.IsVisible())
    {
        Widgets.ActorSequencerWidget.Render(DeltaTime);
    }
    if (PanelVisibility.bShowConsole && Widgets.ConsoleWidget.IsFloatingWindowMode())
    {
        Widgets.ConsoleWidget.Render(DeltaTime);
    }

	for (auto& Widget : Widgets.ViewerWindowWidgets)
	{
		if (!Widget || !Widget->IsOpen())
		{
			continue;
		}

		FEditorViewer* Viewer = Widget->GetViewer();
		if (!Viewer)
		{
			continue;
		}

		FEditorTabId TabId;
		TabId = MakeEditorViewerTabId(Viewer->GetFileName(), Viewer);
		if (EditorTabs.IsTabDetached(TabId))
		{
			Widget->Render(DeltaTime);
		}
	}

	for (auto& Widget : Widgets.AnimationSequenceViewerWidgets)
	{
		if (!Widget || !Widget->IsOpen())
		{
			continue;
		}

		FEditorViewer* Viewer = Widget->GetViewer();
		if (!Viewer)
		{
			continue;
		}

		const FEditorTabId TabId = MakeAnimationSequenceViewerTabId(Viewer->GetFileName(), Viewer);
		if (EditorTabs.IsTabDetached(TabId))
		{
			Widget->Render(DeltaTime);
		}
	}

	if (Widgets.LuaAnimGraphWidget.IsOpen())
	{
		const FEditorTabId TabId = MakeLuaAnimGraphEditorTabId(Widgets.LuaAnimGraphWidget.GetAssetPath());
		if (EditorTabs.IsTabDetached(TabId))
		{
			Widgets.LuaAnimGraphWidget.Render(DeltaTime);
			if (Widgets.LuaAnimGraphWidget.ConsumeDockRequest())
			{
				RequestDetachEditorTab(TabId, false);
			}
			else if (!Widgets.LuaAnimGraphWidget.IsOpen())
			{
				RequestCloseEditorTab(TabId);
			}
		}
	}

	if (Widgets.AnimationStateMachineWidget.IsOpen())
	{
		const FEditorTabId TabId = MakeAnimationStateMachineEditorTabId(Widgets.AnimationStateMachineWidget.GetAssetPath());
		if (EditorTabs.IsTabDetached(TabId))
		{
			Widgets.AnimationStateMachineWidget.Render(DeltaTime);
			if (Widgets.AnimationStateMachineWidget.ConsumeDockRequest())
			{
				RequestDetachEditorTab(TabId, false);
			}
			else if (!Widgets.AnimationStateMachineWidget.IsOpen())
			{
				RequestCloseEditorTab(TabId);
			}
		}
	}

	FlushClosedViewerWidgets();
}

float FEditorMainPanel::ResolveEffectiveDeltaTime(float DeltaTime) const
{
    float EffectiveDeltaTime = DeltaTime;
    if (EffectiveDeltaTime <= 0.0f)
    {
        EffectiveDeltaTime = ImGui::GetIO().DeltaTime;
        if (EffectiveDeltaTime <= 0.0f)
        {
            EffectiveDeltaTime = 1.0f / 60.0f;
        }
    }
    return EffectiveDeltaTime;
}

void FEditorMainPanel::UpdateConsoleDrawerAnimation(float EffectiveDeltaTime)
{
    if (!PanelVisibility.bShowConsole)
    {
        ConsoleState.bDrawerVisible = false;
    }

    const float TargetAnim = ConsoleState.bDrawerVisible ? 1.0f : 0.0f;
    constexpr float AnimSpeed = 8.0f;
    if (ConsoleState.DrawerAnim < TargetAnim)
    {
        ConsoleState.DrawerAnim = std::min(1.0f, ConsoleState.DrawerAnim + EffectiveDeltaTime * AnimSpeed);
    }
    else if (ConsoleState.DrawerAnim > TargetAnim)
    {
        ConsoleState.DrawerAnim = std::max(0.0f, ConsoleState.DrawerAnim - EffectiveDeltaTime * AnimSpeed);
    }
}

void FEditorMainPanel::RenderLateFrameOverlays(float DeltaTime, float EffectiveDeltaTime, bool bDrawEditorPanels)
{
    UpdateFooterEventLogs();
    if (bDrawEditorPanels)
    {
        Widgets.ContentBrowserWidget.Render(DeltaTime);
        PanelVisibility.bShowContentBrowser = Widgets.ContentBrowserWidget.IsVisible();
        HandleContentBrowserViewportDrop();
    }
    RenderConsoleDrawer(DeltaTime);
    RenderFooterOverlay(DeltaTime);
    if (EditorEngine)
    {
        EditorEngine->GetNotificationService().RenderToasts(DeltaTime);
    }
}

void FEditorMainPanel::EndImGuiFrame()
{
    ImGui::Render();
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

    const ImGuiIO& IO = ImGui::GetIO();
    if (IO.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();
    }
}
