#pragma once

#include <windows.h>

struct FCursorControlState
{
	bool bHideInClient = false;
	bool bLockToScreenPos = false;
	POINT LockScreenPos = { 0, 0 };
	HWND OwnerWindow = nullptr;
};

class FCursorControl
{
public:
	static void SetState(const FCursorControlState& InState)
	{
		GState = InState;
		ApplyPlatformState(GState);
	}

	static FCursorControlState GetState()
	{
		return GState;
	}

	static void Apply()
	{
		ApplyPlatformState(GState);
	}

	static void Clear()
	{
		GState = FCursorControlState{};
		ApplyPlatformState(GState);
	}

private:
	static inline FCursorControlState GState{};
	static inline bool bCursorHidden = false;

	static void SetCursorHidden(bool bHide)
	{
		if (bHide)
		{
			while (::ShowCursor(FALSE) >= 0) {}
		}
		else
		{
			while (::ShowCursor(TRUE) < 0) {}
		}
		bCursorHidden = bHide;
	}

	static void ApplyPlatformState(const FCursorControlState& State)
	{
		const bool bOwnerForeground = State.OwnerWindow != nullptr && ::GetForegroundWindow() == State.OwnerWindow;
		const bool bOwnerCaptured = State.OwnerWindow != nullptr && ::GetCapture() == State.OwnerWindow;
		const bool bHasValidOwner = bOwnerForeground || bOwnerCaptured;
		const bool bShouldHideCursor = State.bHideInClient && bHasValidOwner;

		if (bShouldHideCursor != bCursorHidden)
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
};
