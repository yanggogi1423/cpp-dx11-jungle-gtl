#include "Editor/Input/EditorViewportModes.h"

FEditorSelectMode::FEditorSelectMode(FEditorViewportClient* InOwner)
	: Owner(InOwner)
{
	if (Owner)
	{
		GizmoTool = std::make_unique<FEditorGizmoTool>(Owner);
		SelectionTool = std::make_unique<FEditorSelectionTool>(Owner);
	}
}

bool FEditorSelectMode::HandleGizmoInput(float DeltaTime)
{
	if (!GizmoTool)
	{
		return false;
	}

	return GizmoTool->HandleInput(DeltaTime);
}

bool FEditorSelectMode::HandleSelectionInput(float DeltaTime)
{
	if (!SelectionTool)
	{
		return false;
	}

	return SelectionTool->HandleInput(DeltaTime);
}

bool FEditorSelectMode::HasPendingIdPickRequest() const
{
	const FEditorSelectionTool* Tool = static_cast<const FEditorSelectionTool*>(SelectionTool.get());
	return Tool ? Tool->HasPendingIdPickRequest() : false;
}

void FEditorSelectMode::GetPendingIdPickCoord(uint32& OutX, uint32& OutY) const
{
	OutX = 0u;
	OutY = 0u;
	const FEditorSelectionTool* Tool = static_cast<const FEditorSelectionTool*>(SelectionTool.get());
	if (Tool)
	{
		Tool->GetPendingIdPickCoord(OutX, OutY);
	}
}

bool FEditorSelectMode::HasPendingIdPickReadback() const
{
	const FEditorSelectionTool* Tool = static_cast<const FEditorSelectionTool*>(SelectionTool.get());
	return Tool ? Tool->HasPendingIdPickReadback() : false;
}

uint32 FEditorSelectMode::GetPendingIdPickReadbackRequestId() const
{
	const FEditorSelectionTool* Tool = static_cast<const FEditorSelectionTool*>(SelectionTool.get());
	return Tool ? Tool->GetPendingIdPickReadbackRequestId() : 0u;
}

void FEditorSelectMode::BeginPendingIdPickReadback(uint32 InRequestId)
{
	FEditorSelectionTool* Tool = static_cast<FEditorSelectionTool*>(SelectionTool.get());
	if (Tool)
	{
		Tool->BeginPendingIdPickReadback(InRequestId);
	}
}

void FEditorSelectMode::CancelPendingIdPickReadback()
{
	FEditorSelectionTool* Tool = static_cast<FEditorSelectionTool*>(SelectionTool.get());
	if (Tool)
	{
		Tool->CancelPendingIdPickReadback();
	}
}

void FEditorSelectMode::SetIdPickResult(uint32 InId)
{
	FEditorSelectionTool* Tool = static_cast<FEditorSelectionTool*>(SelectionTool.get());
	if (Tool)
	{
		Tool->SetIdPickResult(InId);
	}
}

void FEditorSelectMode::ResetIdPickingState()
{
	FEditorSelectionTool* Tool = static_cast<FEditorSelectionTool*>(SelectionTool.get());
	if (Tool)
	{
		Tool->ResetIdPickingState();
	}
}

void FEditorSelectMode::ResetInputState()
{
	FEditorSelectionTool* Tool = static_cast<FEditorSelectionTool*>(SelectionTool.get());
	if (Tool)
	{
		Tool->ResetInputState();
	}
}
