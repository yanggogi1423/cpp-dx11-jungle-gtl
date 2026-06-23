#pragma once

#include "Core/CoreTypes.h"
#include <d3d11.h>

class SSplitter;
class SWindow;
class FLevelEditorViewportClient;
class FEditorViewportClient;
class FSelectionManager;
class FEditorSettings;
class FWindowsWindow;
class FRenderer;
class UWorld;
class UEditorEngine;
class FLevelViewportLayoutUI;

// 뷰포트 레이아웃 종류 (12가지, UE 동일)
enum class EViewportLayout : uint8
{
	OnePane,
	TwoPanesHoriz,
	TwoPanesVert,
	ThreePanesLeft,
	ThreePanesRight,
	ThreePanesTop,
	ThreePanesBottom,
	FourPanes2x2,
	FourPanesLeft,
	FourPanesRight,
	FourPanesTop,
	FourPanesBottom,

	MAX
};

// 뷰포트 레이아웃 관리 — SSplitter 트리와 SWindow 리프를 소유하며
// LevelViewportClient 생성/배치/레이아웃 전환을 담당
class FLevelViewportLayout
{
public:
	static constexpr int32 MaxViewportSlots = 4;

	FLevelViewportLayout() = default;
	~FLevelViewportLayout() = default;

	void Initialize(UEditorEngine* InEditor, FWindowsWindow* InWindow, FRenderer& InRenderer,
		FSelectionManager* InSelectionManager);
	void Release();

	// FEditorSettings ↔ 뷰포트 상태 동기화
	void SaveToSettings();
	void LoadFromSettings();

	// 레이아웃 전환
	void SetLayout(EViewportLayout NewLayout);
	void SetLayoutAnimated(EViewportLayout NewLayout);
	EViewportLayout GetLayout() const { return CurrentLayout; }

	// 편의용 토글 (OnePane ↔ FourPanes2x2)
	void ToggleViewportSplit();
	bool IsSplitViewport() const { return CurrentLayout != EViewportLayout::OnePane; }

	// ImGui "Viewport" 창에 레이아웃 계산 + 렌더
	void RenderViewportUI(float DeltaTime);

	bool IsMouseOverViewport() const { return bMouseOverViewport; }

	const TArray<FEditorViewportClient*>& GetAllViewportClients() const { return AllViewportClients; }
	const TArray<FLevelEditorViewportClient*>& GetLevelViewportClients() const { return LevelViewportClients; }

	void SetActiveViewport(FLevelEditorViewportClient* InClient);
	FLevelEditorViewportClient* GetActiveViewport() const { return ActiveViewportClient; }

	void SetWorld(UWorld* InWorld);
	void ResetViewport(UWorld* InWorld);
	void DestroyAllCameras();
	void BeginPIEViewportMode();
	void EndPIEViewportMode();

	static int32 GetSlotCount(EViewportLayout Layout);
	
	FRenderer * GetRenderer() { return RendererPtr; }

private:
	friend class FLevelViewportLayoutUI;

	enum class ELayoutTransitionState : uint8
	{
		None,
		CollapsingCurrent,
		ExpandingTarget
	};

	void StartAnimatedLayoutTransition(EViewportLayout NewLayout);
	void TickLayoutTransition(float DeltaTime);
	void BeginCurrentLayoutCollapsePhase();
	void BeginTargetLayoutExpandPhase();
	void EndLayoutTransition();

	int32 GetActiveSlotIndex() const;
	bool DoesWindowContainSlot(const SWindow* InWindow, int32 SlotIndex) const;
	void ApplyFocusCollapseRecursive(SSplitter* InNode, int32 FocusSlotIndex);
	void CollectSplitterRatios(TArray<float>& OutRatios) const;
	void ApplySplitterRatios(const TArray<float>& InRatios);

	SSplitter* BuildSplitterTree(EViewportLayout Layout);
	void EnsureViewportSlots(int32 RequiredCount);
	void ShrinkViewportSlots(int32 RequiredCount);
	void RenderPaneToolbar(int32 SlotIndex);
	void RenderActiveViewportStatOverlay();

	// 아이콘 텍스처
	void LoadLayoutIcons(ID3D11Device* Device);
	void ReleaseLayoutIcons();

	UEditorEngine* Editor = nullptr;
	FWindowsWindow* Window = nullptr;
	FRenderer* RendererPtr = nullptr;
	FSelectionManager* SelectionManager = nullptr;

	EViewportLayout CurrentLayout = EViewportLayout::OnePane;

	TArray<FEditorViewportClient*> AllViewportClients;
	TArray<FLevelEditorViewportClient*> LevelViewportClients;
	FLevelEditorViewportClient* ActiveViewportClient = nullptr;

	SSplitter* RootSplitter = nullptr;
	SWindow* ViewportWindows[MaxViewportSlots] = {};
	int32 ActiveSlotCount = 1;

	SSplitter* DraggingSplitter = nullptr;
	bool bMouseOverViewport = false;
	bool bPIEViewportMode = false;
	FLevelEditorViewportClient* PIEFocusedViewportClient = nullptr;

	// 레이아웃 아이콘 SRV (EViewportLayout::MAX 개)
	ID3D11ShaderResourceView* LayoutIcons[static_cast<int>(EViewportLayout::MAX)] = {};

	ELayoutTransitionState LayoutTransitionState = ELayoutTransitionState::None;
	EViewportLayout PendingTargetLayout = EViewportLayout::OnePane;
	EViewportLayout LastSplitLayout = EViewportLayout::FourPanes2x2;
	bool bSuppressLastSplitLayoutUpdate = false;
	bool bUseCoverTransitionToOnePane = false;
	bool bUseCoverTransitionFromOnePane = false;
	bool bRequestPreserveSplitOnOnePane = false;
	bool bIsTemporaryOnePane = false;
	int32 TemporaryOnePaneSourceSlot = 0;
	int32 TransitionFocusSlot = 0;
	float LayoutTransitionElapsed = 0.0f;
	float LayoutTransitionDuration = 0.18f;
	TArray<float> TransitionStartRatios;
	TArray<float> TransitionTargetRatios;
};
