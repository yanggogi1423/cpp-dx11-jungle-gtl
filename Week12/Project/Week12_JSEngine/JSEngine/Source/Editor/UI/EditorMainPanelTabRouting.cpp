#include "Editor/UI/EditorMainPanel.h"

#include "Editor/UI/EditorDetachedWindowChrome.h"
#include "Editor/Viewer/EditorViewer.h"

#include "ImGui/imgui.h"
#include "ImGui/imgui_internal.h"

#include <cstring>

namespace
{
	constexpr const char* ParticleDetachedWindowName = "Particle System Editor###ParticleSystemEditorDetached";
	constexpr const char* AnimGraphDetachedWindowName = "Anim Graph###AnimGraphEditorDetached";
	constexpr const char* LuaAnimGraphDetachedWindowName = "Lua Anim Graph###LuaAnimGraphEditorDetached";

	FString GetFileNameFromPath(const FString& Path)
	{
		const size_t SlashIndex = Path.find_last_of("/\\");
		return SlashIndex == FString::npos ? Path : Path.substr(SlashIndex + 1);
	}

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
}

bool FEditorMainPanel::IsLevelEditorTabActive() const
{
	return EditorTabs.GetActiveTabKind() == EEditorTabKind::LevelEditor;
}

bool FEditorMainPanel::IsLevelEditorViewportVisible() const
{
	return IsLevelEditorTabActive();
}

bool FEditorMainPanel::IsParticlePreviewViewportVisible() const
{
	const FEditorTabEntry* ActiveTab = EditorTabs.GetActiveTab();
	const bool bDockedActive = ActiveTab &&
		ActiveTab->Id.Kind == EEditorTabKind::ParticleSystemEditor &&
		!ActiveTab->bDetached;

	bool bDetachedOpen = false;
	for (const FEditorTabEntry& Tab : EditorTabs.GetTabs())
	{
		if (Tab.Id.Kind == EEditorTabKind::ParticleSystemEditor && Tab.bDetached && bDetachedParticleSystemEditorOpen)
		{
			bDetachedOpen = true;
			break;
		}
	}

	return (bDockedActive || bDetachedOpen) &&
		Widgets.ParticleSystemWidget.IsPreviewViewportVisible() &&
		Widgets.ParticleSystemWidget.HasValidPreviewViewportRect();
}

FSceneViewport* FEditorMainPanel::GetParticlePreviewViewport()
{
	return Widgets.ParticleSystemWidget.GetPreviewViewport();
}

const FSceneViewport* FEditorMainPanel::GetParticlePreviewViewport() const
{
	return Widgets.ParticleSystemWidget.GetPreviewViewport();
}

FEditorViewerWindowWidget* FEditorMainPanel::FindViewerWidgetForTab(const FEditorTabId& TabId) const
{
	if (TabId.Kind != EEditorTabKind::SkeletalMeshViewer &&
		TabId.Kind != EEditorTabKind::StaticMeshViewer &&
		TabId.Kind != EEditorTabKind::AnimSequenceViewer)
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

void FEditorMainPanel::RenderAnimGraphEditorDocument(float DeltaTime)
{
	const FEditorTabEntry* ActiveTab = EditorTabs.GetActiveTab();
	if (!ActiveTab || ActiveTab->Id.Kind != EEditorTabKind::AnimGraphEditor)
	{
		return;
	}

	constexpr ImGuiWindowFlags WindowFlags = 0;
	if (!ImGui::Begin("Viewport", nullptr, WindowFlags))
	{
		ImGui::End();
		return;
	}

	if (ActiveTab->bDetached)
	{
		ImGui::TextDisabled("This Anim Graph tab is detached.");
		if (ImGui::Button("Dock Back"))
		{
			RequestDetachEditorTab(ActiveTab->Id, false);
		}
		ImGui::End();
		return;
	}

	if (!ActiveTab->Id.PayloadId.empty()
		&& Widgets.AnimGraphWidget.GetEditingPath() != ActiveTab->Id.PayloadId)
	{
		Widgets.AnimGraphWidget.Open(ActiveTab->Id.PayloadId);
	}

	EditorTabs.SetTabDirty(ActiveTab->Id, Widgets.AnimGraphWidget.IsDirty());
	Widgets.AnimGraphWidget.RenderEmbedded(DeltaTime);
	ImGui::End();
}

void FEditorMainPanel::RenderLuaAnimGraphEditorDocument(float DeltaTime)
{
	const FEditorTabEntry* ActiveTab = EditorTabs.GetActiveTab();
	if (!ActiveTab || ActiveTab->Id.Kind != EEditorTabKind::LuaAnimGraphEditor)
	{
		return;
	}

	constexpr ImGuiWindowFlags WindowFlags = 0;
	if (!ImGui::Begin("Viewport", nullptr, WindowFlags))
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
		ImGui::End();
		return;
	}

	if (!ActiveTab->Id.PayloadId.empty()
		&& Widgets.LuaAnimGraphWidget.GetAssetPath() != ActiveTab->Id.PayloadId)
	{
		Widgets.LuaAnimGraphWidget.OpenAsset(ActiveTab->Id.PayloadId);
	}

	EditorTabs.SetTabDirty(ActiveTab->Id, Widgets.LuaAnimGraphWidget.IsDirty());
	Widgets.LuaAnimGraphWidget.RenderEmbedded(DeltaTime);
	ImGui::End();
}

void FEditorMainPanel::RenderParticleSystemEditorDocument(float DeltaTime)
{
	const FEditorTabEntry* ActiveTab = EditorTabs.GetActiveTab();
	if (!ActiveTab || ActiveTab->Id.Kind != EEditorTabKind::ParticleSystemEditor)
	{
		return;
	}

	constexpr ImGuiWindowFlags WindowFlags = 0;
	if (!ImGui::Begin("Viewport", nullptr, WindowFlags))
	{
		ImGui::End();
		return;
	}

	if (ActiveTab->bDetached)
	{
		ImGui::TextDisabled("This particle editor tab is detached.");
		if (ImGui::Button("Dock Back"))
		{
			RequestDetachEditorTab(ActiveTab->Id, false);
		}
		ImGui::End();
		return;
	}

	if (!ActiveTab->Id.PayloadId.empty() &&
		Widgets.ParticleSystemWidget.GetDocumentPath() != ActiveTab->Id.PayloadId)
	{
		Widgets.ParticleSystemWidget.OpenParticleSystem(ActiveTab->Id.PayloadId);
	}

	EditorTabs.SetTabDirty(ActiveTab->Id, Widgets.ParticleSystemWidget.IsDirty());
	Widgets.ParticleSystemWidget.RenderEmbedded(DeltaTime);
	ImGui::End();
}

void FEditorMainPanel::RenderDetachedAnimGraphEditorDocument(float DeltaTime)
{
	const FEditorTabEntry* DetachedTab = nullptr;
	for (const FEditorTabEntry& Tab : EditorTabs.GetTabs())
	{
		if (Tab.Id.Kind == EEditorTabKind::AnimGraphEditor && Tab.bDetached)
		{
			DetachedTab = &Tab;
			break;
		}
	}
	if (!DetachedTab)
	{
		return;
	}

	if (!DetachedTab->Id.PayloadId.empty() &&
		Widgets.AnimGraphWidget.GetEditingPath() != DetachedTab->Id.PayloadId)
	{
		Widgets.AnimGraphWidget.Open(DetachedTab->Id.PayloadId);
	}

	const float TitleBarFramePaddingY = FEditorDetachedWindowChrome::GetTitleBarFramePaddingY();
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(13.0f, TitleBarFramePaddingY));
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(9.0f, 4.0f));
	ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.055f, 0.060f, 0.072f, 1.0f));
	ImGui::PushStyleColor(ImGuiCol_MenuBarBg, ImVec4(0.055f, 0.060f, 0.072f, 1.0f));
	ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.18f, 0.20f, 0.25f, 1.0f));
	ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.15f, 0.17f, 0.22f, 1.0f));

	FEditorDetachedWindowChrome::ApplyWindowClass(0x4A534147u); // "JSAG"
	ImGui::SetNextWindowSize(ImVec2(1240.0f, 760.0f), ImGuiCond_FirstUseEver);
	if (const ImGuiViewport* MainViewport = ImGui::GetMainViewport())
	{
		ImGui::SetNextWindowPos(ImVec2(MainViewport->Pos.x + 132.0f, MainViewport->Pos.y + 96.0f), ImGuiCond_FirstUseEver);
	}

	bool bOpen = true;
	constexpr ImGuiWindowFlags WindowFlags =
		ImGuiWindowFlags_MenuBar |
		ImGuiWindowFlags_NoTitleBar |
		ImGuiWindowFlags_NoCollapse;

	if (ImGui::Begin(AnimGraphDetachedWindowName, &bOpen, WindowFlags))
	{
		static ImVec2 LastWindowPos(0.0f, 0.0f);
		static bool bDraggingWindow = false;
		bool bDockRequested = false;
		bool bCloseRequested = false;
		const bool bDockByDraggingToTabStrip =
			FEditorDetachedWindowChrome::WasCurrentWindowDraggedToMainTabStrip(LastWindowPos, bDraggingWindow);

		const FString Title = FString("Anim Graph - ") + GetFileNameFromPath(DetachedTab->Id.PayloadId);
		FEditorDetachedWindowChrome::RenderMenuBar(
			Title.c_str(),
			"AnimGraphDetached",
			[this, &bDockRequested]()
			{
				if (ImGui::BeginMenu("File"))
				{
					const char* SaveLabel = Widgets.AnimGraphWidget.IsDirty() ? "Save Asset *" : "Save Asset";
					if (ImGui::MenuItem(SaveLabel, "Ctrl+S"))
					{
						Widgets.AnimGraphWidget.Save();
					}
					ImGui::EndMenu();
				}
				if (ImGui::BeginMenu("Window"))
				{
					if (ImGui::MenuItem("Dock Back"))
					{
						bDockRequested = true;
					}
					ImGui::EndMenu();
				}
				if (ImGui::BeginMenu("Settings"))
				{
					if (ImGui::MenuItem("Editor Settings"))
					{
						OpenEditorSettingsPanel();
					}
					if (ImGui::MenuItem("Project Settings"))
					{
						OpenProjectSettingsPanel();
					}
					if (ImGui::MenuItem("World Settings"))
					{
						OpenWorldSettingsPanel();
					}
					ImGui::EndMenu();
				}
			},
			bCloseRequested);

		EditorTabs.SetTabDirty(DetachedTab->Id, Widgets.AnimGraphWidget.IsDirty());
		Widgets.AnimGraphWidget.RenderEmbedded(DeltaTime);

		if (bDockRequested || bDockByDraggingToTabStrip)
		{
			RequestDetachEditorTab(DetachedTab->Id, false);
		}
		else if (bCloseRequested)
		{
			RequestCloseEditorTab(DetachedTab->Id);
		}
	}
	ImGui::End();

	ImGui::PopStyleColor(4);
	ImGui::PopStyleVar(5);

	if (!bOpen)
	{
		RequestCloseEditorTab(DetachedTab->Id);
	}
}

void FEditorMainPanel::RenderDetachedLuaAnimGraphEditorDocument(float DeltaTime)
{
	const FEditorTabEntry* DetachedTab = nullptr;
	for (const FEditorTabEntry& Tab : EditorTabs.GetTabs())
	{
		if (Tab.Id.Kind == EEditorTabKind::LuaAnimGraphEditor && Tab.bDetached)
		{
			DetachedTab = &Tab;
			break;
		}
	}
	if (!DetachedTab)
	{
		return;
	}

	if (!DetachedTab->Id.PayloadId.empty() &&
		Widgets.LuaAnimGraphWidget.GetAssetPath() != DetachedTab->Id.PayloadId)
	{
		Widgets.LuaAnimGraphWidget.OpenAsset(DetachedTab->Id.PayloadId);
	}

	const float TitleBarFramePaddingY = FEditorDetachedWindowChrome::GetTitleBarFramePaddingY();
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(13.0f, TitleBarFramePaddingY));
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(9.0f, 4.0f));
	ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.055f, 0.060f, 0.072f, 1.0f));
	ImGui::PushStyleColor(ImGuiCol_MenuBarBg, ImVec4(0.055f, 0.060f, 0.072f, 1.0f));
	ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.18f, 0.20f, 0.25f, 1.0f));
	ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.15f, 0.17f, 0.22f, 1.0f));

	FEditorDetachedWindowChrome::ApplyWindowClass(0x4A534C41u); // "JSLA"
	ImGui::SetNextWindowSize(ImVec2(1240.0f, 760.0f), ImGuiCond_FirstUseEver);
	if (const ImGuiViewport* MainViewport = ImGui::GetMainViewport())
	{
		ImGui::SetNextWindowPos(ImVec2(MainViewport->Pos.x + 144.0f, MainViewport->Pos.y + 108.0f), ImGuiCond_FirstUseEver);
	}

	bool bOpen = true;
	constexpr ImGuiWindowFlags WindowFlags =
		ImGuiWindowFlags_MenuBar |
		ImGuiWindowFlags_NoTitleBar |
		ImGuiWindowFlags_NoCollapse;

	if (ImGui::Begin(LuaAnimGraphDetachedWindowName, &bOpen, WindowFlags))
	{
		static ImVec2 LastWindowPos(0.0f, 0.0f);
		static bool bDraggingWindow = false;
		bool bDockRequested = false;
		bool bCloseRequested = false;
		const bool bDockByDraggingToTabStrip =
			FEditorDetachedWindowChrome::WasCurrentWindowDraggedToMainTabStrip(LastWindowPos, bDraggingWindow);

		const FString Title = FString("Lua Anim Graph - ") + GetFileNameFromPath(DetachedTab->Id.PayloadId);
		FEditorDetachedWindowChrome::RenderMenuBar(
			Title.c_str(),
			"LuaAnimGraphDetached",
			[this, &bDockRequested]()
			{
				if (ImGui::BeginMenu("File"))
				{
					const char* SaveLabel = Widgets.LuaAnimGraphWidget.IsDirty() ? "Save Asset *" : "Save Asset";
					if (ImGui::MenuItem(SaveLabel, "Ctrl+S"))
					{
						Widgets.LuaAnimGraphWidget.SaveAsset();
					}
					ImGui::EndMenu();
				}
				if (ImGui::BeginMenu("Window"))
				{
					if (ImGui::MenuItem("Dock Back"))
					{
						bDockRequested = true;
					}
					ImGui::EndMenu();
				}
				if (ImGui::BeginMenu("Settings"))
				{
					if (ImGui::MenuItem("Editor Settings"))
					{
						OpenEditorSettingsPanel();
					}
					if (ImGui::MenuItem("Project Settings"))
					{
						OpenProjectSettingsPanel();
					}
					if (ImGui::MenuItem("World Settings"))
					{
						OpenWorldSettingsPanel();
					}
					ImGui::EndMenu();
				}
			},
			bCloseRequested);

		EditorTabs.SetTabDirty(DetachedTab->Id, Widgets.LuaAnimGraphWidget.IsDirty());
		Widgets.LuaAnimGraphWidget.RenderEmbedded(DeltaTime);

		if (bDockRequested || bDockByDraggingToTabStrip)
		{
			RequestDetachEditorTab(DetachedTab->Id, false);
		}
		else if (bCloseRequested)
		{
			RequestCloseEditorTab(DetachedTab->Id);
		}
	}
	ImGui::End();

	ImGui::PopStyleColor(4);
	ImGui::PopStyleVar(5);

	if (!bOpen)
	{
		RequestCloseEditorTab(DetachedTab->Id);
	}
}

void FEditorMainPanel::RenderDetachedParticleSystemEditorDocument(float DeltaTime)
{
	if (!bDetachedParticleSystemEditorOpen)
	{
		return;
	}

	const FEditorTabEntry* ParticleTab = nullptr;
	for (const FEditorTabEntry& Tab : EditorTabs.GetTabs())
	{
		if (Tab.Id.Kind == EEditorTabKind::ParticleSystemEditor && Tab.bDetached)
		{
			ParticleTab = &Tab;
			break;
		}
	}
	if (!ParticleTab)
	{
		bDetachedParticleSystemEditorOpen = false;
		return;
	}

	if (!ParticleTab->Id.PayloadId.empty() &&
		Widgets.ParticleSystemWidget.GetDocumentPath() != ParticleTab->Id.PayloadId)
	{
		Widgets.ParticleSystemWidget.OpenParticleSystem(ParticleTab->Id.PayloadId);
	}

	const float TitleBarFramePaddingY = FEditorDetachedWindowChrome::GetTitleBarFramePaddingY();

	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(13.0f, TitleBarFramePaddingY));
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(9.0f, 4.0f));
	ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.055f, 0.060f, 0.072f, 1.0f));
	ImGui::PushStyleColor(ImGuiCol_MenuBarBg, ImVec4(0.055f, 0.060f, 0.072f, 1.0f));
	ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.18f, 0.20f, 0.25f, 1.0f));
	ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.15f, 0.17f, 0.22f, 1.0f));

	FEditorDetachedWindowChrome::ApplyWindowClass(0x4A535045u); // "JSPE" - detached particle editor window class
	ImGui::SetNextWindowSize(ImVec2(1200.0f, 720.0f), ImGuiCond_FirstUseEver);
	if (const ImGuiViewport* MainViewport = ImGui::GetMainViewport())
	{
		ImGui::SetNextWindowPos(
			ImVec2(MainViewport->Pos.x + 120.0f, MainViewport->Pos.y + 90.0f),
			ImGuiCond_FirstUseEver);
	}

	bool bOpen = bDetachedParticleSystemEditorOpen;
	constexpr ImGuiWindowFlags WindowFlags =
		ImGuiWindowFlags_MenuBar |
		ImGuiWindowFlags_NoTitleBar |
		ImGuiWindowFlags_NoCollapse;

	if (ImGui::Begin(ParticleDetachedWindowName, &bOpen, WindowFlags))
	{
		static ImVec2 LastDetachedParticleWindowPos(0.0f, 0.0f);
		static bool bDraggingDetachedParticleWindow = false;
		bool bDockRequested = false;
		bool bCloseRequested = false;
		const bool bDockByDraggingToTabStrip =
			FEditorDetachedWindowChrome::WasCurrentWindowDraggedToMainTabStrip(
				LastDetachedParticleWindowPos,
				bDraggingDetachedParticleWindow);
		Widgets.ParticleSystemWidget.RenderDetachedDocumentChrome(bDockRequested, bCloseRequested);

		ImGui::BeginChild("##DetachedParticleToolbar", ImVec2(0.0f, 40.0f), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
		ImGui::SetCursorPos(ImVec2(8.0f, 6.0f));
		Widgets.ParticleSystemWidget.RenderDocumentToolbarControls();
		ImGui::EndChild();

		EditorTabs.SetTabDirty(ParticleTab->Id, Widgets.ParticleSystemWidget.IsDirty());
		Widgets.ParticleSystemWidget.RenderEmbedded(DeltaTime);

		if (bDockRequested || bDockByDraggingToTabStrip)
		{
			RequestDetachEditorTab(ParticleTab->Id, false);
		}
		else if (bCloseRequested)
		{
			RequestCloseEditorTab(ParticleTab->Id);
		}
	}
	ImGui::End();

	ImGui::PopStyleColor(4);
	ImGui::PopStyleVar(5);

	if (!bOpen)
	{
		RequestCloseEditorTab(ParticleTab->Id);
	}
}

void FEditorMainPanel::RequestDockViewer(FEditorViewer* Viewer)
{
	if (!Viewer)
	{
		return;
	}

	RequestDetachEditorTab(MakeEditorViewerTabId(Viewer->GetFileName(), Viewer), false);
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

		for (const FEditorTabEntry& Tab : EditorTabs.GetTabs())
		{
			if (Tab.Id.Kind != EEditorTabKind::ParticleSystemEditor || !Tab.bDetached || !bDetachedParticleSystemEditorOpen)
			{
				continue;
			}

			if (IsNamedWindowFocusedOrHovered(ParticleDetachedWindowName, bFocusedOnly))
			{
				return Tab.Id;
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

	if (!EditorTabs.IsTabDetached(TabId))
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

bool FEditorMainPanel::ShouldRouteParticlePreviewViewportInput() const
{
	return IsParticlePreviewViewportVisible();
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

	for (const auto& Widget : Widgets.ViewerWindowWidgets)
	{
		if (Widget && Widget->GetViewer() == Viewer)
		{
			return FindImGuiWindowZOrder(Widget->GetWindowName().c_str());
		}
	}
	return 0;
}

int32 FEditorMainPanel::GetParticlePreviewViewportZOrder() const
{
	const FEditorTabEntry* ActiveTab = EditorTabs.GetActiveTab();
	if (ActiveTab &&
		ActiveTab->Id.Kind == EEditorTabKind::ParticleSystemEditor &&
		!ActiveTab->bDetached)
	{
		return FindImGuiWindowZOrder("Viewport");
	}

	return FindImGuiWindowZOrder(ParticleDetachedWindowName);
}
