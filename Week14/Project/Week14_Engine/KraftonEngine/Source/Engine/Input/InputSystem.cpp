#include "Engine/Input/InputSystem.h"
#include <xinput.h>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace
{
    typedef DWORD(WINAPI* PFN_XInputGetState)(DWORD, XINPUT_STATE*);

    struct FXInputRuntime
    {
        HMODULE XInputDLL = nullptr;
        PFN_XInputGetState XInputGetState = nullptr;
        bool bLoadAttempted = false;

        void EnsureLoaded()
        {
            if (bLoadAttempted)
            {
                return;
            }

            bLoadAttempted = true;
            const char* XInputDllNames[] = {
                "xinput1_4.dll",
                "xinput1_3.dll",
                "xinput9_1_0.dll",
                "xinput1_2.dll",
                "xinput1_1.dll"
            };

            for (const char* DllName : XInputDllNames)
            {
                if (HMODULE Candidate = ::LoadLibraryA(DllName))
                {
                    PFN_XInputGetState CandidateGetState = reinterpret_cast<PFN_XInputGetState>(::GetProcAddress(Candidate, "XInputGetState"));
                    if (CandidateGetState != nullptr)
                    {
                        XInputDLL = Candidate;
                        XInputGetState = CandidateGetState;
                        return;
                    }

                    ::FreeLibrary(Candidate);
                }
            }
        }
    };

    FXInputRuntime& GetXInputRuntime()
    {
        static FXInputRuntime Runtime;
        Runtime.EnsureLoaded();
        return Runtime;
    }

    float NormalizeThumbstickAxis(SHORT RawValue, SHORT DeadZone)
    {
        const float AbsValue = static_cast<float>(std::abs(static_cast<int>(RawValue)));
        if (AbsValue <= static_cast<float>(DeadZone))
        {
            return 0.0f;
        }

        const float MaxMagnitude = RawValue < 0 ? 32768.0f : 32767.0f;
        const float Normalized = (AbsValue - static_cast<float>(DeadZone)) / (MaxMagnitude - static_cast<float>(DeadZone));
        const float Clamped = (std::max)(0.0f, (std::min)(Normalized, 1.0f));
        return RawValue < 0 ? -Clamped : Clamped;
    }

    float NormalizeTriggerAxis(BYTE RawValue)
    {
        if (RawValue <= XINPUT_GAMEPAD_TRIGGER_THRESHOLD)
        {
            return 0.0f;
        }

        const float Numerator = static_cast<float>(RawValue - XINPUT_GAMEPAD_TRIGGER_THRESHOLD);
        const float Denominator = 255.0f - static_cast<float>(XINPUT_GAMEPAD_TRIGGER_THRESHOLD);
        return (std::max)(0.0f, (std::min)(Numerator / Denominator, 1.0f));
    }

    int32 ToGamepadButtonIndex(EGamepadButton Button)
    {
        return static_cast<int32>(Button);
    }

    constexpr float GamepadActivityEpsilon = 0.20f;
}

void InputSystem::Tick()
{
    // 윈도우 포커스가 없으면 모든 입력 상태 해제
    bWindowFocused = !OwnerHWnd || GetForegroundWindow() == OwnerHWnd;
    if (!bWindowFocused)
    {
        ResetAllKeyStates();
        ResetTransientState();
        UpdateCurrentSnapshot();
        return;
    }

    for (int i = 0; i < 256; ++i)
    {
        PrevStates[i] = CurrentStates[i];
        CurrentStates[i] = (GetAsyncKeyState(i) & 0x8000) != 0;
    }

    std::memcpy(PrevGamepadButtons, CurrentGamepadButtons, sizeof(CurrentGamepadButtons));
    PollGamepadState();

    bLeftDragJustStarted = false;
    bRightDragJustStarted = false;
    bLeftDragJustEnded = false;
    bRightDragJustEnded = false;

    PrevScrollDelta = ScrollDelta;
    ScrollDelta = 0;

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

    if (GetKeyDown(VK_LBUTTON))
    {
        bLeftDragCandidate = true;
        LeftMouseDownPos = MousePos;
    }
    if (GetKeyDown(VK_RBUTTON))
    {
        bRightDragCandidate = true;
        RightMouseDownPos = MousePos;
    }

    // Left drag
    if (!bLeftDragging && IsDraggingLeft())
    {
        FilterDragThreshold(bLeftDragCandidate, bLeftDragging, bLeftDragJustStarted,
            LeftMouseDownPos, LeftDragStartPos);
    }
    else if (GetKeyUp(VK_LBUTTON))
    {
        if (bLeftDragging) bLeftDragJustEnded = true;
        bLeftDragging = false;
        bLeftDragCandidate = false;
    }

    // Right drag
    if (!bRightDragging && IsDraggingRight())
    {
        FilterDragThreshold(bRightDragCandidate, bRightDragging, bRightDragJustStarted,
            RightMouseDownPos, RightDragStartPos);
    }
    else if (GetKeyUp(VK_RBUTTON))
    {
        if (bRightDragging) bRightDragJustEnded = true;
        bRightDragging = false;
        bRightDragCandidate = false;
    }

    bool bKeyboardMouseActivity = false;
    for (int VK = 0; VK < 256; ++VK)
    {
        if (CurrentStates[VK] != PrevStates[VK])
        {
            bKeyboardMouseActivity = true;
            break;
        }
    }

    bool bMouseMoveActivity = FrameMouseDeltaX != 0 || FrameMouseDeltaY != 0;
    if (bIgnoreNextMouseMoveForDeviceHeuristics && bMouseMoveActivity)
    {
        bMouseMoveActivity = false;
    }
    bIgnoreNextMouseMoveForDeviceHeuristics = false;
    bKeyboardMouseActivity = bKeyboardMouseActivity || bMouseMoveActivity || PrevScrollDelta != 0;

    bool bGamepadActivity = false;
    for (int32 ButtonIndex = 0; ButtonIndex < FInputSystemSnapshot::GamepadButtonCount; ++ButtonIndex)
    {
        if (CurrentGamepadButtons[ButtonIndex] != PrevGamepadButtons[ButtonIndex])
        {
            bGamepadActivity = true;
            break;
        }
    }
    bGamepadActivity = bGamepadActivity ||
        std::abs(GamepadLeftStickX) >= GamepadActivityEpsilon ||
        std::abs(GamepadLeftStickY) >= GamepadActivityEpsilon ||
        std::abs(GamepadRightStickX) >= GamepadActivityEpsilon ||
        std::abs(GamepadRightStickY) >= GamepadActivityEpsilon ||
        GamepadLeftTrigger >= GamepadActivityEpsilon ||
        GamepadRightTrigger >= GamepadActivityEpsilon;

    if (bKeyboardMouseActivity)
    {
        LastInputDevice = ELastInputDevice::KeyboardMouse;
    }
    else if (bGamepadActivity)
    {
        LastInputDevice = ELastInputDevice::Gamepad;
    }

    UpdateCurrentSnapshot();
}

FInputSystemSnapshot InputSystem::TickAndMakeSnapshot()
{
    Tick();
    return MakeSnapshot();
}

FInputSystemSnapshot InputSystem::MakeSnapshot() const
{
    return CurrentSnapshot;
}

void InputSystem::RefreshSnapshot()
{
    UpdateCurrentSnapshot();
}

void InputSystem::SetUseRawMouse(bool bEnable)
{
    if (bUseRawMouse == bEnable)
    {
        return;
    }

    bUseRawMouse = bEnable;
    ResetMouseDelta();
    UpdateCurrentSnapshot();
}

void InputSystem::AddRawMouseDelta(int DeltaX, int DeltaY)
{
    RawMouseDeltaAccumX += DeltaX;
    RawMouseDeltaAccumY += DeltaY;
}

void InputSystem::AddTextInput(uint32_t Codepoint)
{
    if (Codepoint == 0)
    {
        return;
    }

    TextInputQueue.push_back(Codepoint);
    ScriptTextInputQueue.push_back(Codepoint);
}

TArray<uint32_t> InputSystem::ConsumeTextInput()
{
    TArray<uint32_t> Result;
    Result.swap(TextInputQueue);
    return Result;
}

TArray<uint32_t> InputSystem::ConsumeScriptTextInput()
{
    TArray<uint32_t> Result;
    Result.swap(ScriptTextInputQueue);
    return Result;
}

void InputSystem::ResetTransientState()
{
    bLeftDragJustStarted = false;
    bRightDragJustStarted = false;
    bLeftDragJustEnded = false;
    bRightDragJustEnded = false;
    ResetGamepadTransientState();
    ResetDragState();
    ResetMouseDelta();
    ResetWheelDelta();
    TextInputQueue.clear();
    ScriptTextInputQueue.clear();
    bIgnoreNextMouseMoveForDeviceHeuristics = false;
    UpdateCurrentSnapshot();
}

void InputSystem::ResetAllKeyStates()
{
    for (int VK = 0; VK < 256; ++VK)
    {
        CurrentStates[VK] = false;
        PrevStates[VK] = false;
    }
    ResetGamepadState();
    std::memset(PrevGamepadButtons, 0, sizeof(PrevGamepadButtons));
    bIgnoreNextMouseMoveForDeviceHeuristics = false;
    UpdateCurrentSnapshot();
}

void InputSystem::ResetMouseDelta()
{
    GetCursorPos(&MousePos);
    PrevMousePos = MousePos;
    FrameMouseDeltaX = 0;
    FrameMouseDeltaY = 0;
    RawMouseDeltaAccumX = 0;
    RawMouseDeltaAccumY = 0;
    bIgnoreNextMouseMoveForDeviceHeuristics = false;
    UpdateCurrentSnapshot();
}

void InputSystem::ResetWheelDelta()
{
    ScrollDelta = 0;
    PrevScrollDelta = 0;
    UpdateCurrentSnapshot();
}

void InputSystem::ResetCaptureStateForPIEEnd()
{
    SetUseRawMouse(false);
    ResetAllKeyStates();
    ResetTransientState();
    GuiState.bUsingMouse = false;
    GuiState.bUsingKeyboard = false;
    GuiState.bUsingTextInput = false;
    bIgnoreNextMouseMoveForDeviceHeuristics = false;
    UpdateCurrentSnapshot();
}

void InputSystem::PollGamepadState()
{
    ResetGamepadState();

    FXInputRuntime& Runtime = GetXInputRuntime();
    if (Runtime.XInputGetState == nullptr)
    {
        return;
    }

    XINPUT_STATE XInputState{};
    if (Runtime.XInputGetState(0, &XInputState) != ERROR_SUCCESS)
    {
        return;
    }

    bGamepadConnected = true;

    const XINPUT_GAMEPAD& Gamepad = XInputState.Gamepad;
    CurrentGamepadButtons[ToGamepadButtonIndex(EGamepadButton::FaceBottom)] = (Gamepad.wButtons & XINPUT_GAMEPAD_A) != 0;
    CurrentGamepadButtons[ToGamepadButtonIndex(EGamepadButton::FaceRight)] = (Gamepad.wButtons & XINPUT_GAMEPAD_B) != 0;
    CurrentGamepadButtons[ToGamepadButtonIndex(EGamepadButton::FaceLeft)] = (Gamepad.wButtons & XINPUT_GAMEPAD_X) != 0;
    CurrentGamepadButtons[ToGamepadButtonIndex(EGamepadButton::FaceTop)] = (Gamepad.wButtons & XINPUT_GAMEPAD_Y) != 0;
    CurrentGamepadButtons[ToGamepadButtonIndex(EGamepadButton::LeftShoulder)] = (Gamepad.wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER) != 0;
    CurrentGamepadButtons[ToGamepadButtonIndex(EGamepadButton::RightShoulder)] = (Gamepad.wButtons & XINPUT_GAMEPAD_RIGHT_SHOULDER) != 0;
    CurrentGamepadButtons[ToGamepadButtonIndex(EGamepadButton::Back)] = (Gamepad.wButtons & XINPUT_GAMEPAD_BACK) != 0;
    CurrentGamepadButtons[ToGamepadButtonIndex(EGamepadButton::Start)] = (Gamepad.wButtons & XINPUT_GAMEPAD_START) != 0;
    CurrentGamepadButtons[ToGamepadButtonIndex(EGamepadButton::LeftThumb)] = (Gamepad.wButtons & XINPUT_GAMEPAD_LEFT_THUMB) != 0;
    CurrentGamepadButtons[ToGamepadButtonIndex(EGamepadButton::RightThumb)] = (Gamepad.wButtons & XINPUT_GAMEPAD_RIGHT_THUMB) != 0;
    CurrentGamepadButtons[ToGamepadButtonIndex(EGamepadButton::DPadUp)] = (Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_UP) != 0;
    CurrentGamepadButtons[ToGamepadButtonIndex(EGamepadButton::DPadDown)] = (Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_DOWN) != 0;
    CurrentGamepadButtons[ToGamepadButtonIndex(EGamepadButton::DPadLeft)] = (Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_LEFT) != 0;
    CurrentGamepadButtons[ToGamepadButtonIndex(EGamepadButton::DPadRight)] = (Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_RIGHT) != 0;

    GamepadLeftStickX = NormalizeThumbstickAxis(Gamepad.sThumbLX, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);
    GamepadLeftStickY = NormalizeThumbstickAxis(Gamepad.sThumbLY, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);
    GamepadRightStickX = NormalizeThumbstickAxis(Gamepad.sThumbRX, XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE);
    GamepadRightStickY = NormalizeThumbstickAxis(Gamepad.sThumbRY, XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE);
    GamepadLeftTrigger = NormalizeTriggerAxis(Gamepad.bLeftTrigger);
    GamepadRightTrigger = NormalizeTriggerAxis(Gamepad.bRightTrigger);
}

void InputSystem::ResetGamepadState()
{
    bGamepadConnected = false;
    std::memset(CurrentGamepadButtons, 0, sizeof(CurrentGamepadButtons));
    GamepadLeftStickX = 0.0f;
    GamepadLeftStickY = 0.0f;
    GamepadRightStickX = 0.0f;
    GamepadRightStickY = 0.0f;
    GamepadLeftTrigger = 0.0f;
    GamepadRightTrigger = 0.0f;
}

void InputSystem::ResetGamepadTransientState()
{
    std::memcpy(PrevGamepadButtons, CurrentGamepadButtons, sizeof(CurrentGamepadButtons));
}

void InputSystem::UpdateCurrentSnapshot()
{
    FInputSystemSnapshot Snapshot{};
    for (int VK = 0; VK < 256; ++VK)
    {
        Snapshot.KeyDown[VK] = CurrentStates[VK];
        Snapshot.KeyPressed[VK] = CurrentStates[VK] && !PrevStates[VK];
        Snapshot.KeyReleased[VK] = !CurrentStates[VK] && PrevStates[VK];
    }

    Snapshot.bLeftMouseDown = Snapshot.KeyDown[VK_LBUTTON];
    Snapshot.bLeftMousePressed = Snapshot.KeyPressed[VK_LBUTTON];
    Snapshot.bLeftMouseReleased = Snapshot.KeyReleased[VK_LBUTTON];
    Snapshot.bRightMouseDown = Snapshot.KeyDown[VK_RBUTTON];
    Snapshot.bRightMousePressed = Snapshot.KeyPressed[VK_RBUTTON];
    Snapshot.bRightMouseReleased = Snapshot.KeyReleased[VK_RBUTTON];
    Snapshot.bMiddleMouseDown = Snapshot.KeyDown[VK_MBUTTON];
    Snapshot.bMiddleMousePressed = Snapshot.KeyPressed[VK_MBUTTON];
    Snapshot.bMiddleMouseReleased = Snapshot.KeyReleased[VK_MBUTTON];
    Snapshot.bXButton1Down = Snapshot.KeyDown[VK_XBUTTON1];
    Snapshot.bXButton1Pressed = Snapshot.KeyPressed[VK_XBUTTON1];
    Snapshot.bXButton1Released = Snapshot.KeyReleased[VK_XBUTTON1];
    Snapshot.bXButton2Down = Snapshot.KeyDown[VK_XBUTTON2];
    Snapshot.bXButton2Pressed = Snapshot.KeyPressed[VK_XBUTTON2];
    Snapshot.bXButton2Released = Snapshot.KeyReleased[VK_XBUTTON2];

    Snapshot.MousePos = MousePos;
    Snapshot.MouseDeltaX = FrameMouseDeltaX;
    Snapshot.MouseDeltaY = FrameMouseDeltaY;
    Snapshot.ScrollDelta = PrevScrollDelta;

    Snapshot.bLeftDragStarted = bLeftDragJustStarted;
    Snapshot.bLeftDragging = bLeftDragging;
    Snapshot.bLeftDragEnded = bLeftDragJustEnded;
    Snapshot.LeftDragVector = GetLeftDragVector();

    Snapshot.bRightDragStarted = bRightDragJustStarted;
    Snapshot.bRightDragging = bRightDragging;
    Snapshot.bRightDragEnded = bRightDragJustEnded;
    Snapshot.RightDragVector = GetRightDragVector();

    Snapshot.bUsingRawMouse = bUseRawMouse;
    Snapshot.bGuiUsingMouse = GuiState.bUsingMouse;
    Snapshot.bGuiUsingKeyboard = GuiState.bUsingKeyboard;
    Snapshot.bGuiUsingTextInput = GuiState.bUsingTextInput;
    Snapshot.bWindowFocused = bWindowFocused;
    Snapshot.bGamepadConnected = bGamepadConnected;
    for (int32 ButtonIndex = 0; ButtonIndex < FInputSystemSnapshot::GamepadButtonCount; ++ButtonIndex)
    {
        Snapshot.GamepadButtonDown[ButtonIndex] = CurrentGamepadButtons[ButtonIndex];
        Snapshot.GamepadButtonPressed[ButtonIndex] = CurrentGamepadButtons[ButtonIndex] && !PrevGamepadButtons[ButtonIndex];
        Snapshot.GamepadButtonReleased[ButtonIndex] = !CurrentGamepadButtons[ButtonIndex] && PrevGamepadButtons[ButtonIndex];
    }
    Snapshot.GamepadLeftStickX = GamepadLeftStickX;
    Snapshot.GamepadLeftStickY = GamepadLeftStickY;
    Snapshot.GamepadRightStickX = GamepadRightStickX;
    Snapshot.GamepadRightStickY = GamepadRightStickY;
    Snapshot.GamepadLeftTrigger = GamepadLeftTrigger;
    Snapshot.GamepadRightTrigger = GamepadRightTrigger;
    CurrentSnapshot = Snapshot;
}

void InputSystem::ResetDragState()
{
    bLeftDragCandidate = false;
    bRightDragCandidate = false;
    bLeftDragging = false;
    bRightDragging = false;
    bLeftDragJustStarted = false;
    bRightDragJustStarted = false;
    bLeftDragJustEnded = false;
    bRightDragJustEnded = false;
    LeftDragStartPos = MousePos;
    LeftMouseDownPos = MousePos;
    RightDragStartPos = MousePos;
    RightMouseDownPos = MousePos;
}

void InputSystem::FilterDragThreshold(
    bool& bCandidate, bool& bDragging, bool& bJustStarted,
    const POINT& MouseDownPos, POINT& DragStartPos)
{
    if (bCandidate && !bDragging)
    {
        int DX = MousePos.x - MouseDownPos.x;
        int DY = MousePos.y - MouseDownPos.y;
        int DistSq = DX * DX + DY * DY;

        if (DistSq >= DRAG_THRESHOLD * DRAG_THRESHOLD)
        {
            bJustStarted = true;
            bDragging = true;
            DragStartPos = MouseDownPos;
        }
    }
}

POINT InputSystem::GetLeftDragVector() const
{
    POINT V;
    V.x = MousePos.x - LeftDragStartPos.x;
    V.y = MousePos.y - LeftDragStartPos.y;
    return V;
}

POINT InputSystem::GetRightDragVector() const
{
    POINT V;
    V.x = MousePos.x - RightDragStartPos.x;
    V.y = MousePos.y - RightDragStartPos.y;
    return V;
}

float InputSystem::GetLeftDragDistance() const
{
    POINT V = GetLeftDragVector();
    return std::sqrt((float)(V.x * V.x + V.y * V.y));
}

float InputSystem::GetRightDragDistance() const
{
    POINT V = GetRightDragVector();
    return std::sqrt((float)(V.x * V.x + V.y * V.y));
}
