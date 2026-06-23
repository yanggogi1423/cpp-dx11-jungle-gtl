#include "Editor/UI/EditorMainPanel.h"

#include "Editor/EditorEngine.h"
#include "Editor/UI/AnimSequenceViewerContextBuilder.h"
#include "Editor/Viewer/EditorViewer.h"

#include "ImGui/imgui.h"

#include <algorithm>
#include <utility>

FEditorPropertyWidget& FEditorMainPanel::GetPropertyWidget()
{
    return Widgets.PropertyWidget;
}

FEditorMaterialWidget& FEditorMainPanel::GetMaterialWidget()
{
    return Widgets.MaterialWidget;
}

FEditorSceneWidget& FEditorMainPanel::GetSceneWidget()
{
    return Widgets.SceneWidget;
}

FEditorControlWidget& FEditorMainPanel::GetControlWidget()
{
    return Widgets.ControlWidget;
}

void FEditorMainPanel::RefreshContentBrowser()
{
    Widgets.ContentBrowserWidget.Refresh();
}

void FEditorMainPanel::InitializeEditorWidgets(UEditorEngine* InEditorEngine)
{
    Widgets.ConsoleWidget.Initialize(InEditorEngine);
    Widgets.ContentBrowserWidget.Initialize(InEditorEngine);
    Widgets.ActorSequencerWidget.Initialize(InEditorEngine);
    Widgets.AnimationStateMachineWidget.Initialize(InEditorEngine);
    Widgets.BlueprintWidget.Initialize(InEditorEngine);
    Widgets.ControlWidget.Initialize(InEditorEngine);
    Widgets.CurveEditorWidget.Initialize(InEditorEngine);
    Widgets.MaterialWidget.Initialize(InEditorEngine);
    Widgets.PropertyWidget.Initialize(InEditorEngine);
    Widgets.SceneWidget.Initialize(InEditorEngine);
    Widgets.ViewportOverlayWidget.Initialize(InEditorEngine);
    Widgets.StatWidget.Initialize(InEditorEngine);
    Widgets.PlayStreamWidget.Initialize(InEditorEngine);
    Widgets.ToolbarWidget.Initialize(InEditorEngine);
    Widgets.RuntimeUIPreviewWidget.Initialize(InEditorEngine);
    Widgets.LuaAnimGraphWidget.Initialize(InEditorEngine);
}

void FEditorMainPanel::OpenCurveAsset(const FString& CurvePath)
{
    Widgets.CurveEditorWidget.OpenCurveAsset(CurvePath);
}

void FEditorMainPanel::OpenAnimationStateMachineAsset(const FString& AssetPath)
{
    if (!Widgets.AnimationStateMachineWidget.OpenAsset(AssetPath))
    {
        return;
    }

    const FString OpenedPath = Widgets.AnimationStateMachineWidget.GetAssetPath();
    const FEditorTabId TabId = MakeAnimationStateMachineEditorTabId(OpenedPath);
    const FString TabLabel = MakeAnimationStateMachineEditorTabLabel(OpenedPath);
    EditorTabs.OpenOrFocusTab(TabId, TabLabel);
    EditorTabs.SetTabLabel(TabId, TabLabel);
    ActivateEditorTab(TabId);
}

void FEditorMainPanel::OpenViewer(FEditorViewer* Viewer)
{
    FEditorTabId ViewerTabId;
    if (Viewer)
    {
        ViewerTabId = MakeEditorViewerTabId(Viewer->GetFileName(), Viewer);
        EditorTabs.OpenOrFocusTab(ViewerTabId, MakeEditorViewerTabLabel(Viewer->GetFileName()));
    }

    for (auto& Widget : Widgets.ViewerWindowWidgets)
    {
        if (Widget->GetViewer() == Viewer)
        {
            Widget->SetOpen(true);
            if (EditorTabs.IsTabDetached(ViewerTabId))
            {
                ImGui::SetWindowFocus(Widget->GetWindowName().c_str());
                const TArray<FEditorTabEntry>& Tabs = EditorTabs.GetTabs();
                if (!Tabs.empty())
                {
                    ActivateEditorTab(Tabs[0].Id);
                }
            }
            return;
        }
    }

    for (auto* Pending : PendingOpenViewers)
    {
        if (Pending == Viewer)
        {
            return;
        }
    }

	PendingOpenViewers.push_back(Viewer);
}

void FEditorMainPanel::OpenAnimationSequenceViewer(FEditorViewer* Viewer)
{
	if (!Viewer)
	{
		return;
	}

	for (auto& Widget : Widgets.AnimationSequenceViewerWidgets)
	{
		if (Widget && Widget->GetViewer() == Viewer)
		{
			const FEditorTabId TabId = MakeAnimationSequenceViewerTabId(Viewer->GetFileName(), Viewer);
			EditorTabs.OpenOrFocusTab(TabId, MakeAnimationSequenceViewerTabLabel(Viewer->GetFileName()));
			Widget->SetOpen(true);
			if (EditorTabs.IsTabDetached(TabId))
			{
				ImGui::SetWindowFocus(Widget->GetWindowName().c_str());
				const TArray<FEditorTabEntry>& Tabs = EditorTabs.GetTabs();
				if (!Tabs.empty())
				{
					ActivateEditorTab(Tabs[0].Id);
				}
			}
			return;
		}
	}

	const FEditorTabId TabId = MakeAnimationSequenceViewerTabId(Viewer->GetFileName(), Viewer);
	EditorTabs.OpenOrFocusTab(TabId, MakeAnimationSequenceViewerTabLabel(Viewer->GetFileName()));

	auto WidgetPtr = std::make_unique<FEditorAnimationSequenceViewerWidget>();
	WidgetPtr->Initialize(EditorEngine);
	WidgetPtr->SetViewer(Viewer);
	WidgetPtr->SetOpen(true);
	Widgets.AnimationSequenceViewerWidgets.emplace_back(std::move(WidgetPtr));
}

void FEditorMainPanel::OpenAnimationSequenceAsset(const FAnimSequenceViewerContext& Context)
{
	if (!EditorEngine || !Context.AnimSequence || !Context.PreviewMesh)
	{
		return;
	}

	FEditorViewer* Viewer = EditorEngine->CreateAnimationSequencePreviewViewer(
		Context.AssetPath,
		Context.TargetSkeletalMeshPath);
	if (!Viewer)
	{
		return;
	}

	const FEditorTabId TabId = MakeAnimationSequenceViewerTabId(Context.AssetPath, Viewer);
	EditorTabs.OpenOrFocusTab(TabId, MakeAnimationSequenceViewerTabLabel(Context.AssetPath));

	for (auto& Widget : Widgets.AnimationSequenceViewerWidgets)
	{
		if (!Widget || Widget->GetViewer() != Viewer)
		{
			continue;
		}

		Widget->SetContext(Context);
		Widget->SetOpen(true);
		if (EditorTabs.IsTabDetached(TabId))
		{
			ImGui::SetWindowFocus(Widget->GetWindowName().c_str());
			const TArray<FEditorTabEntry>& Tabs = EditorTabs.GetTabs();
			if (!Tabs.empty())
			{
				ActivateEditorTab(Tabs[0].Id);
			}
		}
		return;
	}

	auto WidgetPtr = std::make_unique<FEditorAnimationSequenceViewerWidget>();
	WidgetPtr->Initialize(EditorEngine);
	WidgetPtr->SetViewer(Viewer);
	WidgetPtr->SetContext(Context);
	WidgetPtr->SetOpen(true);
	Widgets.AnimationSequenceViewerWidgets.emplace_back(std::move(WidgetPtr));
}

void FEditorMainPanel::FlushOpenViewerWidgets()
{
    auto& V = Widgets.ViewerWindowWidgets;

    for (auto* Viewer : PendingOpenViewers)
    {
        auto WidgetPtr = std::make_unique<FEditorViewerWindowWidget>();

        WidgetPtr->Initialize(EditorEngine);
        WidgetPtr->SetViewer(Viewer);
        WidgetPtr->SetOpen(true);

        Widgets.ViewerWindowWidgets.emplace_back(std::move(WidgetPtr));
    }

    PendingOpenViewers.clear();
}

void FEditorMainPanel::CloseViewer(FEditorViewer* Viewer)
{
	if (!Viewer)
	{
		return;
	}

	EditorTabs.CloseTab(MakeEditorViewerTabId(Viewer->GetFileName(), Viewer));
	EditorTabs.CloseTab(MakeAnimationSequenceViewerTabId(Viewer->GetFileName(), Viewer));
	PendingOpenViewers.erase(std::remove(PendingOpenViewers.begin(), PendingOpenViewers.end(), Viewer), PendingOpenViewers.end());

	// Open false 처리 후 Flush
    for (auto& Widget : Widgets.ViewerWindowWidgets)
		if (Widget->GetViewer() == Viewer)
		{
            Widget->SetOpen(false);
			Widget->SetViewer(nullptr);
            break;
		}

	for (auto& Widget : Widgets.AnimationSequenceViewerWidgets)
	{
		if (Widget && Widget->GetViewer() == Viewer)
		{
			Widget->SetOpen(false);
			Widget->SetViewer(nullptr);
			break;
		}
	}
}

void FEditorMainPanel::FlushClosedViewerWidgets()
{
    auto& V = Widgets.ViewerWindowWidgets;
    V.erase(
        std::remove_if(V.begin(), V.end(),
                       [](const std::unique_ptr<FEditorViewerWindowWidget>& W)
                       { return !W || !W->IsOpen() || !W->GetViewer(); }),
        V.end());

	auto& AnimViewers = Widgets.AnimationSequenceViewerWidgets;
	AnimViewers.erase(
		std::remove_if(AnimViewers.begin(), AnimViewers.end(),
			[](const std::unique_ptr<FEditorAnimationSequenceViewerWidget>& W)
			{ return !W || !W->IsOpen() || !W->GetViewer(); }),
		AnimViewers.end());
}

void FEditorMainPanel::OpenCurveFromActorSequence(
    UCurveFloatAsset* Curve,
    UActorSequenceComponent* SequenceComp,
    const FString& SourceLabel,
    const FString& SourcePath,
    int32 InitialSelectedKeyIndex)
{
    Widgets.CurveEditorWidget.OpenCurveFromActorSequence(
        Curve,
        SequenceComp,
        SourceLabel,
        SourcePath,
        InitialSelectedKeyIndex);
}

void FEditorMainPanel::OpenCurveFromAnimSequence(
    UCurveFloatAsset* Curve,
    const FString& SourceLabel,
    const FString& SourcePath,
    std::function<bool(UCurveFloatAsset*)> SaveCallback,
    int32 InitialSelectedKeyIndex)
{
    Widgets.CurveEditorWidget.OpenCurveFromAnimSequence(
        Curve,
        SourceLabel,
        SourcePath,
        std::move(SaveCallback),
        InitialSelectedKeyIndex);
}

void FEditorMainPanel::OpenActorSequencer(UActorSequenceComponent* SequenceComp)
{
    Widgets.ActorSequencerWidget.Open(SequenceComp);
}

FEditorViewer* FEditorMainPanel::ResolveAnimationSequenceViewerSource() const
{
	const FEditorTabEntry* ActiveTab = EditorTabs.GetActiveTab();
	if (ActiveTab)
	{
		if (FEditorViewerWindowWidget* ViewerWidget = FindViewerWidgetForTab(ActiveTab->Id))
		{
			return ViewerWidget->GetViewer();
		}

		if (FEditorAnimationSequenceViewerWidget* AnimWidget = FindAnimationSequenceViewerWidgetForTab(ActiveTab->Id))
		{
			return AnimWidget->GetViewer();
		}
	}

	if (!EditorEngine)
	{
		return nullptr;
	}

	for (const auto& Viewer : EditorEngine->GetViewers())
	{
		if (Viewer)
		{
			return Viewer.get();
		}
	}

	return nullptr;
}

void FEditorMainPanel::OpenAnimationSequenceViewerFromWindowMenu()
{
	OpenAnimationSequenceViewer(ResolveAnimationSequenceViewerSource());
}

void FEditorMainPanel::BindEditorWidgetCallbacks()
{
    Widgets.RuntimeUIPreviewWidget.SetRmlRenderQueue(
        [this](const FRuntimeUIRenderContext& Context)
        {
            QueueRuntimeUIDrawCallback(ImGui::GetWindowDrawList(), Context);
        });
    Widgets.ToolbarWidget.SetViewportOverlayWidget(&Widgets.ViewportOverlayWidget);
    Widgets.ToolbarWidget.SetPlayStreamWidget(&Widgets.PlayStreamWidget);
    Widgets.ToolbarWidget.SetPIEViewportFullscreenCallback([this](bool bEnabled) { SetPIEViewportFullscreenEnabled(bEnabled); });
    Widgets.ToolbarWidget.SetBuildGameCallback([this]() { RequestBuildGame(); });
    Widgets.ToolbarWidget.SetRuntimeUIPreviewOpenCallback([this]() { OpenRuntimeUIPreviewAsset(); });
    Widgets.ToolbarWidget.SetActiveCommandHandlers(
        [this](const FEditorShortcut& Shortcut)
        {
            return ExecuteActiveEditorShortcut(Shortcut);
        },
        [this](EEditorCommandId CommandId)
        {
            return ExecuteActiveEditorCommand(CommandId);
        });
    Widgets.ToolbarWidget.SetActiveMenuRenderer(
        [this]()
        {
            return RenderActiveDocumentMainMenu();
        });
    Widgets.ToolbarWidget.SetPanelVisibilityRefs(
        &PanelVisibility.bShowConsole,
        &PanelVisibility.bShowControl,
        &PanelVisibility.bShowProperty,
        &PanelVisibility.bShowSceneManager,
        &PanelVisibility.bShowMaterialEditor,
        &PanelVisibility.bShowStatProfiler,
        &PanelVisibility.bShowEditorDebug,
        &PanelVisibility.bShowContentBrowser,
        &PanelVisibility.bShowUndoHistory,
        &PanelVisibility.bShowRuntimeUIPreview,
        &PanelVisibility.bShowProjectSettings,
        &PanelVisibility.bShowWorldSettings,
        &PanelVisibility.bShowEditorSettings,
        &PIEViewportState.bFullscreenEnabled);
}

void FEditorMainPanel::ResetWidgetSelections()
{
    Widgets.PropertyWidget.ResetSelection();
    Widgets.MaterialWidget.ResetSelection();
    Widgets.ActorSequencerWidget.ResetTarget();
}
