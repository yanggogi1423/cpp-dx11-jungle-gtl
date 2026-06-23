#pragma once

#include "Core/CoreTypes.h"
#include "Mesh/ObjImporter.h"

class FRenderer;
class FWindowsWindow;
class UObjViewerEngine;

// ObjViewer ImGui 패널 — Obj 목록 + 3D 프리뷰 뷰포트
class FObjViewerPanel
{
public:
	void Create(FWindowsWindow* InWindow, FRenderer& InRenderer, UObjViewerEngine* InEngine);
	void Release();
	void Render(float DeltaTime);
	void Update();
	bool IsCapturingMouse() const { return bWantCaptureMouse; }
	bool IsCapturingKeyboard() const { return bWantCaptureKeyboard; }

private:
	void RenderMeshList();
	void RenderPreviewViewport(float DeltaTime);
	void RenderImportPopup();

private:
	FWindowsWindow* Window = nullptr;
	UObjViewerEngine* Engine = nullptr;

	int32 SelectedMeshIndex = -1;
	int32 SelectedObjIndex = -1;
	bool bShowImportPopup = false;
	FImportOptions PendingImportOptions;
	bool bWantCaptureMouse = false;
	bool bWantCaptureKeyboard = false;
	bool bPreviewViewportHovered = false;
};
