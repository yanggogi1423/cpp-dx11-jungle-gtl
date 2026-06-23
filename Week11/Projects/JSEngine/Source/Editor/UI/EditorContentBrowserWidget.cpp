#include "Editor/UI/EditorContentBrowserWidget.h"

#include "Editor/EditorEngine.h"
#include "Editor/EditorRenderPipeline.h"
#include "Editor/UI/EditorChromeConstants.h"
#include "Editor/UI/EditorMainPanel.h"
#include "Editor/Settings/EditorSettings.h"
#include "Editor/UI/AnimSequenceViewerContextBuilder.h"
#include "Animation/AnimationStateMachine.h"
#include "Asset/AssetFile.h"
#include "Asset/AssetMetaData.h"
#include "Asset/CurveFloatAsset.h"
#include "Asset/EditorAssetImportService.h"
#include "Blueprint/BlueprintAsset.h"
#include "Component/BlueprintComponent.h"
#include "Animation/AnimLuaProgramAsset.h"
#include "AnimGraph/LuaAnimGraph.h"
#include "Asset/SkeletalMesh.h"
#include "Asset/StaticMesh.h"
#include "Core/ResourceManager.h"
#include "Runtime/Script/ScriptManager.h"
#include "Render/Resource/Material.h"
#include "Render/Renderer/Renderer.h"
#include "UI/RuntimeUILayoutAsset.h"
#include "ImGui/imgui.h"
#include "ImGui/imgui_impl_win32.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <d3d11.h>
#include <fstream>
#include <functional>
#include <Windows.h>
#include <shellapi.h>

namespace
{
constexpr uint32 ContentBrowserThumbnailSnapshotSize = 128;

bool IsParentDirectoryReference(const std::filesystem::path& Path)
{
	for (const std::filesystem::path& Part : Path)
	{
		if (Part == L"..")
		{
			return true;
		}
	}
	return false;
}

std::filesystem::path ResolveBrowserPath(const FString& SavedPath)
{
	std::filesystem::path Path;
	if (SavedPath.empty())
	{
		Path = std::filesystem::path(FPaths::RootDir()) / L"Asset";
	}
	else
	{
		Path = FPaths::ToWide(SavedPath);
		if (!Path.is_absolute())
		{
			Path = std::filesystem::path(FPaths::RootDir()) / Path;
		}
	}

	Path = Path.lexically_normal();
	if (std::filesystem::exists(Path) && std::filesystem::is_directory(Path))
	{
		return Path;
	}
	return std::filesystem::path(FPaths::RootDir()).lexically_normal();
}

FString MakeSavedBrowserPath(const std::filesystem::path& Path)
{
	const std::filesystem::path Root = std::filesystem::path(FPaths::RootDir()).lexically_normal();
	const std::filesystem::path Normalized = Path.lexically_normal();
	const std::filesystem::path Relative = Normalized.lexically_relative(Root);
	if (!Relative.empty() && !IsParentDirectoryReference(Relative))
	{
		return FPaths::ToUtf8(Relative.generic_wstring());
	}
	return FPaths::ToUtf8(Normalized.wstring());
}

FString ToLower(FString Value)
{
	std::transform(Value.begin(), Value.end(), Value.begin(),
		[](unsigned char Ch) { return static_cast<char>(std::tolower(Ch)); });
	return Value;
}

FString SanitizeAssetFileStem(const FString& Value, const FString& Fallback)
{
	FString Result;
	Result.reserve(Value.size());

	bool bLastWasSeparator = false;
	for (unsigned char Ch : Value)
	{
		const bool bInvalid =
			Ch < 32 ||
			Ch == '<' || Ch == '>' || Ch == ':' || Ch == '"' ||
			Ch == '/' || Ch == '\\' || Ch == '|' || Ch == '?' || Ch == '*';

		if (bInvalid || std::isspace(Ch))
		{
			if (!Result.empty() && !bLastWasSeparator)
			{
				Result.push_back('_');
				bLastWasSeparator = true;
			}
			continue;
		}

		Result.push_back(static_cast<char>(Ch));
		bLastWasSeparator = false;
	}

	while (!Result.empty() && (Result.back() == '_' || Result.back() == '.' || Result.back() == ' '))
	{
		Result.pop_back();
	}

	return Result.empty() ? Fallback : Result;
}

FString MakeClipAssetStem(
	const FString& Prefix,
	const FFbxAnimationClipInfo& Clip,
	const FString& FallbackPrefix)
{
	const FString SafePrefix = SanitizeAssetFileStem(Prefix, FallbackPrefix);
	const FString ClipFallback = FString("Clip") + std::to_string(Clip.AnimStackIndex);
	const FString SafeClipName = SanitizeAssetFileStem(Clip.Name, ClipFallback);
	return SafePrefix + "_" + SafeClipName;
}

void ApplyContentBrowserWindowClass()
{
	ImGuiWindowClass WindowClass;
	WindowClass.ClassId = 0x4A534342u; // "JSCB" - content browser detached window class
	WindowClass.ViewportFlagsOverrideSet =
		ImGuiViewportFlags_NoAutoMerge |
		ImGuiViewportFlags_NoDecoration;
	WindowClass.ViewportFlagsOverrideClear = ImGuiViewportFlags_NoTaskBarIcon;
	ImGui::SetNextWindowClass(&WindowClass);
}

HWND GetCurrentViewportHwnd()
{
	ImGuiViewport* Viewport = ImGui::GetWindowViewport();
	if (!Viewport)
	{
		return nullptr;
	}
	return static_cast<HWND>(Viewport->PlatformHandleRaw ? Viewport->PlatformHandleRaw : Viewport->PlatformHandle);
}

ImGui_ImplWin32_CustomChromeRect MakeChromeRect(const ImVec2& Min, const ImVec2& Max, const ImVec2& WindowPos)
{
	return ImGui_ImplWin32_CustomChromeRect{
		static_cast<int>(Min.x - WindowPos.x),
		static_cast<int>(Min.y - WindowPos.y),
		static_cast<int>(Max.x - WindowPos.x),
		static_cast<int>(Max.y - WindowPos.y)
	};
}

void AddChromeRect(ImGui_ImplWin32_CustomChromeRect* Rects, int& Count, const ImVec2& Min, const ImVec2& Max, const ImVec2& WindowPos)
{
	if (Count >= 16)
	{
		return;
	}
	Rects[Count++] = MakeChromeRect(Min, Max, WindowPos);
}

bool IsViewportMaximized(HWND Hwnd)
{
	return Hwnd && ::IsZoomed(Hwnd) != FALSE;
}

void ToggleViewportMaximize(HWND Hwnd)
{
	if (!Hwnd)
	{
		return;
	}
	::PostMessageW(Hwnd, WM_SYSCOMMAND, IsViewportMaximized(Hwnd) ? SC_RESTORE : SC_MAXIMIZE, 0);
}

bool DrawContentBrowserWindowButton(
	const char* Id,
	const char* Tooltip,
	const ImVec2& Size,
	const ImVec4& HoverColor,
	const ImVec4& ActiveColor,
	const std::function<void(ImDrawList*, const ImVec2&, const ImVec2&, ImU32)>& DrawIcon)
{
	ImGui::PushID(Id);
	ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, HoverColor);
	ImGui::PushStyleColor(ImGuiCol_ButtonActive, ActiveColor);

	const bool bClicked = ImGui::InvisibleButton("##Button", Size);
	const bool bHovered = ImGui::IsItemHovered();
	const bool bActive = ImGui::IsItemActive();
	const ImVec2 Min = ImGui::GetItemRectMin();
	const ImVec2 Max = ImGui::GetItemRectMax();
	const ImU32 BgColor = ImGui::GetColorU32(
		bActive ? ActiveColor : (bHovered ? HoverColor : ImVec4(0.0f, 0.0f, 0.0f, 0.0f)));

	ImDrawList* DrawList = ImGui::GetWindowDrawList();
	DrawList->AddRectFilled(Min, Max, BgColor, 0.0f);
	DrawIcon(DrawList, Min, Max, ImGui::GetColorU32(ImVec4(0.82f, 0.85f, 0.90f, 1.0f)));

	if (bHovered && Tooltip)
	{
		ImGui::SetTooltip("%s", Tooltip);
	}

	ImGui::PopStyleColor(3);
	ImGui::PopID();
	return bClicked;
}

bool DrawContentBrowserArrowButton(
	const char* Id,
	const char* Tooltip,
	const ImVec2& Size,
	bool bPointUp,
	bool bEnabled)
{
	ImGui::PushID(Id);
	if (!bEnabled)
	{
		ImGui::BeginDisabled();
	}

	const bool bClicked = ImGui::InvisibleButton("##ArrowButton", Size) && bEnabled;
	const bool bHovered = bEnabled && ImGui::IsItemHovered();
	const bool bActive = bEnabled && ImGui::IsItemActive();
	const ImVec2 Min = ImGui::GetItemRectMin();
	const ImVec2 Max = ImGui::GetItemRectMax();
	ImDrawList* DrawList = ImGui::GetWindowDrawList();

	const ImVec4 BgColor = bActive
		? ImVec4(0.20f, 0.24f, 0.31f, 1.0f)
		: (bHovered ? ImVec4(0.17f, 0.20f, 0.26f, 1.0f) : ImVec4(0.14f, 0.16f, 0.20f, 1.0f));
	const ImVec4 BorderColor = bEnabled
		? ImVec4(0.24f, 0.28f, 0.35f, 1.0f)
		: ImVec4(0.18f, 0.20f, 0.24f, 1.0f);
	const ImU32 IconColor = ImGui::GetColorU32(
		bEnabled ? ImVec4(0.80f, 0.85f, 0.94f, 1.0f) : ImVec4(0.42f, 0.45f, 0.52f, 1.0f));

	DrawList->AddRectFilled(Min, Max, ImGui::GetColorU32(BgColor), 6.0f);
	DrawList->AddRect(Min, Max, ImGui::GetColorU32(BorderColor), 6.0f);

	const ImVec2 Center((Min.x + Max.x) * 0.5f, (Min.y + Max.y) * 0.5f);
	if (bPointUp)
	{
		DrawList->AddLine(ImVec2(Center.x, Center.y - 5.5f), ImVec2(Center.x - 6.0f, Center.y + 1.0f), IconColor, 1.8f);
		DrawList->AddLine(ImVec2(Center.x, Center.y - 5.5f), ImVec2(Center.x + 6.0f, Center.y + 1.0f), IconColor, 1.8f);
		DrawList->AddLine(ImVec2(Center.x, Center.y - 4.0f), ImVec2(Center.x, Center.y + 6.0f), IconColor, 1.8f);
	}
	else
	{
		DrawList->AddLine(ImVec2(Center.x - 6.0f, Center.y), ImVec2(Center.x + 6.0f, Center.y), IconColor, 1.8f);
		DrawList->AddLine(ImVec2(Center.x - 6.0f, Center.y), ImVec2(Center.x - 1.0f, Center.y - 5.0f), IconColor, 1.8f);
		DrawList->AddLine(ImVec2(Center.x - 6.0f, Center.y), ImVec2(Center.x - 1.0f, Center.y + 5.0f), IconColor, 1.8f);
	}

	if (!bEnabled)
	{
		ImGui::EndDisabled();
	}
	if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && Tooltip)
	{
		ImGui::SetTooltip("%s", Tooltip);
	}

	ImGui::PopID();
	return bClicked;
}

const char* GetAssetClassDisplayName(const FString& ClassName)
{
	if (ClassName == "UStaticMesh") return "Static Mesh";
	if (ClassName == "USkeletalMesh") return "Skeletal Mesh";
	if (ClassName == "UAnimSequence") return "Animation Sequence";
	if (ClassName == "UAnimLuaProgramAsset") return "Animation Lua Program";
	if (ClassName == "URuntimeUILayoutAsset") return "Runtime UI Layout";
	if (ClassName == "UAnimationStateMachine") return "Animation State Machine";
	if (ClassName == "UBlueprintAsset") return "Blueprint";
	return "UASSET";
}

const char* GetAssetClassBadge(const FString& ClassName)
{
	if (ClassName == "UStaticMesh") return "MESH";
	if (ClassName == "USkeletalMesh") return "SKEL";
	if (ClassName == "UAnimSequence") return "ANIM";
	if (ClassName == "UAnimLuaProgramAsset") return "ANIM LUA";
	if (ClassName == "URuntimeUILayoutAsset") return "UI";
	if (ClassName == "UAnimationStateMachine") return "STATE";
	if (ClassName == "UBlueprintAsset") return "BP";
	return "UASSET";
}

}

void FEditorContentBrowserWidget::Initialize(UEditorEngine* InEditorEngine)
{
	FEditorWidget::Initialize(InEditorEngine);
	RootPath = std::filesystem::path(FPaths::RootDir()).lexically_normal();
	std::error_code CreateScriptDirEc;
	std::filesystem::create_directories(RootPath / L"Asset/Script", CreateScriptDirEc);
	BrowserRootPaths.clear();
	for (const wchar_t* RootName : { L"Asset", L"Shaders" })
	{
		std::filesystem::path Candidate = (RootPath / RootName).lexically_normal();
		std::error_code Ec;
		if (std::filesystem::exists(Candidate, Ec) && std::filesystem::is_directory(Candidate, Ec))
		{
			BrowserRootPaths.push_back(Candidate);
		}
	}
	if (BrowserRootPaths.empty())
	{
		BrowserRootPaths.push_back(RootPath);
	}
	LoadFromSettings();
	Refresh();
}

void FEditorContentBrowserWidget::OpenAssetRoot()
{
	std::filesystem::path AssetRoot = (RootPath / L"Asset").lexically_normal();
	std::error_code Ec;
	if (!std::filesystem::exists(AssetRoot, Ec) || !std::filesystem::is_directory(AssetRoot, Ec))
	{
		AssetRoot = BrowserRootPaths.empty() ? RootPath : BrowserRootPaths.front();
	}

	BackHistory.clear();
	NavigateTo(AssetRoot, false);
}

void FEditorContentBrowserWidget::Render(float DeltaTime)
{
	const float TargetAlpha = bVisible ? 1.0f : 0.0f;
	const float Step = std::max(DeltaTime, ImGui::GetIO().DeltaTime) * 10.0f;
	if (AnimAlpha < TargetAlpha)
	{
		AnimAlpha = std::min(TargetAlpha, AnimAlpha + Step);
	}
	else if (AnimAlpha > TargetAlpha)
	{
		AnimAlpha = std::max(TargetAlpha, AnimAlpha - Step);
	}

	TickPendingImportTask();

	if (AnimAlpha <= 0.001f)
	{
		bMouseOverBrowser = false;
		bHasBrowserScreenRect = false;
		return;
	}

	if (bNeedsRefresh)
	{
		Refresh();
	}
	if (bPendingMaterialPreviewCacheClear)
	{
		MaterialPreviewCache.clear();
		StaticMeshPreviewCache.clear();
		SkeletalMeshPreviewCache.clear();
		FailedPreviewCacheKeys.clear();
		bPendingMaterialPreviewCacheClear = false;
	}

	const ImGuiViewport* MainViewport = ImGui::GetMainViewport();
	const ImVec2 WorkPos = MainViewport ? MainViewport->WorkPos : ImVec2(0.0f, 0.0f);
	const ImVec2 WorkSize = MainViewport ? MainViewport->WorkSize : ImGui::GetIO().DisplaySize;
	constexpr float FooterHeight = 32.0f;
	constexpr float DrawerMaxHeight = 380.0f;
	const bool bDrawerMode = IsDrawerMode();

	if (bDrawerMode)
	{
		const float DrawerHeight = DrawerMaxHeight * AnimAlpha;
		if (DrawerHeight <= 1.0f)
		{
			bMouseOverBrowser = false;
			bHasBrowserScreenRect = false;
			return;
		}
		ImGui::SetNextWindowPos(
			ImVec2(WorkPos.x, WorkPos.y + WorkSize.y - FooterHeight - DrawerHeight),
			ImGuiCond_Always);
		ImGui::SetNextWindowSize(ImVec2(WorkSize.x, DrawerHeight), ImGuiCond_Always);
	}
	else
	{
		ApplyContentBrowserWindowClass();
		const float Width = std::min(1040.0f, WorkSize.x - 48.0f);
		const float Height = std::min(620.0f, WorkSize.y - 96.0f);
		const ImVec2 WindowPos(
			WorkPos.x + (WorkSize.x - Width) * 0.5f,
			WorkPos.y + 58.0f + (1.0f - AnimAlpha) * 20.0f);
		ImGui::SetNextWindowPos(WindowPos, ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowSize(ImVec2(Width, Height), ImGuiCond_FirstUseEver);
	}
	if (bDrawerMode && MainViewport)
	{
		ImGui::SetNextWindowViewport(MainViewport->ID);
	}
	ImGui::SetNextWindowBgAlpha(0.96f * AnimAlpha);

	int32 PushedStyleVarCount = 0;
	ImGui::PushStyleVar(ImGuiStyleVar_Alpha, AnimAlpha);
	++PushedStyleVarCount;
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
	++PushedStyleVarCount;
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, bDrawerMode ? ImVec2(10.0f, 8.0f) : ImVec2(0.0f, 0.0f));
	++PushedStyleVarCount;
	if (!bDrawerMode)
	{
		const float TitleBarFramePaddingY = std::max(
			0.0f,
			(FEditorChromeMetrics::ApplicationTitleBarHeight - ImGui::GetFontSize()) * 0.5f);
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(13.0f, TitleBarFramePaddingY));
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(9.0f, 4.0f));
		PushedStyleVarCount += 2;
	}

	bool bOpen = bVisible;
	const ImGuiWindowFlags Flags =
		ImGuiWindowFlags_NoDocking |
		(bDrawerMode
			? (ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse)
			: (ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_MenuBar));
	if (!ImGui::Begin(bDrawerMode ? "##EditorContentBrowserDrawer" : "Content Browser", &bOpen, Flags))
	{
		BrowserScreenMin = ImGui::GetWindowPos();
		BrowserScreenMax = ImVec2(BrowserScreenMin.x + ImGui::GetWindowSize().x, BrowserScreenMin.y + ImGui::GetWindowSize().y);
		bHasBrowserScreenRect = true;
		bMouseOverBrowser = IsMouseOverBrowser();
		ImGui::End();
		ImGui::PopStyleVar(PushedStyleVarCount);
		bVisible = bOpen;
		return;
	}
	bVisible = bOpen;
	BrowserScreenMin = ImGui::GetWindowPos();
	BrowserScreenMax = ImVec2(BrowserScreenMin.x + ImGui::GetWindowSize().x, BrowserScreenMin.y + ImGui::GetWindowSize().y);
	bHasBrowserScreenRect = true;
	bMouseOverBrowser =
		IsMouseOverBrowser()
		|| ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);

	if (bDrawerMode)
	{
		DrawBrowserContents();
	}
	else
	{
		DrawFloatingWindowChrome(bOpen);
		if (bOpen)
		{
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 8.0f));
			if (ImGui::BeginChild("##ContentBrowserFloatingBody", ImVec2(0.0f, 0.0f), false))
			{
				DrawBrowserContents();
			}
			ImGui::EndChild();
			ImGui::PopStyleVar();
		}
	}

	bVisible = bOpen;
	ImGui::End();
	ImGui::PopStyleVar(PushedStyleVarCount);
}

bool FEditorContentBrowserWidget::IsMouseOverBrowser() const
{
	if (AnimAlpha <= 0.001f || !bHasBrowserScreenRect)
	{
		return false;
	}

	const ImVec2 MousePos = ImGui::GetIO().MousePos;
	return MousePos.x >= BrowserScreenMin.x
		&& MousePos.x < BrowserScreenMax.x
		&& MousePos.y >= BrowserScreenMin.y
		&& MousePos.y < BrowserScreenMax.y;
}

bool FEditorContentBrowserWidget::ConsumeReleasedDragPayload(FString& OutPayloadType, FString& OutPayloadPath)
{
	if (ActiveDragPayloadPath.empty())
	{
		return false;
	}

	if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
	{
		return false;
	}

	const bool bReleased = ImGui::IsMouseReleased(ImGuiMouseButton_Left);
	if (bReleased)
	{
		OutPayloadType = ActiveDragPayloadType;
		OutPayloadPath = ActiveDragPayloadPath;
	}

	ActiveDragPayloadType.clear();
	ActiveDragPayloadPath.clear();
	return bReleased;
}

void FEditorContentBrowserWidget::DrawBrowserContents()
{
	if (ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows) && ImGui::IsMouseClicked(3))
	{
		NavigateBack();
	}

	DrawToolbar();
	ImGui::Separator();

	const ImGuiIO& IO = ImGui::GetIO();
	const bool bBrowserFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
	if (bBrowserFocused
		&& !IO.WantTextInput
		&& ImGui::IsKeyPressed(ImGuiKey_F2, false))
	{
		RequestRenameSelectedItem();
	}
	if (bBrowserFocused
		&& !IO.WantTextInput
		&& !SelectedPath.empty()
		&& ImGui::IsKeyPressed(ImGuiKey_Delete, false))
	{
		DeleteSelectedItem();
	}

	if (ImGui::BeginTable("##ContentBrowserLayout", 3, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV))
	{
		ImGui::TableSetupColumn("Folders", ImGuiTableColumnFlags_WidthFixed, IsDrawerMode() ? 250.0f : 230.0f);
		ImGui::TableSetupColumn("Assets", ImGuiTableColumnFlags_WidthStretch);
		ImGui::TableSetupColumn("Details", ImGuiTableColumnFlags_WidthFixed, IsDrawerMode() ? 280.0f : 240.0f);

		ImGui::TableNextColumn();
		if (ImGui::BeginChild("##ContentBrowserFolders", ImVec2(0.0f, 0.0f), false))
		{
			DrawDirectoryNode(RootNode);
			PendingRevealPath.clear();
		}
		ImGui::EndChild();

		ImGui::TableNextColumn();
		if (ImGui::BeginChild("##ContentBrowserAssets", ImVec2(0.0f, 0.0f), false, ImGuiWindowFlags_HorizontalScrollbar))
		{
			DrawContentGrid();
		}
		ImGui::EndChild();

		ImGui::TableNextColumn();
		if (ImGui::BeginChild("##ContentBrowserDetails", ImVec2(0.0f, 0.0f), false))
		{
			DrawDetails();
		}
		ImGui::EndChild();

		ImGui::EndTable();
	}

	DrawRenamePopup();
	DrawFbxImportPopup();
}

void FEditorContentBrowserWidget::DrawFloatingWindowChrome(bool& bOpen)
{
	if (!ImGui::BeginMenuBar())
	{
		return;
	}

	constexpr float WindowButtonWidth = 48.0f;
	constexpr float TitleBarHeight = FEditorChromeMetrics::ApplicationTitleBarHeight;
	constexpr float MenuStartX = 0.0f;

	HWND ViewportHwnd = GetCurrentViewportHwnd();
	const ImVec2 WindowPos = ImGui::GetWindowPos();
	const ImVec2 WindowSize = ImGui::GetWindowSize();
	const float ButtonStartX = std::max(0.0f, WindowSize.x - WindowButtonWidth * 3.0f);

	ImGui_ImplWin32_CustomChromeRect ChromeRects[16] = {};
	int ChromeRectCount = 0;
	ImDrawList* DrawList = ImGui::GetWindowDrawList();

	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.0f, 12.0f));
	const float TitleBarFramePaddingY = std::max(
		0.0f,
		(TitleBarHeight - ImGui::GetFontSize()) * 0.5f);
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(18.0f, TitleBarFramePaddingY));
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(12.0f, 8.0f));

	ImGui::SetCursorPos(ImVec2(MenuStartX, 0.0f));
	if (ImGui::BeginMenu("File"))
	{
		if (ImGui::MenuItem("Close"))
		{
			bOpen = false;
		}
		ImGui::EndMenu();
	}
	if (ImGui::BeginMenu("Edit"))
	{
		if (ImGui::MenuItem("Rename", "F2", false, !SelectedPath.empty()))
		{
			RequestRenameSelectedItem();
		}
		if (ImGui::MenuItem("Delete", "Del", false, !SelectedPath.empty()))
		{
			DeleteSelectedItem();
		}
		ImGui::EndMenu();
	}
	if (ImGui::BeginMenu("Asset"))
	{
		if (ImGui::MenuItem("Refresh"))
		{
			Refresh();
		}
		ImGui::Separator();
		if (ImGui::MenuItem("Go to Asset Root"))
		{
			OpenAssetRoot();
		}
		ImGui::EndMenu();
	}
	if (ImGui::BeginMenu("Window"))
	{
		if (ImGui::MenuItem("Drawer Mode"))
		{
			PresentationMode = EPresentationMode::Drawer;
		}
		if (ImGui::MenuItem("Close"))
		{
			bOpen = false;
		}
		ImGui::EndMenu();
	}
	if (ImGui::BeginMenu("Settings"))
	{
		if (EditorEngine)
		{
			FEditorMainPanel& MainPanel = EditorEngine->GetMainPanel();
			if (ImGui::MenuItem("Project Settings"))
			{
				MainPanel.OpenProjectSettingsPanel();
			}
			if (ImGui::MenuItem("World Settings"))
			{
				MainPanel.OpenWorldSettingsPanel();
			}
			if (ImGui::MenuItem("Editor Settings"))
			{
				MainPanel.OpenEditorSettingsPanel();
			}
		}
		ImGui::EndMenu();
	}
	if (ImGui::BeginMenu("Help"))
	{
		ImGui::TextDisabled("Content Browser");
		ImGui::TextDisabled("Double-click assets to open, or drag them into the viewport.");
		ImGui::EndMenu();
	}

	const float MenuEndX = std::min(ButtonStartX, ImGui::GetCursorScreenPos().x - WindowPos.x + 8.0f);
	AddChromeRect(
		ChromeRects,
		ChromeRectCount,
		ImVec2(WindowPos.x, WindowPos.y),
		ImVec2(WindowPos.x + MenuEndX, WindowPos.y + TitleBarHeight),
		WindowPos);

	const char* Title = "Content Browser";
	const ImVec2 TitleSize = ImGui::CalcTextSize(Title);
	const float TitleX = std::clamp(
		MenuEndX + (ButtonStartX - MenuEndX - TitleSize.x) * 0.5f,
		MenuEndX + 8.0f,
		std::max(MenuEndX + 8.0f, ButtonStartX - TitleSize.x - 8.0f));
	DrawList->AddText(
		ImVec2(WindowPos.x + TitleX, WindowPos.y + (TitleBarHeight - TitleSize.y) * 0.5f),
		ImGui::GetColorU32(ImVec4(0.72f, 0.76f, 0.84f, 1.0f)),
		Title);

	const ImVec2 ButtonSize(WindowButtonWidth, TitleBarHeight);
	ImGui::SetCursorPos(ImVec2(ButtonStartX, 0.0f));
	if (DrawContentBrowserWindowButton(
		"ContentBrowserMinimize",
		"Minimize",
		ButtonSize,
		ImVec4(0.14f, 0.16f, 0.20f, 1.0f),
		ImVec4(0.18f, 0.20f, 0.25f, 1.0f),
		[](ImDrawList* InDrawList, const ImVec2& Min, const ImVec2& Max, ImU32 Color)
		{
			const float Y = (Min.y + Max.y) * 0.5f + 4.0f;
			InDrawList->AddLine(ImVec2(Min.x + 17.0f, Y), ImVec2(Max.x - 17.0f, Y), Color, 1.6f);
		}))
	{
		if (ViewportHwnd)
		{
			::PostMessageW(ViewportHwnd, WM_SYSCOMMAND, SC_MINIMIZE, 0);
		}
	}
	AddChromeRect(ChromeRects, ChromeRectCount, ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), WindowPos);

	ImGui::SameLine(0.0f, 0.0f);
	if (DrawContentBrowserWindowButton(
		"ContentBrowserMaximize",
		IsViewportMaximized(ViewportHwnd) ? "Restore" : "Maximize",
		ButtonSize,
		ImVec4(0.14f, 0.16f, 0.20f, 1.0f),
		ImVec4(0.18f, 0.20f, 0.25f, 1.0f),
		[ViewportHwnd](ImDrawList* InDrawList, const ImVec2& Min, const ImVec2& Max, ImU32 Color)
		{
			const bool bMaximized = IsViewportMaximized(ViewportHwnd);
			const ImVec2 A(Min.x + 17.0f, Min.y + 12.0f);
			const ImVec2 B(Max.x - 17.0f, Max.y - 12.0f);
			if (bMaximized)
			{
				InDrawList->AddRect(ImVec2(A.x + 3.0f, A.y), ImVec2(B.x + 3.0f, B.y - 3.0f), Color, 0.0f, 0, 1.4f);
				InDrawList->AddRect(ImVec2(A.x, A.y + 3.0f), ImVec2(B.x, B.y), Color, 0.0f, 0, 1.4f);
			}
			else
			{
				InDrawList->AddRect(A, B, Color, 0.0f, 0, 1.4f);
			}
		}))
	{
		ToggleViewportMaximize(ViewportHwnd);
	}
	AddChromeRect(ChromeRects, ChromeRectCount, ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), WindowPos);

	ImGui::SameLine(0.0f, 0.0f);
	if (DrawContentBrowserWindowButton(
		"ContentBrowserClose",
		"Close",
		ButtonSize,
		ImVec4(0.62f, 0.18f, 0.20f, 1.0f),
		ImVec4(0.46f, 0.10f, 0.13f, 1.0f),
		[](ImDrawList* InDrawList, const ImVec2& Min, const ImVec2& Max, ImU32 Color)
		{
			InDrawList->AddLine(ImVec2(Min.x + 17.0f, Min.y + 12.0f), ImVec2(Max.x - 17.0f, Max.y - 12.0f), Color, 1.6f);
			InDrawList->AddLine(ImVec2(Max.x - 17.0f, Min.y + 12.0f), ImVec2(Min.x + 17.0f, Max.y - 12.0f), Color, 1.6f);
		}))
	{
		bOpen = false;
	}
	AddChromeRect(ChromeRects, ChromeRectCount, ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), WindowPos);

	ImGui_ImplWin32_SetCustomChrome(ViewportHwnd, static_cast<int>(TitleBarHeight), ChromeRects, ChromeRectCount);
	ImGui::PopStyleVar(3);
	ImGui::EndMenuBar();
}

void FEditorContentBrowserWidget::DrawToolbar()
{
	{
	const float ItemSpacing = ImGui::GetStyle().ItemSpacing.x;
	constexpr float ToolbarButtonHeight = 28.0f;
	constexpr float ArrowButtonWidth = 34.0f;
	const ImVec2 RefreshButtonSize(68.0f, ToolbarButtonHeight);
	const ImVec2 ModeButtonSize(116.0f, ToolbarButtonHeight);
	const ImVec2 ArrowButtonSize(ArrowButtonWidth, ToolbarButtonHeight);

	if (ImGui::Button("Refresh", RefreshButtonSize))
	{
		Refresh();
	}
	ImGui::SameLine();
	if (ImGui::Button(IsDrawerMode() ? "Window Mode" : "Drawer Mode", ModeButtonSize))
	{
		PresentationMode = IsDrawerMode() ? EPresentationMode::FloatingWindow : EPresentationMode::Drawer;
	}
	ImGui::SameLine();
	if (DrawContentBrowserArrowButton("Back", "Back", ArrowButtonSize, false, !BackHistory.empty()))
	{
		NavigateBack();
	}
	ImGui::SameLine();
	if (DrawContentBrowserArrowButton("Up", "Up", ArrowButtonSize, true, true))
	{
		const std::filesystem::path Parent = CurrentPath.parent_path();
		if (!Parent.empty() && Parent != CurrentPath)
		{
			NavigateTo(Parent);
		}
	}
	ImGui::SameLine();
	const float RemainingWidth = ImGui::GetContentRegionAvail().x;
	const float SearchWidth = std::min(220.0f, std::max(120.0f, RemainingWidth * 0.28f));
	const float PathWidth = std::max(120.0f, RemainingWidth - SearchWidth - ItemSpacing);
	ImGui::SetNextItemWidth(PathWidth);
	FString PathText = MakeDisplayPath(CurrentPath);
	char PathBuf[512] = {};
	strncpy_s(PathBuf, PathText.c_str(), _TRUNCATE);
	ImGui::InputText("##ContentBrowserPath", PathBuf, sizeof(PathBuf), ImGuiInputTextFlags_ReadOnly);
	ImGui::SameLine();
	char SearchBuf[128] = {};
	strncpy_s(SearchBuf, SearchFilter.c_str(), _TRUNCATE);
	ImGui::SetNextItemWidth(std::min(SearchWidth, ImGui::GetContentRegionAvail().x));
	if (ImGui::InputTextWithHint("##ContentBrowserSearch", "Search", SearchBuf, sizeof(SearchBuf)))
	{
		SearchFilter = SearchBuf;
	}
	return;
	}

#if 0
	if (ImGui::SmallButton("Refresh"))
	{
		Refresh();
	}
	ImGui::SameLine();
	if (ImGui::SmallButton(IsDrawerMode() ? "Window Mode" : "Drawer Mode"))
	{
		PresentationMode = IsDrawerMode() ? EPresentationMode::FloatingWindow : EPresentationMode::Drawer;
	}
	ImGui::SameLine();
	ImGui::BeginDisabled(BackHistory.empty());
	if (ImGui::SmallButton("?"))
	{
		NavigateBack();
	}
	ImGui::EndDisabled();
	if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
	{
		ImGui::SetTooltip("Back");
	}
	ImGui::SameLine();
	if (ImGui::SmallButton("??))
	{
		const std::filesystem::path Parent = CurrentPath.parent_path();
		if (!Parent.empty() && Parent != CurrentPath)
		{
			NavigateTo(Parent);
		}
	}
	if (ImGui::IsItemHovered())
	{
		ImGui::SetTooltip("Up");
	}
	ImGui::SameLine();
	ImGui::SetNextItemWidth(std::min(620.0f, ImGui::GetContentRegionAvail().x * 0.62f));
	FString PathText = MakeDisplayPath(CurrentPath);
	char PathBuf[512] = {};
	strncpy_s(PathBuf, PathText.c_str(), _TRUNCATE);
	ImGui::InputText("##ContentBrowserPath", PathBuf, sizeof(PathBuf), ImGuiInputTextFlags_ReadOnly);
	ImGui::SameLine();
	char SearchBuf[128] = {};
	strncpy_s(SearchBuf, SearchFilter.c_str(), _TRUNCATE);
	ImGui::SetNextItemWidth(std::max(120.0f, ImGui::GetContentRegionAvail().x));
	if (ImGui::InputText("Search", SearchBuf, sizeof(SearchBuf)))
	{
		SearchFilter = SearchBuf;
	}
#endif
}

void FEditorContentBrowserWidget::Refresh()
{
	RebuildRootNode();
	RefreshContent();
	bPendingMaterialPreviewCacheClear = true;
	bNeedsRefresh = false;
}

void FEditorContentBrowserWidget::RefreshAfterAssetMutation()
{
	if (EditorEngine)
	{
		EditorEngine->GetAssetService().RefreshAssetDatabase();
	}
	Refresh();
}

void FEditorContentBrowserWidget::LoadFromSettings()
{
	CurrentPath = ResolveBrowserPath(FEditorSettings::Get().ContentBrowserPath);
	if (!IsPathAllowed(CurrentPath))
	{
		CurrentPath = BrowserRootPaths.empty() ? RootPath : BrowserRootPaths.front();
	}
	PendingRevealPath = CurrentPath;
}

void FEditorContentBrowserWidget::SaveToSettings() const
{
	FEditorSettings::Get().ContentBrowserPath = MakeSavedBrowserPath(CurrentPath);
}

void FEditorContentBrowserWidget::RefreshContent()
{
	CurrentItems = ReadDirectory(CurrentPath);
}

void FEditorContentBrowserWidget::RebuildRootNode()
{
	RootNode = {};
	RootNode.Path = RootPath;
	RootNode.Name = "Project";
	for (const std::filesystem::path& BrowserRoot : BrowserRootPaths)
	{
		std::error_code Ec;
		if (std::filesystem::exists(BrowserRoot, Ec) && std::filesystem::is_directory(BrowserRoot, Ec))
		{
			RootNode.Children.push_back(BuildDirectoryTree(BrowserRoot));
		}
	}
}

FEditorContentBrowserWidget::FDirNode FEditorContentBrowserWidget::BuildDirectoryTree(const std::filesystem::path& DirPath) const
{
	FDirNode Node;
	Node.Path = DirPath;
	Node.Name = FPaths::ToUtf8(DirPath.filename().wstring());
	if (Node.Name.empty())
	{
		Node.Name = "Project";
	}

	std::error_code Ec;
	for (const std::filesystem::directory_entry& Entry : std::filesystem::directory_iterator(DirPath, Ec))
	{
		if (!Entry.is_directory())
		{
			continue;
		}
		Node.Children.push_back(BuildDirectoryTree(Entry.path()));
	}

	std::sort(Node.Children.begin(), Node.Children.end(),
		[](const FDirNode& A, const FDirNode& B)
		{
			return A.Name < B.Name;
		});
	return Node;
}

TArray<FEditorContentBrowserWidget::FContentItem> FEditorContentBrowserWidget::ReadDirectory(const std::filesystem::path& DirPath) const
{
	TArray<FContentItem> Items;
	std::error_code Ec;
	if (IsProjectRootPath(DirPath))
	{
		for (const std::filesystem::path& BrowserRoot : BrowserRootPaths)
		{
			if (std::filesystem::exists(BrowserRoot, Ec) && std::filesystem::is_directory(BrowserRoot, Ec))
			{
				FContentItem Item;
				Item.Path = BrowserRoot;
				Item.Name = FPaths::ToUtf8(BrowserRoot.filename().wstring());
				Item.bIsDirectory = true;
				Items.push_back(Item);
			}
		}
		return Items;
	}
	if (!std::filesystem::exists(DirPath, Ec) || !std::filesystem::is_directory(DirPath, Ec))
	{
		return Items;
	}
	if (!IsPathAllowed(DirPath))
	{
		return Items;
	}

	for (const std::filesystem::directory_entry& Entry : std::filesystem::directory_iterator(DirPath, Ec))
	{
		FContentItem Item;
		Item.Path = Entry.path();
		Item.Name = FPaths::ToUtf8(Entry.path().filename().wstring());
		Item.Extension = ToLower(FPaths::ToUtf8(Entry.path().extension().wstring()));
		Item.bIsDirectory = Entry.is_directory();
		if (!Item.bIsDirectory && IsUAsset(Item.Extension))
		{
			const FString AssetPath = MakeRelativeProjectPath(Item.Path);
			Item.bHasAssetMetadata = FAssetFile::LoadMetadataOnly(AssetPath, Item.AssetMetadata);
		}
		Items.push_back(Item);
	}

	std::sort(Items.begin(), Items.end(),
		[](const FContentItem& A, const FContentItem& B)
		{
			if (A.bIsDirectory != B.bIsDirectory)
			{
				return A.bIsDirectory > B.bIsDirectory;
			}
			return A.Name < B.Name;
		});
	return Items;
}

void FEditorContentBrowserWidget::DrawDirectoryNode(const FDirNode& Node)
{
	ImGuiTreeNodeFlags Flags = Node.Children.empty() ? ImGuiTreeNodeFlags_Leaf : ImGuiTreeNodeFlags_OpenOnArrow;
	if (Node.Path == CurrentPath)
	{
		Flags |= ImGuiTreeNodeFlags_Selected;
	}
	if (!PendingRevealPath.empty())
	{
		const std::filesystem::path Relative = PendingRevealPath.lexically_relative(Node.Path);
		if (Node.Path == RootPath || Relative.empty() || !IsParentDirectoryReference(Relative))
		{
			ImGui::SetNextItemOpen(true, ImGuiCond_Always);
		}
	}

	const bool bOpen = ImGui::TreeNodeEx(Node.Name.c_str(), Flags);
	if (ImGui::IsItemClicked())
	{
		NavigateTo(Node.Path);
	}
	if (!bOpen)
	{
		return;
	}
	for (const FDirNode& Child : Node.Children)
	{
		DrawDirectoryNode(Child);
	}
	ImGui::TreePop();
}

void FEditorContentBrowserWidget::DrawContentGrid()
{
	const FString Filter = ToLower(SearchFilter);
	TArray<FContentItem> VisibleItems;
	for (const FContentItem& Item : CurrentItems)
	{
		if (!Filter.empty() && ToLower(Item.Name).find(Filter) == FString::npos)
		{
			continue;
		}
		VisibleItems.push_back(Item);
	}

	const ImVec2 Available = ImGui::GetContentRegionAvail();
	constexpr float BottomBarHeight = 30.0f;
	const float GridHeight = std::max(1.0f, Available.y - BottomBarHeight);
	if (ImGui::BeginChild("##ContentBrowserAssetGridScroll", ImVec2(0.0f, GridHeight), false, ImGuiWindowFlags_HorizontalScrollbar))
	{
		const float ContentWidth = ImGui::GetContentRegionAvail().x;
		const ImVec2 Tile(TileSize, TileSize + 44.0f);
		constexpr float TileGap = 14.0f;
		const int32 Columns = std::max(1, static_cast<int32>((ContentWidth + TileGap) / (Tile.x + TileGap)));
		const int32 ItemCount = static_cast<int32>(VisibleItems.size());
		const int32 RowCount = Columns > 0
			? (ItemCount + Columns - 1) / Columns
			: 0;
		const float RowHeight = Tile.y + TileGap;

		MaterialPreviewBuildsThisFrame = 0;
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(TileGap, TileGap));
		bool bAnyTileHovered = false;
		ImGuiListClipper Clipper;
		Clipper.Begin(RowCount, RowHeight);
		while (Clipper.Step())
		{
			for (int32 Row = Clipper.DisplayStart; Row < Clipper.DisplayEnd; ++Row)
			{
				const int32 StartIndex = Row * Columns;
				const int32 EndIndex = std::min(StartIndex + Columns, ItemCount);
				for (int32 Index = StartIndex; Index < EndIndex; ++Index)
				{
					if (Index > StartIndex)
					{
						ImGui::SameLine(0.0f, TileGap);
					}
					DrawContentTile(VisibleItems[Index], Tile);
					bAnyTileHovered |= ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup);
				}
			}
		}
		ImGui::PopStyleVar();

		if (!bAnyTileHovered
			&& ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup)
			&& ImGui::IsMouseClicked(ImGuiMouseButton_Right))
		{
			bOpenContentContextMenu = true;
			bContentContextMenuHasSelection = false;
		}

		if (bOpenContentContextMenu)
		{
			ImGui::OpenPopup("##ContentBrowserContextMenu");
			bOpenContentContextMenu = false;
		}
		if (ImGui::BeginPopup("##ContentBrowserContextMenu"))
		{
			DrawContentContextMenu(bContentContextMenuHasSelection);
			ImGui::EndPopup();
		}
	}
	ImGui::EndChild();

	const float SliderWidth = 112.0f;
	const float StartX = std::max(ImGui::GetCursorPosX(), ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - SliderWidth - 34.0f);
	ImGui::SetCursorPosX(StartX);
	ImGui::TextDisabled("Size");
	ImGui::SameLine();
	ImGui::SetNextItemWidth(SliderWidth);
	ImGui::SliderFloat("##ContentTileSize", &TileSize, 48.0f, 128.0f, "%.0f");
}

void FEditorContentBrowserWidget::DrawContentTile(const FContentItem& Item, const ImVec2& TileSize)
{
	ImGui::PushID(FPaths::ToUtf8(Item.Path.wstring()).c_str());
	const bool bSelected = SelectedPath == Item.Path;
	if (ImGui::Selectable("##ContentTile", bSelected, 0, TileSize))
	{
		SelectedPath = Item.Path;
	}
	const bool bTileHovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup);

	if (bTileHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
	{
		SelectedPath = Item.Path;
		bOpenContentContextMenu = true;
		bContentContextMenuHasSelection = true;
	}

	const ImVec2 Min = ImGui::GetItemRectMin();
	const ImVec2 Max = ImGui::GetItemRectMax();
	ImDrawList* DrawList = ImGui::GetWindowDrawList();
	const ImU32 Bg = ImGui::GetColorU32(bSelected ? ImVec4(0.19f, 0.28f, 0.43f, 1.0f) : ImVec4(0.13f, 0.15f, 0.18f, 1.0f));
	const ImU32 Border = ImGui::GetColorU32(bSelected ? ImVec4(0.36f, 0.58f, 0.88f, 1.0f) : ImVec4(0.25f, 0.28f, 0.33f, 1.0f));
	DrawList->AddRectFilled(Min, Max, Bg, 6.0f);
	DrawList->AddRect(Min, Max, Border, 6.0f);

	const ImVec2 IconMin(Min.x + 10.0f, Min.y + 8.0f);
	const ImVec2 IconMax(Max.x - 10.0f, Min.y + TileSize.y - 46.0f);
	ID3D11ShaderResourceView* PreviewSRV = nullptr;
	if (!Item.bIsDirectory)
	{
		if (IsPreviewableImage(Item.Extension))
		{
			PreviewSRV = GetImagePreviewSRV(Item);
		}
		else
		{
			const uint32 PreviewWidth = static_cast<uint32>(std::max(1.0f, IconMax.x - IconMin.x));
			const uint32 PreviewHeight = static_cast<uint32>(std::max(1.0f, IconMax.y - IconMin.y));
			const bool bHighPriorityPreview = bSelected || bTileHovered;
			if (IsMaterialAsset(Item))
			{
				PreviewSRV = GetMaterialPreviewSRV(Item, PreviewWidth, PreviewHeight, bHighPriorityPreview);
			}
			else if (IsStaticMeshAsset(Item))
			{
				PreviewSRV = GetStaticMeshPreviewSRV(Item, PreviewWidth, PreviewHeight, bHighPriorityPreview);
			}
			else if (IsSkeletalMeshAsset(Item))
			{
				PreviewSRV = GetSkeletalMeshPreviewSRV(Item, PreviewWidth, PreviewHeight, bHighPriorityPreview);
			}
		}
	}

	if (Item.bIsDirectory)
	{
		DrawFolderIcon(DrawList, IconMin, IconMax, GetItemColor(Item));
	}
	else if (PreviewSRV)
	{
		DrawList->AddRectFilled(IconMin, IconMax, ImGui::GetColorU32(ImVec4(0.05f, 0.06f, 0.07f, 1.0f)), 5.0f);
		DrawList->AddImage(reinterpret_cast<ImTextureID>(PreviewSRV), IconMin, IconMax);
		DrawList->AddRect(IconMin, IconMax, ImGui::GetColorU32(ImVec4(0.25f, 0.28f, 0.33f, 0.75f)), 5.0f);
	}
	else
	{
		DrawList->AddRectFilled(IconMin, IconMax, GetItemColor(Item), 5.0f);
		if (IsMaterialAsset(Item))
		{
			const ImVec2 Center((IconMin.x + IconMax.x) * 0.5f, (IconMin.y + IconMax.y) * 0.5f - 4.0f);
			const float Radius = std::max(10.0f, std::min(IconMax.x - IconMin.x, IconMax.y - IconMin.y) * 0.28f);
			DrawList->AddCircleFilled(Center, Radius, ImGui::GetColorU32(ImVec4(0.18f, 0.20f, 0.24f, 0.82f)), 32);
			DrawList->AddCircleFilled(ImVec2(Center.x - Radius * 0.32f, Center.y - Radius * 0.28f), Radius * 0.36f,
				ImGui::GetColorU32(ImVec4(0.95f, 0.78f, 0.42f, 0.85f)), 20);
			DrawList->AddCircleFilled(ImVec2(Center.x + Radius * 0.22f, Center.y + Radius * 0.12f), Radius * 0.42f,
				ImGui::GetColorU32(ImVec4(0.45f, 0.62f, 0.88f, 0.78f)), 20);
			const char* Kind = Item.bHasAssetMetadata && Item.AssetMetadata.ClassName == UMaterialInstance::StaticClass()->ClassName
				? "MI"
				: "MAT";
			const ImVec2 TextSize = ImGui::CalcTextSize(Kind);
			DrawList->AddText(ImVec2(Center.x - TextSize.x * 0.5f, IconMax.y - 22.0f),
				ImGui::GetColorU32(ImVec4(0.96f, 0.97f, 0.99f, 1.0f)), Kind);
		}
		else if (IsCurveAsset(Item.Path))
		{
			const float Width = IconMax.x - IconMin.x;
			const float Height = IconMax.y - IconMin.y;
			const ImU32 LineColor = ImGui::GetColorU32(ImVec4(0.98f, 0.93f, 0.48f, 1.0f));
			ImVec2 Prev(IconMin.x + Width * 0.15f, IconMax.y - Height * 0.20f);
			for (int32 Step = 1; Step <= 5; ++Step)
			{
				const float Alpha = static_cast<float>(Step) / 5.0f;
				const float X = IconMin.x + Width * (0.15f + Alpha * 0.70f);
				const float Y = IconMax.y - Height * (0.20f + Alpha * Alpha * 0.62f);
				const ImVec2 Next(X, Y);
				DrawList->AddLine(Prev, Next, LineColor, 3.0f);
				Prev = Next;
			}
			const char* Kind = "CURVE";
			const ImVec2 TextSize = ImGui::CalcTextSize(Kind);
			DrawList->AddText(ImVec2((IconMin.x + IconMax.x - TextSize.x) * 0.5f, IconMax.y - 22.0f),
				ImGui::GetColorU32(ImVec4(0.96f, 0.97f, 0.99f, 1.0f)), Kind);
		}
		else if (IsUAsset(Item.Extension))
		{
			const char* Kind = Item.bHasAssetMetadata
				? GetAssetClassBadge(Item.AssetMetadata.ClassName)
				: "UASSET";
			const ImVec2 TextSize = ImGui::CalcTextSize(Kind);
			DrawList->AddText(
				ImVec2((IconMin.x + IconMax.x - TextSize.x) * 0.5f, (IconMin.y + IconMax.y - TextSize.y) * 0.5f),
				ImGui::GetColorU32(ImVec4(0.96f, 0.97f, 0.99f, 1.0f)),
				Kind);
		}
	}

	FString Label = Item.Name;
	auto Ellipsize = [](FString Text, float MaxWidth)
	{
		if (ImGui::CalcTextSize(Text.c_str()).x <= MaxWidth)
		{
			return Text;
		}
		while (!Text.empty() && ImGui::CalcTextSize((Text + "...").c_str()).x > MaxWidth)
		{
			Text.pop_back();
		}
		return Text + "...";
	};

	const float LabelWidth = TileSize.x - 12.0f;
	Label = Ellipsize(Label, LabelWidth);
	FString ExtLine = "file";
	if (Item.bIsDirectory)
	{
		ExtLine = "folder";
	}
	else if (!Item.Extension.empty())
	{
		ExtLine = Item.Extension;
	}
	if (IsCurveAsset(Item.Path))
	{
		ExtLine = UCurveFloatAsset::StaticClass()->ClassName;
	}
	else if (IsUAsset(Item.Extension) && Item.bHasAssetMetadata)
	{
		ExtLine = GetAssetClassDisplayName(Item.AssetMetadata.ClassName);
	}
	ExtLine = Ellipsize(ExtLine, LabelWidth);

	const ImVec2 TextClipMin(Min.x + 6.0f, Max.y - 39.0f);
	const ImVec2 TextClipMax(Max.x - 6.0f, Max.y - 3.0f);
	DrawList->PushClipRect(TextClipMin, TextClipMax, true);
	DrawList->AddText(ImVec2(Min.x + 6.0f, Max.y - 35.0f), ImGui::GetColorU32(ImGuiCol_Text), Label.c_str());
	DrawList->AddText(ImVec2(Min.x + 6.0f, Max.y - 18.0f), ImGui::GetColorU32(ImGuiCol_TextDisabled), ExtLine.c_str());
	DrawList->PopClipRect();

	if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
	{
		if (Item.bIsDirectory)
		{
			NavigateTo(Item.Path);
		}
		else if (IsUAsset(Item.Extension))
		{
			if (!Item.bHasAssetMetadata)
			{
				EditorEngine->GetNotificationService().Warning("Invalid .uasset metadata.");
			}
			else if (Item.AssetMetadata.ClassName == USkeletalMesh::StaticClass()->ClassName)
			{
				EditorEngine->CreateViewer(MakeRelativeProjectPath(Item.Path));
			}
			else if (Item.AssetMetadata.ClassName == "UAnimSequence")
			{
				FAnimSequenceViewerContext Context;
				if (FAnimSequenceViewerContextBuilder::BuildFromUAsset(MakeRelativeProjectPath(Item.Path), Context))
				{
					EditorEngine->GetMainPanel().OpenAnimationSequenceAsset(Context);
				}
				else
				{
					EditorEngine->GetNotificationService().Error(Context.ErrorMessage);
				}
			}
			else if (Item.AssetMetadata.ClassName == UCurveFloatAsset::StaticClass()->ClassName)
			{
				EditorEngine->GetMainPanel().OpenCurveAsset(MakeRelativeProjectPath(Item.Path));
			}
			else if (Item.AssetMetadata.ClassName == URuntimeUILayoutAsset::StaticClass()->ClassName)
			{
				EditorEngine->GetMainPanel().OpenRuntimeUILayoutAsset(MakeRelativeProjectPath(Item.Path));
			}
			else if (Item.AssetMetadata.ClassName == UAnimLuaProgramAsset::StaticClass()->ClassName)
			{
				EditorEngine->GetMainPanel().OpenLuaAnimGraphAsset(MakeRelativeProjectPath(Item.Path));
			}
			else if (Item.AssetMetadata.ClassName == UBlueprintAsset::StaticClass()->ClassName)
			{
				EditorEngine->GetMainPanel().OpenBlueprintAsset(MakeRelativeProjectPath(Item.Path));
			}
			else if (IsMaterialAsset(Item))
			{
				if (UMaterialInterface* Material = ResolveMaterialAsset(Item.Path))
				{
					EditorEngine->GetMainPanel().OpenMaterialAsset(Material);
				}
				else
				{
					EditorEngine->GetNotificationService().Warning("Failed to load material .uasset.");
				}
			}
			else if (Item.AssetMetadata.ClassName == UAnimationStateMachine::StaticClass()->ClassName)
			{
				EditorEngine->GetMainPanel().OpenAnimationStateMachineAsset(MakeRelativeProjectPath(Item.Path));
			}
			//else if (Item.AssetMetadata.AssetClass == EAssetClass::Skeleton)
			//{
			//	EditorEngine->GetNotificationService().Info("Skeleton Viewer is not implemented yet.");
			//}
			//else if (Item.AssetMetadata.AssetClass == EAssetClass::Material)
			//{
			//	EditorEngine->GetNotificationService().Info("Material .uasset routing is not implemented yet.");
			//}
			//else if (Item.AssetMetadata.AssetClass == EAssetClass::Curve)
			//{
			//	EditorEngine->GetNotificationService().Info("Curve .uasset routing is not implemented yet.");
			//}
			else
			{
				EditorEngine->GetNotificationService().Warning("Unknown .uasset AssetClass.");
			}
		}
		else if (IsMaterialAsset(Item))
		{
			if (UMaterialInterface* Material = ResolveMaterialAsset(Item.Path))
			{
				EditorEngine->GetMainPanel().OpenMaterialAsset(Material);
			}
		}
		else if (IsCurveAsset(Item.Path))
		{
			EditorEngine->GetMainPanel().OpenCurveAsset(MakeRelativeProjectPath(Item.Path));
		}
		else if (Item.Extension == ".scene")
		{
			FEditorCommandArgs Args;
			Args.ScenePath = FPaths::ToUtf8(Item.Path.wstring());
			Args.bPromptSave = true;
			EditorEngine->GetCommandSystem().Execute(EEditorCommand::OpenScene, Args);
		}
		else if (IsPrefabAsset(Item.Extension))
		{
			EditorEngine->GetNotificationService().Info("Prefab selected. Drag to viewport or right-click to spawn.");
		}
		else if (Item.Extension == ".obj")
		{
			StartObjStaticMeshImportTask(Item.Path);
		}
		else if (Item.Extension == ".fbx")
		{
			OpenFbxImportPopup(Item.Path);
		}
		else if (Item.Extension == ".rml")
		{
			EditorEngine->GetMainPanel().OpenRuntimeUIPreviewAsset(MakeRelativeProjectPath(Item.Path));
		}
		else
		{
			ShellExecuteW(nullptr, L"open", Item.Path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
		}
	}

	if (!Item.bIsDirectory && ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
	{
		const FString PayloadPath = MakeRelativeProjectPath(Item.Path);
		const FString PayloadType = GetPayloadType(Item);
		ActiveDragPayloadPath = PayloadPath;
		ActiveDragPayloadType = PayloadType;
		ImGui::SetDragDropPayload(PayloadType.c_str(), PayloadPath.c_str(), PayloadPath.size() + 1);
		ImGui::TextUnformatted(Item.Name.c_str());
		ImGui::TextDisabled("%s", PayloadPath.c_str());
		ImGui::EndDragDropSource();
	}
	ImGui::PopID();
}

void FEditorContentBrowserWidget::DrawContentContextMenu(bool bHasSelectedItem)
{
	const bool bCanCreateHere = !IsProjectRootPath(CurrentPath) && IsPathAllowed(CurrentPath);
	ImGui::BeginDisabled(!bCanCreateHere);
	if (ImGui::BeginMenu("Create"))
	{
		if (ImGui::MenuItem("Folder"))
		{
			CreateFolder();
			ImGui::CloseCurrentPopup();
		}
		if (ImGui::MenuItem("Text File"))
		{
			CreateTextFile();
			ImGui::CloseCurrentPopup();
		}
		if (ImGui::MenuItem("Lua Script"))
		{
			CreateLuaScriptFile();
			ImGui::CloseCurrentPopup();
		}
		if (ImGui::MenuItem("Blueprint"))
		{
			CreateBlueprintAsset();
			ImGui::CloseCurrentPopup();
		}
		if (ImGui::MenuItem("Lua Anim Graph"))
		{
			CreateLuaAnimGraphAsset();
			ImGui::CloseCurrentPopup();
		}
		if (ImGui::MenuItem("Material"))
		{
			CreateMaterialAsset();
			ImGui::CloseCurrentPopup();
		}
		if (ImGui::MenuItem("Curve"))
		{
			CreateCurveAsset();
			ImGui::CloseCurrentPopup();
		}
		if (ImGui::MenuItem("Animation State Machine"))
		{
			CreateAnimationStateMachineAsset();
			ImGui::CloseCurrentPopup();
		}
		if (ImGui::MenuItem("Scene"))
		{
			CreateSceneAsset();
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndMenu();
	}
	ImGui::EndDisabled();

	ImGui::Separator();
	ImGui::BeginDisabled(!bHasSelectedItem || SelectedPath.empty());
	const FString SelectedExtension = ToLower(FPaths::ToUtf8(SelectedPath.extension().wstring()));
	const bool bCanOpenLuaAnimGraph = IsAnimLuaProgramAssetPath(SelectedPath);
	if (ImGui::MenuItem("Open Lua Anim Graph Editor", nullptr, false, bCanOpenLuaAnimGraph))
	{
		EditorEngine->GetMainPanel().OpenLuaAnimGraphAsset(MakeRelativeProjectPath(SelectedPath));
		ImGui::CloseCurrentPopup();
	}
	if (ImGui::MenuItem("Spawn Prefab at Origin", nullptr, false, IsPrefabAsset(SelectedExtension)))
	{
		EditorEngine->GetMainPanel().SpawnPrefabAtOrigin(FPaths::ToUtf8(SelectedPath.wstring()));
		ImGui::CloseCurrentPopup();
	}
	if (ImGui::MenuItem("Rename", "F2"))
	{
		RequestRenameSelectedItem();
		ImGui::CloseCurrentPopup();
	}
	if (ImGui::MenuItem("Delete", "Del"))
	{
		DeleteSelectedItem();
		ImGui::CloseCurrentPopup();
	}
	ImGui::EndDisabled();
}

bool FEditorContentBrowserWidget::CreateFolder()
{
	std::error_code Ec;
	const std::filesystem::path NewPath = MakeUniquePath(CurrentPath / L"New Folder");
	std::filesystem::create_directories(NewPath, Ec);
	if (Ec)
	{
		return false;
	}
	SelectedPath = NewPath;
	if (EditorEngine)
	{
		const FEditorFileSystemState AfterState =
			EditorEngine->GetUndoSystem().CaptureFileSystemState(FPaths::ToUtf8(NewPath.wstring()), "Create Folder");
		EditorEngine->GetUndoSystem().RecordCreateFileSystemPath(AfterState, "Create Folder");
	}
	RefreshAfterAssetMutation();
	return true;
}

bool FEditorContentBrowserWidget::CreateTextFile()
{
	const std::filesystem::path NewPath = MakeUniquePath(CurrentPath / L"New Text.txt");
	std::ofstream OutFile(NewPath, std::ios::out | std::ios::trunc);
	if (!OutFile.is_open())
	{
		return false;
	}
	OutFile.close();
	SelectedPath = NewPath;
	if (EditorEngine)
	{
		const FEditorFileSystemState AfterState =
			EditorEngine->GetUndoSystem().CaptureFileSystemState(FPaths::ToUtf8(NewPath.wstring()), "Create Text File");
		EditorEngine->GetUndoSystem().RecordCreateFileSystemPath(AfterState, "Create Text File");
	}
	RefreshAfterAssetMutation();
	return true;
}

bool FEditorContentBrowserWidget::CreateLuaScriptFile()
{
	const std::filesystem::path TargetDir = ResolveLuaScriptCreateDirectory();
	std::error_code CreateDirEc;
	std::filesystem::create_directories(TargetDir, CreateDirEc);
	if (CreateDirEc)
	{
		return false;
	}

	const std::filesystem::path NewPath = MakeUniquePath(TargetDir / L"New Script.lua");
	const FString RelativePath = MakeRelativeProjectPath(NewPath);
	if (!FScriptManager::Get().CreateScript(FName(RelativePath.c_str())))
	{
		return false;
	}

	SelectedPath = NewPath;
	if (EditorEngine)
	{
		const FEditorFileSystemState AfterState =
			EditorEngine->GetUndoSystem().CaptureFileSystemState(FPaths::ToUtf8(NewPath.wstring()), "Create Lua Script");
		EditorEngine->GetUndoSystem().RecordCreateFileSystemPath(AfterState, "Create Lua Script");
	}
	if (CurrentPath.lexically_normal() != TargetDir.lexically_normal())
	{
		if (EditorEngine)
		{
			EditorEngine->GetAssetService().RefreshAssetDatabase();
		}
		NavigateTo(TargetDir);
	}
	else
	{
		RefreshAfterAssetMutation();
	}
	return true;
}

bool FEditorContentBrowserWidget::CreateBlueprintAsset()
{
	const std::filesystem::path NewPath = MakeUniquePath(CurrentPath / L"New Blueprint.uasset");
	const FString RelativePath = MakeRelativeProjectPath(NewPath);

	UBlueprintAsset Asset;
	FBlueprintGraph& Graph = Asset.GetGraph();

	if (FFunction* BeginPlayFunction = UBlueprintComponent::StaticClass()->FindFunction("BeginPlay"))
	{
		AddEventNode(Graph, BeginPlayFunction);
	}
	else
	{
		AddEventNode(Graph, "BeginPlay");
	}

	if (!Asset.SaveToFile(RelativePath))
	{
		return false;
	}

	SelectedPath = NewPath;
	if (EditorEngine)
	{
		const FEditorFileSystemState AfterState =
			EditorEngine->GetUndoSystem().CaptureFileSystemState(FPaths::ToUtf8(NewPath.wstring()), "Create Blueprint");
		EditorEngine->GetUndoSystem().RecordCreateFileSystemPath(AfterState, "Create Blueprint");
	}

	RefreshAfterAssetMutation();
	return true;
}

bool FEditorContentBrowserWidget::CreateLuaAnimGraphAsset()
{
	const std::filesystem::path NewPath = MakeUniquePath(CurrentPath / L"New Lua Anim Graph.uasset");
	const FString RelativePath = MakeRelativeProjectPath(NewPath);

	FAssetMetaData MetaData;
	MetaData.PayloadVersion = 4;
	MetaData.ClassName = UAnimLuaProgramAsset::StaticClass()->ClassName;
	MetaData.DisplayName = FPaths::ToUtf8(NewPath.stem().wstring());

	FAnimLuaProgramAssetPayload Payload;
	Payload.Graph = MakeDefaultLuaAnimGraph();
	Payload.GeneratedLuaSource = FLuaAnimGraphCodeGenerator().Generate(Payload.Graph);

	if (!FAssetFile::Save(RelativePath, MetaData, [&](FArchive& Ar)
		{
			Payload.Serialize(Ar, MetaData.PayloadVersion);
			return true;
		}))
	{
		return false;
	}

	SelectedPath = NewPath;
	if (EditorEngine)
	{
		const FEditorFileSystemState AfterState =
			EditorEngine->GetUndoSystem().CaptureFileSystemState(FPaths::ToUtf8(NewPath.wstring()), "Create Lua Anim Graph");
		EditorEngine->GetUndoSystem().RecordCreateFileSystemPath(AfterState, "Create Lua Anim Graph");
	}
	RefreshAfterAssetMutation();
	return true;
}

bool FEditorContentBrowserWidget::CreateMaterialAsset()
{
	const std::filesystem::path NewPath = MakeUniquePath(CurrentPath / L"New Material.uasset");
	const FString RelativePath = MakeRelativeProjectPath(NewPath);
	const FString MaterialName = FPaths::ToUtf8(NewPath.stem().wstring());

	UMaterial* Material = FResourceManager::Get().GetOrCreateMaterial(MaterialName, RelativePath, EMaterialShaderType::SurfaceLit);
	if (!Material)
	{
		return false;
	}

	Material->MaterialData.Name = MaterialName;
	Material->MaterialData.bHasDiffuseTexture = false;
	Material->MaterialData.bHasAmbientTexture = false;
	Material->MaterialData.bHasSpecularTexture = false;
	Material->MaterialData.bHasBumpTexture = false;
	Material->SetParam("AmbientColor", FMaterialParamValue(Material->MaterialData.AmbientColor));
	Material->SetParam("DiffuseColor", FMaterialParamValue(Material->MaterialData.DiffuseColor));
	Material->SetParam("SpecularColor", FMaterialParamValue(Material->MaterialData.SpecularColor));
	Material->SetParam("EmissiveColor", FMaterialParamValue(Material->MaterialData.EmissiveColor));
	Material->SetParam("Shininess", FMaterialParamValue(Material->MaterialData.Shininess));
	Material->SetParam("Opacity", FMaterialParamValue(Material->MaterialData.Opacity));
	Material->SetParam("ScrollUV", FMaterialParamValue(FVector2(0.0f, 0.0f)));
	Material->SetParam("bHasDiffuseMap", FMaterialParamValue(false));
	Material->SetParam("bHasSpecularMap", FMaterialParamValue(false));
	Material->SetParam("bHasAmbientMap", FMaterialParamValue(false));
	Material->SetParam("bHasEmissiveMap", FMaterialParamValue(false));
	Material->SetParam("bHasBumpMap", FMaterialParamValue(false));
	if (UTexture* DefaultWhite = FResourceManager::Get().GetTexture("DefaultWhite"))
	{
		Material->SetParam("DiffuseMap", FMaterialParamValue(DefaultWhite));
		Material->SetParam("AmbientMap", FMaterialParamValue(DefaultWhite));
		Material->SetParam("SpecularMap", FMaterialParamValue(DefaultWhite));
		Material->SetParam("EmissiveMap", FMaterialParamValue(DefaultWhite));
		Material->SetParam("BumpMap", FMaterialParamValue(DefaultWhite));
	}

	if (!FResourceManager::Get().SerializeMaterial(RelativePath, Material))
	{
		return false;
	}

	SelectedPath = NewPath;
	if (EditorEngine)
	{
		const FEditorFileSystemState AfterState =
			EditorEngine->GetUndoSystem().CaptureFileSystemState(FPaths::ToUtf8(NewPath.wstring()), "Create Material");
		EditorEngine->GetUndoSystem().RecordCreateFileSystemPath(AfterState, "Create Material");
	}
	RefreshAfterAssetMutation();
	return true;
}

bool FEditorContentBrowserWidget::CreateCurveAsset()
{
	const std::filesystem::path NewPath = MakeUniquePath(CurrentPath / L"New Curve.uasset");
	const FString RelativePath = MakeRelativeProjectPath(NewPath);

	UCurveFloatAsset* Curve = UObjectManager::Get().CreateObject<UCurveFloatAsset>();
	if (!Curve)
	{
		return false;
	}

	Curve->SetAssetPath(RelativePath);

	FCurveKey StartKey;
	StartKey.Time = 0.0f;
	StartKey.Value = 0.0f;
	StartKey.InterpMode = ECurveInterpMode::Linear;

	FCurveKey EndKey;
	EndKey.Time = 1.0f;
	EndKey.Value = 1.0f;
	EndKey.InterpMode = ECurveInterpMode::Linear;

	FFloatCurve& FloatCurve = Curve->GetMutableCurve();
	FloatCurve.Keys.clear();
	FloatCurve.Keys.push_back(StartKey);
	FloatCurve.Keys.push_back(EndKey);
	FloatCurve.SortKeys();

	if (!FResourceManager::Get().SaveCurve(RelativePath, Curve))
	{
		return false;
	}

	SelectedPath = NewPath;
	if (EditorEngine)
	{
		const FEditorFileSystemState AfterState =
			EditorEngine->GetUndoSystem().CaptureFileSystemState(FPaths::ToUtf8(NewPath.wstring()), "Create Curve");
		EditorEngine->GetUndoSystem().RecordCreateFileSystemPath(AfterState, "Create Curve");
	}
	RefreshAfterAssetMutation();
	return true;
}

bool FEditorContentBrowserWidget::CreateAnimationStateMachineAsset()
{
	const std::filesystem::path NewPath = MakeUniquePath(CurrentPath / L"New Animation State Machine.uasset");
	const FString RelativePath = MakeRelativeProjectPath(NewPath);

	UAnimationStateMachine* StateMachine = UObjectManager::Get().CreateObject<UAnimationStateMachine>();
	if (!StateMachine)
	{
		return false;
	}

	StateMachine->InitialState = FName::None;
	StateMachine->States.clear();
	StateMachine->Transitions.clear();

	if (!FResourceManager::Get().SaveAnimationStateMachine(RelativePath, StateMachine))
	{
		UObjectManager::Get().DestroyObject(StateMachine);
		return false;
	}

	SelectedPath = NewPath;
	if (EditorEngine)
	{
		const FEditorFileSystemState AfterState =
			EditorEngine->GetUndoSystem().CaptureFileSystemState(FPaths::ToUtf8(NewPath.wstring()), "Create Animation State Machine");
		EditorEngine->GetUndoSystem().RecordCreateFileSystemPath(AfterState, "Create Animation State Machine");
	}
	RefreshAfterAssetMutation();
	return true;
}

bool FEditorContentBrowserWidget::CreateSceneAsset()
{
	if (!EditorEngine)
	{
		return false;
	}

	const std::filesystem::path NewPath = MakeUniquePath(CurrentPath / L"New Scene.Scene");
	if (!EditorEngine->GetSceneService().CreateSceneAsset(FPaths::ToUtf8(NewPath.wstring())).bSuccess)
	{
		return false;
	}

	SelectedPath = NewPath;
	if (EditorEngine)
	{
		const FEditorFileSystemState AfterState =
			EditorEngine->GetUndoSystem().CaptureFileSystemState(FPaths::ToUtf8(NewPath.wstring()), "Create Scene");
		EditorEngine->GetUndoSystem().RecordCreateFileSystemPath(AfterState, "Create Scene");
	}
	RefreshAfterAssetMutation();
	return true;
}

bool FEditorContentBrowserWidget::DeleteSelectedItem()
{
	if (SelectedPath.empty())
	{
		return false;
	}
	if (SelectedPath == CurrentPath || !IsPathAllowed(SelectedPath))
	{
		return false;
	}
	for (const std::filesystem::path& BrowserRoot : BrowserRootPaths)
	{
		if (SelectedPath.lexically_normal() == BrowserRoot.lexically_normal())
		{
			return false;
		}
	}

	const std::wstring Message = L"Delete selected content?\n\n" + SelectedPath.wstring();
	const int Result = MessageBoxW(nullptr, Message.c_str(), L"Delete Content", MB_ICONWARNING | MB_YESNO | MB_DEFBUTTON2);
	if (Result != IDYES)
	{
		return false;
	}

	const FEditorFileSystemState BeforeState = EditorEngine
		? EditorEngine->GetUndoSystem().CaptureFileSystemState(FPaths::ToUtf8(SelectedPath.wstring()), "Delete Asset")
		: FEditorFileSystemState{};

	std::error_code Ec;
	if (std::filesystem::is_directory(SelectedPath, Ec))
	{
		std::filesystem::remove_all(SelectedPath, Ec);
	}
	else
	{
		std::filesystem::remove(SelectedPath, Ec);
	}
	if (Ec)
	{
		return false;
	}

	SelectedPath.clear();
	if (EditorEngine)
	{
		EditorEngine->GetUndoSystem().RecordDeleteFileSystemPath(BeforeState, "Delete Asset");
	}
	RefreshAfterAssetMutation();
	return true;
}

void FEditorContentBrowserWidget::RequestRenameSelectedItem()
{
	if (SelectedPath.empty() || !IsPathAllowed(SelectedPath))
	{
		return;
	}
	for (const std::filesystem::path& BrowserRoot : BrowserRootPaths)
	{
		if (SelectedPath.lexically_normal() == BrowserRoot.lexically_normal())
		{
			return;
		}
	}

	RenameSourcePath = SelectedPath;
	const FString CurrentName = FPaths::ToUtf8(SelectedPath.filename().wstring());
	strncpy_s(RenameBuffer, CurrentName.c_str(), _TRUNCATE);
	bRenamePopupRequested = true;
}

bool FEditorContentBrowserWidget::CommitRename()
{
	if (RenameSourcePath.empty() || RenameBuffer[0] == '\0')
	{
		return false;
	}

	std::filesystem::path NewName(FPaths::ToWide(FString(RenameBuffer)));
	if (NewName.has_parent_path() || NewName.is_absolute())
	{
		return false;
	}

	std::error_code Ec;
	if (!std::filesystem::exists(RenameSourcePath, Ec))
	{
		return false;
	}

	const bool bIsDirectory = std::filesystem::is_directory(RenameSourcePath, Ec);
	if (!bIsDirectory && NewName.extension().empty())
	{
		NewName += RenameSourcePath.extension();
	}

	const std::filesystem::path TargetPath = (RenameSourcePath.parent_path() / NewName).lexically_normal();
	if (!IsPathAllowed(TargetPath) || TargetPath == RenameSourcePath)
	{
		return false;
	}
	if (std::filesystem::exists(TargetPath, Ec))
	{
		return false;
	}

	const std::filesystem::path OldPath = RenameSourcePath;
	std::filesystem::rename(RenameSourcePath, TargetPath, Ec);
	if (Ec)
	{
		return false;
	}

	SelectedPath = TargetPath;
	RenameSourcePath.clear();
	if (EditorEngine)
	{
		EditorEngine->GetUndoSystem().RecordRenameFileSystemPath(
			FPaths::ToUtf8(OldPath.wstring()),
			FPaths::ToUtf8(TargetPath.wstring()),
			"Rename Asset");
	}
	RefreshAfterAssetMutation();
	return true;
}

void FEditorContentBrowserWidget::DrawRenamePopup()
{
	if (bRenamePopupRequested)
	{
		ImGui::OpenPopup("Rename Content");
		bRenamePopupRequested = false;
	}

	if (ImGui::BeginPopupModal("Rename Content", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::TextUnformatted("Rename selected content");
		ImGui::TextDisabled("References are not remapped yet.");
		ImGui::SetNextItemWidth(320.0f);
		const bool bEnter = ImGui::InputText("##RenameContentInput", RenameBuffer, sizeof(RenameBuffer), ImGuiInputTextFlags_EnterReturnsTrue);
		ImGui::Spacing();
		if (ImGui::Button("Rename") || bEnter)
		{
			if (CommitRename())
			{
				ImGui::CloseCurrentPopup();
			}
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel"))
		{
			RenameSourcePath.clear();
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}
}

void FEditorContentBrowserWidget::OpenFbxImportPopup(const std::filesystem::path& FbxPath)
{
	PendingFbxImportPath = FbxPath;
	const FString SourcePath = MakeRelativeProjectPath(PendingFbxImportPath);
	PendingFbxMeshInfo = FFbxMeshContentInfo();
	EditorEngine->GetNotificationService().Info("Opened FBX import options.");

    const auto ClipInspectStart = std::chrono::steady_clock::now();
	PendingFbxAnimationClips = FResourceManager::Get().InspectAnimationClips(SourcePath);
    const auto ClipInspectEnd = std::chrono::steady_clock::now();
    const double ClipInspectTime = std::chrono::duration<double>(ClipInspectEnd - ClipInspectStart).count();
    UE_LOG("[FEditorContentBrowserWidget::OpenFbxImportPopup] Inspect AnimationClips Time %.6f", ClipInspectTime);

	PendingAnimClipIndex = 0;
	PendingTargetSkeletalMeshIndex = 0;
	FString DefaultAnimAssetName = FPaths::ToUtf8(PendingFbxImportPath.stem().wstring());
	if (!PendingFbxAnimationClips.empty() && !PendingFbxAnimationClips[0].Name.empty())
	{
		DefaultAnimAssetName = PendingFbxAnimationClips[0].Name;
	}
	DefaultAnimAssetName = SanitizeAssetFileStem(
		DefaultAnimAssetName,
		FPaths::ToUtf8(PendingFbxImportPath.stem().wstring()));
	strncpy_s(PendingAnimAssetNameBuffer, DefaultAnimAssetName.c_str(), _TRUNCATE);

	bImportFbxAsStaticMesh = PendingFbxAnimationClips.empty();
	bImportFbxAsSkeletalMesh = false;
	bImportFbxAsAnimationSequence = !PendingFbxAnimationClips.empty();
	bImportAllFbxAnimationClips = false;
	bFbxImportPopupRequested = true;
}

void FEditorContentBrowserWidget::DrawFbxImportPopup()
{
	if (bFbxImportPopupRequested)
	{
		ImGui::OpenPopup("Import FBX");
		bFbxImportPopupRequested = false;
	}

	if (!ImGui::BeginPopupModal("Import FBX", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
	{
		return;
	}

	const FString SourcePath = MakeRelativeProjectPath(PendingFbxImportPath);
	ImGui::TextWrapped("%s", SourcePath.c_str());
	ImGui::Spacing();
	ImGui::TextDisabled("Detected");
	ImGui::TextDisabled("Mesh type detection skipped for fast open.");
	ImGui::Text("Animation Clips: %d", static_cast<int32>(PendingFbxAnimationClips.size()));
	ImGui::Separator();

	ImGui::Checkbox("Import Static Mesh", &bImportFbxAsStaticMesh);
	ImGui::Checkbox("Import Skeletal Mesh", &bImportFbxAsSkeletalMesh);

	if (!PendingFbxAnimationClips.empty())
	{
		ImGui::Spacing();
		ImGui::TextDisabled("Animation Sequence");
		ImGui::Checkbox("Import Animation Sequence", &bImportFbxAsAnimationSequence);
		const int32 ClipCount = static_cast<int32>(PendingFbxAnimationClips.size());
		if (ClipCount > 1)
		{
			ImGui::Checkbox("Import All Animation Clips", &bImportAllFbxAnimationClips);
		}
		else
		{
			bImportAllFbxAnimationClips = false;
		}
		PendingAnimClipIndex = std::clamp(PendingAnimClipIndex, 0, std::max(0, ClipCount - 1));
		const FFbxAnimationClipInfo& SelectedClip = PendingFbxAnimationClips[PendingAnimClipIndex];
		ImGui::BeginDisabled(bImportAllFbxAnimationClips);
		if (ImGui::BeginCombo("Clip", SelectedClip.Name.c_str()))
		{
			for (int32 ClipIndex = 0; ClipIndex < ClipCount; ++ClipIndex)
			{
				const bool bSelected = PendingAnimClipIndex == ClipIndex;
				const FFbxAnimationClipInfo& Clip = PendingFbxAnimationClips[ClipIndex];
				FString Label = Clip.Name.empty()
					? FString("Clip ") + std::to_string(Clip.AnimStackIndex)
					: Clip.Name;
				if (ImGui::Selectable(Label.c_str(), bSelected))
				{
					PendingAnimClipIndex = ClipIndex;
					if (!Clip.Name.empty())
					{
						const FString SafeClipName = SanitizeAssetFileStem(
							Clip.Name,
							FString("Clip") + std::to_string(Clip.AnimStackIndex));
						strncpy_s(PendingAnimAssetNameBuffer, SafeClipName.c_str(), _TRUNCATE);
					}
				}
				if (bSelected)
				{
					ImGui::SetItemDefaultFocus();
				}
			}
			ImGui::EndCombo();
		}
		ImGui::EndDisabled();

		ImGui::SetNextItemWidth(320.0f);
		ImGui::InputText(
			bImportAllFbxAnimationClips ? "Animation Asset Prefix" : "Animation Asset Name",
			PendingAnimAssetNameBuffer,
			sizeof(PendingAnimAssetNameBuffer));

		const TArray<FString>& SkeletalMeshPaths = EditorEngine->GetAssetService().GetSkeletalMeshAssetPaths();
		const bool bHasTargetMesh = !SkeletalMeshPaths.empty();
		PendingTargetSkeletalMeshIndex = std::clamp(
			PendingTargetSkeletalMeshIndex,
			0,
			std::max(0, static_cast<int32>(SkeletalMeshPaths.size()) - 1));
		const FString TargetPreview = bHasTargetMesh ? SkeletalMeshPaths[PendingTargetSkeletalMeshIndex] : "No SkeletalMesh assets";
		ImGui::BeginDisabled(!bHasTargetMesh);
		if (ImGui::BeginCombo("Target SkeletalMesh", TargetPreview.c_str()))
		{
			for (int32 MeshIndex = 0; MeshIndex < static_cast<int32>(SkeletalMeshPaths.size()); ++MeshIndex)
			{
				const bool bSelected = PendingTargetSkeletalMeshIndex == MeshIndex;
				if (ImGui::Selectable(SkeletalMeshPaths[MeshIndex].c_str(), bSelected))
				{
					PendingTargetSkeletalMeshIndex = MeshIndex;
				}
				if (bSelected)
				{
					ImGui::SetItemDefaultFocus();
				}
			}
			ImGui::EndCombo();
		}
		ImGui::EndDisabled();
	}

	ImGui::Separator();
	if (ImGui::Button("Import"))
	{
		if (StartFbxImportTask())
		{
			ImGui::CloseCurrentPopup();
		}
	}
	ImGui::SameLine();
	if (ImGui::Button("Cancel"))
	{
		EditorEngine->GetNotificationService().Info("FBX import canceled.");
		ImGui::CloseCurrentPopup();
	}

	ImGui::EndPopup();
}

void FEditorContentBrowserWidget::StartObjStaticMeshImportTask(const std::filesystem::path& SourcePath)
{
	if (!EditorEngine)
	{
		return;
	}
	if (PendingImportTask.IsActive())
	{
		EditorEngine->GetNotificationService().Warning("Another asset import is already running.");
		return;
	}

	std::filesystem::path DestinationPath = SourcePath;
	DestinationPath.replace_extension(L".uasset");
	DestinationPath = MakeUniquePath(DestinationPath);

	PendingImportTask.Reset();
	PendingImportTask.Kind = EAssetImportTaskKind::ObjStaticMesh;
	PendingImportTask.Step = EAssetImportTaskStep::ObjImport;
	PendingImportTask.SourcePath = SourcePath;
	PendingImportTask.DestinationPath = DestinationPath;
	PendingImportTask.SourceAssetPath = MakeRelativeProjectPath(SourcePath);
	PendingImportTask.DestinationAssetPath = MakeRelativeProjectPath(DestinationPath);
	PendingImportTask.ToastHandle = EditorEngine->GetNotificationService().BeginTask(
		"Importing OBJ",
		"Queued StaticMesh import...",
		0.05f);
}

bool FEditorContentBrowserWidget::StartFbxImportTask()
{
	if (!EditorEngine)
	{
		return false;
	}
	if (PendingImportTask.IsActive())
	{
		EditorEngine->GetNotificationService().Warning("Another asset import is already running.");
		return false;
	}

	const bool bWillImportStaticMesh = bImportFbxAsStaticMesh;
	const bool bWillImportSkeletalMesh = bImportFbxAsSkeletalMesh;
	const bool bWillImportAnimation =
		bImportFbxAsAnimationSequence && !PendingFbxAnimationClips.empty();
	if (!bWillImportStaticMesh && !bWillImportSkeletalMesh && !bWillImportAnimation)
	{
		EditorEngine->GetNotificationService().Warning("No FBX import option selected.");
		return false;
	}

	PendingImportTask.Reset();
	PendingImportTask.Kind = EAssetImportTaskKind::Fbx;
	PendingImportTask.Step = bWillImportStaticMesh
		? EAssetImportTaskStep::FbxStaticMesh
		: (bWillImportSkeletalMesh
			? EAssetImportTaskStep::FbxSkeletalMesh
			: EAssetImportTaskStep::FbxPrepareAnimation);
	PendingImportTask.SourcePath = PendingFbxImportPath;
	PendingImportTask.SourceAssetPath = MakeRelativeProjectPath(PendingFbxImportPath);
	PendingImportTask.AnimationClips = PendingFbxAnimationClips;
	PendingImportTask.AnimationAssetNameInput = FString(PendingAnimAssetNameBuffer);
	PendingImportTask.FallbackPrefix = FPaths::ToUtf8(PendingFbxImportPath.stem().wstring());
	PendingImportTask.ClipIndex = std::clamp(
		PendingAnimClipIndex,
		0,
		std::max(0, static_cast<int32>(PendingFbxAnimationClips.size()) - 1));
	PendingImportTask.TargetSkeletalMeshIndex = PendingTargetSkeletalMeshIndex;
	PendingImportTask.bImportStaticMesh = bWillImportStaticMesh;
	PendingImportTask.bImportSkeletalMesh = bWillImportSkeletalMesh;
	PendingImportTask.bImportAnimationSequence = bWillImportAnimation;
	PendingImportTask.bImportAllAnimationClips =
		bImportAllFbxAnimationClips && PendingFbxAnimationClips.size() > 1;
	PendingImportTask.ToastHandle = EditorEngine->GetNotificationService().BeginTask(
		"Importing FBX",
		"Queued FBX import...",
		0.05f);
	return true;
}

void FEditorContentBrowserWidget::AdvancePendingImportTask(EAssetImportTaskStep NextStep)
{
	PendingImportTask.Step = NextStep;
	PendingImportTask.bStepPrepared = false;
}

void FEditorContentBrowserWidget::FinishPendingImportTask(EEditorNotificationType Type, const FString& Message)
{
	if (EditorEngine)
	{
		EditorEngine->GetNotificationService().FinishTask(PendingImportTask.ToastHandle, Type, Message);
	}
	PendingImportTask.Reset();
}

void FEditorContentBrowserWidget::TickPendingImportTask()
{
	if (!EditorEngine || !PendingImportTask.IsActive())
	{
		return;
	}

	if (PendingImportTask.bWaitingFirstFrame)
	{
		PendingImportTask.bWaitingFirstFrame = false;
		return;
	}

	FEditorAssetImportService ImportService;
	auto& Notifications = EditorEngine->GetNotificationService();

	switch (PendingImportTask.Step)
	{
	case EAssetImportTaskStep::ObjImport:
	{
		if (!PendingImportTask.bStepPrepared)
		{
			Notifications.UpdateTask(
				PendingImportTask.ToastHandle,
				"Building StaticMesh asset... Editor may pause briefly.",
				0.35f);
			PendingImportTask.bStepPrepared = true;
			return;
		}

		PendingImportTask.bAttemptedImport = true;
		if (ImportService.ImportStaticMeshFromSource(
			PendingImportTask.SourceAssetPath,
			PendingImportTask.DestinationAssetPath))
		{
			SelectedPath = PendingImportTask.DestinationPath;
			PendingImportTask.bImportedAny = true;
			AdvancePendingImportTask(EAssetImportTaskStep::ObjRefresh);
		}
		else
		{
			FinishPendingImportTask(EEditorNotificationType::Error, "Failed to import StaticMesh from OBJ.");
		}
		return;
	}
	case EAssetImportTaskStep::ObjRefresh:
	{
		if (!PendingImportTask.bStepPrepared)
		{
			Notifications.UpdateTask(
				PendingImportTask.ToastHandle,
				"Refreshing Content Browser...",
				0.90f);
			PendingImportTask.bStepPrepared = true;
			return;
		}

		RefreshAfterAssetMutation();
		FinishPendingImportTask(EEditorNotificationType::Info, "Imported StaticMesh .uasset.");
		return;
	}
	case EAssetImportTaskStep::FbxStaticMesh:
	{
		if (!PendingImportTask.bImportStaticMesh)
		{
			AdvancePendingImportTask(EAssetImportTaskStep::FbxSkeletalMesh);
			return;
		}
		if (!PendingImportTask.bStepPrepared)
		{
			Notifications.UpdateTask(
				PendingImportTask.ToastHandle,
				"Importing StaticMesh... Editor may pause briefly.",
				0.18f);
			PendingImportTask.bStepPrepared = true;
			return;
		}

		std::filesystem::path DestinationPath = PendingImportTask.SourcePath;
		DestinationPath.replace_extension(L".uasset");
		DestinationPath = MakeUniquePath(DestinationPath);

		const FString AssetPath = MakeRelativeProjectPath(DestinationPath);
		PendingImportTask.bAttemptedImport = true;
		if (ImportService.ImportStaticMeshFromSource(PendingImportTask.SourceAssetPath, AssetPath))
		{
			SelectedPath = DestinationPath;
			const FEditorFileSystemState AfterState =
				EditorEngine->GetUndoSystem().CaptureFileSystemState(
					FPaths::ToUtf8(DestinationPath.wstring()),
					"Import Static Mesh");
			EditorEngine->GetUndoSystem().RecordCreateFileSystemPath(AfterState, "Import Static Mesh");
			PendingImportTask.bImportedAny = true;
			++PendingImportTask.SuccessCount;
			RefreshAfterAssetMutation();
		}
		else
		{
			PendingImportTask.bHadFailure = true;
			++PendingImportTask.FailCount;
		}

		AdvancePendingImportTask(EAssetImportTaskStep::FbxSkeletalMesh);
		return;
	}
	case EAssetImportTaskStep::FbxSkeletalMesh:
	{
		if (!PendingImportTask.bImportSkeletalMesh)
		{
			AdvancePendingImportTask(EAssetImportTaskStep::FbxPrepareAnimation);
			return;
		}
		if (!PendingImportTask.bStepPrepared)
		{
			Notifications.UpdateTask(
				PendingImportTask.ToastHandle,
				"Importing SkeletalMesh... Editor may pause briefly.",
				0.22f);
			PendingImportTask.bStepPrepared = true;
			return;
		}

		std::filesystem::path DestinationPath = PendingImportTask.SourcePath;
		DestinationPath.replace_extension(L".uasset");
		DestinationPath = MakeUniquePath(DestinationPath);

		const FString AssetPath = MakeRelativeProjectPath(DestinationPath);
		PendingImportTask.bAttemptedImport = true;
		if (ImportService.ImportSkeletalMeshFromFbx(PendingImportTask.SourceAssetPath, AssetPath))
		{
			SelectedPath = DestinationPath;
			PendingImportTask.ImportedSkeletalMeshAssetPath = AssetPath;
			const FEditorFileSystemState AfterState =
				EditorEngine->GetUndoSystem().CaptureFileSystemState(
					FPaths::ToUtf8(DestinationPath.wstring()),
					"Import Skeletal Mesh");
			EditorEngine->GetUndoSystem().RecordCreateFileSystemPath(AfterState, "Import Skeletal Mesh");
			PendingImportTask.bImportedAny = true;
			++PendingImportTask.SuccessCount;
			RefreshAfterAssetMutation();
		}
		else
		{
			PendingImportTask.bHadFailure = true;
			++PendingImportTask.FailCount;
		}

		AdvancePendingImportTask(EAssetImportTaskStep::FbxPrepareAnimation);
		return;
	}
	case EAssetImportTaskStep::FbxPrepareAnimation:
	{
		if (!PendingImportTask.bImportAnimationSequence)
		{
			AdvancePendingImportTask(EAssetImportTaskStep::FbxRefresh);
			return;
		}
		if (!PendingImportTask.bStepPrepared)
		{
			Notifications.UpdateTask(
				PendingImportTask.ToastHandle,
				"Preparing Animation Sequence import...",
				0.48f);
			PendingImportTask.bStepPrepared = true;
			return;
		}

		EditorEngine->GetAssetService().RefreshAssetDatabase();
		const TArray<FString>& SkeletalMeshPaths = EditorEngine->GetAssetService().GetSkeletalMeshAssetPaths();
		if (SkeletalMeshPaths.empty())
		{
			PendingImportTask.bHadFailure = true;
			++PendingImportTask.FailCount;
			AdvancePendingImportTask(EAssetImportTaskStep::FbxRefresh);
			return;
		}

		PendingImportTask.TargetSkeletalMeshIndex = std::clamp(
			PendingImportTask.TargetSkeletalMeshIndex,
			0,
			std::max(0, static_cast<int32>(SkeletalMeshPaths.size()) - 1));
		PendingImportTask.TargetSkeletalMeshAssetPath =
			PendingImportTask.ImportedSkeletalMeshAssetPath.empty()
				? SkeletalMeshPaths[PendingImportTask.TargetSkeletalMeshIndex]
				: PendingImportTask.ImportedSkeletalMeshAssetPath;

		PendingImportTask.ClipsToImport.clear();
		if (PendingImportTask.bImportAllAnimationClips)
		{
			PendingImportTask.ClipsToImport = PendingImportTask.AnimationClips;
		}
		else if (!PendingImportTask.AnimationClips.empty())
		{
			const int32 ClampedIndex = std::clamp(
				PendingImportTask.ClipIndex,
				0,
				std::max(0, static_cast<int32>(PendingImportTask.AnimationClips.size()) - 1));
			PendingImportTask.ClipsToImport.push_back(PendingImportTask.AnimationClips[ClampedIndex]);
		}

		PendingImportTask.ClipIndex = 0;
		AdvancePendingImportTask(EAssetImportTaskStep::FbxAnimationClip);
		return;
	}
	case EAssetImportTaskStep::FbxAnimationClip:
	{
		if (PendingImportTask.ClipIndex >= static_cast<int32>(PendingImportTask.ClipsToImport.size()))
		{
			AdvancePendingImportTask(EAssetImportTaskStep::FbxRefresh);
			return;
		}

		const int32 TotalClips = static_cast<int32>(PendingImportTask.ClipsToImport.size());
		if (!PendingImportTask.bStepPrepared)
		{
			const FString Message =
				"Importing Animation Sequence " +
				std::to_string(PendingImportTask.ClipIndex + 1) +
				" / " +
				std::to_string(std::max(1, TotalClips)) +
				"... Editor may pause briefly.";
			const float Progress = 0.55f + 0.30f *
				(static_cast<float>(PendingImportTask.ClipIndex) / static_cast<float>(std::max(1, TotalClips)));
			Notifications.UpdateTask(PendingImportTask.ToastHandle, Message, Progress);
			PendingImportTask.bStepPrepared = true;
			return;
		}

		const FFbxAnimationClipInfo& Clip = PendingImportTask.ClipsToImport[PendingImportTask.ClipIndex];
		std::filesystem::path DestinationPath = PendingImportTask.SourcePath;
		if (PendingImportTask.bImportAllAnimationClips)
		{
			const FString AssetStem = MakeClipAssetStem(
				PendingImportTask.AnimationAssetNameInput,
				Clip,
				PendingImportTask.FallbackPrefix);
			std::filesystem::path AssetFileName(FPaths::ToWide(AssetStem));
			AssetFileName += L".uasset";
			DestinationPath.replace_filename(AssetFileName);
		}
		else
		{
			std::filesystem::path AssetFileName(FPaths::ToWide(PendingImportTask.AnimationAssetNameInput));
			if (AssetFileName.empty())
			{
				AssetFileName = PendingImportTask.SourcePath.stem();
			}
			if (AssetFileName.has_parent_path() || AssetFileName.is_absolute())
			{
				FinishPendingImportTask(
					EEditorNotificationType::Warning,
					"Animation asset name must not include a path.");
				return;
			}
			if (AssetFileName.extension().empty())
			{
				AssetFileName += L".uasset";
			}
			else
			{
				AssetFileName.replace_extension(L".uasset");
			}
			DestinationPath.replace_filename(AssetFileName);
		}
		DestinationPath = MakeUniquePath(DestinationPath);

		const FString AssetPath = MakeRelativeProjectPath(DestinationPath);
		PendingImportTask.bAttemptedImport = true;
		if (ImportService.ImportAnimationSequenceFromFbx(
			PendingImportTask.SourceAssetPath,
			AssetPath,
			PendingImportTask.TargetSkeletalMeshAssetPath,
			Clip.AnimStackIndex))
		{
			SelectedPath = DestinationPath;
			const FEditorFileSystemState AfterState =
				EditorEngine->GetUndoSystem().CaptureFileSystemState(
					FPaths::ToUtf8(DestinationPath.wstring()),
					"Import Animation Sequence");
			EditorEngine->GetUndoSystem().RecordCreateFileSystemPath(AfterState, "Import Animation Sequence");
			PendingImportTask.bImportedAny = true;
			++PendingImportTask.SuccessCount;
			RefreshAfterAssetMutation();
		}
		else
		{
			PendingImportTask.bHadFailure = true;
			++PendingImportTask.FailCount;
		}

		++PendingImportTask.ClipIndex;
		PendingImportTask.bStepPrepared = false;
		return;
	}
	case EAssetImportTaskStep::FbxRefresh:
	{
		if (!PendingImportTask.bStepPrepared)
		{
			Notifications.UpdateTask(
				PendingImportTask.ToastHandle,
				"Refreshing Content Browser...",
				0.93f);
			PendingImportTask.bStepPrepared = true;
			return;
		}

		if (PendingImportTask.bImportedAny)
		{
			RefreshAfterAssetMutation();
		}

		if (PendingImportTask.bImportedAny && PendingImportTask.bHadFailure)
		{
			FinishPendingImportTask(
				EEditorNotificationType::Warning,
				"Imported " + std::to_string(PendingImportTask.SuccessCount) +
				" FBX asset(s). Failed " + std::to_string(PendingImportTask.FailCount) + ".");
		}
		else if (PendingImportTask.bImportedAny)
		{
			FinishPendingImportTask(
				EEditorNotificationType::Info,
				"Imported " + std::to_string(PendingImportTask.SuccessCount) + " FBX asset(s).");
		}
		else if (PendingImportTask.bImportAnimationSequence && PendingImportTask.AnimationClips.empty())
		{
			FinishPendingImportTask(EEditorNotificationType::Warning, "FBX has no animation clips to import.");
		}
		else
		{
			FinishPendingImportTask(EEditorNotificationType::Error, "Failed to import FBX assets.");
		}
		return;
	}
	default:
		break;
	}
}

std::filesystem::path FEditorContentBrowserWidget::MakeUniquePath(const std::filesystem::path& DesiredPath) const
{
	std::filesystem::path Candidate = DesiredPath;
	std::error_code Ec;
	if (!std::filesystem::exists(Candidate, Ec))
	{
		return Candidate;
	}

	const std::filesystem::path Parent = DesiredPath.parent_path();
	const std::wstring Stem = DesiredPath.stem().wstring();
	const std::wstring Extension = DesiredPath.extension().wstring();
	for (int32 Index = 1; Index < 10000; ++Index)
	{
		Candidate = Parent / (Stem + L" " + std::to_wstring(Index) + Extension);
		if (!std::filesystem::exists(Candidate, Ec))
		{
			return Candidate;
		}
	}
	return DesiredPath;
}

void FEditorContentBrowserWidget::DrawFolderIcon(ImDrawList* DrawList, const ImVec2& Min, const ImVec2& Max, ImU32 Color) const
{
	const float Width = Max.x - Min.x;
	const float Height = Max.y - Min.y;
	const ImVec2 TabMin(Min.x + Width * 0.08f, Min.y + Height * 0.14f);
	const ImVec2 TabMax(Min.x + Width * 0.44f, Min.y + Height * 0.34f);
	const ImVec2 BodyMin(Min.x + Width * 0.06f, Min.y + Height * 0.26f);
	const ImVec2 BodyMax(Min.x + Width * 0.94f, Min.y + Height * 0.86f);
	const ImU32 Shadow = ImGui::GetColorU32(ImVec4(0.04f, 0.05f, 0.06f, 0.42f));
	const ImU32 Highlight = ImGui::GetColorU32(ImVec4(1.0f, 0.92f, 0.60f, 0.18f));

	DrawList->AddRectFilled(ImVec2(BodyMin.x + 2.0f, BodyMin.y + 3.0f), ImVec2(BodyMax.x + 2.0f, BodyMax.y + 3.0f), Shadow, 5.0f);
	DrawList->AddRectFilled(TabMin, TabMax, Color, 4.0f);
	DrawList->AddRectFilled(BodyMin, BodyMax, Color, 5.0f);
	DrawList->AddRectFilled(ImVec2(BodyMin.x + 4.0f, BodyMin.y + 5.0f), ImVec2(BodyMax.x - 4.0f, BodyMin.y + 11.0f), Highlight, 4.0f);
	DrawList->AddRect(BodyMin, BodyMax, ImGui::GetColorU32(ImVec4(0.20f, 0.18f, 0.10f, 0.45f)), 5.0f);
}

void FEditorContentBrowserWidget::DrawDetails()
{
	ImGui::TextUnformatted("Details");
	ImGui::Separator();
	if (SelectedPath.empty())
	{
		ImGui::TextDisabled("No asset selected.");
		return;
	}

	const std::filesystem::path Filename = SelectedPath.filename();
	const FString PathText = FPaths::ToUtf8(SelectedPath.wstring());
	ImGui::TextWrapped("%s", FPaths::ToUtf8(Filename.wstring()).c_str());
	ImGui::Spacing();
	ImGui::TextDisabled("Path");
	ImGui::TextWrapped("%s", PathText.c_str());

	std::error_code Ec;
	if (std::filesystem::exists(SelectedPath, Ec) && !std::filesystem::is_directory(SelectedPath, Ec))
	{
		ImGui::Spacing();
		ImGui::Text("Size: %.2f KB", static_cast<double>(std::filesystem::file_size(SelectedPath, Ec)) / 1024.0);
		DrawAssetPreview();
	}
}

void FEditorContentBrowserWidget::DrawAssetPreview()
{
	const FString Extension = ToLower(FPaths::ToUtf8(SelectedPath.extension().wstring()));
	const FString RelativePath = MakeRelativeProjectPath(SelectedPath);
	if (IsPreviewableImage(Extension))
	{
		if (UTexture* Texture = FResourceManager::Get().LoadTexture(RelativePath))
		{
			if (ID3D11ShaderResourceView* SRV = Texture->GetSRV())
			{
				ImGui::Spacing();
				ImGui::TextDisabled("Preview");
				const float Width = std::min(ImGui::GetContentRegionAvail().x, 220.0f);
				ImGui::Image(reinterpret_cast<ImTextureID>(SRV), ImVec2(Width, Width));
			}
		}
		return;
	}

	if (IsMaterialAssetPath(SelectedPath))
	{
		UMaterialInterface* Material = FResourceManager::Get().GetMaterialInterface(RelativePath);
		if (!Material)
		{
			Material = FResourceManager::Get().GetMaterialInterface(FPaths::Normalize(FPaths::ToUtf8(SelectedPath.wstring())));
		}
		if (!Material)
		{
			Material = FResourceManager::Get().GetMaterialInterface(FPaths::ToUtf8(SelectedPath.stem().wstring()));
		}
		ImGui::Spacing();
		ImGui::TextDisabled("Material");
		if (!Material)
		{
			ImGui::TextWrapped("Not loaded in ResourceManager.");
			return;
		}

		ImGui::TextWrapped("%s", Material->GetName().c_str());
		TMap<FString, FMaterialParamValue> Params;
		Material->GatherAllParams(Params);

		ImGui::Spacing();
		ImGui::TextDisabled("Color Parameters");
		int32 ColorCount = 0;
		for (const auto& [ParamName, ParamValue] : Params)
		{
			if (ParamValue.Type == EMaterialParamType::Vector3 && std::holds_alternative<FVector>(ParamValue.Value))
			{
				const FVector& Color = std::get<FVector>(ParamValue.Value);
				ImGui::ColorButton(ParamName.c_str(), ImVec4(Color.X, Color.Y, Color.Z, 1.0f), ImGuiColorEditFlags_NoTooltip, ImVec2(38.0f, 20.0f));
				ImGui::SameLine();
				ImGui::TextUnformatted(ParamName.c_str());
				++ColorCount;
			}
		}
		if (ColorCount == 0)
		{
			ImGui::TextDisabled("No color parameters.");
		}

		ImGui::Spacing();
		ImGui::TextDisabled("Texture Parameters");
		int32 TextureCount = 0;
		for (const auto& [ParamName, ParamValue] : Params)
		{
			if (ParamValue.Type == EMaterialParamType::Texture && std::holds_alternative<UTexture*>(ParamValue.Value))
			{
				UTexture* Texture = std::get<UTexture*>(ParamValue.Value);
				if (Texture && Texture->GetSRV())
				{
					ImGui::TextDisabled("%s", ParamName.c_str());
					ImGui::Image(reinterpret_cast<ImTextureID>(Texture->GetSRV()), ImVec2(72.0f, 72.0f));
					++TextureCount;
				}
			}
		}
		if (TextureCount == 0)
		{
			ImGui::TextDisabled("No texture parameter preview.");
		}
		return;
	}

	if (IsCurveAsset(SelectedPath))
	{
		UCurveFloatAsset* Curve = FResourceManager::Get().LoadCurve(RelativePath);
		ImGui::Spacing();
		ImGui::TextDisabled("Float Curve");
		if (!Curve)
		{
			ImGui::TextWrapped("Not loaded in ResourceManager.");
			return;
		}

		const FFloatCurve& FloatCurve = Curve->GetCurve();
		ImGui::Text("Keys: %d", static_cast<int32>(FloatCurve.Keys.size()));
		ImGui::Text("Range: %.3f - %.3f", FloatCurve.GetStartTime(), FloatCurve.GetEndTime());
		ImGui::Text("Value at 0.0: %.3f", Curve->Evaluate(0.0f));
		ImGui::Text("Value at 1.0: %.3f", Curve->Evaluate(1.0f));
		return;
	}

	if (IsUAsset(Extension))
	{
		FAssetMetaData Metadata;
		ImGui::Spacing();
		ImGui::TextDisabled("UAsset");
		if (!FAssetFile::LoadMetadataOnly(RelativePath, Metadata))
		{
			ImGui::TextWrapped("Invalid or unreadable .uasset metadata.");
			return;
		}

		ImGui::Text("Class: %s", GetAssetClassDisplayName(Metadata.ClassName));
		if (!Metadata.DisplayName.empty())
		{
			ImGui::TextWrapped("Name: %s", Metadata.DisplayName.c_str());
		}
		if (!Metadata.AssetGuid.empty())
		{
			ImGui::TextWrapped("Guid: %s", Metadata.AssetGuid.c_str());
		}
		if (!Metadata.SourceFile.empty())
		{
			ImGui::TextWrapped("Source: %s", Metadata.SourceFile.c_str());
		}
		if (Metadata.ClassName == "UAnimSequence")
		{
			//ImGui::Text("Anim Stack: %d", Metadata.AnimStackIndex);
		}
		//if (!Metadata.Dependencies.empty())
		//{
		//	ImGui::Spacing();
		//	ImGui::TextDisabled("Dependencies");
		//	for (const FAssetDependency& Dependency : Metadata.Dependencies)
		//	{
		//		ImGui::TextWrapped("%s: %s", Dependency.Type.c_str(), Dependency.Path.c_str());
		//	}
		//}
		return;
	}

	if (IsSequenceAsset(Extension))
	{
		ImGui::Spacing();
		ImGui::TextDisabled("Sequence Asset");
		ImGui::TextWrapped(".sequence is reserved for future Level Sequence / Animation Sequence assets.");
		return;
	}

	if (IsPrefabAsset(Extension))
	{
		ImGui::Spacing();
		ImGui::TextDisabled("Prefab Template");
		ImGui::TextWrapped("Spawns a normal independent actor. Scene saves the spawned actor, not a prefab link.");
		ImGui::Spacing();
		if (ImGui::Button("Spawn at Origin"))
		{
			EditorEngine->GetMainPanel().SpawnPrefabAtOrigin(RelativePath);
		}
	}
}

ID3D11ShaderResourceView* FEditorContentBrowserWidget::GetImagePreviewSRV(const FContentItem& Item)
{
	if (!IsPreviewableImage(Item.Extension))
	{
		return nullptr;
	}

	UTexture* Texture = FResourceManager::Get().GetTexture(MakeRelativeProjectPath(Item.Path));
	return Texture ? Texture->GetSRV() : nullptr;
}

ID3D11ShaderResourceView* FEditorContentBrowserWidget::GetMaterialPreviewSRV(const FContentItem& Item, uint32 Width, uint32 Height, bool bHighPriority)
{
	(void)bHighPriority;
	if (!EditorEngine || !IsMaterialAsset(Item) || Width == 0 || Height == 0)
	{
		return nullptr;
	}

	Width = ContentBrowserThumbnailSnapshotSize;
	Height = ContentBrowserThumbnailSnapshotSize;

	const FString RelativePath = MakeRelativeProjectPath(Item.Path);
	const FString CacheKey = RelativePath + "#thumbnail";
	auto Found = MaterialPreviewCache.find(CacheKey);
	if (Found != MaterialPreviewCache.end())
	{
		return Found->second.SRV.Get();
	}

	if (FailedPreviewCacheKeys.find(CacheKey) != FailedPreviewCacheKeys.end())
	{
		return nullptr;
	}

	if (MaterialPreviewBuildsThisFrame >= 1)
	{
		return nullptr;
	}
	++MaterialPreviewBuildsThisFrame;

	UMaterialInterface* Material = ResolveMaterialAsset(Item.Path);
	if (!Material)
	{
		FailedPreviewCacheKeys.insert(CacheKey);
		return nullptr;
	}

	if (MaterialPreviewMesh == nullptr)
	{
		MaterialPreviewMesh = FResourceManager::Get().LoadStaticMesh("Asset\\Mesh\\PreviewSphere.uasset");
	}
	if (!MaterialPreviewMesh || !MaterialPreviewMesh->HasValidMeshData())
	{
		FailedPreviewCacheKeys.insert(CacheKey);
		return nullptr;
	}

	FEditorRenderPipeline* RenderPipeline = EditorEngine->GetEditorRenderPipeline();
	if (!RenderPipeline)
	{
		FailedPreviewCacheKeys.insert(CacheKey);
		return nullptr;
	}

	ID3D11ShaderResourceView* PreviewSRV = RenderPipeline->RenderMaterialPreview(
		EditorEngine->GetRenderer(),
		MaterialPreviewMesh,
		Material,
		Width,
		Height,
		0.8f,
		0.25f,
		4.0f);

	FMaterialPreviewSnapshot Snapshot;
	if (!CapturePreviewSnapshot(PreviewSRV, Snapshot, Width, Height))
	{
		FailedPreviewCacheKeys.insert(CacheKey);
		return nullptr;
	}

	FMaterialPreviewSnapshot& CachedSnapshot = MaterialPreviewCache[CacheKey];
	CachedSnapshot = Snapshot;
	return CachedSnapshot.SRV.Get();
}

ID3D11ShaderResourceView* FEditorContentBrowserWidget::GetStaticMeshPreviewSRV(const FContentItem& Item, uint32 Width, uint32 Height, bool bHighPriority)
{
	(void)bHighPriority;
	if (!EditorEngine || !IsStaticMeshAsset(Item) || Width == 0 || Height == 0)
	{
		return nullptr;
	}

	Width = ContentBrowserThumbnailSnapshotSize;
	Height = ContentBrowserThumbnailSnapshotSize;

	const FString RelativePath = MakeRelativeProjectPath(Item.Path);
	const FString CacheKey = RelativePath + "#thumbnail";
	auto Found = StaticMeshPreviewCache.find(CacheKey);
	if (Found != StaticMeshPreviewCache.end())
	{
		return Found->second.SRV.Get();
	}

	if (FailedPreviewCacheKeys.find(CacheKey) != FailedPreviewCacheKeys.end())
	{
		return nullptr;
	}

	if (MaterialPreviewBuildsThisFrame >= 1)
	{
		return nullptr;
	}
	++MaterialPreviewBuildsThisFrame;

	UStaticMesh* Mesh = FResourceManager::Get().LoadStaticMesh(RelativePath);
	if (!Mesh || !Mesh->HasValidMeshData())
	{
		FailedPreviewCacheKeys.insert(CacheKey);
		return nullptr;
	}

	UMaterialInterface* PreviewMaterial = nullptr;
	const TArray<FStaticMeshMaterialSlot>& Slots = Mesh->GetMaterialSlots();
	for (const FStaticMeshMaterialSlot& Slot : Slots)
	{
		if (Slot.Material)
		{
			PreviewMaterial = Slot.Material;
			break;
		}
	}
	if (!PreviewMaterial)
	{
		PreviewMaterial = FResourceManager::Get().GetOrCreateMaterial("DefaultWhite", EMaterialShaderType::SurfaceLit);
	}
	if (!PreviewMaterial)
	{
		FailedPreviewCacheKeys.insert(CacheKey);
		return nullptr;
	}

	FEditorRenderPipeline* RenderPipeline = EditorEngine->GetEditorRenderPipeline();
	if (!RenderPipeline)
	{
		FailedPreviewCacheKeys.insert(CacheKey);
		return nullptr;
	}

	ID3D11ShaderResourceView* PreviewSRV = RenderPipeline->RenderMaterialPreview(
		EditorEngine->GetRenderer(),
		Mesh,
		PreviewMaterial,
		Width,
		Height,
		0.8f,
		0.25f,
		4.0f);

	FMaterialPreviewSnapshot Snapshot;
	if (!CapturePreviewSnapshot(PreviewSRV, Snapshot, Width, Height))
	{
		FailedPreviewCacheKeys.insert(CacheKey);
		return nullptr;
	}

	FMaterialPreviewSnapshot& CachedSnapshot = StaticMeshPreviewCache[CacheKey];
	CachedSnapshot = Snapshot;
	return CachedSnapshot.SRV.Get();
}

ID3D11ShaderResourceView* FEditorContentBrowserWidget::GetSkeletalMeshPreviewSRV(const FContentItem& Item, uint32 Width, uint32 Height, bool bHighPriority)
{
	(void)bHighPriority;
	if (!EditorEngine || !IsSkeletalMeshAsset(Item) || Width == 0 || Height == 0)
	{
		return nullptr;
	}

	Width = ContentBrowserThumbnailSnapshotSize;
	Height = ContentBrowserThumbnailSnapshotSize;

	const FString RelativePath = MakeRelativeProjectPath(Item.Path);
	const FString CacheKey = RelativePath + "#thumbnail";
	auto Found = SkeletalMeshPreviewCache.find(CacheKey);
	if (Found != SkeletalMeshPreviewCache.end())
	{
		return Found->second.SRV.Get();
	}

	if (FailedPreviewCacheKeys.find(CacheKey) != FailedPreviewCacheKeys.end())
	{
		return nullptr;
	}

	if (MaterialPreviewBuildsThisFrame >= 1)
	{
		return nullptr;
	}
	++MaterialPreviewBuildsThisFrame;

	USkeletalMesh* Mesh = FResourceManager::Get().LoadSkeletalMesh(RelativePath);
	if (!Mesh || !Mesh->HasValidMeshData())
	{
		FailedPreviewCacheKeys.insert(CacheKey);
		return nullptr;
	}

	FEditorRenderPipeline* RenderPipeline = EditorEngine->GetEditorRenderPipeline();
	if (!RenderPipeline)
	{
		FailedPreviewCacheKeys.insert(CacheKey);
		return nullptr;
	}

	ID3D11ShaderResourceView* PreviewSRV = RenderPipeline->RenderSkeletalMeshPreview(
		EditorEngine->GetRenderer(),
		Mesh,
		Width,
		Height,
		0.8f,
		0.25f,
		4.0f);

	FMaterialPreviewSnapshot Snapshot;
	if (!CapturePreviewSnapshot(PreviewSRV, Snapshot, Width, Height))
	{
		FailedPreviewCacheKeys.insert(CacheKey);
		return nullptr;
	}

	FMaterialPreviewSnapshot& CachedSnapshot = SkeletalMeshPreviewCache[CacheKey];
	CachedSnapshot = Snapshot;
	return CachedSnapshot.SRV.Get();
}

bool FEditorContentBrowserWidget::CapturePreviewSnapshot(ID3D11ShaderResourceView* SourceSRV, FMaterialPreviewSnapshot& OutSnapshot, uint32 Width, uint32 Height)
{
	if (!EditorEngine || !SourceSRV)
	{
		return false;
	}

	ID3D11Device* Device = EditorEngine->GetRenderer().GetFD3DDevice().GetDevice();
	ID3D11DeviceContext* Context = EditorEngine->GetRenderer().GetFD3DDevice().GetDeviceContext();
	if (!Device || !Context)
	{
		return false;
	}

	TComPtr<ID3D11Resource> SourceResource;
	SourceSRV->GetResource(SourceResource.GetAddressOf());
	if (!SourceResource)
	{
		return false;
	}

	TComPtr<ID3D11Texture2D> SourceTexture;
	if (FAILED(SourceResource.As(&SourceTexture)) || !SourceTexture)
	{
		return false;
	}

	D3D11_TEXTURE2D_DESC Desc = {};
	SourceTexture->GetDesc(&Desc);
	Desc.Width = Width;
	Desc.Height = Height;
	Desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	Desc.CPUAccessFlags = 0;
	Desc.MiscFlags = 0;
	Desc.Usage = D3D11_USAGE_DEFAULT;

	OutSnapshot.Texture.Reset();
	OutSnapshot.SRV.Reset();
	if (FAILED(Device->CreateTexture2D(&Desc, nullptr, OutSnapshot.Texture.GetAddressOf())))
	{
		return false;
	}

	Context->CopyResource(OutSnapshot.Texture.Get(), SourceTexture.Get());
	if (FAILED(Device->CreateShaderResourceView(OutSnapshot.Texture.Get(), nullptr, OutSnapshot.SRV.GetAddressOf())))
	{
		OutSnapshot.Texture.Reset();
		return false;
	}

	OutSnapshot.Width = Width;
	OutSnapshot.Height = Height;
	return true;
}

UMaterialInterface* FEditorContentBrowserWidget::ResolveMaterialAsset(const std::filesystem::path& Path)
{
	const FString RelativePath = MakeRelativeProjectPath(Path);
	UMaterialInterface* Material = FResourceManager::Get().GetMaterialInterface(RelativePath);
	if (Material)
	{
		return Material;
	}
	const FString AbsolutePath = FPaths::Normalize(FPaths::ToUtf8(Path.wstring()));
	Material = FResourceManager::Get().GetMaterialInterface(AbsolutePath);
	if (Material)
	{
		return Material;
	}

	const FString Extension = ToLower(FPaths::ToUtf8(Path.extension().wstring()));
	if (IsMaterialAssetPath(Path))
	{
		FResourceManager::Get().DeserializeMaterial(RelativePath);
	}

	Material = FResourceManager::Get().GetMaterialInterface(RelativePath);
	if (Material)
	{
		return Material;
	}
	Material = FResourceManager::Get().GetMaterialInterface(AbsolutePath);
	if (Material)
	{
		return Material;
	}
	return FResourceManager::Get().GetMaterialInterface(FPaths::ToUtf8(Path.stem().wstring()));
}

void FEditorContentBrowserWidget::NavigateTo(const std::filesystem::path& Path)
{
	NavigateTo(Path, true);
}

void FEditorContentBrowserWidget::NavigateTo(const std::filesystem::path& Path, bool bAddHistory)
{
	const std::filesystem::path Normalized = Path.lexically_normal();
	std::error_code Ec;
	if (!std::filesystem::exists(Normalized, Ec) || !std::filesystem::is_directory(Normalized, Ec))
	{
		return;
	}
	if (!IsProjectRootPath(Normalized) && !IsPathAllowed(Normalized))
	{
		return;
	}
	if (bAddHistory && CurrentPath != Normalized)
	{
		BackHistory.push_back(CurrentPath);
	}
	CurrentPath = Normalized;
	PendingRevealPath = CurrentPath;
	SelectedPath.clear();
	RefreshContent();
	SaveToSettings();
}

void FEditorContentBrowserWidget::NavigateBack()
{
	while (!BackHistory.empty())
	{
		const std::filesystem::path Previous = BackHistory.back();
		BackHistory.pop_back();
		std::error_code Ec;
		if (std::filesystem::exists(Previous, Ec) && std::filesystem::is_directory(Previous, Ec))
		{
			NavigateTo(Previous, false);
			return;
		}
	}
}

FString FEditorContentBrowserWidget::MakeDisplayPath(const std::filesystem::path& Path) const
{
	const std::filesystem::path Relative = Path.lexically_relative(RootPath);
	if (!Relative.empty() && !IsParentDirectoryReference(Relative))
	{
		return FString("Project/") + FPaths::ToUtf8(Relative.generic_wstring());
	}
	return FPaths::ToUtf8(Path.wstring());
}

FString FEditorContentBrowserWidget::GetPayloadType(const FContentItem& Item) const
{
	if (IsUAsset(Item.Extension))
	{
		if (IsMaterialAsset(Item))
		{
			return "MaterialContentItem";
		}
		if (Item.bHasAssetMetadata && Item.AssetMetadata.ClassName == "USkeletalMesh")
		{
			return "ObjectContentItem";
		}
		if (Item.bHasAssetMetadata && Item.AssetMetadata.ClassName == "UAnimSequence")
		{
			return "AnimSequenceContentItem";
		}
		if (Item.bHasAssetMetadata && Item.AssetMetadata.ClassName == UCurveFloatAsset::StaticClass()->ClassName)
		{
			return "CurveContentItem";
		}
		if (Item.bHasAssetMetadata && Item.AssetMetadata.ClassName == UAnimationStateMachine::StaticClass()->ClassName)
		{
			return "AnimationStateMachineContentItem";
		}
		return "ContentBrowserPath";
	}
	if (Item.Extension == ".obj" || Item.Extension == ".fbx")
	{
		return "ObjectContentItem";
	}
	if (IsCurveAsset(Item.Path))
	{
		return "CurveContentItem";
	}
	if (Item.Extension == ".prefab")
	{
		return "PrefabContentItem";
	}
	if (Item.Extension == ".lua")
	{
		return "LuaScriptContentItem";
	}
	if (Item.Extension == ".rml")
	{
		return "RMLContentItem";
	}
	if (Item.Extension == ".png")
	{
		return "PNGElement";
	}
	if (Item.Extension == ".jpg" || Item.Extension == ".jpeg" || Item.Extension == ".dds")
	{
		return "TextureContentItem";
	}
	return "ContentBrowserPath";
}

ImU32 FEditorContentBrowserWidget::GetItemColor(const FContentItem& Item) const
{
	if (Item.bIsDirectory)
	{
		return ImGui::GetColorU32(ImVec4(0.82f, 0.61f, 0.22f, 1.0f));
	}
	if (Item.Extension == ".scene")
	{
		return ImGui::GetColorU32(ImVec4(0.26f, 0.52f, 0.78f, 1.0f));
	}
	if (Item.Extension == ".obj")
	{
		return ImGui::GetColorU32(ImVec4(0.40f, 0.65f, 0.54f, 1.0f));
	}
	if (IsUAsset(Item.Extension))
	{
		if (!Item.bHasAssetMetadata)
		{
			return ImGui::GetColorU32(ImVec4(0.35f, 0.38f, 0.44f, 1.0f));
		}
		if (Item.AssetMetadata.ClassName == "USkeletalMesh")
		{
			return ImGui::GetColorU32(ImVec4(0.40f, 0.65f, 0.54f, 1.0f));
		}
		if (Item.AssetMetadata.ClassName == "USkeleton")
		{
			return ImGui::GetColorU32(ImVec4(0.54f, 0.68f, 0.86f, 1.0f));
		}
		if (Item.AssetMetadata.ClassName == UAnimationStateMachine::StaticClass()->ClassName)
		{
			return ImGui::GetColorU32(ImVec4(0.42f, 0.62f, 0.86f, 1.0f));
		}
		//case EAssetClass::AnimSequence:
		//	return ImGui::GetColorU32(ImVec4(0.78f, 0.55f, 0.34f, 1.0f));
		//case EAssetClass::Material:
		//	return ImGui::GetColorU32(ImVec4(0.65f, 0.44f, 0.72f, 1.0f));
		//case EAssetClass::Curve:
		//	return ImGui::GetColorU32(ImVec4(0.42f, 0.50f, 0.78f, 1.0f));
		//case EAssetClass::Unknown:
		//default:
		//	return ImGui::GetColorU32(ImVec4(0.35f, 0.38f, 0.44f, 1.0f));
		if (IsMaterialAsset(Item))
		{
			return ImGui::GetColorU32(ImVec4(0.65f, 0.44f, 0.72f, 1.0f));
		}
	}
	if (IsCurveAsset(Item.Path))
	{
		return ImGui::GetColorU32(ImVec4(0.42f, 0.50f, 0.78f, 1.0f));
	}
	if (IsSequenceAsset(Item.Extension))
	{
		return ImGui::GetColorU32(ImVec4(0.78f, 0.55f, 0.34f, 1.0f));
	}
	if (Item.Extension == ".prefab")
	{
		return ImGui::GetColorU32(ImVec4(0.58f, 0.72f, 0.92f, 1.0f));
	}
	if (Item.Extension == ".lua")
	{
		return ImGui::GetColorU32(ImVec4(0.52f, 0.72f, 0.58f, 1.0f));
	}
	if (Item.Extension == ".rml" || Item.Extension == ".rcss")
	{
		return ImGui::GetColorU32(ImVec4(0.72f, 0.60f, 0.38f, 1.0f));
	}
	if (Item.Extension == ".png")
	{
		return ImGui::GetColorU32(ImVec4(0.70f, 0.52f, 0.38f, 1.0f));
	}
	if (Item.Extension == ".hlsl" || Item.Extension == ".hlsli" || Item.Extension == ".fx")
	{
		return ImGui::GetColorU32(ImVec4(0.38f, 0.58f, 0.86f, 1.0f));
	}
	return ImGui::GetColorU32(ImVec4(0.35f, 0.38f, 0.44f, 1.0f));
}

bool FEditorContentBrowserWidget::IsPathAllowed(const std::filesystem::path& Path) const
{
	const std::filesystem::path Normalized = Path.lexically_normal();
	for (const std::filesystem::path& BrowserRoot : BrowserRootPaths)
	{
		const std::filesystem::path Relative = Normalized.lexically_relative(BrowserRoot);
		if (Normalized == BrowserRoot || (!Relative.empty() && !IsParentDirectoryReference(Relative)))
		{
			return true;
		}
	}
	return false;
}

bool FEditorContentBrowserWidget::IsProjectRootPath(const std::filesystem::path& Path) const
{
	return Path.lexically_normal() == RootPath.lexically_normal();
}

bool FEditorContentBrowserWidget::IsPreviewableImage(const FString& Extension) const
{
	return Extension == ".png" || Extension == ".jpg" || Extension == ".jpeg" || Extension == ".dds";
}

bool FEditorContentBrowserWidget::IsMaterialAsset(const FContentItem& Item) const
{
	return Item.Extension == ".uasset"
		&& Item.bHasAssetMetadata
		&& (Item.AssetMetadata.ClassName == UMaterial::StaticClass()->ClassName
			|| Item.AssetMetadata.ClassName == UMaterialInstance::StaticClass()->ClassName);
}

bool FEditorContentBrowserWidget::IsStaticMeshAsset(const FContentItem& Item) const
{
	return Item.Extension == ".uasset"
		&& Item.bHasAssetMetadata
		&& Item.AssetMetadata.ClassName == UStaticMesh::StaticClass()->ClassName;
}

bool FEditorContentBrowserWidget::IsSkeletalMeshAsset(const FContentItem& Item) const
{
	return Item.Extension == ".uasset"
		&& Item.bHasAssetMetadata
		&& Item.AssetMetadata.ClassName == USkeletalMesh::StaticClass()->ClassName;
}

bool FEditorContentBrowserWidget::IsMaterialAssetPath(const std::filesystem::path& Path) const
{
	const FString Extension = ToLower(FPaths::ToUtf8(Path.extension().wstring()));
	if (Extension != ".uasset")
	{
		return false;
	}

	FAssetMetaData MetaData;
	const FString RelativePath = MakeRelativeProjectPath(Path);
	return FAssetFile::LoadMetadataOnly(RelativePath, MetaData)
		&& (MetaData.ClassName == UMaterial::StaticClass()->ClassName
			|| MetaData.ClassName == UMaterialInstance::StaticClass()->ClassName);
}

bool FEditorContentBrowserWidget::IsAnimLuaProgramAssetPath(const std::filesystem::path& Path) const
{
	const FString Extension = ToLower(FPaths::ToUtf8(Path.extension().wstring()));
	if (Extension != ".uasset")
	{
		return false;
	}

	FAssetMetaData MetaData;
	const FString RelativePath = MakeRelativeProjectPath(Path);
	return FAssetFile::LoadMetadataOnly(RelativePath, MetaData)
		&& MetaData.ClassName == UAnimLuaProgramAsset::StaticClass()->ClassName;
}

bool FEditorContentBrowserWidget::IsCurveAsset(const std::filesystem::path& Path) const
{
	const FString Extension = ToLower(FPaths::ToUtf8(Path.extension().wstring()));
	if (Extension != ".uasset")
	{
		return false;
	}

	FAssetMetaData MetaData;
	return FAssetFile::LoadMetadataOnly(MakeRelativeProjectPath(Path), MetaData)
		&& MetaData.ClassName == UCurveFloatAsset::StaticClass()->ClassName;
}

bool FEditorContentBrowserWidget::IsUAsset(const FString& Extension) const
{
	return Extension == ".uasset";
}

bool FEditorContentBrowserWidget::IsSequenceAsset(const FString& Extension) const
{
	return Extension == ".sequence";
}

bool FEditorContentBrowserWidget::IsPrefabAsset(const FString& Extension) const
{
	return Extension == ".prefab";
}

std::filesystem::path FEditorContentBrowserWidget::ResolveLuaScriptCreateDirectory() const
{
	const std::filesystem::path AssetScriptDir = (RootPath / L"Asset" / L"Script").lexically_normal();
	const std::filesystem::path NormalizedCurrent = CurrentPath.lexically_normal();

	auto IsInsideDir = [](const std::filesystem::path& Path, const std::filesystem::path& Dir)
	{
		const std::filesystem::path Relative = Path.lexically_relative(Dir);
		return Path == Dir || (!Relative.empty() && !IsParentDirectoryReference(Relative));
	};

	if (IsInsideDir(NormalizedCurrent, AssetScriptDir))
	{
		return NormalizedCurrent;
	}
	return AssetScriptDir;
}

FString FEditorContentBrowserWidget::MakeRelativeProjectPath(const std::filesystem::path& Path) const
{
	const std::filesystem::path Relative = Path.lexically_normal().lexically_relative(RootPath);
	if (!Relative.empty() && !IsParentDirectoryReference(Relative))
	{
		return FPaths::Normalize(FPaths::ToUtf8(Relative.generic_wstring()));
	}
	return FPaths::Normalize(FPaths::ToUtf8(Path.wstring()));
}
