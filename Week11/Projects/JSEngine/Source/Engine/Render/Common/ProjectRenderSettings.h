#pragma once

#include "Core/CoreTypes.h"
#include "Render/Common/SkinningTypes.h"

struct FProjectRenderSettings
{
	static ESkinningMode SkinningMode;

	static ESkinningMode GetSkinningMode() { return SkinningMode; }
	static bool IsGPUSkinningEnabled() { return SkinningMode == ESkinningMode::GPU; }
	static void SetSkinningMode(ESkinningMode Mode) { SkinningMode = Mode; }
};
