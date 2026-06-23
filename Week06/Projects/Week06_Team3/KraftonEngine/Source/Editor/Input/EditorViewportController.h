#pragma once

#include "Editor/Input/EditorNavigationTool.h"
#include "Editor/Input/EditorViewportCommandTool.h"
#include "Editor/Input/EditorViewportModes.h"

#include <windows.h>
#include <memory>

class FEditorViewportClient;

class FEditorViewportController
{
public:
	explicit FEditorViewportController(FEditorViewportClient* InOwner);

	bool SetMode(EEditorViewportModeType InModeType);
	EEditorViewportModeType GetMode() const;
	bool CycleMode();

	bool HandleViewportCommandInput(float DeltaTime);
	bool HandleGizmoInput(float DeltaTime);
	bool HandleSelectionInput(float DeltaTime);
	bool HandleNavigationInput(float DeltaTime);
	bool GetSelectionMarquee(POINT& OutStart, POINT& OutCurrent, bool& bOutAdditive) const;
	FEditorNavigationTool* GetNavigationTool() const { return NavigationTool.get(); }

private:
	FEditorViewportClient* Owner = nullptr;
	std::unique_ptr<IEditorViewportMode> ActiveMode;
	std::unique_ptr<FEditorViewportCommandTool> ViewportCommandTool;
	std::unique_ptr<FEditorNavigationTool> NavigationTool;
};
