#include "Editor/Input/EditorViewportController.h"

#include "Editor/Input/EditorViewportCommandTool.h"
#include "Editor/Input/EditorNavigationTool.h"
#include "Editor/Viewport/EditorViewportClient.h"

namespace
{
std::unique_ptr<IEditorViewportMode> CreateMode(EEditorViewportModeType InModeType, FEditorViewportClient* InOwner)
{
	switch (InModeType)
	{
	case EEditorViewportModeType::Select:
	default:
		return std::make_unique<FEditorSelectMode>(InOwner);
	}
}

EEditorViewportModeType GetNextModeType(EEditorViewportModeType InCurrentModeType)
{
	switch (InCurrentModeType)
	{
	case EEditorViewportModeType::Select:
	default:
		return EEditorViewportModeType::Select;
	}
}
}

FEditorViewportController::FEditorViewportController(FEditorViewportClient* InOwner)
	: Owner(InOwner)
{
	if (Owner)
	{
		ActiveMode = CreateMode(EEditorViewportModeType::Select, Owner);
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
	return ActiveMode != nullptr;
}

EEditorViewportModeType FEditorViewportController::GetMode() const
{
	if (!ActiveMode)
	{
		return EEditorViewportModeType::Select;
	}

	return ActiveMode->GetType();
}

bool FEditorViewportController::CycleMode()
{
	if (!ActiveMode)
	{
		return false;
	}

	const EEditorViewportModeType CurrentModeType = ActiveMode->GetType();
	const EEditorViewportModeType NextModeType = GetNextModeType(CurrentModeType);
	if (NextModeType == CurrentModeType)
	{
		return false;
	}

	return SetMode(NextModeType);
}

bool FEditorViewportController::HandleViewportCommandInput(float DeltaTime)
{
	if (!Owner || !ViewportCommandTool)
	{
		return false;
	}

	return ViewportCommandTool->HandleInput(DeltaTime);
}

bool FEditorViewportController::HandleGizmoInput(float DeltaTime)
{
	if (!Owner || !ActiveMode)
	{
		return false;
	}

	return ActiveMode->HandleGizmoInput(DeltaTime);
}

bool FEditorViewportController::HandleSelectionInput(float DeltaTime)
{
	if (!Owner || !ActiveMode)
	{
		return false;
	}

	return ActiveMode->HandleSelectionInput(DeltaTime);
}

bool FEditorViewportController::HandleNavigationInput(float DeltaTime)
{
	if (!Owner || !NavigationTool)
	{
		return false;
	}

	return NavigationTool->HandleInput(DeltaTime);
}

bool FEditorViewportController::IsNavigationInputActiveNow() const
{
	if (!NavigationTool)
	{
		return false;
	}

	const FEditorNavigationTool* NavTool = static_cast<const FEditorNavigationTool*>(NavigationTool.get());
	return NavTool->IsInputActiveNow();
}

void FEditorViewportController::TickNavigationSmoothing(float DeltaTime)
{
	if (!NavigationTool)
	{
		return;
	}

	FEditorNavigationTool* NavTool = static_cast<FEditorNavigationTool*>(NavigationTool.get());
	NavTool->TickSmoothing(DeltaTime);
}

void FEditorViewportController::SyncNavigationFromCamera()
{
	if (!NavigationTool)
	{
		return;
	}

	FEditorNavigationTool* NavTool = static_cast<FEditorNavigationTool*>(NavigationTool.get());
	NavTool->SyncFromCamera();
}

bool FEditorViewportController::HasPendingIdPickRequest() const
{
	return ActiveMode ? ActiveMode->HasPendingIdPickRequest() : false;
}

void FEditorViewportController::GetPendingIdPickCoord(uint32& OutX, uint32& OutY) const
{
	OutX = 0u;
	OutY = 0u;
	if (ActiveMode)
	{
		ActiveMode->GetPendingIdPickCoord(OutX, OutY);
	}
}

bool FEditorViewportController::HasPendingIdPickReadback() const
{
	return ActiveMode ? ActiveMode->HasPendingIdPickReadback() : false;
}

uint32 FEditorViewportController::GetPendingIdPickReadbackRequestId() const
{
	return ActiveMode ? ActiveMode->GetPendingIdPickReadbackRequestId() : 0u;
}

void FEditorViewportController::BeginPendingIdPickReadback(uint32 InRequestId)
{
	if (ActiveMode)
	{
		ActiveMode->BeginPendingIdPickReadback(InRequestId);
	}
}

void FEditorViewportController::CancelPendingIdPickReadback()
{
	if (ActiveMode)
	{
		ActiveMode->CancelPendingIdPickReadback();
	}
}

void FEditorViewportController::SetIdPickResult(uint32 InId)
{
	if (ActiveMode)
	{
		ActiveMode->SetIdPickResult(InId);
	}
}

void FEditorViewportController::ResetIdPickingState()
{
	if (ActiveMode)
	{
		ActiveMode->ResetIdPickingState();
	}
}

void FEditorViewportController::ResetInputState()
{
	if (ActiveMode)
	{
		ActiveMode->ResetInputState();
	}
}
