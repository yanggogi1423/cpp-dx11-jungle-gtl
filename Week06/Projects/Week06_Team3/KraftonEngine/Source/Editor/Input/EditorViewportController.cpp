#include "Editor/Input/EditorViewportController.h"

#include "Editor/Input/EditorNavigationTool.h"
#include "Editor/Input/EditorViewportCommandTool.h"
#include "Editor/Viewport/EditorViewportClient.h"

namespace
{
std::unique_ptr<IEditorViewportMode> CreateMode(EEditorViewportModeType InModeType, FEditorViewportClient* InOwner)
{
	switch (InModeType)
	{
	case EEditorViewportModeType::Select:
		return std::make_unique<FEditorTransformMode>(InOwner, EEditorViewportModeType::Select);
	case EEditorViewportModeType::Translate:
		return std::make_unique<FEditorTransformMode>(InOwner, EEditorViewportModeType::Translate);
	case EEditorViewportModeType::Rotate:
		return std::make_unique<FEditorTransformMode>(InOwner, EEditorViewportModeType::Rotate);
	case EEditorViewportModeType::Scale:
		return std::make_unique<FEditorTransformMode>(InOwner, EEditorViewportModeType::Scale);
	default:
		return std::make_unique<FEditorTransformMode>(InOwner, EEditorViewportModeType::Select);
	}
}

EEditorViewportModeType GetNextModeType(EEditorViewportModeType InCurrentModeType)
{
	switch (InCurrentModeType)
	{
	case EEditorViewportModeType::Select:
		return EEditorViewportModeType::Translate;
	case EEditorViewportModeType::Translate:
		return EEditorViewportModeType::Rotate;
	case EEditorViewportModeType::Rotate:
		return EEditorViewportModeType::Scale;
	case EEditorViewportModeType::Scale:
		return EEditorViewportModeType::Select;
	default:
		return EEditorViewportModeType::Translate;
	}
}
}

FEditorViewportController::FEditorViewportController(FEditorViewportClient* InOwner)
	: Owner(InOwner)
{
	if (Owner)
	{
		ActiveMode = CreateMode(EEditorViewportModeType::Select, Owner);
		if (ActiveMode)
		{
			ActiveMode->OnActivated();
		}
		ViewportCommandTool = std::make_unique<FEditorViewportCommandTool>(Owner, this);
		NavigationTool = std::make_unique<FEditorNavigationTool>(Owner);
	}
}

bool FEditorViewportController::SetMode(EEditorViewportModeType InModeType)
{
	if (!Owner)
	{
		return false;
	}
	if (ActiveMode && ActiveMode->GetType() == InModeType)
	{
		return true;
	}
	ActiveMode = CreateMode(InModeType, Owner);
	if (ActiveMode)
	{
		ActiveMode->OnActivated();
	}
	return ActiveMode != nullptr;
}

EEditorViewportModeType FEditorViewportController::GetMode() const
{
	return ActiveMode ? ActiveMode->GetType() : EEditorViewportModeType::Select;
}

bool FEditorViewportController::CycleMode()
{
	if (!ActiveMode)
	{
		return false;
	}
	const EEditorViewportModeType NextModeType = GetNextModeType(ActiveMode->GetType());
	return SetMode(NextModeType);
}

bool FEditorViewportController::HandleViewportCommandInput(float DeltaTime)
{
	return ViewportCommandTool ? ViewportCommandTool->HandleInput(DeltaTime) : false;
}

bool FEditorViewportController::HandleGizmoInput(float DeltaTime)
{
	return ActiveMode ? ActiveMode->HandleGizmoInput(DeltaTime) : false;
}

bool FEditorViewportController::HandleSelectionInput(float DeltaTime)
{
	return ActiveMode ? ActiveMode->HandleSelectionInput(DeltaTime) : false;
}

bool FEditorViewportController::HandleNavigationInput(float DeltaTime)
{
	return NavigationTool ? NavigationTool->HandleInput(DeltaTime) : false;
}

bool FEditorViewportController::GetSelectionMarquee(POINT& OutStart, POINT& OutCurrent, bool& bOutAdditive) const
{
	if (!ActiveMode)
	{
		OutStart = { 0, 0 };
		OutCurrent = { 0, 0 };
		bOutAdditive = false;
		return false;
	}
	return ActiveMode->GetSelectionMarquee(OutStart, OutCurrent, bOutAdditive);
}

