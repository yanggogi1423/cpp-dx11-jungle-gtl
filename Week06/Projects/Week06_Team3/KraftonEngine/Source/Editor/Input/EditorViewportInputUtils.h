#pragma once

#include "Engine/Input/InputTypes.h"

namespace EditorViewportInputUtils
{
	inline constexpr LONG PaneToolbarHeight = 34;
	inline constexpr float PaneToolbarHeightF = static_cast<float>(PaneToolbarHeight);

	inline bool IsMouseBlockedByImGuiForViewport(const FViewportInputContext& Context)
	{
		return Context.bImGuiCapturedMouse && !Context.bCaptured && !Context.bHovered;
	}

	inline bool IsInViewportToolbarDeadZone(const FViewportInputContext& Context)
	{
		return Context.MouseLocalPos.y >= 0 && Context.MouseLocalPos.y < PaneToolbarHeight;
	}

	inline bool IsLeftNavigationDragActive(const FViewportInputContext& Context)
	{
		if (IsInViewportToolbarDeadZone(Context))
		{
			return false;
		}

		// ImGui가 마우스를 캡처 중이면 (relative 모드 유지 중이 아닌 한) 에디터 내비게이션은 개입하지 않는다.
		if (IsMouseBlockedByImGuiForViewport(Context))
		{
			return false;
		}

		if (!Context.Frame.IsDown(VK_LBUTTON) && !Context.WasPointerDragEnded(EPointerButton::Left))
		{
			return false;
		}

		// 상대 마우스 모드에 진입한 LMB look은 버튼이 눌려 있는 동안 지속으로 간주한다.
		if (Context.bRelativeMouseMode && Context.Frame.IsDown(VK_LBUTTON))
		{
			return true;
		}

		const bool bCtrl = Context.Frame.IsDown(VK_CONTROL);
		const bool bAlt = Context.Frame.IsDown(VK_MENU);
		if ((bCtrl && bAlt) || bAlt)
		{
			return false;
		}

		if (Context.Frame.bLeftDragging)
		{
			return true;
		}

		FPointerGesture LeftGesture{};
		if (Context.GetPointerGesture(EPointerButton::Left, LeftGesture)
			&& (LeftGesture.bStarted || LeftGesture.bActive || LeftGesture.bEnded))
		{
			return true;
		}

		constexpr int32 DragThreshold = 5;
		const LONG DragX = LeftGesture.TotalDelta.x;
		const LONG DragY = LeftGesture.TotalDelta.y;
		return (DragX * DragX + DragY * DragY) >= (DragThreshold * DragThreshold);
	}
}
