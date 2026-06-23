#pragma once

#include "Editor/UI/EditorConsoleWidget.h"
#include "Editor/UI/EditorControlWidget.h"
#include "Editor/UI/EditorPropertyWidget.h"
#include "Editor/UI/EditorLevelWidget.h"
#include "Editor/UI/EditorStatWidget.h"

class FRenderer;
class UEditorEngine;
class FWindowsWindow;
struct ImFont;
struct ID3D11ShaderResourceView;

class FEditorMainPanel
{
public:
	void Create(FWindowsWindow* InWindow, FRenderer& InRenderer, UEditorEngine* InEditorEngine);
	void Release();
	void Render(float DeltaTime);
	void Update();
	bool IsCapturingMouse() const { return bWantCaptureMouse; }
	bool IsCapturingKeyboard() const { return bWantCaptureKeyboard; }

private:
	void RenderMainMenuBar();
	void RenderEditorToolbar();
	void RenderDockSpace();
	void RenderEditorDebugPanel();
	void RenderShortcutOverlay();
	void RenderFooterOverlay(float DeltaTime);
	void RenderConsoleDrawer();

private:
	FWindowsWindow* Window = nullptr;
	UEditorEngine* EditorEngine = nullptr;
	FEditorConsoleWidget ConsoleWidget;
	FEditorControlWidget ControlWidget;
	FEditorPropertyWidget PropertyWidget;
	FEditorLevelWidget LevelWidget;
	FEditorStatWidget StatWidget;
	
	bool bConsoleDrawerVisible = false;
	bool bBringConsoleDrawerToFrontNextFrame = false;
	bool bFocusConsoleInputNextFrame = false;
	bool bFocusConsoleButtonNextFrame = false;
	int32 ConsoleBacktickCycleState = 0; // 0: none, 1: input focus, 2: drawer open + input focus
	
	bool bShowControlPanel = true;
	bool bShowLevelPanel = true;
	bool bShowPropertyPanel = true;
	bool bShowEditorDebugPanel = false;
	bool bShowStatPanel = false;
	bool bShowShortcutOverlay = false;
	bool bWantCaptureMouse = false;
	bool bWantCaptureKeyboard = false;
	float ConsoleDrawerAnim = 0.0f;
	
	ImFont* FooterFont = nullptr;
	ImFont* FooterBoldFont = nullptr;
	ID3D11ShaderResourceView* AddActorIconSRV = nullptr;
};
