#pragma once

class IEditorViewportTool
{
public:
	virtual ~IEditorViewportTool() = default;
	virtual bool HandleInput(float DeltaTime) = 0;
};

