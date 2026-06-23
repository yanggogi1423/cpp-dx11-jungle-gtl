#include "Engine/Input/CursorControl.h"

namespace
{
FCursorControlState GCursorControlState;
bool GCursorHidden = false;

void SetCursorHidden(bool bHide)
{
	if (bHide)
	{
		while (::ShowCursor(FALSE) >= 0)
		{
		}
	}
	else
	{
		while (::ShowCursor(TRUE) < 0)
		{
		}
	}

	GCursorHidden = bHide;
}

void ApplyPlatformState(const FCursorControlState& State)
{
	const bool bOwnerForeground = State.OwnerWindow != nullptr && ::GetForegroundWindow() == State.OwnerWindow;
	const bool bOwnerCaptured = State.OwnerWindow != nullptr && ::GetCapture() == State.OwnerWindow;
	const bool bHasValidOwner = bOwnerForeground || bOwnerCaptured;
	const bool bShouldHideCursor = State.bHideInClient && bHasValidOwner;

	if (bShouldHideCursor != GCursorHidden)
	{
		SetCursorHidden(bShouldHideCursor);
	}

	if (!bShouldHideCursor)
	{
		::ClipCursor(nullptr);
		return;
	}

	::SetCursor(nullptr);
	if (State.bLockToScreenPos)
	{
		const POINT LockPos = State.LockScreenPos;
		const RECT LockRect = { LockPos.x, LockPos.y, LockPos.x + 1, LockPos.y + 1 };
		::SetCursorPos(LockPos.x, LockPos.y);
		::ClipCursor(&LockRect);
	}
}
}

void FCursorControl::SetState(const FCursorControlState& InState)
{
	GCursorControlState = InState;
	ApplyPlatformState(GCursorControlState);
}

FCursorControlState FCursorControl::GetState()
{
	return GCursorControlState;
}

void FCursorControl::Apply()
{
	ApplyPlatformState(GCursorControlState);
}

void FCursorControl::Clear()
{
	GCursorControlState = FCursorControlState{};
	ApplyPlatformState(GCursorControlState);
}
