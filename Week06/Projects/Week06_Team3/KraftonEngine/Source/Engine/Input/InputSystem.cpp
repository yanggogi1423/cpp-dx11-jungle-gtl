#include "Engine/Input/InputSystem.h"

#include <cmath>

void InputSystem::Tick()
{
    if (OwnerHWnd && GetForegroundWindow() != OwnerHWnd)
    {
        HandleOutOfFocusTick();
        return;
    }

    SampleKeyStates();
    ResetDragEdgeFlags();
    PrevScrollDelta = ScrollDelta;
    ScrollDelta = 0;
    SampleMouseDelta();
    BeginDragCandidates();
    UpdateDragState(VK_LBUTTON, LeftDragState);
    UpdateDragState(VK_RBUTTON, RightDragState);
}

FInputSystemSnapshot InputSystem::TickAndMakeSnapshot()
{
    Tick();
    return MakeSnapshot();
}

FInputSystemSnapshot InputSystem::MakeSnapshot() const
{
    FInputSystemSnapshot Snapshot{};
    for (int VK = 0; VK < 256; ++VK)
    {
        Snapshot.KeyDown[VK] = CurrentStates[VK];
        Snapshot.KeyPressed[VK] = CurrentStates[VK] && !PrevStates[VK];
        Snapshot.KeyReleased[VK] = !CurrentStates[VK] && PrevStates[VK];
    }

    Snapshot.MousePos = MousePos;
    Snapshot.MouseDeltaX = FrameMouseDeltaX;
    Snapshot.MouseDeltaY = FrameMouseDeltaY;
    Snapshot.ScrollDelta = PrevScrollDelta;

    Snapshot.bLeftDragStarted = LeftDragState.bJustStarted;
    Snapshot.bLeftDragging = LeftDragState.bDragging;
    Snapshot.bLeftDragEnded = LeftDragState.bJustEnded;
    Snapshot.LeftDragVector = GetLeftDragVector();

    Snapshot.bRightDragStarted = RightDragState.bJustStarted;
    Snapshot.bRightDragging = RightDragState.bDragging;
    Snapshot.bRightDragEnded = RightDragState.bJustEnded;
    Snapshot.RightDragVector = GetRightDragVector();

    Snapshot.bUsingRawMouse = bUseRawMouse;
    return Snapshot;
}

void InputSystem::FilterDragThreshold(FDragState& State)
{
    if (State.bCandidate && !State.bDragging)
    {
        const int DX = MousePos.x - State.MouseDownPos.x;
        const int DY = MousePos.y - State.MouseDownPos.y;
        const int DistSq = DX * DX + DY * DY;

        if (DistSq >= DRAG_THRESHOLD * DRAG_THRESHOLD)
        {
            State.bJustStarted = true;
            State.bDragging = true;
            State.DragStartPos = State.MouseDownPos;
        }
    }
}

void InputSystem::ResetDragEdgeFlags()
{
    LeftDragState.bJustStarted = false;
    RightDragState.bJustStarted = false;
    LeftDragState.bJustEnded = false;
    RightDragState.bJustEnded = false;
}

void InputSystem::HandleOutOfFocusTick()
{
    switch (FocusLossPolicy)
    {
    case EInputFocusLossPolicy::ResetAllInputs:
    default:
        ResetAllInputStateOnFocusLoss();
        return;
    }
}

void InputSystem::ResetAllInputStateOnFocusLoss()
{
    for (int i = 0; i < 256; ++i)
    {
        PrevStates[i] = CurrentStates[i];
        CurrentStates[i] = false;
    }

    LeftDragState.bJustStarted = false;
    RightDragState.bJustStarted = false;
    LeftDragState.bJustEnded = LeftDragState.bDragging;
    RightDragState.bJustEnded = RightDragState.bDragging;
    LeftDragState.bDragging = false;
    RightDragState.bDragging = false;
    LeftDragState.bCandidate = false;
    RightDragState.bCandidate = false;

    PrevScrollDelta = ScrollDelta;
    ScrollDelta = 0;
    FrameMouseDeltaX = 0;
    FrameMouseDeltaY = 0;
    RawMouseDeltaAccumX = 0;
    RawMouseDeltaAccumY = 0;

    GetCursorPos(&MousePos);
    PrevMousePos = MousePos;
}

void InputSystem::SampleKeyStates()
{
    for (int i = 0; i < 256; ++i)
    {
        PrevStates[i] = CurrentStates[i];
        CurrentStates[i] = (GetAsyncKeyState(i) & 0x8000) != 0;
    }
}

void InputSystem::SampleMouseDelta()
{
    PrevMousePos = MousePos;
    GetCursorPos(&MousePos);

    FrameMouseDeltaX = MousePos.x - PrevMousePos.x;
    FrameMouseDeltaY = MousePos.y - PrevMousePos.y;
    if (bUseRawMouse)
    {
        FrameMouseDeltaX = RawMouseDeltaAccumX;
        FrameMouseDeltaY = RawMouseDeltaAccumY;
    }

    RawMouseDeltaAccumX = 0;
    RawMouseDeltaAccumY = 0;
}

void InputSystem::BeginDragCandidates()
{
    if (GetKeyDown(VK_LBUTTON))
    {
        LeftDragState.bCandidate = true;
        LeftDragState.MouseDownPos = MousePos;
    }
    if (GetKeyDown(VK_RBUTTON))
    {
        RightDragState.bCandidate = true;
        RightDragState.MouseDownPos = MousePos;
    }
}

void InputSystem::UpdateDragState(int Key, FDragState& State)
{
    if (!State.bDragging && GetKey(Key) && MouseMoved())
    {
        FilterDragThreshold(State);
        return;
    }

    if (!GetKeyUp(Key))
    {
        return;
    }

    if (State.bDragging)
    {
        State.bJustEnded = true;
    }
    State.bDragging = false;
    State.bCandidate = false;
}

void InputSystem::AddRawMouseDelta(int DeltaX, int DeltaY)
{
    RawMouseDeltaAccumX += DeltaX;
    RawMouseDeltaAccumY += DeltaY;
}

POINT InputSystem::GetLeftDragVector() const
{
    POINT V;
    V.x = MousePos.x - LeftDragState.DragStartPos.x;
    V.y = MousePos.y - LeftDragState.DragStartPos.y;
    return V;
}

POINT InputSystem::GetRightDragVector() const
{
    POINT V;
    V.x = MousePos.x - RightDragState.DragStartPos.x;
    V.y = MousePos.y - RightDragState.DragStartPos.y;
    return V;
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
