#pragma once

#include "Editor/UI/EditorConsoleWidget.h"
#include "Editor/UI/EditorControlWidget.h"
#include "Editor/Settings/EditorSettings.h"
#include "Editor/UI/EditorFooterLogSystem.h"
#include "Editor/UI/EditorPropertyWidget.h"
#include "Editor/UI/EditorSceneWidget.h"
#include "Editor/UI/EditorStatWidget.h"
#include "Math/Vector.h"
#include "Profiling/Stats.h"

class FRenderer;
class UEditorEngine;
class FWindowsWindow;
class AActor;
struct ID3D11ShaderResourceView;

class FEditorMainPanel
{
public:
	void Create(FWindowsWindow* InWindow, FRenderer& InRenderer, UEditorEngine* InEditorEngine);
	void Release();
	void Render(float DeltaTime);
	void Update();
	void HideEditorWindowsForPIE();
	void RestoreEditorWindowsAfterPIE();

private:
	void UpdateFooterEventLogs();
	void ProcessPendingDebugActions();
	void RenderMainMenuBar();
	void RenderEditorToolbar();
	void RenderDockSpace();
	void RenderEditorDebugPanel();
	void RenderShortcutOverlay();
	void RenderStatOverlay();
	void RenderFooterOverlay(float DeltaTime);
	void RenderConsoleDrawer(float DeltaTime);
#if STATS
	void RenderHiZDebug(const class FEditorSettings& Settings);
#endif

	FWindowsWindow* Window = nullptr;
	UEditorEngine* EditorEngine = nullptr;
	FEditorConsoleWidget ConsoleWidget;
	FEditorControlWidget ControlWidget;
	FEditorPropertyWidget PropertyWidget;
	FEditorSceneWidget SceneWidget;
	FEditorStatWidget StatWidget;
	FEditorFooterLogSystem FooterLogSystem;
	bool bShowWidgetList = false;
	bool bShowShortcutOverlay = false;
	bool bShowEditorDebugPanel = false;
	bool bConsoleDrawerVisible = false;
	bool bBringConsoleDrawerToFrontNextFrame = false;
	bool bFocusConsoleInputNextFrame = false;
	bool bFocusConsoleButtonNextFrame = false;
	int32 ConsoleBacktickCycleState = 0;
	float ConsoleDrawerAnim = 0.0f;
	bool bHideEditorWindows = false;
	bool bHasSavedUIVisibility = false;
	bool bSavedShowWidgetList = false;
	FEditorSettings::FUIVisibility SavedUIVisibility{};
	bool bFooterEventStateInitialized = false;
	bool bPrevPIEPlaying = false;
	int32 PrevPIEControlMode = -1;
	bool bPrevHadLevelPath = false;
	FString PrevLevelPath;
	int32 DebugPlaceActorTypeIndex = 0;
	int32 DebugGridRows = 10;
	int32 DebugGridCols = 10;
	int32 DebugGridLayers = 1;
	float DebugGridSpacing = 2.0f;
	bool bDebugGridCenter = true;
	bool bDebugUseCameraOrigin = true;
	float DebugCameraForwardDistance = 30.0f;
	FVector DebugManualGridOrigin = FVector(0.0f, 0.0f, 0.0f);
	bool bDebugRandomYaw = false;
	float DebugRandomYawRange = 180.0f;
	bool bDebugApplyJitter = false;
	float DebugJitterXY = 0.0f;
	float DebugJitterZ = 0.0f;
	TArray<AActor*> DebugLastSpawnedActors;
	bool bPendingClearLastBatch = false;
	ID3D11ShaderResourceView* AddActorIconSRV = nullptr;
};
