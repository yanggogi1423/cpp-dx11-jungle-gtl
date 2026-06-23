#pragma once

#include "Editor/Input/EditorViewportTools.h"

class FEditorViewportClient;

class FEditorGizmoTool final : public IEditorViewportTool
{
public:
	explicit FEditorGizmoTool(FEditorViewportClient* InOwner);
	bool HandleInput(float DeltaTime) override;

private:
	FEditorViewportClient* Owner = nullptr;
};

