#pragma once

#include "Core/CoreTypes.h"

class FLevelViewportLayout;

class FLevelViewportLayoutUI
{
public:
	static void RenderViewportUI(FLevelViewportLayout& Layout, float DeltaTime);
	static void RenderActiveViewportStatOverlay(FLevelViewportLayout& Layout);
	static void RenderPaneToolbar(FLevelViewportLayout& Layout, int32 SlotIndex);
	static void ReleaseResources();
};
