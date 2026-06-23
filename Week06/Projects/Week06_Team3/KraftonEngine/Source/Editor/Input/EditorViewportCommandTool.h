#pragma once

#include "Editor/Input/EditorViewportTools.h"
#include "Editor/Input/EditorViewportModes.h"

class FEditorViewportClient;
class FEditorViewportController;

class FEditorViewportCommandTool final : public IEditorViewportTool
{
public:
	FEditorViewportCommandTool(FEditorViewportClient* InOwner, FEditorViewportController* InController);
	bool HandleInput(float DeltaTime) override;

private:
	bool FocusPrimarySelection();
	bool SetMode(EEditorViewportModeType InModeType);
	bool DeleteSelectedActors();
	bool SelectAllActors();
	bool NewScene();
	bool LoadScene();
	bool SaveScene();
	bool SaveSceneAs();
	bool DuplicateSelection();
	bool TryCycleGizmoMode();
	bool TryToggleCoordinateSpace();

	FEditorViewportClient* Owner = nullptr;
	FEditorViewportController* Controller = nullptr;
};

