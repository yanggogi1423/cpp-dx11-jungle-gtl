#include "Editor/Viewport/FLevelViewportLayout.h"

#include "Editor/EditorEngine.h"
#include "Editor/Viewport/FLevelViewportLayoutUI.h"
#include "Editor/Viewport/LevelEditorViewportClient.h"
#include "Editor/Settings/EditorSettings.h"
#include "Editor/Selection/SelectionManager.h"
#include "Editor/Viewport/ViewportCamera.h"
#include "Engine/Runtime/WindowsWindow.h"
#include "Render/Pipeline/Renderer.h"
#include "Viewport/Viewport.h"
#include "UI/SSplitter.h"
#include "Math/MathUtils.h"
#include "Platform/Paths.h"
#include "Editor/Gizmo/TransformGizmo.h"
#include "ImGui/imgui.h"
#include "WICTextureLoader.h"

// ─── 레이아웃별 슬롯 수 ─────────────────────────────────────

int32 FLevelViewportLayout::GetSlotCount(EViewportLayout Layout)
{
	switch (Layout)
	{
	case EViewportLayout::OnePane:          return 1;
	case EViewportLayout::TwoPanesHoriz:
	case EViewportLayout::TwoPanesVert:     return 2;
	case EViewportLayout::ThreePanesLeft:
	case EViewportLayout::ThreePanesRight:
	case EViewportLayout::ThreePanesTop:
	case EViewportLayout::ThreePanesBottom: return 3;
	default:                                return 4;
	}
}

// ─── 아이콘 파일명 매핑 ──────────────────────────────────────

static const wchar_t* GetLayoutIconFileName(EViewportLayout Layout)
{
	switch (Layout)
	{
	case EViewportLayout::OnePane:          return L"ViewportLayout_OnePane.png";
	case EViewportLayout::TwoPanesHoriz:   return L"ViewportLayout_TwoPanesHoriz.png";
	case EViewportLayout::TwoPanesVert:    return L"ViewportLayout_TwoPanesVert.png";
	case EViewportLayout::ThreePanesLeft:  return L"ViewportLayout_ThreePanesLeft.png";
	case EViewportLayout::ThreePanesRight: return L"ViewportLayout_ThreePanesRight.png";
	case EViewportLayout::ThreePanesTop:   return L"ViewportLayout_ThreePanesTop.png";
	case EViewportLayout::ThreePanesBottom:return L"ViewportLayout_ThreePanesBottom.png";
	case EViewportLayout::FourPanes2x2:    return L"ViewportLayout_FourPanes2x2.png";
	case EViewportLayout::FourPanesLeft:   return L"ViewportLayout_FourPanesLeft.png";
	case EViewportLayout::FourPanesRight:  return L"ViewportLayout_FourPanesRight.png";
	case EViewportLayout::FourPanesTop:    return L"ViewportLayout_FourPanesTop.png";
	case EViewportLayout::FourPanesBottom: return L"ViewportLayout_FourPanesBottom.png";
	default:                               return L"";
	}
}

// ─── 아이콘 로드/해제 ────────────────────────────────────────

void FLevelViewportLayout::LoadLayoutIcons(ID3D11Device* Device)
{
	if (!Device) return;

	std::wstring IconDir = FPaths::Combine(FPaths::RootDir(), L"Asset/Editor/Icon/");

	for (int32 i = 0; i < static_cast<int32>(EViewportLayout::MAX); ++i)
	{
		std::wstring Path = IconDir + GetLayoutIconFileName(static_cast<EViewportLayout>(i));
		DirectX::CreateWICTextureFromFile(
			Device, Path.c_str(),
			nullptr, &LayoutIcons[i]);
	}
}

void FLevelViewportLayout::ReleaseLayoutIcons()
{
	for (int32 i = 0; i < static_cast<int32>(EViewportLayout::MAX); ++i)
	{
		if (LayoutIcons[i])
		{
			LayoutIcons[i]->Release();
			LayoutIcons[i] = nullptr;
		}
	}
}

// ─── Initialize / Release ────────────────────────────────────

void FLevelViewportLayout::Initialize(UEditorEngine* InEditor, FWindowsWindow* InWindow, FRenderer& InRenderer,
	FSelectionManager* InSelectionManager)
{
	Editor = InEditor;
	Window = InWindow;
	RendererPtr = &InRenderer;
	SelectionManager = InSelectionManager;

	// 아이콘 로드
	LoadLayoutIcons(InRenderer.GetFD3DDevice().GetDevice());

	// LevelViewportClient 생성 (단일 뷰포트)
	auto* LevelVC = new FLevelEditorViewportClient();
	LevelVC->SetSettings(&FEditorSettings::Get());
	LevelVC->Initialize(Window);
	LevelVC->SetViewportSize(Window->GetWidth(), Window->GetHeight());
	LevelVC->SetWorld(Editor->GetWorld());
	LevelVC->SetGizmo(SelectionManager->GetGizmo());
	LevelVC->SetSelectionManager(SelectionManager);

	auto* VP = new FViewport();
	VP->Initialize(InRenderer.GetFD3DDevice().GetDevice(),
		static_cast<uint32>(Window->GetWidth()),
		static_cast<uint32>(Window->GetHeight()));
	VP->SetClient(LevelVC);
	LevelVC->SetViewport(VP);

	LevelVC->CreateCamera();
	LevelVC->ResetCamera();

	AllViewportClients.push_back(LevelVC);
	LevelViewportClients.push_back(LevelVC);
	SetActiveViewport(LevelVC);

	ViewportWindows[0] = new SWindow();
	LevelVC->SetLayoutWindow(ViewportWindows[0]);
	ActiveSlotCount = 1;
	CurrentLayout = EViewportLayout::OnePane;
}

void FLevelViewportLayout::Release()
{
	SSplitter::DestroyTree(RootSplitter);
	RootSplitter = nullptr;
	DraggingSplitter = nullptr;

	for (int32 i = 0; i < MaxViewportSlots; ++i)
	{
		delete ViewportWindows[i];
		ViewportWindows[i] = nullptr;
	}

	ActiveViewportClient = nullptr;
	for (FEditorViewportClient* VC : AllViewportClients)
	{
		if (FViewport* VP = VC->GetViewport())
		{
			VP->Release();
			delete VP;
		}
		delete VC;
	}
	AllViewportClients.clear();
	LevelViewportClients.clear();

	FLevelViewportLayoutUI::ReleaseResources();
	ReleaseLayoutIcons();
}

// ─── 활성 뷰포트 ────────────────────────────────────────────

void FLevelViewportLayout::SetActiveViewport(FLevelEditorViewportClient* InClient)
{
	if (ActiveViewportClient)
	{
		ActiveViewportClient->SetActive(false);
	}
	ActiveViewportClient = InClient;
	if (ActiveViewportClient)
	{
		ActiveViewportClient->SetActive(true);
	}
}

void FLevelViewportLayout::SetWorld(UWorld* InWorld)
{
	for (FEditorViewportClient* VC : AllViewportClients)
		VC->SetWorld(InWorld);
}

void FLevelViewportLayout::ResetViewport(UWorld* InWorld)
{
	for (FLevelEditorViewportClient* VC : LevelViewportClients)
	{
		VC->CreateCamera();
		VC->SetWorld(InWorld);
		VC->ResetIdPickingState();
		VC->SetGizmo(SelectionManager ? SelectionManager->GetGizmo() : nullptr);
		VC->GetRenderOptions().ShowFlags.bGizmo = true;
		VC->ResetCamera();

		// 카메라 재생성 후 현재 뷰포트 크기로 AspectRatio 동기화
		if (FViewport* VP = VC->GetViewport())
		{
			FViewportCamera* Cam = VC->GetCamera();
			if (Cam && VP->GetWidth() > 0 && VP->GetHeight() > 0)
			{
				Cam->OnResize(static_cast<int32>(VP->GetWidth()), static_cast<int32>(VP->GetHeight()));
			}
		}

		// 기존 뷰포트 타입(Ortho 방향 등)을 새 카메라에 재적용
		VC->SetViewportType(VC->GetRenderOptions().ViewportType);
	}
}

void FLevelViewportLayout::DestroyAllCameras()
{
	for (FEditorViewportClient* VC : AllViewportClients)
	{
		VC->DestroyCamera();
		VC->SetWorld(nullptr);
	}
}

void FLevelViewportLayout::BeginPIEViewportMode()
{
	bPIEViewportMode = true;
	PIEFocusedViewportClient = ActiveViewportClient;
	if (PIEFocusedViewportClient)
	{
		PIEFocusedViewportClient->TriggerPIEStartOutlineFlash(0.3f, 0.7f);
	}
}

void FLevelViewportLayout::EndPIEViewportMode()
{
	for (FLevelEditorViewportClient* VC : LevelViewportClients)
	{
		if (VC)
		{
			VC->ClearPIEStartOutlineFlash();
		}
	}

	bPIEViewportMode = false;
	PIEFocusedViewportClient = nullptr;
}

int32 FLevelViewportLayout::GetActiveSlotIndex() const
{
	for (int32 i = 0; i < static_cast<int32>(LevelViewportClients.size()); ++i)
	{
		if (LevelViewportClients[i] == ActiveViewportClient)
		{
			return i;
		}
	}
	return 0;
}

bool FLevelViewportLayout::DoesWindowContainSlot(const SWindow* InWindow, int32 SlotIndex) const
{
	if (!InWindow || SlotIndex < 0 || SlotIndex >= MaxViewportSlots || !ViewportWindows[SlotIndex])
	{
		return false;
	}

	if (!InWindow->IsSplitter())
	{
		return InWindow == ViewportWindows[SlotIndex];
	}

	const SSplitter* Split = static_cast<const SSplitter*>(InWindow);
	return DoesWindowContainSlot(Split->GetSideLT(), SlotIndex)
		|| DoesWindowContainSlot(Split->GetSideRB(), SlotIndex);
}

void FLevelViewportLayout::ApplyFocusCollapseRecursive(SSplitter* InNode, int32 FocusSlotIndex)
{
	if (!InNode)
	{
		return;
	}

	constexpr float FocusMin = 0.02f;
	constexpr float FocusMax = 0.98f;
	const bool bFocusInLT = DoesWindowContainSlot(InNode->GetSideLT(), FocusSlotIndex);
	const bool bFocusInRB = DoesWindowContainSlot(InNode->GetSideRB(), FocusSlotIndex);
	if (bFocusInLT && !bFocusInRB)
	{
		InNode->SetRatio(FocusMax);
	}
	else if (!bFocusInLT && bFocusInRB)
	{
		InNode->SetRatio(FocusMin);
	}

	if (SSplitter* LT = SSplitter::AsSplitter(InNode->GetSideLT()))
	{
		ApplyFocusCollapseRecursive(LT, FocusSlotIndex);
	}
	if (SSplitter* RB = SSplitter::AsSplitter(InNode->GetSideRB()))
	{
		ApplyFocusCollapseRecursive(RB, FocusSlotIndex);
	}
}

void FLevelViewportLayout::CollectSplitterRatios(TArray<float>& OutRatios) const
{
	OutRatios.clear();
	if (!RootSplitter)
	{
		return;
	}

	TArray<SSplitter*> Splitters;
	SSplitter::CollectSplitters(RootSplitter, Splitters);
	OutRatios.reserve(Splitters.size());
	for (SSplitter* Split : Splitters)
	{
		OutRatios.push_back(Split ? Split->GetRatio() : 0.5f);
	}
}

void FLevelViewportLayout::ApplySplitterRatios(const TArray<float>& InRatios)
{
	if (!RootSplitter || InRatios.empty())
	{
		return;
	}

	TArray<SSplitter*> Splitters;
	SSplitter::CollectSplitters(RootSplitter, Splitters);
	const size_t Count = (std::min)(Splitters.size(), InRatios.size());
	for (size_t i = 0; i < Count; ++i)
	{
		if (Splitters[i])
		{
			Splitters[i]->SetRatio(Clamp(InRatios[i], 0.02f, 0.98f));
		}
	}
}

void FLevelViewportLayout::EndLayoutTransition()
{
	LayoutTransitionState = ELayoutTransitionState::None;
	LayoutTransitionElapsed = 0.0f;
	TransitionStartRatios.clear();
	TransitionTargetRatios.clear();
	bSuppressLastSplitLayoutUpdate = false;
	bUseCoverTransitionToOnePane = false;
	bUseCoverTransitionFromOnePane = false;
}

void FLevelViewportLayout::BeginCurrentLayoutCollapsePhase()
{
	if (!RootSplitter)
	{
		if (PendingTargetLayout == EViewportLayout::OnePane)
		{
			SetLayout(EViewportLayout::OnePane);
			EndLayoutTransition();
		}
		else
		{
			BeginTargetLayoutExpandPhase();
		}
		return;
	}

	CollectSplitterRatios(TransitionStartRatios);
	ApplyFocusCollapseRecursive(RootSplitter, TransitionFocusSlot);
	CollectSplitterRatios(TransitionTargetRatios);
	ApplySplitterRatios(TransitionStartRatios);

	LayoutTransitionState = ELayoutTransitionState::CollapsingCurrent;
	LayoutTransitionElapsed = 0.0f;
}

void FLevelViewportLayout::BeginTargetLayoutExpandPhase()
{
	if (PendingTargetLayout == EViewportLayout::OnePane)
	{
		SetLayout(EViewportLayout::OnePane);
		EndLayoutTransition();
		return;
	}

	SetLayout(PendingTargetLayout);
	if (!RootSplitter)
	{
		EndLayoutTransition();
		return;
	}

	const int32 TargetSlotCount = GetSlotCount(PendingTargetLayout);
	TransitionFocusSlot = (std::max)(0, (std::min)((std::max)(0, TargetSlotCount - 1), TransitionFocusSlot));

	CollectSplitterRatios(TransitionTargetRatios);
	if (bUseCoverTransitionFromOnePane)
	{
		// 분할 슬롯은 시작 프레임부터 최종 배치로 두고, 포커스 뷰포트 오버레이 축소로 전환한다.
		TransitionStartRatios = TransitionTargetRatios;
	}
	else
	{
		ApplyFocusCollapseRecursive(RootSplitter, TransitionFocusSlot);
		CollectSplitterRatios(TransitionStartRatios);
		ApplySplitterRatios(TransitionStartRatios);
	}

	LayoutTransitionState = ELayoutTransitionState::ExpandingTarget;
	LayoutTransitionElapsed = 0.0f;
}

void FLevelViewportLayout::TickLayoutTransition(float DeltaTime)
{
	if (LayoutTransitionState == ELayoutTransitionState::None)
	{
		return;
	}

	if (!RootSplitter || TransitionStartRatios.empty() || TransitionTargetRatios.empty())
	{
		EndLayoutTransition();
		return;
	}

	LayoutTransitionElapsed += DeltaTime;
	float T = Clamp(LayoutTransitionElapsed / LayoutTransitionDuration, 0.0f, 1.0f);
	const float SmoothT = T * T * (3.0f - 2.0f * T);
	const bool bCoverToOnePane =
		bUseCoverTransitionToOnePane
		&& LayoutTransitionState == ELayoutTransitionState::CollapsingCurrent
		&& PendingTargetLayout == EViewportLayout::OnePane;
	const bool bCoverFromOnePane =
		bUseCoverTransitionFromOnePane
		&& LayoutTransitionState == ELayoutTransitionState::ExpandingTarget
		&& PendingTargetLayout != EViewportLayout::OnePane;

	if (!bCoverToOnePane && !bCoverFromOnePane)
	{
		TArray<float> InterpolatedRatios;
		const size_t Count = (std::min)(TransitionStartRatios.size(), TransitionTargetRatios.size());
		InterpolatedRatios.resize(Count);
		for (size_t i = 0; i < Count; ++i)
		{
			InterpolatedRatios[i] = TransitionStartRatios[i] + (TransitionTargetRatios[i] - TransitionStartRatios[i]) * SmoothT;
		}
		ApplySplitterRatios(InterpolatedRatios);
	}

	if (T < 1.0f)
	{
		return;
	}

	if (LayoutTransitionState == ELayoutTransitionState::CollapsingCurrent)
	{
		if (PendingTargetLayout == EViewportLayout::OnePane)
		{
			SetLayout(EViewportLayout::OnePane);
			EndLayoutTransition();
			return;
		}

		BeginTargetLayoutExpandPhase();
		return;
	}

	EndLayoutTransition();
}

void FLevelViewportLayout::StartAnimatedLayoutTransition(EViewportLayout NewLayout)
{
	if (NewLayout == CurrentLayout)
	{
		return;
	}

	if (LayoutTransitionState != ELayoutTransitionState::None)
	{
		return;
	}

	PendingTargetLayout = NewLayout;
	if (NewLayout != EViewportLayout::OnePane)
	{
		bUseCoverTransitionToOnePane = false;
	}
	else
	{
		bUseCoverTransitionFromOnePane = false;
	}
	if (CurrentLayout == EViewportLayout::OnePane && bIsTemporaryOnePane && NewLayout != EViewportLayout::OnePane)
	{
		const int32 TargetMaxSlot = (std::max)(0, GetSlotCount(NewLayout) - 1);
		TransitionFocusSlot = (std::max)(0, (std::min)(TargetMaxSlot, TemporaryOnePaneSourceSlot));
	}
	else
	{
		const int32 SourceMaxSlot = (std::max)(0, GetSlotCount(CurrentLayout) - 1);
		TransitionFocusSlot = (std::max)(0, (std::min)(SourceMaxSlot, GetActiveSlotIndex()));
	}

	if (CurrentLayout == EViewportLayout::OnePane)
	{
		BeginTargetLayoutExpandPhase();
		return;
	}

	BeginCurrentLayoutCollapsePhase();
}

// ─── 뷰포트 슬롯 관리 ───────────────────────────────────────

void FLevelViewportLayout::EnsureViewportSlots(int32 RequiredCount)
{
	// 현재 슬롯보다 더 필요하면 추가 생성
	while (static_cast<int32>(LevelViewportClients.size()) < RequiredCount)
	{
		int32 Idx = static_cast<int32>(LevelViewportClients.size());

		auto* LevelVC = new FLevelEditorViewportClient();
		LevelVC->SetSettings(&FEditorSettings::Get());
		LevelVC->Initialize(Window);
		LevelVC->SetViewportSize(Window->GetWidth(), Window->GetHeight());
		LevelVC->SetWorld(Editor->GetWorld());
		LevelVC->SetGizmo(SelectionManager->GetGizmo());
		LevelVC->SetSelectionManager(SelectionManager);

		auto* VP = new FViewport();
		VP->Initialize(RendererPtr->GetFD3DDevice().GetDevice(),
			static_cast<uint32>(Window->GetWidth()),
			static_cast<uint32>(Window->GetHeight()));
		VP->SetClient(LevelVC);
		LevelVC->SetViewport(VP);

		LevelVC->CreateCamera();
		LevelVC->ResetCamera();

		AllViewportClients.push_back(LevelVC);
		LevelViewportClients.push_back(LevelVC);

		ViewportWindows[Idx] = new SWindow();
		LevelVC->SetLayoutWindow(ViewportWindows[Idx]);
	}
}

void FLevelViewportLayout::ShrinkViewportSlots(int32 RequiredCount)
{
	while (static_cast<int32>(LevelViewportClients.size()) > RequiredCount)
	{
		FLevelEditorViewportClient* VC = LevelViewportClients.back();
		int32 Idx = static_cast<int32>(LevelViewportClients.size()) - 1;
		LevelViewportClients.pop_back();

		for (auto It = AllViewportClients.begin(); It != AllViewportClients.end(); ++It)
		{
			if (*It == VC) { AllViewportClients.erase(It); break; }
		}

		if (ActiveViewportClient == VC)
			SetActiveViewport(LevelViewportClients[0]);

		if (FViewport* VP = VC->GetViewport())
		{
			VP->Release();
			delete VP;
		}
		VC->DestroyCamera();
		delete VC;

		delete ViewportWindows[Idx];
		ViewportWindows[Idx] = nullptr;
	}
}

// ─── SSplitter 트리 빌드 ─────────────────────────────────────

SSplitter* FLevelViewportLayout::BuildSplitterTree(EViewportLayout Layout)
{
	SWindow** W = ViewportWindows;

	switch (Layout)
	{
	case EViewportLayout::OnePane:
		return nullptr; // 트리 불필요

	case EViewportLayout::TwoPanesHoriz:
	{
		// H → [0] | [1]
		auto* Root = new SSplitterH();
		Root->SetSideLT(W[0]);
		Root->SetSideRB(W[1]);
		return Root;
	}
	case EViewportLayout::TwoPanesVert:
	{
		// V → [0] / [1]
		auto* Root = new SSplitterV();
		Root->SetSideLT(W[0]);
		Root->SetSideRB(W[1]);
		return Root;
	}
	case EViewportLayout::ThreePanesLeft:
	{
		// H → [0] | V([1]/[2])
		auto* RightV = new SSplitterV();
		RightV->SetSideLT(W[1]);
		RightV->SetSideRB(W[2]);
		auto* Root = new SSplitterH();
		Root->SetSideLT(W[0]);
		Root->SetSideRB(RightV);
		return Root;
	}
	case EViewportLayout::ThreePanesRight:
	{
		// H → V([0]/[1]) | [2]
		auto* LeftV = new SSplitterV();
		LeftV->SetSideLT(W[0]);
		LeftV->SetSideRB(W[1]);
		auto* Root = new SSplitterH();
		Root->SetSideLT(LeftV);
		Root->SetSideRB(W[2]);
		return Root;
	}
	case EViewportLayout::ThreePanesTop:
	{
		// V → [0] / H([1]|[2])
		auto* BottomH = new SSplitterH();
		BottomH->SetSideLT(W[1]);
		BottomH->SetSideRB(W[2]);
		auto* Root = new SSplitterV();
		Root->SetSideLT(W[0]);
		Root->SetSideRB(BottomH);
		return Root;
	}
	case EViewportLayout::ThreePanesBottom:
	{
		// V → H([0]|[1]) / [2]
		auto* TopH = new SSplitterH();
		TopH->SetSideLT(W[0]);
		TopH->SetSideRB(W[1]);
		auto* Root = new SSplitterV();
		Root->SetSideLT(TopH);
		Root->SetSideRB(W[2]);
		return Root;
	}
	case EViewportLayout::FourPanes2x2:
	{
		// H → V([0]/[2]) | V([1]/[3])
		auto* LeftV = new SSplitterV();
		LeftV->SetSideLT(W[0]);
		LeftV->SetSideRB(W[2]);
		auto* RightV = new SSplitterV();
		RightV->SetSideLT(W[1]);
		RightV->SetSideRB(W[3]);
		auto* Root = new SSplitterH();
		Root->SetSideLT(LeftV);
		Root->SetSideRB(RightV);
		return Root;
	}
	case EViewportLayout::FourPanesLeft:
	{
		// H → [0] | V([1] / V([2]/[3]))
		auto* InnerV = new SSplitterV();
		InnerV->SetSideLT(W[2]);
		InnerV->SetSideRB(W[3]);
		auto* RightV = new SSplitterV();
		RightV->SetRatio(0.333f);
		RightV->SetSideLT(W[1]);
		RightV->SetSideRB(InnerV);
		auto* Root = new SSplitterH();
		Root->SetSideLT(W[0]);
		Root->SetSideRB(RightV);
		return Root;
	}
	case EViewportLayout::FourPanesRight:
	{
		// H → V([0] / V([1]/[2])) | [3]
		auto* InnerV = new SSplitterV();
		InnerV->SetSideLT(W[1]);
		InnerV->SetSideRB(W[2]);
		auto* LeftV = new SSplitterV();
		LeftV->SetRatio(0.333f);
		LeftV->SetSideLT(W[0]);
		LeftV->SetSideRB(InnerV);
		auto* Root = new SSplitterH();
		Root->SetSideLT(LeftV);
		Root->SetSideRB(W[3]);
		return Root;
	}
	case EViewportLayout::FourPanesTop:
	{
		// V → [0] / H([1] | H([2]|[3]))
		auto* InnerH = new SSplitterH();
		InnerH->SetSideLT(W[2]);
		InnerH->SetSideRB(W[3]);
		auto* BottomH = new SSplitterH();
		BottomH->SetRatio(0.333f);
		BottomH->SetSideLT(W[1]);
		BottomH->SetSideRB(InnerH);
		auto* Root = new SSplitterV();
		Root->SetSideLT(W[0]);
		Root->SetSideRB(BottomH);
		return Root;
	}
	case EViewportLayout::FourPanesBottom:
	{
		// V → H([0] | H([1]|[2])) / [3]
		auto* InnerH = new SSplitterH();
		InnerH->SetSideLT(W[1]);
		InnerH->SetSideRB(W[2]);
		auto* TopH = new SSplitterH();
		TopH->SetRatio(0.333f);
		TopH->SetSideLT(W[0]);
		TopH->SetSideRB(InnerH);
		auto* Root = new SSplitterV();
		Root->SetSideLT(TopH);
		Root->SetSideRB(W[3]);
		return Root;
	}
	default:
		return nullptr;
	}
}

// ─── 레이아웃 전환 ──────────────────────────────────────────

void FLevelViewportLayout::SetLayout(EViewportLayout NewLayout)
{
	if (NewLayout == CurrentLayout) return;

	const bool bWasOnePane = (CurrentLayout == EViewportLayout::OnePane);
	const bool bWasTemporaryOnePane = bIsTemporaryOnePane;

	// 기존 트리 해제
	SSplitter::DestroyTree(RootSplitter);
	RootSplitter = nullptr;
	DraggingSplitter = nullptr;

	int32 RequiredSlots = GetSlotCount(NewLayout);
	int32 OldSlotCount = static_cast<int32>(LevelViewportClients.size());
	const bool bUseTemporaryOnePane = (NewLayout == EViewportLayout::OnePane && bRequestPreserveSplitOnOnePane);

	if (bUseTemporaryOnePane)
	{
		TemporaryOnePaneSourceSlot = (std::max)(0, (std::min)((std::max)(0, OldSlotCount - 1), GetActiveSlotIndex()));
		bIsTemporaryOnePane = true;
	}

	// 슬롯 수 조정 (toggle 기반 OnePane에서는 split 슬롯을 보존)
	if (!bUseTemporaryOnePane)
	{
		if (RequiredSlots > OldSlotCount)
			EnsureViewportSlots(RequiredSlots);
		else if (RequiredSlots < OldSlotCount)
			ShrinkViewportSlots(RequiredSlots);
	}
	else
	{
		RequiredSlots = OldSlotCount;
	}

	// 분할 전환 시 새로 추가된 슬롯에 Top, Front, Right 순으로 기본 설정
	if (NewLayout != EViewportLayout::OnePane)
	{
		constexpr ELevelViewportType DefaultTypes[] = {
			ELevelViewportType::Top,
			ELevelViewportType::Front,
			ELevelViewportType::Right
		};
		// 새로 생성된 슬롯에만 적용
		const int32 StartIdx = OldSlotCount;
		for (int32 i = StartIdx; i < RequiredSlots && (i - 1) < 3; ++i)
		{
			LevelViewportClients[i]->SetViewportType(DefaultTypes[i - 1]);
		}
	}

	// 새 트리 빌드
	RootSplitter = BuildSplitterTree(NewLayout);
	ActiveSlotCount = (NewLayout == EViewportLayout::OnePane) ? 1 : RequiredSlots;
	CurrentLayout = NewLayout;

	if (NewLayout != EViewportLayout::OnePane)
	{
		bIsTemporaryOnePane = false;
	}
	else if (!bUseTemporaryOnePane && !bWasTemporaryOnePane)
	{
		TemporaryOnePaneSourceSlot = 0;
	}

	bRequestPreserveSplitOnOnePane = false;

	if (!bSuppressLastSplitLayoutUpdate && CurrentLayout != EViewportLayout::OnePane)
	{
		LastSplitLayout = CurrentLayout;
	}
}

void FLevelViewportLayout::SetLayoutAnimated(EViewportLayout NewLayout)
{
	StartAnimatedLayoutTransition(NewLayout);
}

void FLevelViewportLayout::ToggleViewportSplit()
{
	bSuppressLastSplitLayoutUpdate = true;

	if (CurrentLayout == EViewportLayout::OnePane)
	{
		bUseCoverTransitionToOnePane = false;
		bUseCoverTransitionFromOnePane = bIsTemporaryOnePane;
		SetLayoutAnimated(LastSplitLayout == EViewportLayout::OnePane ? EViewportLayout::FourPanes2x2 : LastSplitLayout);
	}
	else
	{
		bUseCoverTransitionToOnePane = true;
		bUseCoverTransitionFromOnePane = false;
		bRequestPreserveSplitOnOnePane = true;
		SetLayoutAnimated(EViewportLayout::OnePane);
	}
}

// ─── Viewport UI 렌더링 ─────────────────────────────────────

void FLevelViewportLayout::RenderViewportUI(float DeltaTime)
{
	FLevelViewportLayoutUI::RenderViewportUI(*this, DeltaTime);
}

void FLevelViewportLayout::RenderActiveViewportStatOverlay()
{
	FLevelViewportLayoutUI::RenderActiveViewportStatOverlay(*this);
}

// ─── 각 뷰포트 패인 툴바 오버레이 ──────────────────────────

void FLevelViewportLayout::RenderPaneToolbar(int32 SlotIndex)
{
	FLevelViewportLayoutUI::RenderPaneToolbar(*this, SlotIndex);
}

// ─── FEditorSettings ↔ 뷰포트 상태 동기화 ──────────────────

void FLevelViewportLayout::SaveToSettings()
{
	FEditorSettings& S = FEditorSettings::Get();

	S.LayoutType = static_cast<int32>(CurrentLayout);

	// 뷰포트별 렌더 옵션 저장
	for (int32 i = 0; i < ActiveSlotCount && i < static_cast<int32>(LevelViewportClients.size()); ++i)
	{
		S.SlotOptions[i] = LevelViewportClients[i]->GetRenderOptions();
	}

	// Splitter 비율 저장
	if (RootSplitter)
	{
		TArray<SSplitter*> AllSplitters;
		SSplitter::CollectSplitters(RootSplitter, AllSplitters);
		S.SplitterCount = static_cast<int32>(AllSplitters.size());
		if (S.SplitterCount > 3) S.SplitterCount = 3;
		for (int32 i = 0; i < S.SplitterCount; ++i)
		{
			S.SplitterRatios[i] = AllSplitters[i]->GetRatio();
		}
	}
	else
	{
		S.SplitterCount = 0;
	}

	// Perspective 카메라 (slot 0) 저장
	if (!LevelViewportClients.empty())
	{
		FViewportCamera* Cam = LevelViewportClients[0]->GetCamera();
		if (Cam)
		{
			S.PerspCamLocation = Cam->GetWorldLocation();
			S.PerspCamRotation = Cam->GetRelativeRotation();
			const FViewportCameraState& CS = Cam->GetCameraState();
			S.PerspCamFOV = CS.FOV * (180.0f / 3.14159265358979f); // rad → deg
			S.PerspCamNearClip = CS.NearZ;
			S.PerspCamFarClip = CS.FarZ;
		}
	}
}

void FLevelViewportLayout::LoadFromSettings()
{
	const FEditorSettings& S = FEditorSettings::Get();

	// 레이아웃 전환 (슬롯 생성 + 트리 빌드)
	EViewportLayout NewLayout = static_cast<EViewportLayout>(S.LayoutType);
	if (NewLayout >= EViewportLayout::MAX)
		NewLayout = EViewportLayout::OnePane;

	// OnePane이 아니면 레이아웃 적용 (Initialize에서 이미 OnePane으로 생성됨)
	if (NewLayout != EViewportLayout::OnePane)
	{
		// SetLayout 내부 bWasOnePane 분기를 피하기 위해 직접 전환
		SSplitter::DestroyTree(RootSplitter);
		RootSplitter = nullptr;
		DraggingSplitter = nullptr;

		int32 RequiredSlots = GetSlotCount(NewLayout);
		EnsureViewportSlots(RequiredSlots);

		RootSplitter = BuildSplitterTree(NewLayout);
		ActiveSlotCount = RequiredSlots;
		CurrentLayout = NewLayout;
	}

	// 뷰포트별 렌더 옵션 적용
	for (int32 i = 0; i < ActiveSlotCount && i < static_cast<int32>(LevelViewportClients.size()); ++i)
	{
		FLevelEditorViewportClient* VC = LevelViewportClients[i];
		VC->GetRenderOptions() = S.SlotOptions[i];

		// ViewportType에 따라 카메라 ortho/방향 설정
		VC->SetViewportType(S.SlotOptions[i].ViewportType);
	}

	// Splitter 비율 복원
	if (RootSplitter)
	{
		TArray<SSplitter*> AllSplitters;
		SSplitter::CollectSplitters(RootSplitter, AllSplitters);
		for (int32 i = 0; i < S.SplitterCount && i < static_cast<int32>(AllSplitters.size()); ++i)
		{
			AllSplitters[i]->SetRatio(S.SplitterRatios[i]);
		}
	}

	// Perspective 카메라 (slot 0) 복원
	if (!LevelViewportClients.empty())
	{
		FViewportCamera* Cam = LevelViewportClients[0]->GetCamera();
		if (Cam)
		{
			Cam->SetRelativeLocation(S.PerspCamLocation);
			Cam->SetRelativeRotation(S.PerspCamRotation);

			FViewportCameraState CS = Cam->GetCameraState();
			CS.FOV = S.PerspCamFOV * (3.14159265358979f / 180.0f); // deg → rad
			CS.NearZ = S.PerspCamNearClip;
			CS.FarZ = S.PerspCamFarClip;
			Cam->SetCameraState(CS);
		}
	}
}
