#include "ObjViewer/ObjViewerViewportClient.h"

#include "ObjViewer/ObjViewerInputMapping.h"
#include "Engine/Runtime/WindowsWindow.h"
#include "Editor/Viewport/ViewportCamera.h"
#include "UI/SWindow.h"
#include "Viewport/Viewport.h"
#include "Math/MathUtils.h"
#include "ImGui/imgui.h"

#include <cmath>

class FObjViewerCommandInputContext final : public IInputContext
{
public:
	FObjViewerCommandInputContext(FObjViewerViewportClient* InOwner, float* InDeltaTime)
		: Owner(InOwner), DeltaTimePtr(InDeltaTime)
	{
	}

	bool HandleInput(FViewportInputContext& Context) override
	{
		(void)Context;
		if (!Owner || !DeltaTimePtr)
		{
			return false;
		}
		Owner->EnsureInputController();
		return Owner->InputController ? Owner->InputController->HandleCommandInput(*DeltaTimePtr) : false;
	}

private:
	FObjViewerViewportClient* Owner = nullptr;
	float* DeltaTimePtr = nullptr;
};

class FObjViewerNavigationInputContext final : public IInputContext
{
public:
	FObjViewerNavigationInputContext(FObjViewerViewportClient* InOwner, float* InDeltaTime)
		: Owner(InOwner), DeltaTimePtr(InDeltaTime)
	{
	}

	bool HandleInput(FViewportInputContext& Context) override
	{
		(void)Context;
		if (!Owner || !DeltaTimePtr)
		{
			return false;
		}
		Owner->EnsureInputController();
		return Owner->InputController ? Owner->InputController->HandleNavigationInput(*DeltaTimePtr) : false;
	}

private:
	FObjViewerViewportClient* Owner = nullptr;
	float* DeltaTimePtr = nullptr;
};

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
	Camera = std::make_unique<FViewportCamera>();
}

void FObjViewerViewportClient::DestroyCamera()
{
	Camera.reset();
}

void FObjViewerViewportClient::ResetCamera()
{
	OrbitTarget = FVector(0, 0, 0);
	OrbitDistance = 5.0f;
	OrbitYaw = 0.0f;
	OrbitPitch = 30.0f;
}

static void UpdateOrbitCamera(FViewportCamera* Camera, const FVector& Target, float Distance, float Yaw, float Pitch)
{
	float YawRad = Yaw * DEG_TO_RAD;
	float PitchRad = Pitch * DEG_TO_RAD;

	float CosPitch = cosf(PitchRad);
	FVector Offset;
	Offset.X = Distance * CosPitch * cosf(YawRad);
	Offset.Y = Distance * CosPitch * sinf(YawRad);
	Offset.Z = Distance * sinf(PitchRad);

	Camera->SetWorldLocation(Target + Offset);
	Camera->LookAt(Target);
}

void FObjViewerViewportClient::Tick(float DeltaTime)
{
	if (!bHasInputContext)
	{
		return;
	}

	DispatchDeltaTime = DeltaTime;
	EnsureInputController();
	EnsureInputContextStack();
	for (IInputContext* Context : InputContextStack)
	{
		if (Context && Context->HandleInput(InputContext))
		{
			break;
		}
	}

	if (Camera)
	{
		UpdateOrbitCamera(Camera.get(), OrbitTarget, OrbitDistance, OrbitYaw, OrbitPitch);
	}

	bHasInputContext = false;
}

bool FObjViewerViewportClient::ProcessInput(FViewportInputContext& Context)
{
	InputContext = Context;
	bHasInputContext = true;
	return false;
}

bool FObjViewerViewportClient::WantsRelativeMouseMode(const FViewportInputContext& Context, POINT& OutRestoreScreenPos) const
{
	OutRestoreScreenPos = Context.Frame.MouseScreenPos;

	if (Context.bImGuiCapturedMouse || !Camera)
	{
		return false;
	}

	const bool bMouseOwnedByViewport = Context.bCaptured || Context.bHovered || Context.bRelativeMouseMode;
	if (!bMouseOwnedByViewport)
	{
		return false;
	}

	return ObjViewerInputMapping::IsTriggered(Context, ObjViewerInputMapping::EObjViewerAction::LookRightDown)
		|| ObjViewerInputMapping::IsTriggered(Context, ObjViewerInputMapping::EObjViewerAction::PanMiddleDown);
}

void FObjViewerViewportClient::EnsureInputController()
{
	if (!InputController)
	{
		InputController = std::make_unique<FObjViewerViewportController>(this);
	}
}

void FObjViewerViewportClient::EnsureInputContextStack()
{
	if (bInputContextStackInitialized)
	{
		return;
	}

	CommandInputContext = std::make_unique<FObjViewerCommandInputContext>(this, &DispatchDeltaTime);
	NavigationInputContext = std::make_unique<FObjViewerNavigationInputContext>(this, &DispatchDeltaTime);
	InputContextStack.clear();
	InputContextStack.push_back(CommandInputContext.get());
	InputContextStack.push_back(NavigationInputContext.get());
	bInputContextStackInitialized = true;
}

void FObjViewerViewportClient::SetViewportRect(float X, float Y, float Width, float Height)
{
	ViewportX = X;
	ViewportY = Y;
	ViewportWidth = Width;
	ViewportHeight = Height;

	// FViewport 리사이즈 요청
	if (Viewport)
	{
		uint32 W = static_cast<uint32>(Width);
		uint32 H = static_cast<uint32>(Height);
		if (W > 0 && H > 0 && (W != Viewport->GetWidth() || H != Viewport->GetHeight()))
		{
			Viewport->RequestResize(W, H);
		}
	}
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

bool FObjViewerViewportClient::GetViewportRect(FRect& OutRect) const
{
	if (ViewportWidth <= 0.0f || ViewportHeight <= 0.0f)
	{
		return false;
	}

	OutRect.X = ViewportX;
	OutRect.Y = ViewportY;
	OutRect.Width = ViewportWidth;
	OutRect.Height = ViewportHeight;
	return true;
}
