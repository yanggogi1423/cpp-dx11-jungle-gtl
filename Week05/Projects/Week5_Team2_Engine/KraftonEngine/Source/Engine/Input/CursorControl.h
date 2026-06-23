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
	static void SetState(const FCursorControlState& InState);
	static FCursorControlState GetState();
	static void Apply();
	static void Clear();
};
