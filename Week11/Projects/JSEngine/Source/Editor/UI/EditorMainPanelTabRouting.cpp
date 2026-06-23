#include "Editor/UI/EditorMainPanel.h"

#include "Editor/EditorEngine.h"
#include "Editor/UI/EditorAnimationSequenceViewerWidget.h"
#include "Editor/Viewer/EditorViewer.h"

#include "ImGui/imgui.h"
#include "ImGui/imgui_internal.h"

#include <cstring>

namespace
{
	int32 FindImGuiWindowZOrder(const char* WindowName)
	{
		if (!WindowName)
		{
			return 0;
		}

		ImGuiContext* Context = ImGui::GetCurrentContext();
		if (!Context)
		{
			return 0;
		}

		for (int32 Index = 0; Index < Context->Windows.Size; ++Index)
		{
			if (Context->Windows[Index] && std::strcmp(Context->Windows[Index]->Name, WindowName) == 0)
			{
				return Index;
			}
		}
		return 0;
	}

	bool IsWindowInHierarchy(ImGuiWindow* Candidate, ImGuiWindow* Root)
	{
		for (ImGuiWindow* Window = Candidate; Window; Window = Window->ParentWindow)
		{
			if (Window == Root)
			{
				return true;
			}
			if (Window->RootWindow == Root || Window->RootWindowDockTree == Root)
			{
				return true;
			}
		}
		return false;
	}

	bool IsNamedWindowFocusedOrHovered(const char* WindowName, bool bFocusedOnly)
	{
		if (!WindowName)
		{
			return false;
		}

		ImGuiContext* Context = ImGui::GetCurrentContext();
		ImGuiWindow* RootWindow = ImGui::FindWindowByName(WindowName);
		if (!Context || !RootWindow || !RootWindow->WasActive || RootWindow->Hidden)
		{
			return false;
		}

		if (IsWindowInHierarchy(Context->NavWindow, RootWindow))
		{
			return true;
		}
		if (!bFocusedOnly && IsWindowInHierarchy(Context->HoveredWindow, RootWindow))
		{
			return true;
		}
		return false;
	}

	HWND GetNamedWindowHwnd(const char* WindowName)
	{
		ImGuiWindow* RootWindow = WindowName ? ImGui::FindWindowByName(WindowName) : nullptr;
		if (!RootWindow || !RootWindow->WasActive || RootWindow->Hidden || !RootWindow->Viewport)
		{
			return nullptr;
		}

		ImGuiViewport* Viewport = RootWindow->Viewport;
		return static_cast<HWND>(
			Viewport->PlatformHandleRaw ? Viewport->PlatformHandleRaw : Viewport->PlatformHandle);
	}

	bool IsNamedPlatformWindowActive(const char* WindowName)
	{
		HWND Hwnd = GetNamedWindowHwnd(WindowName);
		if (!Hwnd || !::IsWindowVisible(Hwnd) || ::IsIconic(Hwnd))
		{
			return false;
		}

		return ::GetForegroundWindow() == Hwnd;
	}
}

bool FEditorMainPanel::IsLevelEditorTabActive() const
{
	return EditorTabs.GetActiveTabKind() == EEditorTabKind::LevelEditor;
}

bool FEditorMainPanel::IsLevelEditorViewportVisible() const
{
	return IsLevelEditorTabActive();
}

FEditorViewerWindowWidget* FEditorMainPanel::FindViewerWidgetForTab(const FEditorTabId& TabId) const
{
	if (TabId.Kind != EEditorTabKind::SkeletalMeshViewer &&
		TabId.Kind != EEditorTabKind::StaticMeshViewer)
	{
		return nullptr;
	}

	for (const auto& Widget : Widgets.ViewerWindowWidgets)
	{
		FEditorViewer* Viewer = Widget ? Widget->GetViewer() : nullptr;
		if (Viewer && Viewer->GetFileName() == TabId.PayloadId)
		{
			return Widget.get();
		}
	}

	return nullptr;
}

FEditorAnimationSequenceViewerWidget* FEditorMainPanel::FindAnimationSequenceViewerWidgetForTab(const FEditorTabId& TabId) const
{
	if (TabId.Kind != EEditorTabKind::AnimationSequenceViewer)
	{
		return nullptr;
	}

	for (const auto& Widget : Widgets.AnimationSequenceViewerWidgets)
	{
		FEditorViewer* Viewer = Widget ? Widget->GetViewer() : nullptr;
		if (Viewer && Viewer->GetFileName() == TabId.PayloadId)
		{
			return Widget.get();
		}
	}

	return nullptr;
}

FEditorViewportState* FEditorMainPanel::ResolveActiveAnimationSequenceViewportState() const
{
	const FEditorTabEntry* ActiveTab = EditorTabs.GetActiveTab();
	if (!ActiveTab || ActiveTab->Id.Kind != EEditorTabKind::AnimationSequenceViewer)
	{
		return nullptr;
	}

	FEditorAnimationSequenceViewerWidget* AnimWidget = FindAnimationSequenceViewerWidgetForTab(ActiveTab->Id);
	FEditorViewer* Viewer = AnimWidget ? AnimWidget->GetViewer() : nullptr;
	return Viewer ? &Viewer->GetViewport().GetState() : nullptr;
}

void FEditorMainPanel::RenderActiveViewerDocument(float DeltaTime)
{
	const FEditorTabEntry* ActiveTab = EditorTabs.GetActiveTab();
	if (!ActiveTab)
	{
		return;
	}

	constexpr ImGuiWindowFlags WindowFlags = 0;
	if (!ImGui::Begin("Viewport", nullptr, WindowFlags))
	{
		ImGui::End();
		return;
	}

	FEditorViewerWindowWidget* ViewerWidget = FindViewerWidgetForTab(ActiveTab->Id);
	if (ActiveTab->bDetached)
	{
		ImGui::TextDisabled("This viewer tab is detached.");
		if (ImGui::Button("Dock Back"))
		{
			RequestDetachEditorTab(ActiveTab->Id, false);
		}
	}
	else if (ViewerWidget)
	{
		ViewerWidget->RenderEmbedded(DeltaTime);
	}
	else
	{
		ImGui::TextDisabled("Viewer tab target is no longer available.");
	}

	ImGui::End();
}

void FEditorMainPanel::RenderActiveAnimationSequenceViewerDocument(float DeltaTime)
{
	const FEditorTabEntry* ActiveTab = EditorTabs.GetActiveTab();
	if (!ActiveTab || ActiveTab->Id.Kind != EEditorTabKind::AnimationSequenceViewer)
	{
		return;
	}

	if (!ImGui::Begin("Viewport", nullptr, 0))
	{
		ImGui::End();
		return;
	}

	FEditorAnimationSequenceViewerWidget* AnimWidget = FindAnimationSequenceViewerWidgetForTab(ActiveTab->Id);
	if (ActiveTab->bDetached)
	{
		ImGui::TextDisabled("This animation viewer tab is detached.");
		if (ImGui::Button("Dock Back"))
		{
			RequestDetachEditorTab(ActiveTab->Id, false);
		}
	}
	else if (AnimWidget)
	{
		AnimWidget->RenderEmbedded(DeltaTime);
	}
	else
	{
		ImGui::TextDisabled("Animation viewer target is no longer available.");
	}

	ImGui::End();
}

void FEditorMainPanel::RenderRuntimeUIPreviewDocument(float DeltaTime)
{
	const FEditorTabEntry* ActiveTab = EditorTabs.GetActiveTab();
	if (!ActiveTab || ActiveTab->Id.Kind != EEditorTabKind::RuntimeUIPreview)
	{
		return;
	}

	constexpr ImGuiWindowFlags WindowFlags = 0;
	if (!ImGui::Begin("Viewport", nullptr, WindowFlags))
	{
		ImGui::End();
		return;
	}

	Widgets.RuntimeUIPreviewWidget.RenderEmbedded(DeltaTime);
	ImGui::End();
}

void FEditorMainPanel::RenderLuaAnimGraphDocument(float DeltaTime)
{
	const FEditorTabEntry* ActiveTab = EditorTabs.GetActiveTab();
	if (!ActiveTab || ActiveTab->Id.Kind != EEditorTabKind::LuaAnimGraphEditor)
	{
		return;
	}

	if (!ImGui::Begin("Viewport", nullptr, 0))
	{
		ImGui::End();
		return;
	}

	if (ActiveTab->bDetached)
	{
		ImGui::TextDisabled("This Lua Anim Graph tab is detached.");
		if (ImGui::Button("Dock Back"))
		{
			RequestDetachEditorTab(ActiveTab->Id, false);
		}
	}
	else if (MakeLuaAnimGraphEditorTabId(Widgets.LuaAnimGraphWidget.GetAssetPath()).Matches(ActiveTab->Id))
	{
		Widgets.LuaAnimGraphWidget.RenderEmbedded(DeltaTime);
	}
	else
	{
		ImGui::TextDisabled("Lua Anim Graph target is no longer available.");
	}
	ImGui::End();
}

void FEditorMainPanel::RenderAnimationStateMachineDocument(float DeltaTime)
{
	const FEditorTabEntry* ActiveTab = EditorTabs.GetActiveTab();
	if (!ActiveTab || ActiveTab->Id.Kind != EEditorTabKind::AnimationStateMachineEditor)
	{
		return;
	}

	if (!ImGui::Begin("Viewport", nullptr, 0))
	{
		ImGui::End();
		return;
	}

	if (ActiveTab->bDetached)
	{
		ImGui::TextDisabled("This Animation State Machine tab is detached.");
		if (ImGui::Button("Dock Back"))
		{
			RequestDetachEditorTab(ActiveTab->Id, false);
		}
	}
	else if (MakeAnimationStateMachineEditorTabId(Widgets.AnimationStateMachineWidget.GetAssetPath()).Matches(ActiveTab->Id))
	{
		Widgets.AnimationStateMachineWidget.RenderEmbedded(DeltaTime);
	}
	else
	{
		ImGui::TextDisabled("Animation State Machine target is no longer available.");
	}
	ImGui::End();
}

void FEditorMainPanel::RenderBlueprintDocument(float DeltaTime)
{
	const FEditorTabEntry* ActiveTab = EditorTabs.GetActiveTab();
	if (!ActiveTab || ActiveTab->Id.Kind != EEditorTabKind::BlueprintEditor)
	{
		return;
	}

	if (!ImGui::Begin("Viewport", nullptr, 0))
	{
		ImGui::End();
		return;
	}

	Widgets.BlueprintWidget.RenderEmbedded(DeltaTime);
	ImGui::End();
}

void FEditorMainPanel::RequestDockViewer(FEditorViewer* Viewer)
{
	if (!Viewer)
	{
		return;
	}

	RequestDetachEditorTab(MakeEditorViewerTabId(Viewer->GetFileName(), Viewer), false);
}

void FEditorMainPanel::RequestDockAnimationSequenceViewer(FEditorViewer* Viewer)
{
	if (!Viewer)
	{
		return;
	}

	RequestDetachEditorTab(MakeAnimationSequenceViewerTabId(Viewer->GetFileName(), Viewer), false);
}

void FEditorMainPanel::RequestDockLuaAnimGraph()
{
	RequestDetachEditorTab(MakeLuaAnimGraphEditorTabId(Widgets.LuaAnimGraphWidget.GetAssetPath()), false);
}

void FEditorMainPanel::RequestDockAnimationStateMachine()
{
	RequestDetachEditorTab(MakeAnimationStateMachineEditorTabId(Widgets.AnimationStateMachineWidget.GetAssetPath()), false);
}

FEditorTabId FEditorMainPanel::GetInputRoutingTabId() const
{
	for (int32 Pass = 0; Pass < 2; ++Pass)
	{
		const bool bFocusedOnly = Pass == 0;
		for (const auto& Widget : Widgets.ViewerWindowWidgets)
		{
			FEditorViewer* Viewer = Widget ? Widget->GetViewer() : nullptr;
			if (!Widget || !Viewer || !Widget->IsOpen())
			{
				continue;
			}

			const FEditorTabId ViewerTabId = MakeEditorViewerTabId(Viewer->GetFileName(), Viewer);
			if (!EditorTabs.IsTabDetached(ViewerTabId))
			{
				continue;
			}

			if (IsNamedWindowFocusedOrHovered(Widget->GetWindowName().c_str(), bFocusedOnly))
			{
				return ViewerTabId;
			}
		}

		for (const auto& Widget : Widgets.AnimationSequenceViewerWidgets)
		{
			FEditorViewer* Viewer = Widget ? Widget->GetViewer() : nullptr;
			if (!Widget || !Viewer || !Widget->IsOpen())
			{
				continue;
			}

			const FEditorTabId TabId = MakeAnimationSequenceViewerTabId(Viewer->GetFileName(), Viewer);
			if (!EditorTabs.IsTabDetached(TabId))
			{
				continue;
			}

			if (IsNamedWindowFocusedOrHovered(Widget->GetWindowName().c_str(), bFocusedOnly))
			{
				return TabId;
			}
		}
	}

	const FEditorTabEntry* ActiveTab = EditorTabs.GetActiveTab();
	if (ActiveTab && !ActiveTab->bDetached)
	{
		return ActiveTab->Id;
	}

	FEditorTabId NoViewportTabId;
	NoViewportTabId.Kind = EEditorTabKind::LevelEditor;
	NoViewportTabId.PayloadId = "__NoViewportInput";
	return NoViewportTabId;
}

bool FEditorMainPanel::ShouldRouteLevelViewportInput() const
{
	const FEditorTabId RoutingTabId = GetInputRoutingTabId();
	return RoutingTabId.Kind == EEditorTabKind::LevelEditor && RoutingTabId.PayloadId.empty();
}

bool FEditorMainPanel::IsViewerViewportVisible(FEditorViewer* Viewer) const
{
	if (!Viewer)
	{
		return false;
	}

	const FEditorTabId TabId = MakeEditorViewerTabId(Viewer->GetFileName(), Viewer);
	const FEditorTabEntry* ActiveTab = EditorTabs.GetActiveTab();
	if (ActiveTab && ActiveTab->Id.Matches(TabId) && !ActiveTab->bDetached)
	{
		return true;
	}

	const FEditorTabId AnimTabId = MakeAnimationSequenceViewerTabId(Viewer->GetFileName(), Viewer);
	if (ActiveTab && ActiveTab->Id.Matches(AnimTabId) && !ActiveTab->bDetached)
	{
		return true;
	}

	const bool bSkeletalDetached = EditorTabs.IsTabDetached(TabId);
	const bool bAnimationDetached = EditorTabs.IsTabDetached(AnimTabId);
	if (!bSkeletalDetached && !bAnimationDetached)
	{
		return false;
	}

	for (const auto& Widget : Widgets.ViewerWindowWidgets)
	{
		if (Widget && Widget->GetViewer() == Viewer && Widget->IsOpen())
		{
			return true;
		}
	}
	for (const auto& Widget : Widgets.AnimationSequenceViewerWidgets)
	{
		if (Widget && Widget->GetViewer() == Viewer && Widget->IsOpen())
		{
			return true;
		}
	}
	return false;
}

bool FEditorMainPanel::ShouldRenderViewerViewport(FEditorViewer* Viewer) const
{
	if (!Viewer)
	{
		return false;
	}

	if (Viewer->ShouldRenderWithoutEditorTab())
	{
		return true;
	}

	const FEditorTabId TabId = MakeEditorViewerTabId(Viewer->GetFileName(), Viewer);
	const FEditorTabEntry* ActiveTab = EditorTabs.GetActiveTab();
	if (ActiveTab && ActiveTab->Id.Matches(TabId) && !ActiveTab->bDetached)
	{
		return true;
	}

	const FEditorTabId AnimTabId = MakeAnimationSequenceViewerTabId(Viewer->GetFileName(), Viewer);
	if (ActiveTab && ActiveTab->Id.Matches(AnimTabId) && !ActiveTab->bDetached)
	{
		return true;
	}

	if (EditorTabs.IsTabDetached(TabId))
	{
		for (const auto& Widget : Widgets.ViewerWindowWidgets)
		{
			if (Widget && Widget->GetViewer() == Viewer && Widget->IsOpen() &&
				IsNamedPlatformWindowActive(Widget->GetWindowName().c_str()))
			{
				return true;
			}
		}
	}

	if (EditorTabs.IsTabDetached(AnimTabId))
	{
		for (const auto& Widget : Widgets.AnimationSequenceViewerWidgets)
		{
			if (Widget && Widget->GetViewer() == Viewer && Widget->IsOpen() &&
				IsNamedPlatformWindowActive(Widget->GetWindowName().c_str()))
			{
				return true;
			}
		}
	}

	return false;
}

bool FEditorMainPanel::ShouldRouteViewerViewportInput(FEditorViewer* Viewer) const
{
	if (!Viewer || !IsViewerViewportVisible(Viewer))
	{
		return false;
	}

	return true;
}

int32 FEditorMainPanel::GetViewerViewportZOrder(FEditorViewer* Viewer) const
{
	if (!Viewer)
	{
		return 0;
	}

	const FEditorTabId TabId = MakeEditorViewerTabId(Viewer->GetFileName(), Viewer);
	const FEditorTabEntry* ActiveTab = EditorTabs.GetActiveTab();
	if (ActiveTab && ActiveTab->Id.Matches(TabId) && !ActiveTab->bDetached)
	{
		return FindImGuiWindowZOrder("Viewport");
	}
	const FEditorTabId AnimTabId = MakeAnimationSequenceViewerTabId(Viewer->GetFileName(), Viewer);
	if (ActiveTab && ActiveTab->Id.Matches(AnimTabId) && !ActiveTab->bDetached)
	{
		return FindImGuiWindowZOrder("Viewport");
	}

	for (const auto& Widget : Widgets.ViewerWindowWidgets)
	{
		if (Widget && Widget->GetViewer() == Viewer)
		{
			return FindImGuiWindowZOrder(Widget->GetWindowName().c_str());
		}
	}
	for (const auto& Widget : Widgets.AnimationSequenceViewerWidgets)
	{
		if (Widget && Widget->GetViewer() == Viewer)
		{
			return FindImGuiWindowZOrder(Widget->GetWindowName().c_str());
		}
	}
	return 0;
}

void FEditorMainPanel::CloseAnimationSequenceViewerTab(const FEditorTabId& TabId)
{
	for (auto& Widget : Widgets.AnimationSequenceViewerWidgets)
	{
		FEditorViewer* Viewer = Widget ? Widget->GetViewer() : nullptr;
		if (Viewer && Viewer->GetFileName() == TabId.PayloadId)
		{
			if (EditorEngine)
			{
				EditorEngine->RemoveViewer(Viewer);
				return;
			}
		}
	}

	EditorTabs.CloseTab(TabId);
}
