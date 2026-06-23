#pragma once

#include "Core/CoreTypes.h"

class IEditorViewportTool
{
public:
	virtual ~IEditorViewportTool() = default;
	virtual bool HandleInput(float DeltaTime) = 0;
};
