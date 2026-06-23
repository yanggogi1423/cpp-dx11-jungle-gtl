#pragma once

#include "Editor/UI/EditorChromeConstants.h"

#include "ImGui/imgui.h"
#include "ImGui/imgui_impl_win32.h"

#include <algorithm>
#include <cmath>
#include <functional>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

namespace FEditorDetachedWindowChrome
{
	inline constexpr float WindowButtonWidth = 48.0f;
	inline constexpr int MaxChromeRectCount = 16;

	inline float GetTitleBarFramePaddingY(float TitleBarHeight = FEditorChromeMetrics::ApplicationTitleBarHeight)
	{
		return std::max(0.0f, (TitleBarHeight - ImGui::GetFontSize()) * 0.5f);
	}

	inline void ApplyWindowClass(ImGuiID ClassId)
	{
		ImGuiWindowClass WindowClass;
		WindowClass.ClassId = ClassId;
		WindowClass.ViewportFlagsOverrideSet =
			ImGuiViewportFlags_NoAutoMerge |
			ImGuiViewportFlags_NoDecoration;
		WindowClass.ViewportFlagsOverrideClear = ImGuiViewportFlags_NoTaskBarIcon;
		ImGui::SetNextWindowClass(&WindowClass);
	}

	inline HWND GetCurrentViewportHwnd()
	{
		ImGuiViewport* Viewport = ImGui::GetWindowViewport();
		if (!Viewport)
		{
			return nullptr;
		}
		return static_cast<HWND>(Viewport->PlatformHandleRaw ? Viewport->PlatformHandleRaw : Viewport->PlatformHandle);
	}

	inline ImGui_ImplWin32_CustomChromeRect MakeChromeRect(const ImVec2& Min, const ImVec2& Max, const ImVec2& WindowPos)
	{
		return ImGui_ImplWin32_CustomChromeRect{
			static_cast<int>(Min.x - WindowPos.x),
			static_cast<int>(Min.y - WindowPos.y),
			static_cast<int>(Max.x - WindowPos.x),
			static_cast<int>(Max.y - WindowPos.y)
		};
	}

	inline void AddChromeRect(ImGui_ImplWin32_CustomChromeRect* Rects, int& Count, const ImVec2& Min, const ImVec2& Max, const ImVec2& WindowPos)
	{
		if (Count >= MaxChromeRectCount)
		{
			return;
		}
		Rects[Count++] = MakeChromeRect(Min, Max, WindowPos);
	}

	inline bool IsViewportMaximized(HWND Hwnd)
	{
		return Hwnd && ::IsZoomed(Hwnd) != FALSE;
	}

	inline bool IsLeftMouseButtonDown()
	{
		return ImGui::IsMouseDown(ImGuiMouseButton_Left) || (::GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
	}

	inline bool DoRectsOverlap(const ImVec2& AMin, const ImVec2& AMax, const ImVec2& BMin, const ImVec2& BMax)
	{
		return AMin.x < BMax.x && AMax.x > BMin.x && AMin.y < BMax.y && AMax.y > BMin.y;
	}

	inline bool IsCurrentWindowTopBarOverMainTabStrip(float Padding = 8.0f)
	{
		const ImGuiViewport* MainViewport = ImGui::GetMainViewport();
		if (!MainViewport)
		{
			return false;
		}

		const ImVec2 WindowPos = ImGui::GetWindowPos();
		const ImVec2 WindowSize = ImGui::GetWindowSize();
		const ImVec2 TopBarMin(WindowPos.x, WindowPos.y);
		const ImVec2 TopBarMax(WindowPos.x + WindowSize.x, WindowPos.y + FEditorChromeMetrics::ApplicationTitleBarHeight);

		const ImVec2 TabStripMin(
			MainViewport->WorkPos.x - Padding,
			MainViewport->WorkPos.y + FEditorChromeMetrics::ApplicationTitleBarHeight - Padding);
		const ImVec2 TabStripMax(
			MainViewport->WorkPos.x + MainViewport->WorkSize.x + Padding,
			TabStripMin.y + FEditorChromeMetrics::TabStripHeight + Padding * 2.0f);

		return DoRectsOverlap(TopBarMin, TopBarMax, TabStripMin, TabStripMax);
	}

	inline bool WasCurrentWindowDraggedToMainTabStrip(ImVec2& LastWindowPos, bool& bDraggingWindow)
	{
		const ImVec2 WindowPos = ImGui::GetWindowPos();
		const bool bHasLastWindowPos = LastWindowPos.x != 0.0f || LastWindowPos.y != 0.0f;
		if (!bHasLastWindowPos)
		{
			LastWindowPos = WindowPos;
			return false;
		}

		const ImVec2 Delta(WindowPos.x - LastWindowPos.x, WindowPos.y - LastWindowPos.y);
		const bool bWindowMoved = std::abs(Delta.x) > 0.5f || std::abs(Delta.y) > 0.5f;
		const bool bMouseDown = IsLeftMouseButtonDown();
		const bool bMouseDragging = ImGui::IsMouseDragging(ImGuiMouseButton_Left, ImGui::GetIO().MouseDragThreshold);
		if (bMouseDown && bMouseDragging && bWindowMoved && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
		{
			bDraggingWindow = true;
		}

		bool bDroppedOnTabStrip = false;
		if (!bMouseDown)
		{
			bDroppedOnTabStrip = bDraggingWindow && IsCurrentWindowTopBarOverMainTabStrip();
			bDraggingWindow = false;
		}

		LastWindowPos = WindowPos;
		return bDroppedOnTabStrip;
	}

	inline void ToggleViewportMaximize(HWND Hwnd)
	{
		if (!Hwnd)
		{
			return;
		}
		::PostMessageW(Hwnd, WM_SYSCOMMAND, IsViewportMaximized(Hwnd) ? SC_RESTORE : SC_MAXIMIZE, 0);
	}

	inline bool DrawWindowButton(
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

	inline void RenderMenuBar(
		const char* Title,
		const char* ButtonIdPrefix,
		const std::function<void()>& DrawMenuContents,
		bool& bCloseRequested)
	{
		if (!ImGui::BeginMenuBar())
		{
			return;
		}

		constexpr float TitleBarHeight = FEditorChromeMetrics::ApplicationTitleBarHeight;
		HWND ViewportHwnd = GetCurrentViewportHwnd();
		const ImVec2 WindowPos = ImGui::GetWindowPos();
		const ImVec2 WindowSize = ImGui::GetWindowSize();
		const float ButtonStartX = std::max(0.0f, WindowSize.x - WindowButtonWidth * 3.0f);

		ImGui_ImplWin32_CustomChromeRect ChromeRects[MaxChromeRectCount] = {};
		int ChromeRectCount = 0;

		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.0f, 12.0f));
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(18.0f, GetTitleBarFramePaddingY(TitleBarHeight)));
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(12.0f, 8.0f));

		ImGui::SetCursorPos(ImVec2(0.0f, 0.0f));
		DrawMenuContents();

		const float MenuEndX = std::min(ButtonStartX, ImGui::GetCursorScreenPos().x - WindowPos.x + 8.0f);
		AddChromeRect(
			ChromeRects,
			ChromeRectCount,
			ImVec2(WindowPos.x, WindowPos.y),
			ImVec2(WindowPos.x + MenuEndX, WindowPos.y + TitleBarHeight),
			WindowPos);

		ImDrawList* DrawList = ImGui::GetWindowDrawList();
		const char* WindowTitle = Title && Title[0] != '\0' ? Title : "";
		const ImVec2 TitleSize = ImGui::CalcTextSize(WindowTitle);
		const float TitleX = std::clamp(
			MenuEndX + (ButtonStartX - MenuEndX - TitleSize.x) * 0.5f,
			MenuEndX + 8.0f,
			std::max(MenuEndX + 8.0f, ButtonStartX - TitleSize.x - 8.0f));
		DrawList->AddText(
			ImVec2(WindowPos.x + TitleX, WindowPos.y + (TitleBarHeight - TitleSize.y) * 0.5f),
			ImGui::GetColorU32(ImVec4(0.72f, 0.76f, 0.84f, 1.0f)),
			WindowTitle);

		const ImVec2 ButtonSize(WindowButtonWidth, TitleBarHeight);
		ImGui::SetCursorPos(ImVec2(ButtonStartX, 0.0f));
		ImGui::PushID(ButtonIdPrefix ? ButtonIdPrefix : "DetachedWindow");
		if (DrawWindowButton(
			"Minimize",
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
		if (DrawWindowButton(
			"Maximize",
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
		if (DrawWindowButton(
			"Close",
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
			bCloseRequested = true;
		}
		AddChromeRect(ChromeRects, ChromeRectCount, ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), WindowPos);
		ImGui::PopID();

		ImGui_ImplWin32_SetCustomChrome(ViewportHwnd, static_cast<int>(TitleBarHeight), ChromeRects, ChromeRectCount);
		ImGui::PopStyleVar(3);
		ImGui::EndMenuBar();
	}
}
