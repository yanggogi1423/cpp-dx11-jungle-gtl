#include "Editor/Input/EditorViewportCommandTool.h"

#include "Components/GizmoComponent.h"
#include "Editor/EditorEngine.h"
#include "Editor/Input/EditorNavigationTool.h"
#include "Editor/Input/EditorViewportController.h"
#include "Editor/Input/EditorViewportInputMapping.h"
#include "Editor/Input/EditorViewportInputUtils.h"
#include "Editor/Selection/SelectionManager.h"
#include "Editor/Viewport/EditorViewportClient.h"
#include "Engine/Runtime/Engine.h"
#include "GameFramework/AActor.h"
#include "GameFramework/World.h"

FEditorViewportCommandTool::FEditorViewportCommandTool(FEditorViewportClient* InOwner, FEditorViewportController* InController)
	: Owner(InOwner), Controller(InController)
{
}

namespace
{
using EAction = EditorViewportInputMapping::EEditorViewportAction;

UEditorEngine* GetEditorEngine()
{
	return Cast<UEditorEngine>(GEngine);
}

EEditorViewportModeType ToModeType(EGizmoMode InMode)
{
	switch (InMode)
	{
	case EGizmoMode::Translate: return EEditorViewportModeType::Translate;
	case EGizmoMode::Rotate: return EEditorViewportModeType::Rotate;
	case EGizmoMode::Scale: return EEditorViewportModeType::Scale;
	default: return EEditorViewportModeType::Select;
	}
}

TArray<EAction> GetCommandActionCandidates()
{
	return {
		EAction::CycleMode,
		EAction::SetModeSelect,
		EAction::SetModeTranslate,
		EAction::SetModeRotate,
		EAction::SetModeScale,
		EAction::CycleGizmoMode,
		EAction::ToggleCoordinateSpace,
		EAction::FocusSelection,
		EAction::DeleteSelection,
		EAction::SelectAll,
		EAction::NewScene,
		EAction::LoadScene,
		EAction::SaveScene,
		EAction::SaveSceneAs,
		EAction::DuplicateSelection
	};
}
}

bool FEditorViewportCommandTool::HandleInput(float DeltaTime)
{
	(void)DeltaTime;
	if (!Owner || !Controller)
	{
		return false;
	}
	if (Owner->GetRoutedInputContext().bImGuiCapturedKeyboard)
	{
		return false;
	}

	EAction Action = EAction::CycleMode;
	if (!EditorViewportInputMapping::TryGetHighestPriorityTriggeredAction(
		Owner->GetRoutedInputContext(),
		GetCommandActionCandidates(),
		Action))
	{
		return false;
	}
	const FViewportInputContext& Context = Owner->GetRoutedInputContext();
	const bool bLeftHeldFlyMove =
		Context.Frame.IsDown(VK_LBUTTON)
		&& !Context.Frame.IsAltDown()
		&& !Context.Frame.IsCtrlDown();
	const bool bMouseNavigationActive =
		EditorViewportInputMapping::IsTriggered(Context, EAction::NavLookRightDown)
		|| EditorViewportInputMapping::IsTriggered(Context, EAction::NavLookMiddleDown)
		|| EditorViewportInputMapping::IsTriggered(Context, EAction::NavOrbitAltLeftDown)
		|| EditorViewportInputMapping::IsTriggered(Context, EAction::NavDollyAltRightDown)
		|| EditorViewportInputMapping::IsTriggered(Context, EAction::NavPanAltMiddleDown)
		|| EditorViewportInputUtils::IsLeftNavigationDragActive(Context)
		|| bLeftHeldFlyMove;

	if (bMouseNavigationActive
		&& (Action == EAction::SetModeSelect
			|| Action == EAction::SetModeTranslate
			|| Action == EAction::SetModeRotate
			|| Action == EAction::SetModeScale))
	{
		return false;
	}

	switch (Action)
	{
	case EAction::CycleMode:
		return Controller->CycleMode();
	case EAction::SetModeSelect:
		return SetMode(EEditorViewportModeType::Select);
	case EAction::SetModeTranslate:
		return SetMode(EEditorViewportModeType::Translate);
	case EAction::SetModeRotate:
		return SetMode(EEditorViewportModeType::Rotate);
	case EAction::SetModeScale:
		return SetMode(EEditorViewportModeType::Scale);
	case EAction::CycleGizmoMode:
		return TryCycleGizmoMode();
	case EAction::ToggleCoordinateSpace:
		return TryToggleCoordinateSpace();
	case EAction::FocusSelection:
		return FocusPrimarySelection();
	case EAction::DeleteSelection:
		return DeleteSelectedActors();
	case EAction::SelectAll:
		return SelectAllActors();
	case EAction::NewScene:
		return NewScene();
	case EAction::LoadScene:
		return LoadScene();
	case EAction::SaveScene:
		return SaveScene();
	case EAction::SaveSceneAs:
		return SaveSceneAs();
	case EAction::DuplicateSelection:
		return DuplicateSelection();
	default:
		return false;
	}
}

bool FEditorViewportCommandTool::SetMode(EEditorViewportModeType InModeType)
{
	if (!Controller || !Owner)
	{
		return false;
	}

	if (!Controller->SetMode(InModeType))
	{
		return false;
	}

	UGizmoComponent* Gizmo = Owner->GetGizmo();
	switch (InModeType)
	{
	case EEditorViewportModeType::Select:
		Owner->GetRenderOptions().ShowFlags.bGizmo = false;
		break;
	case EEditorViewportModeType::Translate:
		Owner->GetRenderOptions().ShowFlags.bGizmo = true;
		if (Gizmo) Gizmo->SetTranslateMode();
		break;
	case EEditorViewportModeType::Rotate:
		Owner->GetRenderOptions().ShowFlags.bGizmo = true;
		if (Gizmo) Gizmo->SetRotateMode();
		break;
	case EEditorViewportModeType::Scale:
		Owner->GetRenderOptions().ShowFlags.bGizmo = true;
		if (Gizmo) Gizmo->SetScaleMode();
		break;
	default:
		break;
	}

	return true;
}

bool FEditorViewportCommandTool::FocusPrimarySelection()
{
	if (!Owner || !Owner->GetSelectionManager() || !Controller)
	{
		return false;
	}

	AActor* PrimarySelection = Owner->GetSelectionManager()->GetPrimarySelection();
	if (!PrimarySelection)
	{
		return false;
	}

	FEditorNavigationTool* NavigationTool = Controller->GetNavigationTool();
	if (!NavigationTool)
	{
		return false;
	}

	NavigationTool->FocusOnTarget(PrimarySelection->GetActorLocation());
	return true;
}

bool FEditorViewportCommandTool::DeleteSelectedActors()
{
	if (!Owner || !Owner->GetSelectionManager())
	{
		return false;
	}

	const TArray<AActor*> SelectedActors = Owner->GetSelectionManager()->GetSelectedActors();
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

	Owner->GetSelectionManager()->ClearSelection();
	return true;
}

bool FEditorViewportCommandTool::SelectAllActors()
{
	if (!Owner || !Owner->GetSelectionManager())
	{
		return false;
	}

	UWorld* World = Owner->GetInteractionWorld();
	if (!World)
	{
		return false;
	}

	const TArray<AActor*>& Actors = World->GetActors();
	if (Actors.empty())
	{
		return false;
	}

	Owner->GetSelectionManager()->ClearSelection();
	bool bSelectedAny = false;
	for (AActor* Actor : Actors)
	{
		if (!Actor || !Actor->IsVisible())
		{
			continue;
		}
		Owner->GetSelectionManager()->ToggleSelect(Actor);
		bSelectedAny = true;
	}
	return bSelectedAny;
}

bool FEditorViewportCommandTool::NewScene()
{
	UEditorEngine* EditorEngine = GetEditorEngine();
	if (!EditorEngine)
	{
		return false;
	}

	EditorEngine->NewScene();
	return true;
}

bool FEditorViewportCommandTool::LoadScene()
{
	UEditorEngine* EditorEngine = GetEditorEngine();
	return EditorEngine ? EditorEngine->LoadSceneWithDialog() : false;
}

bool FEditorViewportCommandTool::SaveScene()
{
	UEditorEngine* EditorEngine = GetEditorEngine();
	return EditorEngine ? EditorEngine->SaveScene() : false;
}

bool FEditorViewportCommandTool::SaveSceneAs()
{
	UEditorEngine* EditorEngine = GetEditorEngine();
	return EditorEngine ? EditorEngine->SaveSceneAsWithDialog() : false;
}

bool FEditorViewportCommandTool::DuplicateSelection()
{
	if (!Owner || !Owner->GetSelectionManager())
	{
		return false;
	}

	const TArray<AActor*> ToDuplicate = Owner->GetSelectionManager()->GetSelectedActors();
	if (ToDuplicate.empty())
	{
		return false;
	}

	TArray<AActor*> NewSelection;
	for (AActor* Src : ToDuplicate)
	{
		if (!Src)
		{
			continue;
		}

		AActor* Dup = Cast<AActor>(Src->Duplicate(nullptr));
		if (Dup)
		{
			NewSelection.push_back(Dup);
		}
	}

	if (NewSelection.empty())
	{
		return false;
	}

	Owner->GetSelectionManager()->ClearSelection();
	for (AActor* Actor : NewSelection)
	{
		Owner->GetSelectionManager()->ToggleSelect(Actor);
	}
	return true;
}

bool FEditorViewportCommandTool::TryCycleGizmoMode()
{
	if (!Owner || !Owner->GetGizmo())
	{
		return false;
	}

	Owner->GetGizmo()->SetNextMode();
	return SetMode(ToModeType(Owner->GetGizmo()->GetMode()));
}

bool FEditorViewportCommandTool::TryToggleCoordinateSpace()
{
	if (!Owner || !Owner->GetGizmo())
	{
		return false;
	}

	Owner->GetGizmo()->ToggleWorldSpace();
	return true;
}
