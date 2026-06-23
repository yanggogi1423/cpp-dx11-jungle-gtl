#include "Engine/Input/InputSystem.h"

#include <cmath>

#include "Engine/Input/CursorControl.h"

namespace
{
void PushKeyEvent(TArray<FInputEvent>& Events, EInputEventType Type, int32 Key, const POINT& MouseScreenPos)
{
    FInputEvent Event;
    Event.Type = Type;
    Event.Key = Key;
    Event.MouseScreenPos = MouseScreenPos;
    Events.push_back(Event);
}

void PushWheelEvent(TArray<FInputEvent>& Events, float WheelNotches, const POINT& MouseScreenPos)
{
    if (WheelNotches == 0.0f)
    {
        return;
    }

    FInputEvent Event;
    Event.Type = EInputEventType::WheelScrolled;
    Event.WheelNotches = WheelNotches;
    Event.MouseScreenPos = MouseScreenPos;
    Events.push_back(Event);
}

void PushDragEvent(
    TArray<FInputEvent>& Events,
    EInputEventType Type,
    EPointerButton Button,
    const POINT& MouseScreenPos,
    const POINT& MouseDelta,
    const POINT& DragDelta)
{
    FInputEvent Event;
    Event.Type = Type;
    Event.PointerButton = Button;
    Event.MouseScreenPos = MouseScreenPos;
    Event.MouseDelta = MouseDelta;
    if (DragDelta.x != 0 || DragDelta.y != 0)
    {
        Event.MouseDelta = DragDelta;
    }
    Events.push_back(Event);
}
}

void InputSystem::BeginRelativeMouseMode(HWND InOwnerWindow, POINT InRestoreScreenPos)
{
    if (!InOwnerWindow)
    {
        return;
    }

    OwnerHWnd = InOwnerWindow;
    RelativeOwnerWindow = InOwnerWindow;
    RelativeRestoreScreenPos = InRestoreScreenPos;
    MousePos = InRestoreScreenPos;
    MouseInputMode = EMouseInputMode::Relative;
    PendingRawDeltaX = 0;
    PendingRawDeltaY = 0;

    ::SetCapture(InOwnerWindow);

    RECT LockRect =
    {
        InRestoreScreenPos.x,
        InRestoreScreenPos.y,
        InRestoreScreenPos.x + 1,
        InRestoreScreenPos.y + 1
    };
    ::SetCursorPos(InRestoreScreenPos.x, InRestoreScreenPos.y);
    ::ClipCursor(&LockRect);

    FCursorControlState CursorState;
    CursorState.bHideInClient = true;
    CursorState.bLockToScreenPos = true;
    CursorState.LockScreenPos = InRestoreScreenPos;
    CursorState.OwnerWindow = InOwnerWindow;
    FCursorControl::SetState(CursorState);

}

void InputSystem::EndRelativeMouseMode()
{
    if (MouseInputMode != EMouseInputMode::Relative)
    {
        return;
    }

    const POINT RestorePos = RelativeRestoreScreenPos;
    const HWND CaptureWindow = RelativeOwnerWindow ? RelativeOwnerWindow : OwnerHWnd;

    MouseInputMode = EMouseInputMode::Absolute;
    RelativeOwnerWindow = nullptr;
    PendingRawDeltaX = 0;
    PendingRawDeltaY = 0;
    MousePos = RestorePos;
    AbsoluteMousePos = RestorePos;

    ::ClipCursor(nullptr);
    if (CaptureWindow && ::GetCapture() == CaptureWindow)
    {
        ::ReleaseCapture();
    }

    FCursorControl::Clear();
    ::SetCursorPos(RestorePos.x, RestorePos.y);

}

void InputSystem::Tick()
{
    CurrentEvents.clear();

    const int32 RawDeltaX = PendingRawDeltaX;
    const int32 RawDeltaY = PendingRawDeltaY;
    PendingRawDeltaX = 0;
    PendingRawDeltaY = 0;

    const bool bOwnerActive = OwnerHWnd
        && (GetForegroundWindow() == OwnerHWnd || GetCapture() == OwnerHWnd || GetCapture() == RelativeOwnerWindow);

    if (OwnerHWnd && !bOwnerActive)
    {
        EndRelativeMouseMode();

        for (int i = 0; i < 256; ++i)
        {
            PrevStates[i] = CurrentStates[i];
            CurrentStates[i] = false;
        }

        bLeftDragJustStarted = false;
        bRightDragJustStarted = false;
        bLeftDragJustEnded = bLeftDragging;
        bRightDragJustEnded = bRightDragging;
        bLeftDragging = false;
        bRightDragging = false;
        bLeftDragCandidate = false;
        bRightDragCandidate = false;
        LeftDragAccum = { 0, 0 };
        RightDragAccum = { 0, 0 };

        PrevScrollDelta = ScrollDelta;
        ScrollDelta = 0;

        GetCursorPos(&AbsoluteMousePos);
        MousePos = AbsoluteMousePos;
    }
    else
    {
        for (int i = 0; i < 256; ++i)
        {
            PrevStates[i] = CurrentStates[i];
            CurrentStates[i] = (GetAsyncKeyState(i) & 0x8000) != 0;
        }

        bLeftDragJustStarted = false;
        bRightDragJustStarted = false;
        bLeftDragJustEnded = false;
        bRightDragJustEnded = false;

        PrevScrollDelta = ScrollDelta;
        ScrollDelta = 0;

        if (MouseInputMode == EMouseInputMode::Absolute)
        {
            GetCursorPos(&AbsoluteMousePos);
            MousePos = AbsoluteMousePos;
        }
        else
        {
            MousePos = RelativeRestoreScreenPos;
        }

        if (GetKeyDown(VK_LBUTTON))
        {
            bLeftDragCandidate = true;
            LeftDragAccum = { 0, 0 };
        }

        if (GetKeyDown(VK_RBUTTON))
        {
            bRightDragCandidate = true;
            RightDragAccum = { 0, 0 };
        }

        if (GetKey(VK_LBUTTON))
        {
            LeftDragAccum.x += RawDeltaX;
            LeftDragAccum.y += RawDeltaY;
        }

        if (GetKey(VK_RBUTTON))
        {
            RightDragAccum.x += RawDeltaX;
            RightDragAccum.y += RawDeltaY;
        }

        if (!bLeftDragging && IsDraggingLeft())
        {
            FilterDragThreshold(bLeftDragCandidate, bLeftDragging, bLeftDragJustStarted, LeftDragAccum);
        }
        else if (GetKeyUp(VK_LBUTTON))
        {
            if (bLeftDragging)
            {
                bLeftDragJustEnded = true;
            }
            bLeftDragging = false;
            bLeftDragCandidate = false;
            LeftDragAccum = { 0, 0 };
        }

        if (!bRightDragging && IsDraggingRight())
        {
            FilterDragThreshold(bRightDragCandidate, bRightDragging, bRightDragJustStarted, RightDragAccum);
        }
        else if (GetKeyUp(VK_RBUTTON))
        {
            if (bRightDragging)
            {
                bRightDragJustEnded = true;
            }
            bRightDragging = false;
            bRightDragCandidate = false;
            RightDragAccum = { 0, 0 };
        }
    }

    ++FrameCounter;
    CurrentFrame.FrameNumber = FrameCounter;
    CurrentFrame.SourceWindow = OwnerHWnd;
    CurrentFrame.MouseInputMode = MouseInputMode;
    CurrentFrame.MouseScreenPos = MousePos;
    CurrentFrame.MouseDelta.x = RawDeltaX;
    CurrentFrame.MouseDelta.y = RawDeltaY;
    CurrentFrame.WheelNotches = PrevScrollDelta / static_cast<float>(WHEEL_DELTA);

    for (int i = 0; i < 256; ++i)
    {
        CurrentFrame.KeyDown[i] = CurrentStates[i];
    }

    CurrentFrame.bLeftDragging = bLeftDragging;
    CurrentFrame.bRightDragging = bRightDragging;
    CurrentFrame.LeftDragVector = GetLeftDragVector();
    CurrentFrame.RightDragVector = GetRightDragVector();

    for (int i = 0; i < 256; ++i)
    {
        const bool bPressed = CurrentStates[i] && !PrevStates[i];
        const bool bReleased = !CurrentStates[i] && PrevStates[i];
        if (bPressed)
        {
            PushKeyEvent(CurrentEvents, EInputEventType::KeyPressed, i, CurrentFrame.MouseScreenPos);
        }
        if (bReleased)
        {
            PushKeyEvent(CurrentEvents, EInputEventType::KeyReleased, i, CurrentFrame.MouseScreenPos);
        }
    }

    PushWheelEvent(CurrentEvents, CurrentFrame.WheelNotches, CurrentFrame.MouseScreenPos);

    if (bLeftDragJustStarted)
    {
        PushDragEvent(
            CurrentEvents,
            EInputEventType::PointerDragStarted,
            EPointerButton::Left,
            CurrentFrame.MouseScreenPos,
            CurrentFrame.MouseDelta,
            CurrentFrame.LeftDragVector);
    }
    if (bLeftDragJustEnded)
    {
        PushDragEvent(
            CurrentEvents,
            EInputEventType::PointerDragEnded,
            EPointerButton::Left,
            CurrentFrame.MouseScreenPos,
            CurrentFrame.MouseDelta,
            CurrentFrame.LeftDragVector);
    }
    if (bRightDragJustStarted)
    {
        PushDragEvent(
            CurrentEvents,
            EInputEventType::PointerDragStarted,
            EPointerButton::Right,
            CurrentFrame.MouseScreenPos,
            CurrentFrame.MouseDelta,
            CurrentFrame.RightDragVector);
    }
    if (bRightDragJustEnded)
    {
        PushDragEvent(
            CurrentEvents,
            EInputEventType::PointerDragEnded,
            EPointerButton::Right,
            CurrentFrame.MouseScreenPos,
            CurrentFrame.MouseDelta,
            CurrentFrame.RightDragVector);
    }
}

void InputSystem::FilterDragThreshold(
    bool& bCandidate, bool& bDragging, bool& bJustStarted,
    const POINT& AccumDelta)
{
    if (bCandidate && !bDragging)
    {
        const int DX = AccumDelta.x;
        const int DY = AccumDelta.y;
        const int DistSq = DX * DX + DY * DY;

        if (DistSq >= DRAG_THRESHOLD * DRAG_THRESHOLD)
        {
            bJustStarted = true;
            bDragging = true;
        }
    }
}

POINT InputSystem::GetLeftDragVector() const
{
    return LeftDragAccum;
}

POINT InputSystem::GetRightDragVector() const
{
    return RightDragAccum;
}

float InputSystem::GetLeftDragDistance() const
{
    const POINT V = GetLeftDragVector();
    return std::sqrt(static_cast<float>(V.x * V.x + V.y * V.y));
}

float InputSystem::GetRightDragDistance() const
{
    const POINT V = GetRightDragVector();
    return std::sqrt(static_cast<float>(V.x * V.x + V.y * V.y));
}
