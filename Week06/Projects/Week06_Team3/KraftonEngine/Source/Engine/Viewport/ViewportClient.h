#pragma once

#include "Core/CoreTypes.h"
#include "Engine/Input/InputTypes.h"

class FViewport;

// UE의 FViewportClient 대응 — 뷰포트 입력/드로잉 인터페이스
class FViewportClient
{
public:
	FViewportClient() = default;
	virtual ~FViewportClient() = default;

	virtual void Draw(FViewport* Viewport, float DeltaTime) {}
	virtual bool InputKey(int32 Key, bool bPressed) { return false; }
	virtual bool InputAxis(float DeltaX, float DeltaY) { return false; }
	virtual bool ProcessInput(FViewportInputContext& Context) { (void)Context; return false; }
	virtual bool WantsRelativeMouseMode(const FViewportInputContext& Context, POINT& OutRestoreScreenPos) const
	{
		(void)Context;
		OutRestoreScreenPos = { 0, 0 };
		return false;
	}
	virtual bool WantsAbsoluteMouseClip(const FViewportInputContext& Context, RECT& OutClipScreenRect) const
	{
		(void)Context;
		OutClipScreenRect = { 0, 0, 0, 0 };
		return false;
	}
};
