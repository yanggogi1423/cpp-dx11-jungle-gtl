#include "Editor/Input/EditorViewportInputContexts.h"

#include "Editor/Viewport/EditorViewportClient.h"

bool FEditorViewportCommandContext::HandleInput(float DeltaTime)
{
	return Owner ? Owner->HandleCommandInput(DeltaTime) : false;
}

bool FEditorViewportGizmoContext::HandleInput(float DeltaTime)
{
	return Owner ? Owner->HandleGizmoInput(DeltaTime) : false;
}

bool FEditorViewportSelectionContext::HandleInput(float DeltaTime)
{
	return Owner ? Owner->HandleSelectionInput(DeltaTime) : false;
}

bool FEditorViewportNavigationContext::HandleInput(float DeltaTime)
{
	return Owner ? Owner->HandleNavigationInput(DeltaTime) : false;
}
