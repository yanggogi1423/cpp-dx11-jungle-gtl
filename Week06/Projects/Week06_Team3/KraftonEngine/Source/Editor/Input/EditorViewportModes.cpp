#include "Editor/Input/EditorViewportModes.h"

#include "Editor/Input/EditorGizmoTool.h"
#include "Editor/Input/EditorSelectionTool.h"
#include "Editor/Viewport/EditorViewportClient.h"
#include "Components/GizmoComponent.h"

FEditorTransformMode::FEditorTransformMode(FEditorViewportClient* InOwner, EEditorViewportModeType InModeType)
	: Owner(InOwner)
	, ModeType(InModeType)
{
	if (Owner)
	{
		GizmoTool = std::make_unique<FEditorGizmoTool>(Owner);
		SelectionTool = std::make_unique<FEditorSelectionTool>(Owner);
	}
}

void FEditorTransformMode::OnActivated()
{
	if (!Owner || !Owner->GetGizmo())
	{
		return;
	}

	switch (ModeType)
	{
	case EEditorViewportModeType::Translate:
		Owner->GetGizmo()->SetTranslateMode();
		break;
	case EEditorViewportModeType::Rotate:
		Owner->GetGizmo()->SetRotateMode();
		break;
	case EEditorViewportModeType::Scale:
		Owner->GetGizmo()->SetScaleMode();
		break;
	case EEditorViewportModeType::Select:
	default:
		break;
	}
}

bool FEditorTransformMode::HandleGizmoInput(float DeltaTime)
{
	return GizmoTool ? GizmoTool->HandleInput(DeltaTime) : false;
}

bool FEditorTransformMode::HandleSelectionInput(float DeltaTime)
{
	return SelectionTool ? SelectionTool->HandleInput(DeltaTime) : false;
}

bool FEditorTransformMode::GetSelectionMarquee(POINT& OutStart, POINT& OutCurrent, bool& bOutAdditive) const
{
	if (!SelectionTool)
	{
		OutStart = { 0, 0 };
		OutCurrent = { 0, 0 };
		bOutAdditive = false;
		return false;
	}
	return SelectionTool->GetSelectionMarquee(OutStart, OutCurrent, bOutAdditive);
}

