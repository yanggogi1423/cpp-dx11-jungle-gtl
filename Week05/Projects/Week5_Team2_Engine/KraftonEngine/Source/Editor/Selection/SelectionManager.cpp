#include "Editor/Selection/SelectionManager.h"

#include "Editor/Gizmo/TransformGizmo.h"
#include "GameFramework/AActor.h"
#include "GameFramework/World.h"

#include <algorithm>
#include <climits>
#include <cmath>

FSelectionManager::~FSelectionManager() = default;

void FSelectionManager::Init(UWorld* InWorld)
{
	Gizmo = std::make_unique<FTransformGizmo>();
	Gizmo->Initialize(InWorld);
}

void FSelectionManager::Shutdown()
{
	ClearSelection();

	if (Gizmo)
	{
		Gizmo->Shutdown();
		Gizmo.reset();
	}
}

void FSelectionManager::SetWorld(UWorld* InWorld)
{
	if (!Gizmo)
	{
		return;
	}

	SelectedActors.clear();
	PrimarySelection = nullptr;
	Gizmo->SetWorld(InWorld);

	SyncGizmo();
}

void FSelectionManager::Select(AActor* Actor)
{
	SelectedActors.clear();
	PrimarySelection = nullptr;
	if (Actor)
	{
		SelectedActors.push_back(Actor);
		PrimarySelection = Actor;
	}
	SyncGizmo();
}

void FSelectionManager::AddSelect(AActor* Actor)
{
	if (!Actor)
	{
		return;
	}

	if (!IsSelected(Actor))
	{
		SelectedActors.push_back(Actor);
	}
	PrimarySelection = Actor;
	SyncGizmo();
}

void FSelectionManager::SelectRange(AActor* ClickedActor, const TArray<AActor*>& ActorList)
{
	if (!ClickedActor) return;

	int32 ClickedIdx = -1;
	for (int32 i = 0; i < static_cast<int32>(ActorList.size()); ++i)
	{
		if (ActorList[i] == ClickedActor) { ClickedIdx = i; break; }
	}
	if (ClickedIdx == -1) return;

	int32 AnchorIdx = ClickedIdx;
	int32 MinDist = INT_MAX;
	for (AActor* Sel : SelectedActors)
	{
		for (int32 i = 0; i < static_cast<int32>(ActorList.size()); ++i)
		{
			if (ActorList[i] == Sel)
			{
				int32 Dist = std::abs(i - ClickedIdx);
				if (Dist < MinDist)
				{
					MinDist = Dist;
					AnchorIdx = i;
				}
				break;
			}
		}
	}

	int32 Lo = std::min(AnchorIdx, ClickedIdx);
	int32 Hi = std::max(AnchorIdx, ClickedIdx);

	SelectedActors.clear();
	for (int32 i = Lo; i <= Hi; ++i)
	{
		if (ActorList[i])
		{
			SelectedActors.push_back(ActorList[i]);
		}
	}
	PrimarySelection = ClickedActor;
	SyncGizmo();
}

void FSelectionManager::ToggleSelect(AActor* Actor)
{
	if (!Actor) return;

	auto It = std::find(SelectedActors.begin(), SelectedActors.end(), Actor);
	if (It != SelectedActors.end())
	{
		SelectedActors.erase(It);
		if (PrimarySelection == Actor)
		{
			PrimarySelection = SelectedActors.empty() ? nullptr : SelectedActors.back();
		}
	}
	else
	{
		SelectedActors.push_back(Actor);
		PrimarySelection = Actor;
	}
	SyncGizmo();
}

void FSelectionManager::Deselect(AActor* Actor)
{
	auto It = std::find(SelectedActors.begin(), SelectedActors.end(), Actor);
	if (It != SelectedActors.end())
	{
		SelectedActors.erase(It);
		if (PrimarySelection == Actor)
		{
			PrimarySelection = SelectedActors.empty() ? nullptr : SelectedActors.back();
		}
	}
	SyncGizmo();
}

void FSelectionManager::ClearSelection()
{
	SelectedActors.clear();
	PrimarySelection = nullptr;
	SyncGizmo();
}

AActor* FSelectionManager::GetPrimarySelection() const
{
	if (PrimarySelection && IsSelected(PrimarySelection))
	{
		return PrimarySelection;
	}

	return SelectedActors.empty() ? nullptr : SelectedActors.back();
}

void FSelectionManager::SyncGizmo()
{
	if (!Gizmo)
	{
		return;
	}

	if (Gizmo->GetWorld())
	{
		Gizmo->EnsureProxyRegistered();
	}

	AActor* Primary = GetPrimarySelection();
	if (Primary)
	{
		Gizmo->SetTarget(Primary);
		Gizmo->SetSelectedActors(&SelectedActors);
	}
	else
	{
		Gizmo->SetSelectedActors(nullptr);
		Gizmo->Deactivate();
	}
}
