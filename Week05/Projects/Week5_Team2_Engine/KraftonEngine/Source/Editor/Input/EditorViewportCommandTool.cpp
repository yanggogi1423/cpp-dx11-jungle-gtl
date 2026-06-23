#include "Editor/Input/EditorViewportCommandTool.h"

#include "Editor/Input/EditorViewportController.h"
#include "Editor/Input/EditorViewportInputMapping.h"
#include "Editor/Input/EditorNavigationTool.h"
#include "Editor/EditorEngine.h"
#include "Editor/Selection/SelectionManager.h"
#include "Editor/Viewport/EditorViewportClient.h"
#include "Engine/Input/InputSystem.h"
#include "Engine/Runtime/Engine.h"
#include "GameFramework/AActor.h"
#include "GameFramework/World.h"

FEditorViewportCommandTool::FEditorViewportCommandTool(FEditorViewportClient* InOwner, FEditorViewportController* InController)
	: Owner(InOwner), Controller(InController)
{
}

bool FEditorViewportCommandTool::HandleInput(float DeltaTime)
{
	(void)DeltaTime;
	if (!Owner || !Controller)
	{
		return false;
	}

	if (Owner->InputContext.bImGuiCapturedKeyboard)
	{
		return false;
	}

	static const TArray<int32> ViewportGlobalActionCandidates =
	{
		static_cast<int32>(EditorViewportInputMapping::EEditorViewportAction::CycleMode),
		static_cast<int32>(EditorViewportInputMapping::EEditorViewportAction::CycleGizmoMode),
		static_cast<int32>(EditorViewportInputMapping::EEditorViewportAction::ToggleGizmoCoordinateSpace),
		static_cast<int32>(EditorViewportInputMapping::EEditorViewportAction::FocusSelection),
		static_cast<int32>(EditorViewportInputMapping::EEditorViewportAction::DeleteSelection),
		static_cast<int32>(EditorViewportInputMapping::EEditorViewportAction::SelectAll),
		static_cast<int32>(EditorViewportInputMapping::EEditorViewportAction::NewLevel),
		static_cast<int32>(EditorViewportInputMapping::EEditorViewportAction::LoadLevel),
		static_cast<int32>(EditorViewportInputMapping::EEditorViewportAction::SaveLevel),
		static_cast<int32>(EditorViewportInputMapping::EEditorViewportAction::SaveLevelAs),
		static_cast<int32>(EditorViewportInputMapping::EEditorViewportAction::PIEEndPlay),
		static_cast<int32>(EditorViewportInputMapping::EEditorViewportAction::PIETogglePossessEject),
		static_cast<int32>(EditorViewportInputMapping::EEditorViewportAction::PIEReleaseMouseCapture)
	};

	int32 TriggeredCommandActionId = 0;
	const bool bHasCommandAction = InputBindingUtils::TryGetHighestPriorityTriggeredAction(
		Owner->InputContext,
		EditorViewportInputMapping::GetBindings(),
		ViewportGlobalActionCandidates,
		TriggeredCommandActionId);

	if (!bHasCommandAction)
	{
		return false;
	}

	switch (static_cast<EditorViewportInputMapping::EEditorViewportAction>(TriggeredCommandActionId))
	{
	case EditorViewportInputMapping::EEditorViewportAction::CycleMode:
		return Controller->CycleMode();
	case EditorViewportInputMapping::EEditorViewportAction::CycleGizmoMode:
		return Owner->TryCycleGizmoMode();
	case EditorViewportInputMapping::EEditorViewportAction::ToggleGizmoCoordinateSpace:
		return ToggleGizmoCoordinateSpace();
	case EditorViewportInputMapping::EEditorViewportAction::FocusSelection:
		return FocusPrimarySelection();
	case EditorViewportInputMapping::EEditorViewportAction::DeleteSelection:
		return DeleteSelectedActors();
	case EditorViewportInputMapping::EEditorViewportAction::SelectAll:
		return SelectAllActors();
	case EditorViewportInputMapping::EEditorViewportAction::NewLevel:
		return NewLevel();
	case EditorViewportInputMapping::EEditorViewportAction::LoadLevel:
		return LoadLevel();
	case EditorViewportInputMapping::EEditorViewportAction::SaveLevel:
		return SaveLevel();
	case EditorViewportInputMapping::EEditorViewportAction::SaveLevelAs:
		return SaveLevelAs();
	case EditorViewportInputMapping::EEditorViewportAction::PIEEndPlay:
		return PIEEndPlay();
	case EditorViewportInputMapping::EEditorViewportAction::PIETogglePossessEject:
		return PIETogglePossessEject();
	case EditorViewportInputMapping::EEditorViewportAction::PIEReleaseMouseCapture:
		return PIEReleaseMouseCapture();
	default:
		return false;
	}
}

bool FEditorViewportCommandTool::FocusPrimarySelection()
{
	if (!Owner || !Owner->Camera || !Owner->SelectionManager)
	{
		return false;
	}

	AActor* PrimarySelection = Owner->SelectionManager->GetPrimarySelection();
	if (!PrimarySelection)
	{
		return false;
	}

	const FVector Target = PrimarySelection->GetActorLocation();
	IEditorViewportTool* NavigationTool = Controller->GetNavigationTool();
	if (!NavigationTool)
	{
		return false;
	}

	FEditorNavigationTool* NavTool = static_cast<FEditorNavigationTool*>(NavigationTool);
	NavTool->FocusOnTarget(Target);
	return true;
}

bool FEditorViewportCommandTool::DeleteSelectedActors()
{
	if (!Owner || !Owner->SelectionManager)
	{
		return false;
	}

	const TArray<AActor*> SelectedActors = Owner->SelectionManager->GetSelectedActors();
	if (SelectedActors.empty())
	{
		return false;
	}

	for (AActor* Actor : SelectedActors)
	{
		if (!Actor)
		{
			continue;
		}

		if (UWorld* ActorWorld = Actor->GetWorld())
		{
			ActorWorld->DestroyActor(Actor);
		}
	}

	Owner->SelectionManager->ClearSelection();
	return true;
}

bool FEditorViewportCommandTool::ToggleGizmoCoordinateSpace()
{
	if (!Owner || !Owner->Gizmo)
	{
		return false;
	}

	Owner->Gizmo->ToggleCoordinateSpace();
	return true;
}

bool FEditorViewportCommandTool::SelectAllActors()
{
	if (!Owner || !Owner->World || !Owner->SelectionManager)
	{
		return false;
	}

	const TArray<AActor*>& Actors = Owner->World->GetActors();
	if (Actors.empty())
	{
		return false;
	}

	Owner->SelectionManager->ClearSelection();

	bool bSelectedAny = false;
	for (AActor* Actor : Actors)
	{
		if (!Actor || !Actor->IsVisible())
		{
			continue;
		}

		Owner->SelectionManager->AddSelect(Actor);
		bSelectedAny = true;
	}

	return bSelectedAny;
}

bool FEditorViewportCommandTool::NewLevel()
{
	UEditorEngine* EditorEngine = Cast<UEditorEngine>(GEngine);
	if (!EditorEngine)
	{
		return false;
	}

	EditorEngine->NewLevel();
	return true;
}

bool FEditorViewportCommandTool::LoadLevel()
{
	UEditorEngine* EditorEngine = Cast<UEditorEngine>(GEngine);
	return EditorEngine ? EditorEngine->LoadLevelWithDialog() : false;
}

bool FEditorViewportCommandTool::SaveLevel()
{
	UEditorEngine* EditorEngine = Cast<UEditorEngine>(GEngine);
	return EditorEngine ? EditorEngine->SaveLevel() : false;
}

bool FEditorViewportCommandTool::SaveLevelAs()
{
	UEditorEngine* EditorEngine = Cast<UEditorEngine>(GEngine);
	return EditorEngine ? EditorEngine->SaveLevelAsWithDialog() : false;
}

bool FEditorViewportCommandTool::PIEEndPlay()
{
	UEditorEngine* EditorEngine = Cast<UEditorEngine>(GEngine);
	if (!EditorEngine || !EditorEngine->IsPIEEnabled())
	{
		return false;
	}

	EditorEngine->EndPIE();
	return true;
}

bool FEditorViewportCommandTool::PIETogglePossessEject()
{
	UEditorEngine* EditorEngine = Cast<UEditorEngine>(GEngine);
	if (!EditorEngine || !EditorEngine->IsPIEEnabled())
	{
		return false;
	}

	return EditorEngine->TogglePIEControlMode();
}

bool FEditorViewportCommandTool::PIEReleaseMouseCapture()
{
	UEditorEngine* EditorEngine = Cast<UEditorEngine>(GEngine);
	if (!EditorEngine || !EditorEngine->IsPIEEnabled())
	{
		return false;
	}

	InputSystem::Get().EndRelativeMouseMode();
	return true;
}
