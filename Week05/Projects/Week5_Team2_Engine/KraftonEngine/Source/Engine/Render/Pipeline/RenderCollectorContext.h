#pragma once

#include "Render/Types/ViewTypes.h"

class FViewportCamera;

struct FRenderCollectorContext
{
	FViewportCamera* Camera = nullptr;
	EViewMode ViewMode = EViewMode::Lit;
	FShowFlags ShowFlags;
};
