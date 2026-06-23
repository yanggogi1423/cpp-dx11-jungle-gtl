#include "ObjViewer/ObjViewerViewportClient.h"

#include "Engine/Runtime/WindowsWindow.h"
#include "Components/CameraComponent.h"
#include "Viewport/Viewport.h"
#include "Math/MathUtils.h"
#include "ImGui/imgui.h"

#include <cmath>

void FObjViewerViewportClient::Initialize(FWindowsWindow* InWindow)
{
	Window = InWindow;
}

void FObjViewerViewportClient::Release()
{
	DestroyCamera();
	if (Viewport)
	{
		Viewport->Release();
		delete Viewport;
		Viewport = nullptr;
	}
}

void FObjViewerViewportClient::CreateCamera()
{
	DestroyCamera();
	Camera = UObjectManager::Get().CreateObject<UCameraComponent>();
}

void FObjViewerViewportClient::DestroyCamera()
{
	if (Camera)
	{
		UObjectManager::Get().DestroyObject(Camera);
		Camera = nullptr;
	}
}

void FObjViewerViewportClient::ResetCamera()
{
	OrbitTarget = FVector(0, 0, 0);
	OrbitDistance = 5.0f;
	OrbitYaw = 0.0f;
	OrbitPitch = 30.0f;
}

static void UpdateOrbitCamera(UCameraComponent* Camera, const FVector& Target, float Distance, float Yaw, float Pitch)
{
	const float YawRad = Yaw * DEG_TO_RAD;
	const float PitchRad = Pitch * DEG_TO_RAD;

	const float CosPitch = cosf(PitchRad);
	FVector Offset;
	Offset.X = Distance * CosPitch * cosf(YawRad);
	Offset.Y = Distance * CosPitch * sinf(YawRad);
	Offset.Z = Distance * sinf(PitchRad);

	Camera->SetWorldLocation(Target + Offset);
	Camera->LookAt(Target);
}

void FObjViewerViewportClient::Tick(float DeltaTime)
{
	TickInput(DeltaTime);

	if (Camera)
	{
		UpdateOrbitCamera(Camera, OrbitTarget, OrbitDistance, OrbitYaw, OrbitPitch);
	}

	bHasRoutedInputContext = false;
}

void FObjViewerViewportClient::TickInput(float DeltaTime)
{
	(void)DeltaTime;
	if (!Camera || !bHasRoutedInputContext)
	{
		return;
	}

	const FViewportInputContext& Context = RoutedInputContext;
	if (!Context.bHovered && !Context.bCaptured && !Context.bRelativeMouseMode)
	{
		return;
	}

	if (Context.Frame.IsDown(VK_RBUTTON))
	{
		const float DeltaX = static_cast<float>(Context.Frame.MouseDelta.x);
		const float DeltaY = static_cast<float>(Context.Frame.MouseDelta.y);

		OrbitYaw += DeltaX * 0.3f;
		OrbitPitch += DeltaY * 0.3f;
		OrbitPitch = Clamp(OrbitPitch, -89.0f, 89.0f);
	}

	if (Context.Frame.IsDown(VK_MBUTTON))
	{
		const float DeltaX = static_cast<float>(Context.Frame.MouseDelta.x);
		const float DeltaY = static_cast<float>(Context.Frame.MouseDelta.y);

		const float PanScale = OrbitDistance * 0.002f;
		const FVector Right = Camera->GetRightVector();
		const FVector Up = Camera->GetUpVector();
		OrbitTarget = OrbitTarget - Right * (DeltaX * PanScale) + Up * (DeltaY * PanScale);
	}

	const float ScrollNotches = Context.Frame.WheelNotches;
	if (ScrollNotches != 0.0f)
	{
		OrbitDistance -= ScrollNotches * OrbitDistance * 0.1f;
		OrbitDistance = Clamp(OrbitDistance, 0.1f, 500.0f);
	}
}

bool FObjViewerViewportClient::ProcessInput(FViewportInputContext& Context)
{
	RoutedInputContext = Context;
	bHasRoutedInputContext = true;
	return false;
}

bool FObjViewerViewportClient::WantsRelativeMouseMode(const FViewportInputContext& Context, POINT& OutRestoreScreenPos) const
{
	OutRestoreScreenPos = Context.Frame.MouseScreenPos;
	if (Context.bImGuiCapturedMouse)
	{
		return false;
	}

	return Context.bFocused
		&& Context.bHovered
		&& (Context.Frame.IsDown(VK_RBUTTON) || Context.Frame.IsDown(VK_MBUTTON));
}

bool FObjViewerViewportClient::WantsAbsoluteMouseClip(const FViewportInputContext& Context, RECT& OutClipScreenRect) const
{
	OutClipScreenRect = { 0, 0, 0, 0 };
	const bool bDragControlActive = Context.bFocused
		&& (Context.Frame.IsDown(VK_RBUTTON) || Context.Frame.IsDown(VK_MBUTTON));
	if (!bDragControlActive)
	{
		return false;
	}

	const FRect Rect = GetViewportScreenRect();
	if (Rect.Width <= 0.0f || Rect.Height <= 0.0f)
	{
		return false;
	}

	OutClipScreenRect.left = static_cast<LONG>(Rect.X);
	OutClipScreenRect.top = static_cast<LONG>(Rect.Y);
	OutClipScreenRect.right = static_cast<LONG>(Rect.X + Rect.Width);
	OutClipScreenRect.bottom = static_cast<LONG>(Rect.Y + Rect.Height);
	return true;
}

void FObjViewerViewportClient::SetViewportRect(float X, float Y, float Width, float Height)
{
	ViewportX = X;
	ViewportY = Y;
	ViewportWidth = Width;
	ViewportHeight = Height;

	if (Viewport)
	{
		const uint32 W = static_cast<uint32>(Width);
		const uint32 H = static_cast<uint32>(Height);
		if (W > 0 && H > 0 && (W != Viewport->GetWidth() || H != Viewport->GetHeight()))
		{
			Viewport->RequestResize(W, H);
		}
	}
}

FRect FObjViewerViewportClient::GetViewportScreenRect() const
{
	FRect Rect{};
	Rect.X = ViewportX;
	Rect.Y = ViewportY;
	Rect.Width = ViewportWidth;
	Rect.Height = ViewportHeight;
	return Rect;
}

void FObjViewerViewportClient::RenderViewportImage()
{
	if (!Viewport || !Viewport->GetSRV()) return;
	if (ViewportWidth <= 0 || ViewportHeight <= 0) return;

	ImDrawList* DrawList = ImGui::GetWindowDrawList();
	ImVec2 Min(ViewportX, ViewportY);
	ImVec2 Max(ViewportX + ViewportWidth, ViewportY + ViewportHeight);

	DrawList->AddImage((ImTextureID)Viewport->GetSRV(), Min, Max);
}
