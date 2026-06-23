#pragma once

#include "Viewport/ViewportClient.h"
#include "Math/Vector.h"
#include "Editor/Viewport/ViewportCamera.h"
#include "ObjViewer/ObjViewerViewportController.h"
#include <memory>

class FWindowsWindow;
class FViewport;
struct FRect;

// ObjViewer용 간이 뷰포트 클라이언트 — 마우스 오빗/줌/팬
class FObjViewerViewportClient : public FViewportClient
{
	friend class FObjViewerViewportController;
	friend class FObjViewerNavigationTool;
	friend class FObjViewerCommandInputContext;
	friend class FObjViewerNavigationInputContext;

public:
	void Initialize(FWindowsWindow* InWindow);
	void Release();

	// Camera lifecycle
	void CreateCamera();
	void DestroyCamera();
	void ResetCamera();
	FViewportCamera* GetCamera() const { return Camera.get(); }

	// Viewport
	void SetViewport(FViewport* InViewport) { Viewport = InViewport; }
	FViewport* GetViewport() const { return Viewport; }
	bool ProcessInput(FViewportInputContext& Context) override;
	bool WantsRelativeMouseMode(const FViewportInputContext& Context, POINT& OutRestoreScreenPos) const override;

	void Tick(float DeltaTime);

	// 뷰포트 영역 설정 (ImGui 패널에서 호출)
	void SetViewportRect(float X, float Y, float Width, float Height);

	// ImDrawList에 SRV를 그려주는 헬퍼
	void RenderViewportImage();
	bool GetViewportRect(FRect& OutRect) const;

private:
	void EnsureInputController();
	void EnsureInputContextStack();

private:
	FViewport* Viewport = nullptr;
	FWindowsWindow* Window = nullptr;
	std::unique_ptr<FViewportCamera> Camera;

	// 오빗 파라미터
	FVector OrbitTarget = FVector(0, 0, 0);
	float OrbitDistance = 5.0f;
	float OrbitYaw = 0.0f;		// degrees
	float OrbitPitch = 30.0f;	// degrees

	// 뷰포트 스크린 영역
	float ViewportX = 0.0f;
	float ViewportY = 0.0f;
	float ViewportWidth = 800.0f;
	float ViewportHeight = 600.0f;
	bool bHasInputContext = false;
	FViewportInputContext InputContext;
	float DispatchDeltaTime = 0.0f;
	std::unique_ptr<FObjViewerViewportController> InputController;
	bool bInputContextStackInitialized = false;
	TArray<IInputContext*> InputContextStack;
	std::unique_ptr<IInputContext> CommandInputContext;
	std::unique_ptr<IInputContext> NavigationInputContext;
};
