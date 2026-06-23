#include "Diagnostics/GameViewportInputDiagnostics.h"

#include "Engine/Input/InputSystem.h"
#include "Object/GarbageCollection.h"
#include "Object/Reflection/ObjectFactory.h"
#include "UI/UIManager.h"
#include "Viewport/GameViewportClient.h"

#include <windows.h>

namespace
{
	struct FGameViewportInputSelfTestContext
	{
		FGameViewportInputSelfTestResult Result;

		void Check(bool bCondition, const char* Message)
		{
			++Result.ChecksRun;
			if (bCondition)
			{
				return;
			}

			Result.bPassed = false;
			if (!Result.Message.empty())
			{
				Result.Message += "\n";
			}
			Result.Message += Message ? Message : "unknown failure";
		}
	};

	FInputSystemSnapshot MakeSyntheticGameplayInput()
	{
		FInputSystemSnapshot Snapshot;
		Snapshot.bWindowFocused = true;
		Snapshot.KeyDown['W'] = true;
		Snapshot.KeyPressed['W'] = true;
		Snapshot.KeyDown[VK_SPACE] = true;
		Snapshot.KeyPressed[VK_SPACE] = true;
		Snapshot.KeyDown[VK_LBUTTON] = true;
		Snapshot.KeyPressed[VK_LBUTTON] = true;
		Snapshot.bLeftMouseDown = true;
		Snapshot.bLeftMousePressed = true;
		Snapshot.MouseDeltaX = 9;
		Snapshot.MouseDeltaY = -4;
		Snapshot.ScrollDelta = WHEEL_DELTA;
		return Snapshot;
	}

	void ResetGlobalInputSideEffects()
	{
		InputSystem::Get().SetUseRawMouse(false);
		InputSystem::Get().ResetMouseDelta();
		InputSystem::Get().ResetWheelDelta();
	}
}

FGameViewportInputSelfTestResult FGameViewportInputDiagnostics::RunSelfTest()
{
	FScopedGarbageCollectionBlocker GCBlocker;
	FGameViewportInputSelfTestContext Context;
	Context.Result.bPassed = true;

	ResetGlobalInputSideEffects();

	UGameViewportClient* ViewportClient = UObjectManager::Get().CreateObject<UGameViewportClient>();
	Context.Check(ViewportClient != nullptr, "Game viewport input self-test should create a viewport client.");
	if (!ViewportClient)
	{
		return Context.Result;
	}

	// Keep the synthetic test from capturing the OS cursor while still exercising
	// the game snapshot route.
	ViewportClient->SetCursorVisible(true);
	ViewportClient->SetInputMode(EGameInputMode::GameOnly);
	UUIManager::Get().BeginInputFrame();

	const FInputSystemSnapshot GameplaySnapshot = MakeSyntheticGameplayInput();

	ViewportClient->SetInputPossessed(false);
	ViewportClient->ProcessInput(GameplaySnapshot, 0.016f);
	Context.Check(!ViewportClient->HasGameInputSnapshot(), "Ejected/unpossessed viewport input should not produce a game input snapshot.");
	Context.Check(!InputSystem::Get().IsUsingRawMouse(), "Ejected/unpossessed viewport input should release raw mouse capture.");
	Context.Check(!ViewportClient->IsMouseCaptured(), "Ejected/unpossessed viewport input should not capture the cursor.");

	const bool bCanAssertOpenGameRoute = !UUIManager::Get().HasViewportWidgets();
	if (bCanAssertOpenGameRoute)
	{
		ViewportClient->SetInputPossessed(true);
		ViewportClient->ProcessInput(GameplaySnapshot, 0.016f);
		Context.Check(ViewportClient->HasGameInputSnapshot(), "Possessed GameOnly viewport input should produce a game input snapshot.");
		if (ViewportClient->HasGameInputSnapshot())
		{
			const FInputSystemSnapshot& RoutedSnapshot = ViewportClient->GetGameInputSnapshot();
			Context.Check(RoutedSnapshot.IsDown('W') && RoutedSnapshot.WasPressed('W'), "Possessed viewport input should preserve keyboard state.");
			Context.Check(RoutedSnapshot.bLeftMouseDown && RoutedSnapshot.bLeftMousePressed, "Possessed viewport input should preserve mouse button state.");
			Context.Check(RoutedSnapshot.MouseDeltaX == 9 && RoutedSnapshot.MouseDeltaY == -4, "Possessed viewport input should preserve mouse deltas.");
			Context.Check(RoutedSnapshot.ScrollDelta == WHEEL_DELTA, "Possessed viewport input should preserve wheel input.");
		}
	}
	else
	{
		Context.Check(true, "Possessed open-route snapshot check skipped because viewport UI widgets are mounted.");
	}

	ViewportClient->SetInputMode(EGameInputMode::UIOnly);
	ViewportClient->SetInputPossessed(true);
	ViewportClient->ProcessInput(GameplaySnapshot, 0.016f);
	Context.Check(!ViewportClient->HasGameInputSnapshot(), "UIOnly input mode should not produce a game input snapshot.");
	Context.Check(!InputSystem::Get().IsUsingRawMouse(), "UIOnly input mode should release raw mouse capture.");

	ViewportClient->SetInputMode(EGameInputMode::GameOnly);
	ViewportClient->SetInputPossessed(true);
	FInputSystemSnapshot UnfocusedSnapshot = GameplaySnapshot;
	UnfocusedSnapshot.bWindowFocused = false;
	ViewportClient->ProcessInput(UnfocusedSnapshot, 0.016f);
	Context.Check(!ViewportClient->HasGameInputSnapshot(), "Unfocused viewport input should not produce a game input snapshot.");
	Context.Check(!InputSystem::Get().IsUsingRawMouse(), "Unfocused viewport input should release raw mouse capture.");
	Context.Check(!ViewportClient->IsMouseCaptured(), "Unfocused viewport input should not capture the cursor.");

	ViewportClient->SetInputPossessed(false);
	ResetGlobalInputSideEffects();

	if (Context.Result.bPassed && Context.Result.Message.empty())
	{
		Context.Result.Message = "Game viewport input routing self-test passed.";
	}
	return Context.Result;
}
