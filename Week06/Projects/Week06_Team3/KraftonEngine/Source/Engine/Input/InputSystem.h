#pragma once
#include <windows.h>

#include "Core/CoreTypes.h"
#include "Core/Singleton.h"

struct FGuiInputState
{
    bool bUsingMouse = false;
    bool bUsingKeyboard = false;
    bool bUsingTextInput = false;
};

struct FInputSystemSnapshot
{
    bool KeyDown[256] = {};
    bool KeyPressed[256] = {};
    bool KeyReleased[256] = {};

    POINT MousePos = { 0, 0 };
    int MouseDeltaX = 0;
    int MouseDeltaY = 0;
    int ScrollDelta = 0;

    bool bLeftDragStarted = false;
    bool bLeftDragging = false;
    bool bLeftDragEnded = false;
    POINT LeftDragVector = { 0, 0 };

    bool bRightDragStarted = false;
    bool bRightDragging = false;
    bool bRightDragEnded = false;
    POINT RightDragVector = { 0, 0 };

    bool bUsingRawMouse = false;

    bool IsDown(int VK) const { return KeyDown[VK]; }
    bool WasPressed(int VK) const { return KeyPressed[VK]; }
    bool WasReleased(int VK) const { return KeyReleased[VK]; }
};

enum class EInputFocusLossPolicy : uint8
{
    ResetAllInputs
};

class InputSystem : public TSingleton<InputSystem>
{
	friend class TSingleton<InputSystem>;

public:
    void Tick();
    FInputSystemSnapshot TickAndMakeSnapshot();
    FInputSystemSnapshot MakeSnapshot() const;
    void SetUseRawMouse(bool bEnable) { bUseRawMouse = bEnable; }
    bool IsUsingRawMouse() const { return bUseRawMouse; }
    void AddRawMouseDelta(int DeltaX, int DeltaY);

    // Keyboard
    bool GetKeyDown(int VK) const { return CurrentStates[VK] && !PrevStates[VK]; }
    bool GetKey(int VK) const { return CurrentStates[VK]; }
    bool GetKeyUp(int VK) const { return !CurrentStates[VK] && PrevStates[VK]; }
    bool IsAnyMouseButtonDown() const
    {
        return GetKey(VK_LBUTTON)
            || GetKey(VK_RBUTTON)
            || GetKey(VK_MBUTTON)
            || GetKey(VK_XBUTTON1)
            || GetKey(VK_XBUTTON2);
    }
    bool IsAnyMouseButtonDownOrDragging() const
    {
        return IsAnyMouseButtonDown()
            || GetLeftDragging()
            || GetRightDragging();
    }

    // Mouse position
    POINT GetMousePos() const { return MousePos; }
    int MouseDeltaX() const { return FrameMouseDeltaX; }
    int MouseDeltaY() const { return FrameMouseDeltaY; }
    bool MouseMoved() const { return MouseDeltaX() != 0 || MouseDeltaY() != 0; }

    // Left drag
    bool IsDraggingLeft() const { return GetKey(VK_LBUTTON) && MouseMoved(); }
    bool GetLeftDragStart() const { return LeftDragState.bJustStarted; }
    bool GetLeftDragging() const { return LeftDragState.bDragging; }
    bool GetLeftDragEnd() const { return LeftDragState.bJustEnded; }
    POINT GetLeftDragVector() const;
    float GetLeftDragDistance() const;

    // Right drag
    bool IsDraggingRight() const { return GetKey(VK_RBUTTON) && MouseMoved(); }
    bool GetRightDragStart() const { return RightDragState.bJustStarted; }
    bool GetRightDragging() const { return RightDragState.bDragging; }
    bool GetRightDragEnd() const { return RightDragState.bJustEnded; }
    POINT GetRightDragVector() const;
    float GetRightDragDistance() const;

    // Scrolling
    void AddScrollDelta(int Delta) { ScrollDelta += Delta; }
    int GetScrollDelta() const { return PrevScrollDelta; }
    bool ScrolledUp() const { return PrevScrollDelta > 0; }
    bool ScrolledDown() const { return PrevScrollDelta < 0; }
    float GetScrollNotches() const { return PrevScrollDelta / (float)WHEEL_DELTA; }

    // Window focus
    void SetOwnerWindow(HWND InHWnd) { OwnerHWnd = InHWnd; }
    void SetFocusLossPolicy(EInputFocusLossPolicy InPolicy) { FocusLossPolicy = InPolicy; }

    // GUI state
    const FGuiInputState& GetGuiInputState() const { return GuiState; }
    void SetGuiMouseCapture(bool bCapture) { GuiState.bUsingMouse = bCapture; }
    void SetGuiKeyboardCapture(bool bCapture) { GuiState.bUsingKeyboard = bCapture; }
    void SetGuiTextInputCapture(bool bCapture) { GuiState.bUsingTextInput = bCapture; }

private:
    struct FDragState
    {
        bool bCandidate = false;
        bool bDragging = false;
        bool bJustStarted = false;
        bool bJustEnded = false;
        POINT DragStartPos = { 0, 0 };
        POINT MouseDownPos = { 0, 0 };
    };

    bool CurrentStates[256] = { false };
    bool PrevStates[256] = { false };

    // Mouse members
    POINT MousePos = { 0, 0 };
    POINT PrevMousePos = { 0, 0 };
    int FrameMouseDeltaX = 0;
    int FrameMouseDeltaY = 0;
    int RawMouseDeltaAccumX = 0;
    int RawMouseDeltaAccumY = 0;
    bool bUseRawMouse = false;

    FDragState LeftDragState{};
    FDragState RightDragState{};

    // Scrolling
    int ScrollDelta = 0;
    int PrevScrollDelta = 0;

    // Window handle for focus check
    HWND OwnerHWnd = nullptr;
    EInputFocusLossPolicy FocusLossPolicy = EInputFocusLossPolicy::ResetAllInputs;

    // GUI InputState
    FGuiInputState GuiState{};

    static constexpr int DRAG_THRESHOLD = 5;

    // Internal drag threshold helper — unified Left/Right logic
    void FilterDragThreshold(FDragState& State);
    void ResetDragEdgeFlags();
    void HandleOutOfFocusTick();
    void SampleKeyStates();
    void SampleMouseDelta();
    void BeginDragCandidates();
    void UpdateDragState(int Key, FDragState& State);
    void ResetAllInputStateOnFocusLoss();
};
