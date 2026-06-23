#include "Editor/Input/EditorViewportInputContexts.h"

#include "Editor/Viewport/EditorViewportClient.h"

FViewportCommandContext::FViewportCommandContext(FEditorViewportClient* InOwner, float* InDeltaTime)
	: Owner(InOwner), DeltaTimePtr(InDeltaTime)
{
}

bool FViewportCommandContext::HandleInput(FViewportInputContext& Context)
{
	if (!Owner || !DeltaTimePtr)
	{
		return false;
	}

	// Top-priority viewport-global command stage.
	// It should consume only global shortcut actions.
	Owner->InputContext = Context;
	Owner->EnsureInputController();
	return Owner->InputController ? Owner->InputController->HandleViewportCommandInput(*DeltaTimePtr) : false;
}

FEditorGizmoInputContext::FEditorGizmoInputContext(FEditorViewportClient* InOwner, float* InDeltaTime)
	: Owner(InOwner), DeltaTimePtr(InDeltaTime)
{
}

bool FEditorGizmoInputContext::HandleInput(FViewportInputContext& Context)
{
	if (!Owner || !DeltaTimePtr)
	{
		return false;
	}

	Owner->InputContext = Context;
	Owner->EnsureInputController();
	return Owner->InputController ? Owner->InputController->HandleGizmoInput(*DeltaTimePtr) : false;
}

FEditorSelectionInputContext::FEditorSelectionInputContext(FEditorViewportClient* InOwner, float* InDeltaTime)
	: Owner(InOwner), DeltaTimePtr(InDeltaTime)
{
}

bool FEditorSelectionInputContext::HandleInput(FViewportInputContext& Context)
{
	if (!Owner || !DeltaTimePtr)
	{
		return false;
	}

	Owner->InputContext = Context;
	Owner->EnsureInputController();
	return Owner->InputController ? Owner->InputController->HandleSelectionInput(*DeltaTimePtr) : false;
}

FEditorNavigationInputContext::FEditorNavigationInputContext(FEditorViewportClient* InOwner, float* InDeltaTime)
	: Owner(InOwner), DeltaTimePtr(InDeltaTime)
{
}

bool FEditorNavigationInputContext::HandleInput(FViewportInputContext& Context)
{
	if (!Owner || !DeltaTimePtr)
	{
		return false;
	}

	Owner->InputContext = Context;
	Owner->EnsureInputController();
	return Owner->InputController ? Owner->InputController->HandleNavigationInput(*DeltaTimePtr) : false;
}
