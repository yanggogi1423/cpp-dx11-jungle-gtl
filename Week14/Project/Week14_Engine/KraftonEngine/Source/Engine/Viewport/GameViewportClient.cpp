#include "Viewport/GameViewportClient.h"

#include "Component/Camera/CameraComponent.h"
#include "Engine/Input/InputSystem.h"
#include "Math/MathUtils.h"
#include "UI/CursorSystem.h"
#include "UI/UIManager.h"
#include "Core/Logging/Log.h"
#include "Viewport/Viewport.h"

#include <windows.h>
#include <algorithm>
#include <cmath>

void UGameViewportClient::BeginGameSession(FViewport* InViewport)
{
	Viewport = InViewport;
	ClearGameInputSnapshot();
	ResetInputState();
	ResetVirtualCursorState();
}

void UGameViewportClient::EndGameSession()
{
	SetInputPossessed(false);
	ResetInputState();
	ResetVirtualCursorState();
	bHasCursorClipRect = false;
	// Shutdown 경로에서는 ProcessInput 이 더 이상 안 돌아 — 커서 캡처/clip 을 명시적으로 해제.
	// 이걸 안 풀면 ::ShowCursor 카운터 음수 + ::ClipCursor 클립이 종료 후에도 남아 다른 앱
	// 까지 영향받음 (특히 ClipCursor 는 프로세스 종료 후에도 잔존하다가 다음 SetCursorPos
	// 까지 유지될 수 있다).
	FCursorSystem::Get().ResetRuntimeState();
	SetCursorCaptured(false);
	Viewport = nullptr;
}

namespace
{
	constexpr float GamepadVirtualCursorDeadZone = 0.20f;
	constexpr float GamepadVirtualCursorSpeed = 900.0f;

	float ApplyGamepadCursorDeadZone(float Value)
	{
		const float AbsValue = std::abs(Value);
		if (AbsValue <= GamepadVirtualCursorDeadZone)
		{
			return 0.0f;
		}

		const float Normalized = (AbsValue - GamepadVirtualCursorDeadZone) / (1.0f - GamepadVirtualCursorDeadZone);
		const float Clamped = (std::max)(0.0f, (std::min)(Normalized, 1.0f));
		return Value < 0.0f ? -Clamped : Clamped;
	}

	bool IsMouseVirtualKey(int VK)
	{
		return VK == VK_LBUTTON || VK == VK_RBUTTON || VK == VK_MBUTTON ||
			VK == VK_XBUTTON1 || VK == VK_XBUTTON2;
	}

	void ClearMouseInput(FInputSystemSnapshot& Snapshot)
	{
		const int MouseKeys[] = { VK_LBUTTON, VK_RBUTTON, VK_MBUTTON, VK_XBUTTON1, VK_XBUTTON2 };
		for (int VK : MouseKeys)
		{
			Snapshot.KeyDown[VK] = false;
			Snapshot.KeyPressed[VK] = false;
			Snapshot.KeyReleased[VK] = false;
		}

		Snapshot.MouseDeltaX = 0;
		Snapshot.MouseDeltaY = 0;
		Snapshot.ScrollDelta = 0;
		Snapshot.bLeftMouseDown = false;
		Snapshot.bLeftMousePressed = false;
		Snapshot.bLeftMouseReleased = false;
		Snapshot.bRightMouseDown = false;
		Snapshot.bRightMousePressed = false;
		Snapshot.bRightMouseReleased = false;
		Snapshot.bMiddleMouseDown = false;
		Snapshot.bMiddleMousePressed = false;
		Snapshot.bMiddleMouseReleased = false;
		Snapshot.bXButton1Down = false;
		Snapshot.bXButton1Pressed = false;
		Snapshot.bXButton1Released = false;
		Snapshot.bXButton2Down = false;
		Snapshot.bXButton2Pressed = false;
		Snapshot.bXButton2Released = false;
		Snapshot.bLeftDragStarted = false;
		Snapshot.bLeftDragging = false;
		Snapshot.bLeftDragEnded = false;
		Snapshot.LeftDragVector = { 0, 0 };
		Snapshot.bRightDragStarted = false;
		Snapshot.bRightDragging = false;
		Snapshot.bRightDragEnded = false;
		Snapshot.RightDragVector = { 0, 0 };
	}

	void ClearKeyboardInput(FInputSystemSnapshot& Snapshot)
	{
		for (int VK = 0; VK < 256; ++VK)
		{
			if (IsMouseVirtualKey(VK))
			{
				continue;
			}
			Snapshot.KeyDown[VK] = false;
			Snapshot.KeyPressed[VK] = false;
			Snapshot.KeyReleased[VK] = false;
		}
	}
}

void UGameViewportClient::ProcessInput(const FInputSystemSnapshot& Snapshot, float DeltaTime)
{
	ClearGameInputSnapshot();
	bVirtualCursorConfirmPressedThisFrame = false;
	bVirtualCursorConfirmReleasedThisFrame = false;

	if (!Snapshot.bWindowFocused)
	{
		ReleaseGameCapture();
		ResetInputState();
		ResetVirtualCursorState();
		return;
	}

	if (!bInputPossessed)
	{
		ReleaseGameCapture();
		ResetVirtualCursorState();
		return;
	}

	const FUIInputCaptureState InitialUIState = UUIManager::Get().GetViewportInputCaptureState();
	UpdateVirtualCursorFromGamepad(Snapshot, DeltaTime, InitialUIState);

	if (Viewport)
	{
		const uint32 ViewportWidth = Viewport->GetWidth();
		const uint32 ViewportHeight = Viewport->GetHeight();
		const int32 ViewportClientX = bHasCursorClipRect ? static_cast<int32>(CursorClipClientRect.left) : 0;
		const int32 ViewportClientY = bHasCursorClipRect ? static_cast<int32>(CursorClipClientRect.top) : 0;
		const int32 ViewportClientWidth = bHasCursorClipRect
			? static_cast<int32>(CursorClipClientRect.right - CursorClipClientRect.left)
			: static_cast<int32>(ViewportWidth);
		const int32 ViewportClientHeight = bHasCursorClipRect
			? static_cast<int32>(CursorClipClientRect.bottom - CursorClipClientRect.top)
			: static_cast<int32>(ViewportHeight);
		UUIManager::Get().PumpViewportInput(
			ViewportWidth,
			ViewportHeight,
			ViewportClientX,
			ViewportClientY,
			ViewportClientWidth,
			ViewportClientHeight);
	}

	const FUIInputCaptureState UIState = UUIManager::Get().GetViewportInputCaptureState();
	ApplyGameCapturePolicy(UIState);

	if (InputMode == EGameInputMode::UIOnly || UIState.bBlocksGameInput)
	{
		return;
	}

	FInputSystemSnapshot GameSnapshot = Snapshot;
	const bool bBlockGameMouse =
		UIState.bWantsMouse ||
		UIState.bBlocksGameMouseLook ||
		UIState.bConsumedMouseThisFrame;
	const bool bBlockGameKeyboard =
		UIState.bWantsTextInput ||
		UIState.bBlocksGameKeyboard ||
		UIState.bConsumedKeyboardThisFrame ||
		UIState.bConsumedTextInputThisFrame;

	if (bBlockGameMouse)
	{
		ClearMouseInput(GameSnapshot);
	}
	if (bBlockGameKeyboard)
	{
		ClearKeyboardInput(GameSnapshot);
	}

	SetGameInputSnapshot(GameSnapshot);
}

POINT UGameViewportClient::GetVirtualCursorClientPos() const
{
	POINT Result = {
		static_cast<LONG>(std::lround(VirtualCursorClientX)),
		static_cast<LONG>(std::lround(VirtualCursorClientY))
	};
	return Result;
}

void UGameViewportClient::SetInputPossessed(bool bPossessed)
{
	if (bInputPossessed == bPossessed)
	{
		return;
	}

	bInputPossessed = bPossessed;
	ResetInputState();

	// 커서 가시성/캡처는 ProcessInput 이 매 프레임 possess + UI WantsMouse 를 보고 결정.
	// 여기서는 게임 입력 라우팅만 토글한다.

	// possess off 로 전환되는 순간 GameInputSnapshot 도 비워서 Lua 폴링이 즉시 빈 입력을 본다.
	// (ProcessInput 호출이 멈춘 뒤에도 이전 값이 남아있는 케이스 방지.)
	if (!bPossessed)
	{
		ClearGameInputSnapshot();
		ResetVirtualCursorState();
		FCursorSystem::Get().SetSoftwareCursorVisible(false);
		ReleaseGameCapture();
	}
}

void UGameViewportClient::SetInputMode(EGameInputMode InMode)
{
	if (InputMode == InMode)
	{
		return;
	}

	InputMode = InMode;
	ClearGameInputSnapshot();
	ResetInputState();
	if (InputMode == EGameInputMode::UIOnly)
	{
		ReleaseGameCapture();
	}
}

void UGameViewportClient::SetCursorVisible(bool bVisible)
{
	if (bVisible)
	{
		SetMouseCaptured(false);
		return;
	}

	SetMouseCaptured(true);
}

void UGameViewportClient::SetCursorLocked(bool bLocked)
{
	SetMouseCaptured(bLocked);
}

void UGameViewportClient::RefreshCursorVisibility()
{
	const bool bShowHardwareCursor = !bCursorCaptured &&
		!FCursorSystem::Get().IsSoftwareCursorActive();
	if (bShowHardwareCursor)
	{
		while (::ShowCursor(TRUE) < 0) {}
	}
	else
	{
		while (::ShowCursor(FALSE) >= 0) {}
	}

	if (!bCursorCaptured)
	{
		::ClipCursor(nullptr);
	}
}

void UGameViewportClient::SetMouseCaptured(bool bCaptured)
{
	bWantsMouseCapture = bCaptured;

	if (!bCaptured)
	{
		ReleaseGameCapture();
		return;
	}

	if (!bInputPossessed || InputMode == EGameInputMode::UIOnly)
	{
		ReleaseGameCapture();
		return;
	}

	InputSystem::Get().SetUseRawMouse(true);
	SetCursorCaptured(true);
}

void UGameViewportClient::ReleaseMouseCapture()
{
	SetMouseCaptured(false);
}

void UGameViewportClient::SetCursorClipRect(const FRect& InViewportScreenRect)
{
	if (InViewportScreenRect.Width <= 1.0f || InViewportScreenRect.Height <= 1.0f)
	{
		bHasCursorClipRect = false;
		if (bCursorCaptured)
		{
			ApplyCursorClip();
		}
		return;
	}

	CursorClipClientRect.left = static_cast<LONG>(InViewportScreenRect.X);
	CursorClipClientRect.top = static_cast<LONG>(InViewportScreenRect.Y);
	CursorClipClientRect.right = static_cast<LONG>(InViewportScreenRect.X + InViewportScreenRect.Width);
	CursorClipClientRect.bottom = static_cast<LONG>(InViewportScreenRect.Y + InViewportScreenRect.Height);
	bHasCursorClipRect = CursorClipClientRect.right > CursorClipClientRect.left
		&& CursorClipClientRect.bottom > CursorClipClientRect.top;

	if (bCursorCaptured)
	{
		ApplyCursorClip();
	}
}

void UGameViewportClient::ResetInputState()
{
	InputSystem::Get().ResetMouseDelta();
	InputSystem::Get().ResetWheelDelta();
	bVirtualCursorConfirmPressedThisFrame = false;
	bVirtualCursorConfirmReleasedThisFrame = false;
}

void UGameViewportClient::ReleaseGameCapture()
{
	InputSystem::Get().SetUseRawMouse(false);
	SetCursorCaptured(false);
}

void UGameViewportClient::ApplyGameCapturePolicy(const FUIInputCaptureState& UIState)
{
	const bool bShouldCaptureMouse = bWantsMouseCapture &&
		InputMode != EGameInputMode::UIOnly &&
		!UIState.bWantsMouse &&
		!UIState.bBlocksGameInput &&
		!UIState.bBlocksGameMouseLook &&
		!UIState.bConsumedMouseThisFrame;

	InputSystem::Get().SetUseRawMouse(bShouldCaptureMouse);
	SetCursorCaptured(bShouldCaptureMouse);
}

void UGameViewportClient::UpdateVirtualCursorFromGamepad(const FInputSystemSnapshot& Snapshot, float DeltaTime, const FUIInputCaptureState& UIState)
{
	const bool bShouldUseVirtualCursor = Snapshot.bGamepadConnected &&
		(InputMode != EGameInputMode::GameOnly ||
		 UIState.bWantsMouse ||
		 UIState.bBlocksGameInput ||
		 UIState.bBlocksGameMouseLook);

	const bool bMouseMotionIntent =
		((Snapshot.MouseDeltaX != 0 || Snapshot.MouseDeltaY != 0) && !bIgnoreNextProgrammaticMouseMove);
	bIgnoreNextProgrammaticMouseMove = false;

	const bool bMouseIntent =
		bMouseMotionIntent ||
		Snapshot.ScrollDelta != 0 ||
		Snapshot.WasPressed(VK_LBUTTON) ||
		Snapshot.WasReleased(VK_LBUTTON) ||
		Snapshot.WasPressed(VK_RBUTTON) ||
		Snapshot.WasReleased(VK_RBUTTON) ||
		Snapshot.WasPressed(VK_MBUTTON) ||
		Snapshot.WasReleased(VK_MBUTTON);

	const float StickX = ApplyGamepadCursorDeadZone(Snapshot.GamepadLeftStickX);
	const float StickY = ApplyGamepadCursorDeadZone(Snapshot.GamepadLeftStickY);
	const bool bGamepadCursorIntent =
		StickX != 0.0f ||
		StickY != 0.0f ||
		Snapshot.WasGamepadButtonPressed(EGamepadButton::FaceBottom) ||
		Snapshot.WasGamepadButtonReleased(EGamepadButton::FaceBottom);

	if (!bShouldUseVirtualCursor)
	{
		ResetVirtualCursorState();
		return;
	}

	if (bMouseIntent)
	{
		ResetVirtualCursorState();
		return;
	}

	if (!Viewport)
	{
		ResetVirtualCursorState();
		return;
	}

	if (!bVirtualCursorActive && !bGamepadCursorIntent)
	{
		return;
	}

	if (!bVirtualCursorInitialized)
	{
		const POINT MousePos = InputSystem::Get().GetMouseClientPos();
		VirtualCursorClientX = static_cast<float>(MousePos.x);
		VirtualCursorClientY = static_cast<float>(MousePos.y);
		bVirtualCursorInitialized = true;
	}

	bVirtualCursorActive = true;
	if (bVirtualCursorOwnsSoftwareCursor)
	{
		FCursorSystem::Get().SetSoftwareCursorVisible(false);
		bVirtualCursorOwnsSoftwareCursor = false;
	}
	bVirtualCursorConfirmPressedThisFrame = Snapshot.WasGamepadButtonPressed(EGamepadButton::FaceBottom);
	bVirtualCursorConfirmReleasedThisFrame = Snapshot.WasGamepadButtonReleased(EGamepadButton::FaceBottom);

	const float SafeDeltaTime = DeltaTime > 0.0f ? DeltaTime : (1.0f / 60.0f);

	VirtualCursorClientX += StickX * GamepadVirtualCursorSpeed * SafeDeltaTime;
	VirtualCursorClientY -= StickY * GamepadVirtualCursorSpeed * SafeDeltaTime;

	float MinX = 0.0f;
	float MinY = 0.0f;
	float MaxX = static_cast<float>((std::max)(1u, Viewport->GetWidth()) - 1u);
	float MaxY = static_cast<float>((std::max)(1u, Viewport->GetHeight()) - 1u);
	if (bHasCursorClipRect)
	{
		MinX = static_cast<float>(CursorClipClientRect.left);
		MinY = static_cast<float>(CursorClipClientRect.top);
		MaxX = static_cast<float>((std::max)(CursorClipClientRect.left, CursorClipClientRect.right - 1));
		MaxY = static_cast<float>((std::max)(CursorClipClientRect.top, CursorClipClientRect.bottom - 1));
	}

	VirtualCursorClientX = FMath::Clamp(VirtualCursorClientX, MinX, MaxX);
	VirtualCursorClientY = FMath::Clamp(VirtualCursorClientY, MinY, MaxY);

	if (OwnerHWnd)
	{
		POINT ScreenPos = {
			static_cast<LONG>(std::lround(VirtualCursorClientX)),
			static_cast<LONG>(std::lround(VirtualCursorClientY))
		};
		if (::ClientToScreen(OwnerHWnd, &ScreenPos))
		{
			::SetCursorPos(ScreenPos.x, ScreenPos.y);
			InputSystem::Get().IgnoreNextMouseMoveForDeviceHeuristics();
			bIgnoreNextProgrammaticMouseMove = true;
		}
	}
}

void UGameViewportClient::ResetVirtualCursorState()
{
	if (bVirtualCursorOwnsSoftwareCursor)
	{
		FCursorSystem::Get().SetSoftwareCursorVisible(false);
		bVirtualCursorOwnsSoftwareCursor = false;
	}

	bVirtualCursorActive = false;
	bVirtualCursorInitialized = false;
	bIgnoreNextProgrammaticMouseMove = false;
	bVirtualCursorConfirmPressedThisFrame = false;
	bVirtualCursorConfirmReleasedThisFrame = false;
}

void UGameViewportClient::SetCursorCaptured(bool bCaptured)
{
	if (bCursorCaptured == bCaptured)
	{
		if (bCaptured)
		{
			ApplyCursorClip();
		}
		return;
	}

	bCursorCaptured = bCaptured;
	if (bCursorCaptured)
	{
		while (::ShowCursor(FALSE) >= 0) {}
		ApplyCursorClip();
		return;
	}

	RefreshCursorVisibility();
}

void UGameViewportClient::ApplyCursorClip()
{
	if (!OwnerHWnd)
	{
		return;
	}

	RECT ClientRect = {};
	if (bHasCursorClipRect)
	{
		ClientRect = CursorClipClientRect;
	}
	else if (!::GetClientRect(OwnerHWnd, &ClientRect))
	{
		return;
	}

	POINT TopLeft = { ClientRect.left, ClientRect.top };
	POINT BottomRight = { ClientRect.right, ClientRect.bottom };
	if (!::ClientToScreen(OwnerHWnd, &TopLeft) || !::ClientToScreen(OwnerHWnd, &BottomRight))
	{
		return;
	}

	RECT ScreenRect = { TopLeft.x, TopLeft.y, BottomRight.x, BottomRight.y };
	if (ScreenRect.right > ScreenRect.left && ScreenRect.bottom > ScreenRect.top)
	{
		::ClipCursor(&ScreenRect);
	}
}

void UGameViewportClient::SetGameInputSnapshot(const FInputSystemSnapshot& Snapshot)
{
	GameInputSnapshot = Snapshot;
	bHasGameInputSnapshot = true;
}

void UGameViewportClient::ClearGameInputSnapshot()
{
	GameInputSnapshot = FInputSystemSnapshot{};
	bHasGameInputSnapshot = false;
}
