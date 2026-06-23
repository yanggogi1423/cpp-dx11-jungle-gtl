#pragma once

#include <windows.h>

#include "Core/Singleton.h"
#include "Engine/Input/InputTypes.h"

class InputSystem : public TSingleton<InputSystem>
{
	friend class TSingleton<InputSystem>;

public:
	void Tick();
	const FInputFrame& GetFrame() const { return CurrentFrame; }
	const TArray<FInputEvent>& GetEvents() const { return CurrentEvents; }

	void AddScrollDelta(int Delta) { ScrollDelta += Delta; }
	void AddRawMouseDelta(int32 DeltaX, int32 DeltaY) { PendingRawDeltaX += DeltaX; PendingRawDeltaY += DeltaY; }
	void SetOwnerWindow(HWND InHWnd) { OwnerHWnd = InHWnd; }
	void BeginRelativeMouseMode(HWND InOwnerWindow, POINT InRestoreScreenPos);
	void EndRelativeMouseMode();
	bool IsRelativeMouseMode() const { return MouseInputMode == EMouseInputMode::Relative; }
	POINT GetRelativeRestoreScreenPos() const { return RelativeRestoreScreenPos; }

private:
	bool CurrentStates[256] = { false };
	bool PrevStates[256] = { false };

	POINT MousePos = { 0, 0 };
	POINT AbsoluteMousePos = { 0, 0 };
	POINT LeftDragAccum = { 0, 0 };
	POINT RightDragAccum = { 0, 0 };
	int32 PendingRawDeltaX = 0;
	int32 PendingRawDeltaY = 0;

	bool bLeftDragCandidate = false;
	bool bRightDragCandidate = false;
	bool bLeftDragging = false;
	bool bRightDragging = false;

	bool bLeftDragJustStarted = false;
	bool bRightDragJustStarted = false;
	bool bLeftDragJustEnded = false;
	bool bRightDragJustEnded = false;

	int ScrollDelta = 0;
	int PrevScrollDelta = 0;

	HWND OwnerHWnd = nullptr;
	HWND RelativeOwnerWindow = nullptr;
	POINT RelativeRestoreScreenPos = { 0, 0 };
	EMouseInputMode MouseInputMode = EMouseInputMode::Absolute;

	uint64 FrameCounter = 0;
	FInputFrame CurrentFrame{};
	TArray<FInputEvent> CurrentEvents;

	static constexpr int DRAG_THRESHOLD = 5;

	bool GetKeyDown(int VK) const { return CurrentStates[VK] && !PrevStates[VK]; }
	bool GetKey(int VK) const { return CurrentStates[VK]; }
	bool GetKeyUp(int VK) const { return !CurrentStates[VK] && PrevStates[VK]; }
	bool HasRawMouseDelta() const { return PendingRawDeltaX != 0 || PendingRawDeltaY != 0; }
	bool IsDraggingLeft() const { return GetKey(VK_LBUTTON) && HasRawMouseDelta(); }
	bool IsDraggingRight() const { return GetKey(VK_RBUTTON) && HasRawMouseDelta(); }

	POINT GetLeftDragVector() const;
	float GetLeftDragDistance() const;
	POINT GetRightDragVector() const;
	float GetRightDragDistance() const;

	void FilterDragThreshold(
		bool& bCandidate, bool& bDragging, bool& bJustStarted,
		const POINT& AccumDelta);
};
