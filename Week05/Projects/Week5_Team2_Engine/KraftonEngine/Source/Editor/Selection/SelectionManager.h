#pragma once

#include "Core/CoreTypes.h"
#include "Editor/Gizmo/TransformGizmo.h"
#include <memory>

class AActor;

class FSelectionManager
{
public:
	~FSelectionManager();
	void Init(class UWorld* InWorld);
	void Shutdown();
	void SetWorld(class UWorld* InWorld);

	void Select(AActor* Actor);
	void AddSelect(AActor* Actor);
	void SelectRange(AActor* ClickedActor, const TArray<AActor*>& ActorList);
	void ToggleSelect(AActor* Actor);
	void Deselect(AActor* Actor);
	void ClearSelection();

	bool IsSelected(AActor* Actor) const
	{
		return std::find(SelectedActors.begin(), SelectedActors.end(), Actor) != SelectedActors.end();
	}

	AActor* GetPrimarySelection() const;

	const TArray<AActor*>& GetSelectedActors() const { return SelectedActors; }
	bool IsEmpty() const { return SelectedActors.empty(); }

	FTransformGizmo* GetGizmo() const { return Gizmo.get(); }

private:
	void SyncGizmo();

	TArray<AActor*> SelectedActors;
	AActor* PrimarySelection = nullptr;
	std::unique_ptr<FTransformGizmo> Gizmo;
};
