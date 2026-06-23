#include "Editor/UI/EditorMainPanel.h"

#include "Editor/EditorEngine.h"
#include "Editor/PIE/PIETypes.h"
#include "Editor/Settings/EditorSettings.h"
#include "Editor/Viewport/LevelEditorViewportClient.h"
#include "Engine/Components/CameraComponent.h"
#include "GameFramework/AActor.h"
#include "Object/Object.h"
#include "Engine/Platform/Paths.h"
#include "Engine/Runtime/WindowsWindow.h"

#include "ImGui/imgui.h"
#include "ImGui/imgui_internal.h"
#include "ImGui/imgui_impl_dx11.h"
#include "ImGui/imgui_impl_win32.h"

#include "Render/Pipeline/Renderer.h"
#include "Engine/Input/InputSystem.h"
#include "WICTextureLoader.h"

#include <utility>
#include <cstring>
#include <algorithm>
#include <random>

#if STATS
#include "Render/Culling/GPUOcclusionCulling.h"
#endif

namespace
{
struct FFXAAPresetEntry
{
	const char* Label;
	float EdgeThreshold;
	float EdgeThresholdMin;
  int32 SearchSteps;
};

constexpr FFXAAPresetEntry GFXAAPresets[] = {
  { "Low",		0.333f, 0.0833f, 2 },
	{ "Medium",		0.250f, 0.0833f, 3 },
	{ "High",		0.200f, 0.0625f, 5 },
	{ "Epic",		0.125f, 0.0312f, 12 },
	{ "Cinematic",	0.063f, 0.0312f, 20 },
};

constexpr int32 GFXAAPresetCount = static_cast<int32>(sizeof(GFXAAPresets) / sizeof(GFXAAPresets[0]));
constexpr int32 GFXAACustomStage = -1;

int32 GetFXAASearchStepsForStage(const FEditorSettings& Settings)
{
 const int32 Stage = Settings.FXAAStage;
	if (Stage >= 0 && Stage < GFXAAPresetCount)
	{
		return GFXAAPresets[Stage].SearchSteps;
	}
	if (Stage == GFXAACustomStage)
	{
      int32 Steps = Settings.FXAASearchSteps;
		if (Steps < 1) Steps = 1;
		if (Steps > 20) Steps = 20;
		return Steps;
	}

	return GFXAAPresets[1].SearchSteps;
}

void ApplyFXAAPresetToSettings(FEditorSettings& Settings)
{
 if (Settings.FXAAStage == GFXAACustomStage)
	{
		return;
	}

	if (Settings.FXAAStage < 0 || Settings.FXAAStage >= GFXAAPresetCount)
	{
     Settings.FXAAStage = 1;
	}

	Settings.FXAAEdgeThreshold = GFXAAPresets[Settings.FXAAStage].EdgeThreshold;
	Settings.FXAAEdgeThresholdMin = GFXAAPresets[Settings.FXAAStage].EdgeThresholdMin;
   Settings.FXAASearchSteps = GFXAAPresets[Settings.FXAAStage].SearchSteps;
}

void PushFXAAConstantsToRenderer(UEditorEngine* EditorEngine, const FEditorSettings& Settings)
{
	if (!EditorEngine)
	{
		return;
	}

	FFXAAConstants FXAA = {};
	FXAA.EdgeThreshold = Settings.FXAAEdgeThreshold;
	FXAA.EdgeThresholdMin = Settings.FXAAEdgeThresholdMin;
 FXAA.SearchSteps = GetFXAASearchStepsForStage(Settings);
	EditorEngine->GetRenderer().SetFXAAConstants(FXAA);
}

FString GetPIEControlModeLogLabel(int32 ModeValue)
{
	if (ModeValue == static_cast<int32>(UEditorEngine::EPIEControlMode::Possessed))
	{
		return "PIE possessed";
	}
	if (ModeValue == static_cast<int32>(UEditorEngine::EPIEControlMode::Ejected))
	{
		return "PIE ejected";
	}
	return "";
}

FString MakeRootRelativePath(const FString& InPath)
{
	if (InPath.empty())
	{
		return InPath;
	}

	FString NormalizedPath = InPath;
	std::replace(NormalizedPath.begin(), NormalizedPath.end(), '\\', '/');

	FString RootPath = FPaths::ToUtf8(FPaths::RootDir());
	std::replace(RootPath.begin(), RootPath.end(), '\\', '/');
	if (!RootPath.empty() && RootPath.back() != '/')
	{
		RootPath.push_back('/');
	}

	if (!RootPath.empty()
		&& NormalizedPath.size() >= RootPath.size()
		&& _strnicmp(NormalizedPath.c_str(), RootPath.c_str(), RootPath.size()) == 0)
	{
		return NormalizedPath.substr(RootPath.size());
	}

	return NormalizedPath;
}
}

void FEditorMainPanel::Create(FWindowsWindow* InWindow, FRenderer& InRenderer, UEditorEngine* InEditorEngine)
{
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();

	ImGuiIO& IO = ImGui::GetIO();
	IO.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
	IO.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
	IO.MouseDrawCursor = false;

	ImGuiStyle& Style = ImGui::GetStyle();
	Style.WindowRounding = 6.0f;
	Style.FrameRounding = 6.0f;
	Style.GrabRounding = 6.0f;
	Style.PopupRounding = 6.0f;
	Style.TabRounding = 6.0f;
	Style.ScrollbarRounding = 6.0f;
	Style.WindowBorderSize = 1.0f;
	Style.FrameBorderSize = 0.0f;
	Style.Colors[ImGuiCol_Text] = ImVec4(0.93f, 0.94f, 0.96f, 1.0f);
	Style.Colors[ImGuiCol_TextDisabled] = ImVec4(0.52f, 0.56f, 0.62f, 1.0f);
	Style.Colors[ImGuiCol_WindowBg] = ImVec4(0.08f, 0.09f, 0.11f, 1.0f);
	Style.Colors[ImGuiCol_ChildBg] = ImVec4(0.10f, 0.11f, 0.14f, 1.0f);
	Style.Colors[ImGuiCol_PopupBg] = ImVec4(0.10f, 0.11f, 0.13f, 0.98f);
	Style.Colors[ImGuiCol_Border] = ImVec4(0.20f, 0.23f, 0.27f, 1.0f);
	Style.Colors[ImGuiCol_FrameBg] = ImVec4(0.17f, 0.19f, 0.22f, 1.0f);
	Style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.21f, 0.24f, 0.29f, 1.0f);
	Style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.16f, 0.30f, 0.53f, 1.0f);
	Style.Colors[ImGuiCol_TitleBg] = ImVec4(0.09f, 0.10f, 0.12f, 1.0f);
	Style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.10f, 0.12f, 0.15f, 1.0f);
	Style.Colors[ImGuiCol_MenuBarBg] = ImVec4(0.11f, 0.12f, 0.15f, 1.0f);
	Style.Colors[ImGuiCol_Button] = ImVec4(0.20f, 0.22f, 0.26f, 0.95f);
	Style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.25f, 0.29f, 0.35f, 1.0f);
	Style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.16f, 0.30f, 0.53f, 1.0f);
	Style.Colors[ImGuiCol_Header] = ImVec4(0.19f, 0.22f, 0.27f, 1.0f);
	Style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.23f, 0.28f, 0.35f, 1.0f);
	Style.Colors[ImGuiCol_HeaderActive] = ImVec4(0.16f, 0.30f, 0.53f, 1.0f);
	Style.Colors[ImGuiCol_Tab] = ImVec4(0.14f, 0.16f, 0.20f, 1.0f);
	Style.Colors[ImGuiCol_TabHovered] = ImVec4(0.22f, 0.26f, 0.33f, 1.0f);
	Style.Colors[ImGuiCol_TabActive] = ImVec4(0.16f, 0.30f, 0.53f, 1.0f);
	Style.Colors[ImGuiCol_CheckMark] = ImVec4(0.32f, 0.61f, 0.93f, 1.0f);
	Style.Colors[ImGuiCol_SliderGrab] = ImVec4(0.32f, 0.61f, 0.93f, 1.0f);
	Style.Colors[ImGuiCol_SliderGrabActive] = ImVec4(0.39f, 0.69f, 0.97f, 1.0f);

	Window = InWindow;
	EditorEngine = InEditorEngine;

	// 한글 지원 폰트 로드 (시스템 맑은 고딕)
	IO.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\malgun.ttf", 16.0f, nullptr, IO.Fonts->GetGlyphRangesKorean());

	ImGui_ImplWin32_Init((void*)InWindow->GetHWND());
	ImGui_ImplDX11_Init(InRenderer.GetFD3DDevice().GetDevice(), InRenderer.GetFD3DDevice().GetDeviceContext());

	const std::wstring AddActorIconPath = FPaths::Combine(FPaths::RootDir(), L"Asset/Editor/ToolIcons/Add_Actor.png");
	DirectX::CreateWICTextureFromFile(InRenderer.GetFD3DDevice().GetDevice(), AddActorIconPath.c_str(), nullptr, &AddActorIconSRV);

	ConsoleWidget.Initialize(InEditorEngine);
	ControlWidget.Initialize(InEditorEngine);
	PropertyWidget.Initialize(InEditorEngine);
	SceneWidget.Initialize(InEditorEngine);
	StatWidget.Initialize(InEditorEngine);
}

void FEditorMainPanel::Release()
{
	ConsoleWidget.Clear();
	FEditorConsoleWidget::ClearHistory();
	if (AddActorIconSRV)
	{
		AddActorIconSRV->Release();
		AddActorIconSRV = nullptr;
	}

	ImGui_ImplDX11_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();
}

void FEditorMainPanel::Render(float DeltaTime)
{
	ImGui_ImplDX11_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();

	RenderMainMenuBar();
	RenderEditorToolbar();
	RenderDockSpace();

	// 뷰포트 렌더링은 EditorEngine이 담당 (SSplitter 레이아웃 + ImGui::Image)
	if (EditorEngine)
	{
		SCOPE_STAT_CAT("EditorEngine->RenderViewportUI", "5_UI");
		EditorEngine->RenderViewportUI(DeltaTime);
	}

	const FEditorSettings& Settings = FEditorSettings::Get();

	// Console는 Footer/Drawer 경로만 사용한다.

	if (!bHideEditorWindows && Settings.UI.bControl)
	{
		SCOPE_STAT_CAT("ControlWidget.Render", "5_UI");
		ControlWidget.Render(DeltaTime);
	}

	if (!bHideEditorWindows && Settings.UI.bProperty)
	{
		SCOPE_STAT_CAT("PropertyWidget.Render", "5_UI");
		PropertyWidget.Render(DeltaTime);
	}

	if (!bHideEditorWindows && Settings.UI.bScene)
	{
		SCOPE_STAT_CAT("SceneWidget.Render", "5_UI");
		SceneWidget.Render(DeltaTime);
	}

	if (!bHideEditorWindows && Settings.UI.bStat)
	{
		SCOPE_STAT_CAT("StatWidget.Render", "5_UI");
		StatWidget.Render(DeltaTime);
	}
	RenderStatOverlay();
	RenderEditorDebugPanel();

#if STATS
	if (!bHideEditorWindows)
	{
		RenderHiZDebug(Settings);
	}
#endif

	float EffectiveDeltaTime = DeltaTime;
	if (EffectiveDeltaTime <= 0.0f)
	{
		EffectiveDeltaTime = ImGui::GetIO().DeltaTime;
		if (EffectiveDeltaTime <= 0.0f)
		{
			EffectiveDeltaTime = 1.0f / 60.0f;
		}
	}
	const float TargetAnim = bConsoleDrawerVisible ? 1.0f : 0.0f;
	const float AnimSpeed = 8.0f;
	if (ConsoleDrawerAnim < TargetAnim)
	{
		ConsoleDrawerAnim += EffectiveDeltaTime * AnimSpeed;
		if (ConsoleDrawerAnim > 1.0f)
		{
			ConsoleDrawerAnim = 1.0f;
		}
	}
	else if (ConsoleDrawerAnim > TargetAnim)
	{
		ConsoleDrawerAnim -= EffectiveDeltaTime * AnimSpeed;
		if (ConsoleDrawerAnim < 0.0f)
		{
			ConsoleDrawerAnim = 0.0f;
		}
	}

	FooterLogSystem.Tick(EffectiveDeltaTime);

	RenderShortcutOverlay();
	RenderConsoleDrawer(DeltaTime);
	RenderFooterOverlay(DeltaTime);

	ImGui::Render();
	ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
}

void FEditorMainPanel::RenderStatOverlay()
{
	if (!EditorEngine)
	{
		return;
	}

	const TArray<FOverlayStatGroup> Groups = EditorEngine->GetOverlayStatSystem().BuildGroups(*EditorEngine);
	bool bHasAnyLines = false;
	for (const FOverlayStatGroup& Group : Groups)
	{
		if (!Group.Lines.empty())
		{
			bHasAnyLines = true;
			break;
		}
	}
	if (!bHasAnyLines)
	{
		return;
	}

	constexpr float PaneToolbarHeight = 34.0f;
	constexpr float OverlayMarginX = 12.0f;
	constexpr float OverlayMarginY = 10.0f;
	ImVec2 OverlayPos(20.0f, 56.0f);
	ImGuiID OverlayViewportID = 0;

	if (FLevelEditorViewportClient* ActiveViewport = EditorEngine->GetActiveViewport())
	{
		const FRect& ActiveRect = ActiveViewport->GetViewportScreenRect();
		if (ActiveRect.Width > 1.0f && ActiveRect.Height > 1.0f)
		{
			OverlayPos = ImVec2(
				ActiveRect.X + OverlayMarginX,
				ActiveRect.Y + PaneToolbarHeight + OverlayMarginY);
		}
	}

	if (const ImGuiViewport* MainViewport = ImGui::GetMainViewport())
	{
		OverlayViewportID = MainViewport->ID;
	}

	ImGui::SetNextWindowPos(OverlayPos, ImGuiCond_Always);
	if (OverlayViewportID != 0)
	{
		ImGui::SetNextWindowViewport(OverlayViewportID);
	}
	ImGui::SetNextWindowBgAlpha(0.78f);

	const ImGuiWindowFlags Flags =
		ImGuiWindowFlags_NoDecoration
		| ImGuiWindowFlags_NoDocking
		| ImGuiWindowFlags_NoSavedSettings
		| ImGuiWindowFlags_AlwaysAutoResize
		| ImGuiWindowFlags_NoFocusOnAppearing
		| ImGuiWindowFlags_NoNav
		| ImGuiWindowFlags_NoMove
		| ImGuiWindowFlags_NoInputs;

	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 10.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
	ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.07f, 0.09f, 0.12f, 0.82f));
	ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.38f, 0.58f, 0.84f, 0.55f));
	ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.90f, 0.94f, 1.00f, 1.00f));

	if (ImGui::Begin("##ViewportStatOverlay", nullptr, Flags))
	{
		bool bFirstGroup = true;
		for (const FOverlayStatGroup& Group : Groups)
		{
			if (Group.Lines.empty())
			{
				continue;
			}

			if (!bFirstGroup)
			{
				ImGui::Spacing();
				ImGui::Separator();
				ImGui::Spacing();
			}

			for (const FString& Line : Group.Lines)
			{
				ImGui::TextUnformatted(Line.c_str());
			}

			bFirstGroup = false;
		}
	}
	ImGui::End();

	ImGui::PopStyleColor(3);
	ImGui::PopStyleVar(3);
}

void FEditorMainPanel::RenderDockSpace()
{
	const ImGuiViewport* MainViewport = ImGui::GetMainViewport();
	if (!MainViewport)
	{
		return;
	}

	constexpr float EditorToolBarHeight = 40.0f;
	constexpr float FooterHeight = 32.0f;
	const ImVec2 DockPos(MainViewport->WorkPos.x, MainViewport->WorkPos.y + EditorToolBarHeight);
	const ImVec2 DockSize(
		MainViewport->WorkSize.x,
		(MainViewport->WorkSize.y > (FooterHeight + EditorToolBarHeight)) ? (MainViewport->WorkSize.y - FooterHeight - EditorToolBarHeight) : 0.0f);

	ImGui::SetNextWindowPos(DockPos);
	ImGui::SetNextWindowSize(DockSize);
	ImGui::SetNextWindowViewport(MainViewport->ID);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);

	const ImGuiWindowFlags DockHostFlags =
		ImGuiWindowFlags_NoDocking
		| ImGuiWindowFlags_NoTitleBar
		| ImGuiWindowFlags_NoCollapse
		| ImGuiWindowFlags_NoResize
		| ImGuiWindowFlags_NoMove
		| ImGuiWindowFlags_NoBringToFrontOnFocus
		| ImGuiWindowFlags_NoNavFocus
		| ImGuiWindowFlags_NoBackground;
	ImGui::Begin("##MainDockHost", nullptr, DockHostFlags);
	ImGui::PopStyleVar(3);

	const ImGuiID DockSpaceId = ImGui::GetID("MainDockSpace");
	ImGui::DockSpace(DockSpaceId, ImVec2(0.0f, 0.0f));
	ImGui::End();
}

void FEditorMainPanel::RenderEditorToolbar()
{
	const ImGuiViewport* MainViewport = ImGui::GetMainViewport();
	if (!MainViewport)
	{
		return;
	}

	constexpr float EditorToolBarHeight = 40.0f;
	const ImVec2 BarPos = MainViewport->WorkPos;
	const ImVec2 BarSize(MainViewport->WorkSize.x, EditorToolBarHeight);

	ImGui::SetNextWindowPos(BarPos, ImGuiCond_Always);
	ImGui::SetNextWindowSize(BarSize, ImGuiCond_Always);
	ImGui::SetNextWindowViewport(MainViewport->ID);

	const ImGuiWindowFlags BarFlags =
		ImGuiWindowFlags_NoDecoration
		| ImGuiWindowFlags_NoDocking
		| ImGuiWindowFlags_NoSavedSettings
		| ImGuiWindowFlags_NoMove
		| ImGuiWindowFlags_NoResize
		| ImGuiWindowFlags_NoNav
		| ImGuiWindowFlags_NoFocusOnAppearing;

	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 8.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
	ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.13f, 0.14f, 0.17f, 0.98f));
	ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.30f, 0.33f, 0.39f, 1.0f));

	if (ImGui::Begin("##EditorToolbar", nullptr, BarFlags))
	{
		const bool bPIEEnabled = EditorEngine ? EditorEngine->IsPlayingInEditor() : false;
		const bool bPIEEjectedMode =
			bPIEEnabled
			&& EditorEngine
			&& EditorEngine->GetPIEControlMode() == UEditorEngine::EPIEControlMode::Ejected;
		const bool bAddActorEnabled = !bPIEEnabled || bPIEEjectedMode;
		const bool bStartEnabled = !bPIEEnabled;
		const bool bStopEnabled = bPIEEnabled;

		const float AddActorButtonSize = 30.0f;
		const float PieButtonSize = 24.0f;
		const float AddActorToPIEGap = 36.0f;
		const float LeftMargin = 10.0f;
		const float ControlCenterY = EditorToolBarHeight * 0.5f;
		const float AddActorStartY = ControlCenterY - AddActorButtonSize * 0.5f;
		const float PieStartY = ControlCenterY - PieButtonSize * 0.5f;

		ImGui::SetCursorPos(ImVec2(LeftMargin, AddActorStartY));
		if (!bAddActorEnabled) ImGui::BeginDisabled();
		const bool bAddActorClicked = ImGui::InvisibleButton("##PIEAddActorButton", ImVec2(AddActorButtonSize, AddActorButtonSize));
		if (!bAddActorEnabled) ImGui::EndDisabled();
		{
			const ImVec2 Min = ImGui::GetItemRectMin();
			const ImVec2 Max = ImGui::GetItemRectMax();
			const bool bHovered = bAddActorEnabled && ImGui::IsItemHovered();
			const ImU32 Bg = ImGui::GetColorU32(
				bAddActorEnabled
				? (bHovered ? ImVec4(0.29f, 0.31f, 0.39f, 1.0f) : ImVec4(0.23f, 0.25f, 0.33f, 1.0f))
				: ImVec4(0.18f, 0.19f, 0.22f, 1.0f));
			const ImU32 Border = ImGui::GetColorU32(bAddActorEnabled ? ImVec4(0.40f, 0.44f, 0.58f, 1.0f) : ImVec4(0.29f, 0.31f, 0.36f, 1.0f));
			ImDrawList* DrawList = ImGui::GetWindowDrawList();
			DrawList->AddRectFilled(Min, Max, Bg, 6.0f);
			DrawList->AddRect(Min, Max, Border, 6.0f);

			if (AddActorIconSRV)
			{
				const float Padding = 6.0f;
				DrawList->AddImage((ImTextureID)AddActorIconSRV, ImVec2(Min.x + Padding, Min.y + Padding), ImVec2(Max.x - Padding, Max.y - Padding));
			}
			else
			{
				const ImVec2 C((Min.x + Max.x) * 0.5f, (Min.y + Max.y) * 0.5f);
				DrawList->AddLine(ImVec2(C.x - 6.0f, C.y), ImVec2(C.x + 6.0f, C.y), ImGui::GetColorU32(ImVec4(0.72f, 0.85f, 0.95f, 1.0f)), 2.0f);
				DrawList->AddLine(ImVec2(C.x, C.y - 6.0f), ImVec2(C.x, C.y + 6.0f), ImGui::GetColorU32(ImVec4(0.72f, 0.85f, 0.95f, 1.0f)), 2.0f);
			}
		}
		if (bAddActorEnabled && bAddActorClicked)
		{
			ImGui::OpenPopup("##PIEBarPlaceActorPopup");
		}
		if (ImGui::BeginPopup("##PIEBarPlaceActorPopup"))
		{
			//	등록된 Placeable 목록을 그대로 메뉴에 노출한다.
			//	새 타입 추가 시 UI 코드를 수정하지 않고 RegisterPlaceableActor만 호출하면 된다.
			for (const UEditorEngine::FPlaceableActorEntry& Entry : EditorEngine->GetPlaceableActorEntries())
			{
				if (ImGui::MenuItem(Entry.DisplayName.c_str()))
				{
					EditorEngine->PlaceActorById(Entry.Id);
				}
			}
			ImGui::EndPopup();
		}

		ImGui::SameLine(0.0f, AddActorToPIEGap);
		const ImVec2 PIEGroupCursor = ImGui::GetCursorScreenPos();
		const float PIEGroupPaddingX = 10.0f;
		const float PIEGroupPaddingY = 4.0f;
		const float PIEGroupSpacing = 6.0f;
		const float PIEGroupWidth = PIEGroupPaddingX * 2.0f + PieButtonSize * 2.0f + PIEGroupSpacing;
		const float PIEGroupHeight = PieButtonSize + PIEGroupPaddingY * 2.0f;
		const ImVec2 PIEGroupMin(PIEGroupCursor.x, ImGui::GetWindowPos().y + ControlCenterY - PIEGroupHeight * 0.5f);
		const ImVec2 PIEGroupMax(PIEGroupMin.x + PIEGroupWidth, PIEGroupMin.y + PIEGroupHeight);
		ImGui::GetWindowDrawList()->AddRectFilled(PIEGroupMin, PIEGroupMax, ImGui::GetColorU32(ImVec4(0.27f, 0.29f, 0.34f, 1.0f)), 4.0f);
		ImGui::GetWindowDrawList()->AddRect(PIEGroupMin, PIEGroupMax, ImGui::GetColorU32(ImVec4(0.39f, 0.42f, 0.48f, 1.0f)), 4.0f);
		const float MidX = PIEGroupMin.x + PIEGroupPaddingX + PieButtonSize + PIEGroupSpacing * 0.5f;
		ImGui::GetWindowDrawList()->AddLine(
			ImVec2(MidX, PIEGroupMin.y + 4.0f),
			ImVec2(MidX, PIEGroupMax.y - 4.0f),
			ImGui::GetColorU32(ImVec4(0.45f, 0.48f, 0.55f, 0.9f)),
			1.0f);
		ImGui::SetCursorPos(ImVec2(
			PIEGroupMin.x - ImGui::GetWindowPos().x + PIEGroupPaddingX,
			PieStartY));

		if (!bStartEnabled) ImGui::BeginDisabled();
		const bool bStartClicked = ImGui::InvisibleButton("##PIEStartButton", ImVec2(PieButtonSize, PieButtonSize));
		if (!bStartEnabled) ImGui::EndDisabled();
		{
			const ImVec2 Min = ImGui::GetItemRectMin();
			const ImVec2 Max = ImGui::GetItemRectMax();
			const bool bHovered = bStartEnabled && ImGui::IsItemHovered();
			const ImU32 Bg = ImGui::GetColorU32(
				bStartEnabled
				? (bHovered ? ImVec4(0.20f, 0.26f, 0.20f, 1.0f) : ImVec4(0.16f, 0.20f, 0.16f, 1.0f))
				: ImVec4(0.14f, 0.14f, 0.15f, 1.0f));
			const ImU32 Border = ImGui::GetColorU32(bStartEnabled ? ImVec4(0.30f, 0.36f, 0.30f, 1.0f) : ImVec4(0.24f, 0.24f, 0.26f, 1.0f));
			const ImU32 Icon = ImGui::GetColorU32(bStartEnabled ? ImVec4(0.52f, 0.92f, 0.56f, 1.0f) : ImVec4(0.45f, 0.45f, 0.47f, 1.0f));
			ImDrawList* DrawList = ImGui::GetWindowDrawList();
			DrawList->AddRectFilled(Min, Max, Bg, 2.0f);
			DrawList->AddRect(Min, Max, Border, 2.0f);
			const ImVec2 C((Min.x + Max.x) * 0.5f, (Min.y + Max.y) * 0.5f);
			const float IconScale = PieButtonSize / 30.0f;
			DrawList->AddTriangleFilled(
				ImVec2(C.x - 5.0f * IconScale, C.y - 8.0f * IconScale),
				ImVec2(C.x - 5.0f * IconScale, C.y + 8.0f * IconScale),
				ImVec2(C.x + 9.0f * IconScale, C.y),
				Icon);
		}
		if (bStartEnabled && bStartClicked && EditorEngine)
		{
			FRequestPlaySessionParams Params;
			EditorEngine->RequestPlaySession(Params);
		}

		ImGui::SetCursorPos(ImVec2(
			PIEGroupMin.x - ImGui::GetWindowPos().x + PIEGroupPaddingX + PieButtonSize + PIEGroupSpacing,
			PieStartY));
		if (!bStopEnabled) ImGui::BeginDisabled();
		const bool bStopClicked = ImGui::InvisibleButton("##PIEStopButton", ImVec2(PieButtonSize, PieButtonSize));
		if (!bStopEnabled) ImGui::EndDisabled();
		{
			const ImVec2 Min = ImGui::GetItemRectMin();
			const ImVec2 Max = ImGui::GetItemRectMax();
			const bool bHovered = bStopEnabled && ImGui::IsItemHovered();
			const ImU32 Bg = ImGui::GetColorU32(
				bStopEnabled
				? (bHovered ? ImVec4(0.26f, 0.20f, 0.20f, 1.0f) : ImVec4(0.20f, 0.16f, 0.16f, 1.0f))
				: ImVec4(0.14f, 0.14f, 0.15f, 1.0f));
			const ImU32 Border = ImGui::GetColorU32(bStopEnabled ? ImVec4(0.38f, 0.30f, 0.30f, 1.0f) : ImVec4(0.24f, 0.24f, 0.26f, 1.0f));
			const ImU32 Icon = ImGui::GetColorU32(bStopEnabled ? ImVec4(0.92f, 0.48f, 0.48f, 1.0f) : ImVec4(0.45f, 0.45f, 0.47f, 1.0f));
			ImDrawList* DrawList = ImGui::GetWindowDrawList();
			DrawList->AddRectFilled(Min, Max, Bg, 2.0f);
			DrawList->AddRect(Min, Max, Border, 2.0f);
			const float StopSquareSize = PieButtonSize * 0.5f;
			const float IconInset = (PieButtonSize - StopSquareSize) * 0.5f;
			const ImVec2 IconMin(Min.x + IconInset, Min.y + IconInset);
			const ImVec2 IconMax(Max.x - IconInset, Max.y - IconInset);
			DrawList->AddRectFilled(IconMin, IconMax, Icon, 2.0f);
		}
		if (bStopEnabled && bStopClicked && EditorEngine)
		{
			EditorEngine->RequestEndPlayMap();
		}
	}
	ImGui::End();

	ImGui::PopStyleColor(2);
	ImGui::PopStyleVar(3);
}

void FEditorMainPanel::RenderEditorDebugPanel()
{
	if (!bShowEditorDebugPanel || !EditorEngine)
	{
		return;
	}

	ImGui::SetNextWindowSize(ImVec2(520.0f, 320.0f), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("Editor Debug", &bShowEditorDebugPanel))
	{
		ImGui::End();
		return;
	}

	FEditorSettings& Settings = FEditorSettings::Get();
	ImGui::DragFloat("Camera Speed", &Settings.CameraSpeed, 0.1f, 0.1f, 32.0f, "%.1f");
	ImGui::DragFloat("Camera Rotate Speed", &Settings.CameraRotationSpeed, 1.0f, 1.0f, 360.0f, "%.0f");
	ImGui::DragFloat("Camera Zoom Speed", &Settings.CameraZoomSpeed, 1.0f, 10.0f, 2000.0f, "%.0f");
	ImGui::Separator();
	ImGui::Checkbox("Camera Smoothing", &Settings.bEnableCameraSmoothing);
	ImGui::BeginDisabled(!Settings.bEnableCameraSmoothing);
	ImGui::DragFloat("Move Smooth Speed", &Settings.CameraMoveSmoothSpeed, 0.05f, 0.1f, 20.0f, "%.2f");
	ImGui::DragFloat("Rotate Smooth Speed", &Settings.CameraRotateSmoothSpeed, 0.05f, 0.1f, 20.0f, "%.2f");
	ImGui::EndDisabled();
	static const char* PickingModeLabels[] =
	{
		"ID Picking (GPU Proxy ID)",
		"Ray-Triangle"
	};
	int32 PickingModeIndex = static_cast<int32>(Settings.PickingMode);
	if (ImGui::Combo("Picking Mode", &PickingModeIndex, PickingModeLabels, IM_ARRAYSIZE(PickingModeLabels)))
	{
		if (PickingModeIndex < static_cast<int32>(EEditorPickingMode::Id))
		{
			PickingModeIndex = static_cast<int32>(EEditorPickingMode::Id);
		}
		if (PickingModeIndex > static_cast<int32>(EEditorPickingMode::RayTriangle))
		{
			PickingModeIndex = static_cast<int32>(EEditorPickingMode::RayTriangle);
		}
		Settings.PickingMode = static_cast<EEditorPickingMode>(PickingModeIndex);
	}
	ImGui::Checkbox("Show ID Buffer Overlay", &Settings.bShowIdBufferOverlay);

	ImGui::Separator();
	if (ImGui::CollapsingHeader("FXAA", ImGuiTreeNodeFlags_DefaultOpen))
	{
       bool bFXAAEnabled = EditorEngine->GetRenderer().IsPostEffectEnabled(EPostEffectType::FXAA);
		if (ImGui::Checkbox("FXAA Enabled", &bFXAAEnabled))
		{
			EditorEngine->GetRenderer().SetPostEffectEnabled(EPostEffectType::FXAA, bFXAAEnabled);
		}

       const bool bValidPresetStage = (Settings.FXAAStage >= 0 && Settings.FXAAStage < GFXAAPresetCount);
		const char* CurrentLabel = bValidPresetStage ? GFXAAPresets[Settings.FXAAStage].Label : "Custom";
		if (ImGui::BeginCombo("FXAA Stage", CurrentLabel))
		{
			for (int32 i = 0; i < GFXAAPresetCount; ++i)
			{
				const bool bSelected = (Settings.FXAAStage == i);
				if (ImGui::Selectable(GFXAAPresets[i].Label, bSelected))
				{
					Settings.FXAAStage = i;
					ApplyFXAAPresetToSettings(Settings);
				}
				if (bSelected)
				{
					ImGui::SetItemDefaultFocus();
				}
			}

			const bool bCustomSelected = (Settings.FXAAStage == GFXAACustomStage);
			if (ImGui::Selectable("Custom", bCustomSelected))
			{
				Settings.FXAAStage = GFXAACustomStage;
			}
			if (bCustomSelected)
			{
				ImGui::SetItemDefaultFocus();
			}
			ImGui::EndCombo();
		}

     const bool bCustomStage = (Settings.FXAAStage == GFXAACustomStage);
		ImGui::BeginDisabled(!bCustomStage);
		if (ImGui::DragFloat("Edge Threshold", &Settings.FXAAEdgeThreshold, 0.0005f, 0.001f, 0.5f, "%.4f"))
		{
			if (Settings.FXAAEdgeThreshold < 0.001f) Settings.FXAAEdgeThreshold = 0.001f;
			if (Settings.FXAAEdgeThreshold > 0.5f) Settings.FXAAEdgeThreshold = 0.5f;
		}
		if (ImGui::DragFloat("Edge Threshold Min", &Settings.FXAAEdgeThresholdMin, 0.0005f, 0.001f, 0.5f, "%.4f"))
		{
			if (Settings.FXAAEdgeThresholdMin < 0.001f) Settings.FXAAEdgeThresholdMin = 0.001f;
			if (Settings.FXAAEdgeThresholdMin > 0.5f) Settings.FXAAEdgeThresholdMin = 0.5f;
		}
       if (ImGui::DragInt("Search Steps", &Settings.FXAASearchSteps, 1.0f, 1, 20, "%d"))
		{
			if (Settings.FXAASearchSteps < 1) Settings.FXAASearchSteps = 1;
			if (Settings.FXAASearchSteps > 20) Settings.FXAASearchSteps = 20;
		}
		ImGui::EndDisabled();

		if (Settings.FXAAEdgeThresholdMin > Settings.FXAAEdgeThreshold)
		{
			Settings.FXAAEdgeThresholdMin = Settings.FXAAEdgeThreshold;
		}
	}

	PushFXAAConstantsToRenderer(EditorEngine, Settings);

	if (FLevelEditorViewportClient* ActiveVC = EditorEngine->GetActiveViewport())
	{
		if (UCameraComponent* ActiveCamera = ActiveVC->GetCamera())
		{
			ImGui::Separator();
			float CameraFOV_Deg = ActiveCamera->GetFOV() * RAD_TO_DEG;
			if (ImGui::DragFloat("Camera FOV", &CameraFOV_Deg, 0.5f, 1.0f, 170.0f, "%.1f"))
			{
				if (CameraFOV_Deg < 1.0f) CameraFOV_Deg = 1.0f;
				if (CameraFOV_Deg > 170.0f) CameraFOV_Deg = 170.0f;
				ActiveCamera->SetFOV(CameraFOV_Deg * DEG_TO_RAD);
			}

			float OrthoWidth = ActiveCamera->GetOrthoWidth();
			if (ImGui::DragFloat("Ortho Height", &OrthoWidth, 0.1f, 0.1f, 5000.0f, "%.2f"))
			{
				if (OrthoWidth < 0.1f) OrthoWidth = 0.1f;
				if (OrthoWidth > 5000.0f) OrthoWidth = 5000.0f;
				ActiveCamera->SetOrthoWidth(OrthoWidth);
			}
		}

		auto& ShowFlags = ActiveVC->GetRenderOptions().ShowFlags;
		ImGui::Separator();
		ImGui::Checkbox("Grid", &ShowFlags.bGrid);
		ImGui::Checkbox("World Axis", &ShowFlags.bWorldAxis);
		ImGui::Checkbox("Gizmo", &ShowFlags.bGizmo);
		ImGui::Checkbox("Bounding Volume", &ShowFlags.bBoundingVolume);
		ImGui::Checkbox("Debug Draw", &ShowFlags.bDebugDraw);
	}

	ImGui::Separator();
	if (ImGui::CollapsingHeader("Place Actors (Grid)", ImGuiTreeNodeFlags_DefaultOpen))
	{
		const TArray<UEditorEngine::FPlaceableActorEntry>& Entries = EditorEngine->GetPlaceableActorEntries();
		if (Entries.empty())
		{
			ImGui::TextUnformatted("No placeable actor entries registered.");
		}
		else
		{
			if (DebugPlaceActorTypeIndex < 0)
			{
				DebugPlaceActorTypeIndex = 0;
			}
			if (DebugPlaceActorTypeIndex >= static_cast<int32>(Entries.size()))
			{
				DebugPlaceActorTypeIndex = static_cast<int32>(Entries.size()) - 1;
			}

			const char* CurrentActorLabel = Entries[DebugPlaceActorTypeIndex].DisplayName.c_str();
			if (ImGui::BeginCombo("Actor Type", CurrentActorLabel))
			{
				for (int32 Index = 0; Index < static_cast<int32>(Entries.size()); ++Index)
				{
					const bool bSelected = (DebugPlaceActorTypeIndex == Index);
					if (ImGui::Selectable(Entries[Index].DisplayName.c_str(), bSelected))
					{
						DebugPlaceActorTypeIndex = Index;
					}
					if (bSelected)
					{
						ImGui::SetItemDefaultFocus();
					}
				}
				ImGui::EndCombo();
			}

			ImGui::DragInt("Rows", &DebugGridRows, 1.0f, 1, 1024, "%d");
			ImGui::DragInt("Cols", &DebugGridCols, 1.0f, 1, 1024, "%d");
			ImGui::DragInt("Layers", &DebugGridLayers, 1.0f, 1, 256, "%d");
			ImGui::DragFloat("Grid Spacing", &DebugGridSpacing, 0.1f, 0.1f, 1000.0f, "%.2f");
			ImGui::Checkbox("Center Grid Around Origin", &bDebugGridCenter);

			ImGui::Separator();
			ImGui::Checkbox("Use Camera Forward Origin", &bDebugUseCameraOrigin);
			if (bDebugUseCameraOrigin)
			{
				ImGui::DragFloat("Camera Forward Distance", &DebugCameraForwardDistance, 0.5f, 0.0f, 100000.0f, "%.1f");
			}
			else
			{
				ImGui::DragFloat3("Manual Origin", &DebugManualGridOrigin.X, 0.1f, -100000.0f, 100000.0f, "%.2f");
			}

			ImGui::Separator();
			ImGui::Checkbox("Random Yaw", &bDebugRandomYaw);
			ImGui::BeginDisabled(!bDebugRandomYaw);
			ImGui::DragFloat("Yaw Range (+/-)", &DebugRandomYawRange, 1.0f, 0.0f, 180.0f, "%.1f");
			ImGui::EndDisabled();

			ImGui::Checkbox("Apply Position Jitter", &bDebugApplyJitter);
			ImGui::BeginDisabled(!bDebugApplyJitter);
			ImGui::DragFloat("Jitter XY", &DebugJitterXY, 0.05f, 0.0f, 1000.0f, "%.2f");
			ImGui::DragFloat("Jitter Z", &DebugJitterZ, 0.05f, 0.0f, 1000.0f, "%.2f");
			ImGui::EndDisabled();

			const int64 TotalSpawnCount =
				static_cast<int64>(DebugGridRows) *
				static_cast<int64>(DebugGridCols) *
				static_cast<int64>(DebugGridLayers);
			ImGui::Text("Total Actors: %lld", static_cast<long long>(TotalSpawnCount));
			ImGui::Text("Last Batch: %u", static_cast<uint32>(DebugLastSpawnedActors.size()));

			if (ImGui::Button("Spawn Grid Actors"))
			{
				UWorld* World = EditorEngine->GetWorld();
				if (!World)
				{
					EditorEngine->EnqueueFooterEventLog("Grid spawn failed: invalid world");
				}
				else if (DebugPlaceActorTypeIndex < 0 || DebugPlaceActorTypeIndex >= static_cast<int32>(Entries.size()))
				{
					EditorEngine->EnqueueFooterEventLog("Grid spawn failed: invalid actor type");
				}
				else
				{
					const UEditorEngine::FPlaceableActorEntry& Entry = Entries[DebugPlaceActorTypeIndex];

					FVector GridOrigin = DebugManualGridOrigin;
					FVector GridRight(1.0f, 0.0f, 0.0f);
					FVector GridForward(0.0f, 1.0f, 0.0f);
					if (bDebugUseCameraOrigin)
					{
						if (FLevelEditorViewportClient* ActiveViewport = EditorEngine->GetActiveViewport())
						{
							if (UCameraComponent* ActiveCamera = ActiveViewport->GetCamera())
							{
								FVector CameraForward = ActiveCamera->GetForwardVector();
								CameraForward.Z = 0.0f;
								if (CameraForward.Length() > 0.0001f)
								{
									CameraForward.Normalize();
									GridForward = CameraForward;
									GridRight = FVector(-CameraForward.Y, CameraForward.X, 0.0f);
								}
								GridOrigin = ActiveCamera->GetWorldLocation() + ActiveCamera->GetForwardVector() * DebugCameraForwardDistance;
							}
						}
					}

					if (DebugGridRows < 1) DebugGridRows = 1;
					if (DebugGridCols < 1) DebugGridCols = 1;
					if (DebugGridLayers < 1) DebugGridLayers = 1;
					if (DebugGridSpacing < 0.1f) DebugGridSpacing = 0.1f;
					if (DebugRandomYawRange < 0.0f) DebugRandomYawRange = 0.0f;
					if (DebugRandomYawRange > 180.0f) DebugRandomYawRange = 180.0f;
					if (DebugJitterXY < 0.0f) DebugJitterXY = 0.0f;
					if (DebugJitterZ < 0.0f) DebugJitterZ = 0.0f;

					const float RowOffset = bDebugGridCenter ? (static_cast<float>(DebugGridRows - 1) * 0.5f) : 0.0f;
					const float ColOffset = bDebugGridCenter ? (static_cast<float>(DebugGridCols - 1) * 0.5f) : 0.0f;
					const float LayerOffset = bDebugGridCenter ? (static_cast<float>(DebugGridLayers - 1) * 0.5f) : 0.0f;

					std::mt19937 RNG{ std::random_device{}() };
					std::uniform_real_distribution<float> YawDist(-DebugRandomYawRange, DebugRandomYawRange);
					std::uniform_real_distribution<float> JitterXYDist(-DebugJitterXY, DebugJitterXY);
					std::uniform_real_distribution<float> JitterZDist(-DebugJitterZ, DebugJitterZ);

					TArray<AActor*> SpawnedActors;
					SpawnedActors.reserve(static_cast<size_t>(TotalSpawnCount));
					int32 SpawnedCount = 0;

					for (int32 Layer = 0; Layer < DebugGridLayers; ++Layer)
					{
						for (int32 Row = 0; Row < DebugGridRows; ++Row)
						{
							for (int32 Col = 0; Col < DebugGridCols; ++Col)
							{
								AActor* SpawnedActor = Entry.SpawnFactory ? Entry.SpawnFactory(World) : nullptr;
								if (!SpawnedActor)
								{
									continue;
								}

								if (Entry.Initializer && !Entry.Initializer(SpawnedActor))
								{
									World->DestroyActor(SpawnedActor);
									continue;
								}

								FVector SpawnLocation = GridOrigin
									+ GridRight * ((static_cast<float>(Col) - ColOffset) * DebugGridSpacing)
									+ GridForward * ((static_cast<float>(Row) - RowOffset) * DebugGridSpacing)
									+ FVector(0.0f, 0.0f, (static_cast<float>(Layer) - LayerOffset) * DebugGridSpacing);

								if (bDebugApplyJitter)
								{
									SpawnLocation
										+= GridRight * JitterXYDist(RNG)
										+ GridForward * JitterXYDist(RNG)
										+ FVector(0.0f, 0.0f, JitterZDist(RNG));
								}

								SpawnedActor->SetActorLocation(SpawnLocation);
								if (bDebugRandomYaw)
								{
									SpawnedActor->SetActorRotation(FVector(0.0f, YawDist(RNG), 0.0f));
								}
								// Render 단계에서 스폰되는 경로라 이번 프레임 WorldTick(FlushPrimitive)을 이미 지났을 수 있다.
								// 바로 보이도록 최종 변환 이후 파티션에 즉시 반영한다.
								World->InsertActorToOctree(SpawnedActor);

								SpawnedActors.push_back(SpawnedActor);
								++SpawnedCount;
							}
						}
					}
					DebugLastSpawnedActors = std::move(SpawnedActors);
					EditorEngine->EnqueueFooterEventLog(
						"Grid placed: " + std::to_string(SpawnedCount) + " actors");
				}
			}

			ImGui::SameLine();
			if (ImGui::Button("Clear Last Batch"))
			{
				// Render 단계에서 즉시 월드를 변경하지 않고 다음 Update 단계에서 처리해
				// 프레임 중간 데이터 접근과 충돌하는 위험을 줄인다.
				bPendingClearLastBatch = true;
			}
		}
	}

	ImGui::End();
}

void FEditorMainPanel::RenderMainMenuBar()
{
	if (!EditorEngine)
	{
		return;
	}

	if (!ImGui::BeginMainMenuBar())
	{
		return;
	}

	if (ImGui::BeginMenu("File"))
	{
		const bool bCanSave = EditorEngine->HasCurrentLevelFilePath();
		if (ImGui::MenuItem("New Scene", "Ctrl+N"))
		{
			EditorEngine->NewScene();
		}
		if (ImGui::MenuItem("Load Level", "Ctrl+O"))
		{
			EditorEngine->LoadSceneWithDialog();
		}
		if (ImGui::MenuItem("Save Level", "Ctrl+S"))
		{
			EditorEngine->SaveScene();
		}
		if (ImGui::MenuItem("Save Level As...", "Ctrl+Shift+S"))
		{
			EditorEngine->SaveSceneAsWithDialog();
		}

		ImGui::Separator();
		if (ImGui::MenuItem("Open Asset Folder"))
		{
			EditorEngine->OpenAssetFolder();
		}

		ImGui::Separator();
		ImGui::BeginDisabled(true);
		ImGui::MenuItem(bCanSave ? "Current: Loaded Level" : "Current: Unsaved Level", nullptr, false, false);
		ImGui::EndDisabled();
		ImGui::EndMenu();
	}

	if (ImGui::BeginMenu("View"))
	{
		FEditorSettings& S = FEditorSettings::Get();
		const bool bMenuConsoleDrawerVisible = bConsoleDrawerVisible;
		if (ImGui::MenuItem("Console Drawer", nullptr, bMenuConsoleDrawerVisible))
		{
			bConsoleDrawerVisible = !bConsoleDrawerVisible;
			if (bConsoleDrawerVisible)
			{
				ConsoleBacktickCycleState = 2;
				bBringConsoleDrawerToFrontNextFrame = true;
				bFocusConsoleInputNextFrame = true;
			}
			else
			{
				ConsoleBacktickCycleState = 0;
				bFocusConsoleButtonNextFrame = true;
			}
		}
		ImGui::MenuItem("Control", nullptr, &S.UI.bControl);
		ImGui::MenuItem("Level Manager", nullptr, &S.UI.bScene);
		ImGui::MenuItem("Property", nullptr, &S.UI.bProperty);
		ImGui::EndMenu();
	}

	if (ImGui::BeginMenu("Settings"))
	{
		ImGui::MenuItem("Editor Debug", nullptr, &bShowEditorDebugPanel);
		ImGui::EndMenu();
	}

	if (ImGui::BeginMenu("Help"))
	{
		if (ImGui::MenuItem("Shortcuts"))
		{
			bShowShortcutOverlay = true;
		}
		ImGui::EndMenu();
	}

	ImGui::EndMainMenuBar();
}

void FEditorMainPanel::RenderShortcutOverlay()
{
	if (!bShowShortcutOverlay)
	{
		return;
	}

	const ImGuiViewport* MainViewport = ImGui::GetMainViewport();
	const ImVec2 OverlayPos = MainViewport ? MainViewport->Pos : ImVec2(0.0f, 0.0f);
	const ImVec2 OverlaySize = MainViewport ? MainViewport->Size : ImGui::GetIO().DisplaySize;

	ImGui::SetNextWindowPos(OverlayPos);
	ImGui::SetNextWindowSize(OverlaySize);
	ImGui::SetNextWindowBgAlpha(0.38f);
	const ImGuiWindowFlags BlockerFlags =
		ImGuiWindowFlags_NoDecoration
		| ImGuiWindowFlags_NoDocking
		| ImGuiWindowFlags_NoSavedSettings
		| ImGuiWindowFlags_NoNav
		| ImGuiWindowFlags_NoMove;
	ImGui::Begin("##ShortcutOverlayBlocker", nullptr, BlockerFlags);
	ImGui::InvisibleButton("##ShortcutOverlayBlockerBtn", OverlaySize);
	if (ImGui::IsItemClicked(ImGuiMouseButton_Left)
		|| ImGui::IsItemClicked(ImGuiMouseButton_Right)
		|| ImGui::IsItemClicked(ImGuiMouseButton_Middle))
	{
		ImGui::SetWindowFocus("Shortcuts");
	}
	ImGui::End();

	const ImVec2 PanelSize(980.0f, 700.0f);
	ImGui::SetNextWindowPos(ImVec2(
		OverlayPos.x + (OverlaySize.x - PanelSize.x) * 0.5f,
		OverlayPos.y + (OverlaySize.y - PanelSize.y) * 0.5f), ImGuiCond_Always);
	ImGui::SetNextWindowSize(PanelSize, ImGuiCond_Always);

	ImGui::PushStyleColor(ImGuiCol_TitleBg, ImVec4(0.14f, 0.15f, 0.18f, 1.0f));
	ImGui::PushStyleColor(ImGuiCol_TitleBgActive, ImVec4(0.16f, 0.17f, 0.20f, 1.0f));
	ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.28f, 0.29f, 0.32f, 1.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
	ImGui::SetNextWindowFocus();
	const ImGuiWindowFlags PanelFlags =
		ImGuiWindowFlags_NoDocking
		| ImGuiWindowFlags_NoSavedSettings
		| ImGuiWindowFlags_NoCollapse
		| ImGuiWindowFlags_NoResize
		| ImGuiWindowFlags_NoMove;

	bool bOpen = bShowShortcutOverlay;
	if (!ImGui::Begin("Shortcuts", &bOpen, PanelFlags))
	{
		ImGui::End();
		ImGui::PopStyleVar();
		ImGui::PopStyleColor(3);
		bShowShortcutOverlay = bOpen;
		return;
	}
	bShowShortcutOverlay = bOpen;

	ImGui::TextUnformatted("현재 코드상 실제로 동작하는 에디터 단축키를 정리했습니다.");
	ImGui::Separator();

	constexpr float ShortcutColumnWidth = 200.0f;
	auto DrawShortcutSection = [ShortcutColumnWidth](const char* InSectionName, const char* InTableId, const TArray<std::pair<const char*, const char*>>& InRows)
	{
		ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.14f, 0.23f, 0.47f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.88f, 0.91f, 0.98f, 1.0f));
		ImGui::Selectable(InSectionName, false, ImGuiSelectableFlags_Disabled, ImVec2(-1.0f, 22.0f));
		ImGui::PopStyleColor(2);

		if (ImGui::BeginTable(
			InTableId,
			2,
			ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoPadOuterX))
		{
			ImGui::TableSetupColumn("Shortcut", ImGuiTableColumnFlags_WidthFixed, ShortcutColumnWidth);
			ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
			ImGui::TableSetColumnIndex(0);
			ImGui::TextUnformatted("Shortcut");
			ImGui::TableSetColumnIndex(1);
			ImGui::TextUnformatted("Action");
			for (const auto& Row : InRows)
			{
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(Row.first);
				ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(Row.second);
			}
			ImGui::EndTable();
		}
		ImGui::Spacing();
	};

	DrawShortcutSection("▼ Viewport Navigation", "ShortcutTable_Nav", {
		{ "Mouse Right Drag", "뷰포트 카메라 회전 (Perspective)" },
		{ "Mouse Middle Drag", "뷰포트 카메라 팬 이동" },
		{ "Alt + Mouse Left Drag", "선택 대상을 기준으로 오빗 회전" },
		{ "Alt + Mouse Right Drag", "카메라 돌리 인/아웃" },
		{ "Mouse Wheel", "휠 카메라 FOV 또는 직교 카메라 줌 조절" },
		{ "Mouse Wheel while rotating", "카메라 이동 속도 조절" },
		{ "W / A / S / D / Q / E", "카메라 이동 (우클릭 중일 때만 적용)" },
		{ "F", "현재 선택된 Actor 축으로 카메라 포커스" },
	});

	DrawShortcutSection("▼ Selection", "ShortcutTable_Selection", {
		{ "Mouse Left Click", "Actor 단일 선택" },
		{ "Shift + Mouse Left Click", "선택 추가" },
		{ "Ctrl + Mouse Left Click", "선택 토글" },
		{ "Ctrl + Alt + Drag", "박스 선택" },
		{ "Ctrl + Alt + Shift + Drag", "기존 선택에 박스 선택 추가" },
		{ "Ctrl + A", "전체 선택" },
	});

	DrawShortcutSection("▼ Gizmo", "ShortcutTable_Gizmo", {
		{ "Mouse Left Drag", "기즈모 축 드래그 조작" },
		{ "Q / W / E / R", "Select / Translate / Rotate / Scale 모드 전환" },
		{ "Space", "기즈모 타입 순환" },
		{ "X", "월드/로컬 기즈모 모드 전환" },
	});

	DrawShortcutSection("▼ File", "ShortcutTable_File", {
		{ "Ctrl + N", "New Level" },
		{ "Ctrl + O", "Load Level" },
		{ "Ctrl + S", "Save Level (경로 없으면 Save As)" },
		{ "Ctrl + Shift + S", "Save Level As" },
	});

	DrawShortcutSection("▼ Editor", "ShortcutTable_Editor", {
		{ "Delete", "선택된 Actor 삭제" },
		{ "Tab", "Editor Mode 순환" },
		{ "Backtick(`)", "Console Mode 순환" },
	});

	DrawShortcutSection("▼ PIE", "ShortcutTable_PIE", {
		{ "Esc", "PIE 종료" },
		{ "F8", "Possess / Eject 토글" },
		{ "Shift + F1", "마우스 캡처 해제" },
		{ "W / A / S / D", "PIE 플레이어 이동" },
		{ "Mouse Move (captured)", "PIE 플레이어 카메라 회전" },
	});

	ImGui::Separator();
	ImGui::TextUnformatted("참고: ImGui 입력창이 키보드를 잡고 있으면 일부 단축키는 동작하지 않을 수 있습니다.");

	ImGui::End();
	ImGui::PopStyleVar();
	ImGui::PopStyleColor(3);
}

void FEditorMainPanel::RenderConsoleDrawer(float DeltaTime)
{
	(void)DeltaTime;
	constexpr float FooterHeight = 32.0f;
	constexpr float DrawerMaxHeight = 320.0f;
	if (ConsoleDrawerAnim <= 0.001f)
	{
		return;
	}

	const ImGuiViewport* MainViewport = ImGui::GetMainViewport();
	const ImVec2 OverlayPos = MainViewport ? MainViewport->WorkPos : ImVec2(0.0f, 0.0f);
	const ImVec2 OverlaySize = MainViewport ? MainViewport->WorkSize : ImGui::GetIO().DisplaySize;
	const float DrawerHeight = DrawerMaxHeight * ConsoleDrawerAnim;
	if (DrawerHeight <= 1.0f)
	{
		return;
	}

	ImGui::SetNextWindowPos(
		ImVec2(OverlayPos.x, OverlayPos.y + OverlaySize.y - FooterHeight - DrawerHeight),
		ImGuiCond_Always);
	ImGui::SetNextWindowSize(ImVec2(OverlaySize.x, DrawerHeight), ImGuiCond_Always);
	if (MainViewport)
	{
		ImGui::SetNextWindowViewport(MainViewport->ID);
	}

	const ImGuiWindowFlags DrawerFlags =
		ImGuiWindowFlags_NoDecoration
		| ImGuiWindowFlags_NoDocking
		| ImGuiWindowFlags_NoSavedSettings
		| ImGuiWindowFlags_NoMove
		| ImGuiWindowFlags_NoResize
		| ImGuiWindowFlags_NoNav
		| ImGuiWindowFlags_NoFocusOnAppearing;

	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 8.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
	if (bBringConsoleDrawerToFrontNextFrame)
	{
		ImGui::SetNextWindowFocus();
		bBringConsoleDrawerToFrontNextFrame = false;
	}
	if (ImGui::Begin("##EditorConsoleDrawer", nullptr, DrawerFlags))
	{
		ConsoleWidget.RenderDrawerToolbar();
		ImGui::Separator();
		ConsoleWidget.RenderLogContents(0.0f);
	}
	ImGui::End();
	ImGui::PopStyleVar(3);
}

void FEditorMainPanel::RenderFooterOverlay(float DeltaTime)
{
	(void)DeltaTime;
	if (!EditorEngine)
	{
		return;
	}

	const ImGuiViewport* MainViewport = ImGui::GetMainViewport();
	const ImVec2 OverlayPos = MainViewport ? MainViewport->WorkPos : ImVec2(0.0f, 0.0f);
	const ImVec2 OverlaySize = MainViewport ? MainViewport->WorkSize : ImGui::GetIO().DisplaySize;
	constexpr float FooterHeight = 32.0f;

	ImGui::SetNextWindowPos(
		ImVec2(OverlayPos.x, OverlayPos.y + OverlaySize.y - FooterHeight),
		ImGuiCond_Always);
	ImGui::SetNextWindowSize(ImVec2(OverlaySize.x, FooterHeight), ImGuiCond_Always);
	if (MainViewport)
	{
		ImGui::SetNextWindowViewport(MainViewport->ID);
	}

	const ImGuiWindowFlags FooterFlags =
		ImGuiWindowFlags_NoDecoration
		| ImGuiWindowFlags_NoDocking
		| ImGuiWindowFlags_NoSavedSettings
		| ImGuiWindowFlags_NoMove
		| ImGuiWindowFlags_NoResize
		| ImGuiWindowFlags_NoNav
		| ImGuiWindowFlags_NoFocusOnAppearing;
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 4.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
	if (ImGui::Begin("##EditorStatusBar", nullptr, FooterFlags))
	{
		const TArray<FString> ActiveLogs = FooterLogSystem.GetActiveMessages();
		if (ImGui::IsKeyPressed(ImGuiKey_GraveAccent, false))
		{
			switch (ConsoleBacktickCycleState)
			{
			case 0:
				ConsoleBacktickCycleState = 1;
				bConsoleDrawerVisible = false;
				bFocusConsoleInputNextFrame = true;
				break;
			case 1:
				ConsoleBacktickCycleState = 2;
				bConsoleDrawerVisible = true;
				bBringConsoleDrawerToFrontNextFrame = true;
				bFocusConsoleInputNextFrame = true;
				break;
			default:
				ConsoleBacktickCycleState = 0;
				bConsoleDrawerVisible = false;
				bFocusConsoleInputNextFrame = false;
				bFocusConsoleButtonNextFrame = true;
				break;
			}
		}

		const bool bDrawerOpen = ConsoleDrawerAnim > 0.5f;
		if (bFocusConsoleButtonNextFrame)
		{
			ImGui::SetKeyboardFocusHere();
			bFocusConsoleButtonNextFrame = false;
		}
		if (ImGui::Button(bDrawerOpen ? "Console ▼" : "Console ▲"))
		{
			bConsoleDrawerVisible = !bConsoleDrawerVisible;
			if (bConsoleDrawerVisible)
			{
				ConsoleBacktickCycleState = 2;
				bBringConsoleDrawerToFrontNextFrame = true;
				bFocusConsoleInputNextFrame = true;
			}
			else
			{
				ConsoleBacktickCycleState = 0;
				bFocusConsoleButtonNextFrame = true;
			}
		}

		ImGui::SameLine();
		const float InputWidth = OverlaySize.x * (bDrawerOpen ? 0.35f : 0.175f);
		ConsoleWidget.RenderInputLine("##FooterConsoleInput", InputWidth, bFocusConsoleInputNextFrame);
		if (bFocusConsoleInputNextFrame)
		{
			ConsoleBacktickCycleState = bConsoleDrawerVisible ? 2 : 1;
		}
		bFocusConsoleInputNextFrame = false;

		ImGui::SameLine();
		ImGui::Text("Domain: %s", EditorEngine->IsPlayingInEditor() ? "PIE" : "Editor");

		const FString LevelLabel = EditorEngine->HasCurrentLevelFilePath()
			? FString("Level: ") + MakeRootRelativePath(EditorEngine->GetCurrentLevelFilePath())
			: FString("Level: Unsaved");
		const float LevelWidth = ImGui::CalcTextSize(LevelLabel.c_str()).x;
		const float LevelX = OverlaySize.x - ImGui::GetStyle().WindowPadding.x - LevelWidth;

		if (!ActiveLogs.empty())
		{
			const FString& LatestLog = ActiveLogs.back();
			const float LogWidth = ImGui::CalcTextSize(LatestLog.c_str()).x;
			float LogX = LevelX - 16.0f - LogWidth;
			const float MinLogX = ImGui::GetCursorPosX() + 8.0f;
			if (LogX < MinLogX)
			{
				LogX = MinLogX;
			}
			ImGui::SameLine(LogX);
			ImGui::TextUnformatted(LatestLog.c_str());
		}

		ImGui::SameLine(LevelX);
		ImGui::TextUnformatted(LevelLabel.c_str());
	}
	ImGui::End();
	ImGui::PopStyleVar(3);
}

#if STATS
void FEditorMainPanel::RenderHiZDebug(const FEditorSettings& Settings)
{
	FGPUOcclusionCulling* Occlusion = EditorEngine ? EditorEngine->GetGPUOcclusion() : nullptr;

	if (!Settings.UI.bHiZDebug || !Occlusion || !Occlusion->IsInitialized() || Occlusion->GetHiZMipCount() == 0)
	{
		if (Occlusion) Occlusion->SetDebugMip(-1);
		return;
	}

	ImGui::SetNextWindowSize(ImVec2(400, 450), ImGuiCond_FirstUseEver);
	if (ImGui::Begin("Hi-Z Debug", &FEditorSettings::Get().UI.bHiZDebug))
	{
		static int SelectedMip = 0;
		int maxMip = static_cast<int>(Occlusion->GetHiZMipCount()) - 1;
		ImGui::SliderInt("Mip Level", &SelectedMip, 0, maxMip);
		SelectedMip = (SelectedMip < 0) ? 0 : (SelectedMip > maxMip ? maxMip : SelectedMip);

		static int VisMode = 1;
		static float Exponent = 128.0f;
		ImGui::Combo("Mode", &VisMode, "Power\0Linear\0");
		if (VisMode == 0)
			ImGui::SliderFloat("Exponent", &Exponent, 1.0f, 512.0f, "%.0f");

		Occlusion->SetDebugMip(SelectedMip);
		Occlusion->SetDebugParams(Exponent, Settings.PerspCamNearClip, Settings.PerspCamFarClip, static_cast<uint32>(VisMode));

		uint32 mipW = Occlusion->GetHiZWidth() >> SelectedMip;
		uint32 mipH = Occlusion->GetHiZHeight() >> SelectedMip;
		if (mipW < 1) mipW = 1;
		if (mipH < 1) mipH = 1;
		ImGui::Text("Mip %d: %ux%u  (src: %ux%u)", SelectedMip, mipW, mipH,
			Occlusion->GetHiZWidth(), Occlusion->GetHiZHeight());
		ImGui::Text("Texture: %s", (SelectedMip & 1) ? "B (odd)" : "A (even)");

		ID3D11ShaderResourceView* srv = Occlusion->GetDebugSRV();
		if (srv)
		{
			float aspect = static_cast<float>(mipW) / static_cast<float>(mipH);
			float displayW = ImGui::GetContentRegionAvail().x;
			float displayH = displayW / aspect;
			ImGui::Image(reinterpret_cast<ImTextureID>(srv), ImVec2(displayW, displayH));
		}
	}
	ImGui::End();
}
#endif

void FEditorMainPanel::Update()
{
	ProcessPendingDebugActions();
	UpdateFooterEventLogs();

	ImGuiIO& IO = ImGui::GetIO();

	bool bWantMouse = IO.WantCaptureMouse;
	bool bWantKeyboard = IO.WantCaptureKeyboard;
	bool bWantTextInput = IO.WantTextInput;
	const bool bAnyUIItemActive = ImGui::IsAnyItemActive();
	const bool bAnyUIItemHovered = ImGui::IsAnyItemHovered();
	const bool bAnyPopupOpen = ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId);
	const bool bAnyWindowHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow);
	const bool bAnyWindowFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_AnyWindow);
	const bool bMouseOverViewportRect = (EditorEngine != nullptr) ? EditorEngine->IsMouseOverViewport() : false;
	bool bHoveredViewportContentWindow = false;
	bool bHoveredNonViewportWindow = false;
	bool bHoveredAnyWindow = false;
	if (ImGuiContext* Context = ImGui::GetCurrentContext())
	{
		bHoveredAnyWindow = (Context->HoveredWindow != nullptr);
		if (ImGuiWindow* HoveredWindow = Context->HoveredWindow)
		{
			const char* HoveredName = HoveredWindow->Name ? HoveredWindow->Name : "";
			bHoveredViewportContentWindow =
				(std::strcmp(HoveredName, "Viewport") == 0)
				|| (std::strncmp(HoveredName, "Viewport###", 11) == 0);
			bHoveredNonViewportWindow = !bHoveredViewportContentWindow;
		}
	}
	if (!bHoveredViewportContentWindow
		&& bMouseOverViewportRect
		&& !bHoveredNonViewportWindow
		&& !bAnyUIItemActive
		&& !bAnyUIItemHovered
		&& !bAnyPopupOpen)
	{
		// 도킹 상태에서 HoveredWindow 이름이 Viewport가 아니어도,
		// 실제 viewport 인터랙션 영역 위라면 viewport 입력을 허용한다.
		bHoveredViewportContentWindow = true;
	}

	// Viewport "본문"에서만 입력을 viewport로 넘긴다.
	// 그 외 ImGui(뷰포트 툴바/팝업/기타 창)는 반드시 ImGui가 소비한다.
	const bool bReleaseMouseToViewport =
		bMouseOverViewportRect
		&& !bHoveredNonViewportWindow
		&& !bAnyPopupOpen;
	const bool bNonViewportImGuiInteraction =
		bHoveredNonViewportWindow
		&& (bAnyWindowHovered || bAnyWindowFocused || bAnyUIItemActive || bAnyUIItemHovered || bAnyPopupOpen || bWantTextInput || bWantKeyboard);

	if (bShowShortcutOverlay)
	{
		bWantMouse = true;
		bWantKeyboard = true;
	}
	else if (bNonViewportImGuiInteraction)
	{
		// Viewport 외 ImGui 창(패널/팝업/타이틀바 드래그 포함)은 항상 ImGui가 입력 소유.
		bWantMouse = true;
	}
	else if (EditorEngine && bReleaseMouseToViewport)
	{
		bWantMouse = false;
		// Viewport 본문 위에서는 키보드도 viewport에 넘겨야 Ctrl/Alt marquee,
		// LMB+WASDQE 같은 에디터 입력이 동작한다.
		// 텍스트 입력이 필요한 경우는 상단의 non-viewport ImGui interaction에서 이미 차단된다.
		bWantKeyboard = false;
	}
	InputSystem::Get().SetGuiMouseCapture(bWantMouse);
	InputSystem::Get().SetGuiKeyboardCapture(bWantKeyboard);
	InputSystem::Get().SetGuiTextInputCapture(bWantTextInput);

	// IME는 ImGui가 텍스트 입력을 원할 때만 활성화.
	if (Window)
	{
		HWND hWnd = Window->GetHWND();
		if (IO.WantTextInput)
		{
			ImmAssociateContextEx(hWnd, NULL, IACE_DEFAULT);
		}
		else
		{
			ImmAssociateContext(hWnd, NULL);
		}
	}
}

void FEditorMainPanel::ProcessPendingDebugActions()
{
	if (!bPendingClearLastBatch || !EditorEngine)
	{
		return;
	}
	bPendingClearLastBatch = false;

	UWorld* World = EditorEngine->GetWorld();
	int32 DestroyedCount = 0;

	if (!World)
	{
		DebugLastSpawnedActors.clear();
		EditorEngine->EnqueueFooterEventLog("Grid cleared: 0 actors");
		return;
	}

	// 배치 삭제 직전에 선택/기즈모 참조를 정리하지 않으면
	// 다음 프레임에 dangling actor 포인터를 따라가며 크래시가 날 수 있다.
	EditorEngine->GetSelectionManager().ClearSelection();

	for (AActor* Actor : DebugLastSpawnedActors)
	{
		if (!Actor || !UObjectManager::Get().IsAlivePointer(Actor) || Actor->GetWorld() != World)
		{
			continue;
		}

		World->DestroyActor(Actor);
		++DestroyedCount;
	}

	DebugLastSpawnedActors.clear();
	EditorEngine->EnqueueFooterEventLog(
		"Grid cleared: " + std::to_string(DestroyedCount) + " actors");
}

void FEditorMainPanel::UpdateFooterEventLogs()
{
	if (!EditorEngine)
	{
		return;
	}

	FString EventLog;
	while (EditorEngine->DequeueFooterEventLog(EventLog))
	{
		FooterLogSystem.Push(EventLog);
	}

	const bool bPIEPlaying = EditorEngine->IsPlayingInEditor();
	const int32 PIEControlMode = bPIEPlaying ? static_cast<int32>(EditorEngine->GetPIEControlMode()) : -1;
	const bool bHasLevelPath = EditorEngine->HasCurrentLevelFilePath();
	const FString LevelPath = bHasLevelPath ? EditorEngine->GetCurrentLevelFilePath() : FString();

	if (!bFooterEventStateInitialized)
	{
		bPrevPIEPlaying = bPIEPlaying;
		PrevPIEControlMode = PIEControlMode;
		bPrevHadLevelPath = bHasLevelPath;
		PrevLevelPath = LevelPath;
		bFooterEventStateInitialized = true;
		return;
	}

	if (!bPrevPIEPlaying && bPIEPlaying)
	{
		FooterLogSystem.Push("PIE started");
	}
	else if (bPrevPIEPlaying && !bPIEPlaying)
	{
		FooterLogSystem.Push("PIE ended");
	}

	if (bPIEPlaying && bPrevPIEPlaying && PIEControlMode != PrevPIEControlMode)
	{
		const FString ModeLog = GetPIEControlModeLogLabel(PIEControlMode);
		if (!ModeLog.empty())
		{
			FooterLogSystem.Push(ModeLog);
		}
	}

	bPrevPIEPlaying = bPIEPlaying;
	PrevPIEControlMode = PIEControlMode;
	bPrevHadLevelPath = bHasLevelPath;
	PrevLevelPath = LevelPath;
}

void FEditorMainPanel::HideEditorWindowsForPIE()
{
	if (bHasSavedUIVisibility)
	{
		bHideEditorWindows = true;
		bShowWidgetList = false;
		return;
	}

	FEditorSettings& Settings = FEditorSettings::Get();
	SavedUIVisibility = Settings.UI;
	bSavedShowWidgetList = bShowWidgetList;
	bHasSavedUIVisibility = true;
	bHideEditorWindows = true;
	bShowWidgetList = false;

	Settings.UI.bConsole = false;
	Settings.UI.bControl = false;
	Settings.UI.bProperty = false;
	Settings.UI.bScene = false;
	Settings.UI.bStat = false;
#if STATS
	Settings.UI.bHiZDebug = false;
#endif
}

void FEditorMainPanel::RestoreEditorWindowsAfterPIE()
{
	if (!bHasSavedUIVisibility)
	{
		bHideEditorWindows = false;
		return;
	}

	FEditorSettings& Settings = FEditorSettings::Get();
	Settings.UI = SavedUIVisibility;
	bShowWidgetList = bSavedShowWidgetList;
	bHideEditorWindows = false;
	bHasSavedUIVisibility = false;
}
