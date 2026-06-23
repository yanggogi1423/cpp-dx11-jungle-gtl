#pragma once

#include "Editor/UI/EditorWidget.h"
#include "Core/CoreTypes.h"

class FEditorLevelWidget : public FEditorWidget
{
public:
	virtual void Initialize(UEditorEngine* InEditorEngine) override;
	virtual void Render(float DeltaTime) override;

private:
	float NewLevelNotificationTimer = 0.f;
};
