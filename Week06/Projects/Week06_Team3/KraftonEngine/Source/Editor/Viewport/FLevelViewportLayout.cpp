#include "Editor/Viewport/FLevelViewportLayout.h"

#include "Editor/EditorEngine.h"
#include "Editor/Input/EditorNavigationTool.h"
#include "Editor/Input/EditorViewportController.h"
#include "Editor/Input/EditorViewportInputUtils.h"
#include "Editor/Input/EditorViewportModes.h"
#include "Editor/Viewport/LevelEditorViewportClient.h"
#include "Editor/Settings/EditorSettings.h"
#include "Editor/Selection/SelectionManager.h"
#include "Engine/Runtime/WindowsWindow.h"
#include "Render/Pipeline/Renderer.h"
#include "Viewport/Viewport.h"
#include "UI/SSplitter.h"
#include "Math/MathUtils.h"
#include "Platform/Paths.h"
#include "Core/RayTypes.h"
#include "Core/CollisionTypes.h"
#include "GameFramework/AActor.h"
#include "GameFramework/World.h"
#include "ImGui/imgui.h"
#include "WICTextureLoader.h"
#include "Components/CameraComponent.h"
#include "Components/GizmoComponent.h"

#include <algorithm>
#include <cfloat>

namespace
{
bool IsPIEPlayingAndPossessed(const UEditorEngine* Editor)
{
	return Editor
		&& Editor->IsPlayingInEditor()
		&& Editor->GetPIEControlMode() == UEditorEngine::EPIEControlMode::Possessed;
}

bool CanOpenPlaceActorMenuInViewport(const UEditorEngine* Editor)
{
	return Editor && (!Editor->IsPlayingInEditor() || Editor->GetPIEControlMode() == UEditorEngine::EPIEControlMode::Ejected);
}

enum class EToolbarIcon : int32
{
	Menu = 0,
	Select,
	Translate,
	Rotate,
	Scale,
	WorldSpace,
	LocalSpace,
	TranslateSnap,
	RotateSnap,
	ScaleSnap,
	Camera,
	Setting,
	Count
};

const wchar_t* GetToolbarIconFileName(EToolbarIcon Icon)
{
	switch (Icon)
	{
	case EToolbarIcon::Menu:          return L"Menu.png";
	case EToolbarIcon::Select:        return L"Select.png";
	case EToolbarIcon::Translate:     return L"Translate.png";
	case EToolbarIcon::Rotate:        return L"Rotate.png";
	case EToolbarIcon::Scale:         return L"Scale.png";
	case EToolbarIcon::WorldSpace:    return L"WorldSpace.png";
	case EToolbarIcon::LocalSpace:    return L"LocalSpace.png";
	case EToolbarIcon::TranslateSnap: return L"Translate_Snap.png";
	case EToolbarIcon::RotateSnap:    return L"Rotate_Snap.png";
	case EToolbarIcon::ScaleSnap:     return L"Scale_Snap.png";
	case EToolbarIcon::Camera:        return L"Camera.png";
	case EToolbarIcon::Setting:       return L"Show_Flag.png";
	default:                              return L"";
	}
}

ID3D11ShaderResourceView** GetToolbarIconTable()
{
	static ID3D11ShaderResourceView* ToolbarIcons[static_cast<int32>(EToolbarIcon::Count)] = {};
	return ToolbarIcons;
}

void EnsureToolbarIconsLoaded(FRenderer* RendererPtr)
{
	static bool bToolbarIconsLoaded = false;
	if (bToolbarIconsLoaded || !RendererPtr)
	{
		return;
	}

	ID3D11ShaderResourceView** ToolbarIcons = GetToolbarIconTable();
	ID3D11Device* Device = RendererPtr->GetFD3DDevice().GetDevice();
	const std::wstring IconDir = FPaths::Combine(FPaths::RootDir(), L"Asset/Editor/ToolIcons/");
	for (int32 i = 0; i < static_cast<int32>(EToolbarIcon::Count); ++i)
	{
		const std::wstring FilePath = IconDir + GetToolbarIconFileName(static_cast<EToolbarIcon>(i));
		DirectX::CreateWICTextureFromFile(Device, FilePath.c_str(), nullptr, &ToolbarIcons[i]);
	}
	bToolbarIconsLoaded = true;
}

ImVec2 GetToolbarIconRenderSize(EToolbarIcon Icon, float FallbackSize, float MaxIconSize)
{
	ID3D11ShaderResourceView** ToolbarIcons = GetToolbarIconTable();
	ID3D11ShaderResourceView* IconSRV = ToolbarIcons[static_cast<int32>(Icon)];
	if (!IconSRV)
	{
		return ImVec2(FallbackSize, FallbackSize);
	}

	ID3D11Resource* Resource = nullptr;
	IconSRV->GetResource(&Resource);
	if (!Resource)
	{
		return ImVec2(FallbackSize, FallbackSize);
	}

	ImVec2 IconSize(FallbackSize, FallbackSize);
	D3D11_RESOURCE_DIMENSION Dimension = D3D11_RESOURCE_DIMENSION_UNKNOWN;
	Resource->GetType(&Dimension);
	if (Dimension == D3D11_RESOURCE_DIMENSION_TEXTURE2D)
	{
		ID3D11Texture2D* Texture2D = static_cast<ID3D11Texture2D*>(Resource);
		D3D11_TEXTURE2D_DESC Desc{};
		Texture2D->GetDesc(&Desc);
		IconSize = ImVec2(static_cast<float>(Desc.Width), static_cast<float>(Desc.Height));
	}
	Resource->Release();

	if (IconSize.x > MaxIconSize || IconSize.y > MaxIconSize)
	{
		const float Scale = (IconSize.x > IconSize.y) ? (MaxIconSize / IconSize.x) : (MaxIconSize / IconSize.y);
		IconSize.x *= Scale;
		IconSize.y *= Scale;
	}
	return IconSize;
}

}

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

	std::wstring IconDir = FPaths::Combine(FPaths::RootDir(), L"Asset/Editor/Icons/");

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

	// Play/Stop 툴바 초기화
	PlayToolbar.Initialize(InEditor, InRenderer.GetFD3DDevice().GetDevice());

	// LevelViewportClient 생성 (단일 뷰포트)
	auto* LevelVC = new FLevelEditorViewportClient();
	LevelVC->SetOverlayStatSystem(&Editor->GetOverlayStatSystem());
	LevelVC->SetSettings(&FEditorSettings::Get());
	LevelVC->Initialize(Window);
	LevelVC->SetViewportSize(Window->GetWidth(), Window->GetHeight());
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
	LastUserActivatedViewportClient = LevelVC;

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

	ReleaseLayoutIcons();
	PlayToolbar.Release();
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
		ActiveViewportClient->SyncNavigationCameraTargetFromCurrent();
		UWorld* World = Editor->GetWorld();
		if (World && ActiveViewportClient->GetCamera())
		{
			World->SetActiveCamera(ActiveViewportClient->GetCamera());
		}
	}
}

FLevelEditorViewportClient* FLevelViewportLayout::GetPIEStartViewport() const
{
	if (LastUserActivatedViewportClient)
	{
		for (FLevelEditorViewportClient* VC : LevelViewportClients)
		{
			if (VC == LastUserActivatedViewportClient)
			{
				return LastUserActivatedViewportClient;
			}
		}
	}
	return ActiveViewportClient;
}

void FLevelViewportLayout::SetWorld(UWorld* /*InWorld*/)
{
	// World는 GEngine->GetWorld() 경유로 조회되므로 별도 저장이 필요 없음.
	// 호환성을 위해 시그니처만 유지.
}

void FLevelViewportLayout::ResetViewport(UWorld* InWorld)
{
	for (FLevelEditorViewportClient* VC : LevelViewportClients)
	{
		VC->CreateCamera();
		VC->ResetCamera();

		// 카메라 재생성 후 현재 뷰포트 크기로 AspectRatio 동기화
		if (FViewport* VP = VC->GetViewport())
		{
			UCameraComponent* Cam = VC->GetCamera();
			if (Cam && VP->GetWidth() > 0 && VP->GetHeight() > 0)
			{
				Cam->OnResize(static_cast<int32>(VP->GetWidth()), static_cast<int32>(VP->GetHeight()));
			}
		}

		// 기존 뷰포트 타입(Ortho 방향 등)을 새 카메라에 재적용
		VC->SetViewportType(VC->GetRenderOptions().ViewportType);
		VC->SyncNavigationCameraTargetFromCurrent();
	}
	if (ActiveViewportClient && InWorld)
		InWorld->SetActiveCamera(ActiveViewportClient->GetCamera());
}

void FLevelViewportLayout::DestroyAllCameras()
{
	for (FEditorViewportClient* VC : AllViewportClients)
	{
		VC->DestroyCamera();
	}
}

void FLevelViewportLayout::DisableWorldAxisForPIE()
{
	if (bHasSavedWorldAxisVisibility)
	{
		for (int32 i = 0; i < ActiveSlotCount && i < static_cast<int32>(LevelViewportClients.size()); ++i)
		{
			LevelViewportClients[i]->GetRenderOptions().ShowFlags.bGrid = false;
			LevelViewportClients[i]->GetRenderOptions().ShowFlags.bWorldAxis = false;
		}
		return;
	}

	for (int32 i = 0; i < MaxViewportSlots; ++i)
	{
		SavedGridVisibility[i] = false;
		SavedWorldAxisVisibility[i] = false;
	}

	for (int32 i = 0; i < ActiveSlotCount && i < static_cast<int32>(LevelViewportClients.size()); ++i)
	{
		FViewportRenderOptions& Opts = LevelViewportClients[i]->GetRenderOptions();
		SavedGridVisibility[i] = Opts.ShowFlags.bGrid;
		SavedWorldAxisVisibility[i] = Opts.ShowFlags.bWorldAxis;
		Opts.ShowFlags.bGrid = false;
		Opts.ShowFlags.bWorldAxis = false;
	}

	bHasSavedWorldAxisVisibility = true;
}

void FLevelViewportLayout::RestoreWorldAxisAfterPIE()
{
	if (!bHasSavedWorldAxisVisibility)
	{
		return;
	}

	for (int32 i = 0; i < ActiveSlotCount && i < static_cast<int32>(LevelViewportClients.size()); ++i)
	{
		LevelViewportClients[i]->GetRenderOptions().ShowFlags.bGrid = SavedGridVisibility[i];
		LevelViewportClients[i]->GetRenderOptions().ShowFlags.bWorldAxis = SavedWorldAxisVisibility[i];
	}

	bHasSavedWorldAxisVisibility = false;
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

void FLevelViewportLayout::NotifyPIEPossessedViewport(FLevelEditorViewportClient* InViewportClient)
{
	if (!bPIEViewportMode)
	{
		return;
	}

	if (!InViewportClient)
	{
		InViewportClient = ActiveViewportClient;
	}

	PIEFocusedViewportClient = InViewportClient;
	if (PIEFocusedViewportClient)
	{
		PIEFocusedViewportClient->TriggerPIEStartOutlineFlash(0.3f, 0.7f);
	}
}

bool FLevelViewportLayout::IsPointOverSplitterBar(const POINT& InScreenPos) const
{
	if (!RootSplitter)
	{
		return false;
	}

	const FPoint MousePoint = { static_cast<float>(InScreenPos.x), static_cast<float>(InScreenPos.y) };
	return SSplitter::FindSplitterAtBar(RootSplitter, MousePoint) != nullptr;
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
	const float T = Clamp(LayoutTransitionElapsed / LayoutTransitionDuration, 0.0f, 1.0f);
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
		LevelVC->SetOverlayStatSystem(&Editor->GetOverlayStatSystem());
		LevelVC->SetSettings(&FEditorSettings::Get());
		LevelVC->Initialize(Window);
		LevelVC->SetViewportSize(Window->GetWidth(), Window->GetHeight());
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
		if (LastUserActivatedViewportClient == VC)
		{
			LastUserActivatedViewportClient = LevelViewportClients.empty() ? nullptr : LevelViewportClients[0];
		}

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
	bMouseOverViewport = false;
	TickLayoutTransition(DeltaTime);
	const bool bPlaceActorPopupWasOpen = ImGui::IsPopupOpen("##ViewportPlaceActorPopup");

	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
	ImGui::Begin("Viewport", nullptr, ImGuiWindowFlags_None);

	ImVec2 ContentPos = ImGui::GetCursorScreenPos();
	ImVec2 ContentSize = ImGui::GetContentRegionAvail();

	if (ContentSize.x > 0 && ContentSize.y > 0)
	{
		FRect ContentRect = {
			ContentPos.x,
			ContentPos.y,
			ContentSize.x,
			ContentSize.y
		};
		const int32 OnePaneSlotIndex = (CurrentLayout == EViewportLayout::OnePane && bIsTemporaryOnePane)
			? (std::max)(0, (std::min)(TemporaryOnePaneSourceSlot, static_cast<int32>(LevelViewportClients.size()) - 1))
			: 0;
		const bool bCoverToOnePane =
			bUseCoverTransitionToOnePane
			&& LayoutTransitionState == ELayoutTransitionState::CollapsingCurrent
			&& PendingTargetLayout == EViewportLayout::OnePane;
		const bool bCoverFromOnePane =
			bUseCoverTransitionFromOnePane
			&& LayoutTransitionState == ELayoutTransitionState::ExpandingTarget
			&& PendingTargetLayout != EViewportLayout::OnePane;
		const bool bRenderViewportOverlayUI = !bCoverToOnePane;

		// SSplitter 레이아웃 계산
		if (RootSplitter)
		{
			RootSplitter->ComputeLayout(ContentRect);
		}
		else if (OnePaneSlotIndex < MaxViewportSlots && ViewportWindows[OnePaneSlotIndex])
		{
			ViewportWindows[OnePaneSlotIndex]->SetRect(ContentRect);
		}

		// Temporary one-pane can render from a non-zero source slot.
		// Keep that slot's layout rect synchronized to the full content rect,
		// otherwise input routing still uses stale split rect from the old layout.
		if (CurrentLayout == EViewportLayout::OnePane
			&& OnePaneSlotIndex >= 0
			&& OnePaneSlotIndex < MaxViewportSlots
			&& ViewportWindows[OnePaneSlotIndex])
		{
			ViewportWindows[OnePaneSlotIndex]->SetRect(ContentRect);
		}

		bool bVisibleSlot[MaxViewportSlots] = { false, false, false, false };
		for (int32 i = 0; i < ActiveSlotCount; ++i)
		{
			const int32 SlotIndex = (CurrentLayout == EViewportLayout::OnePane) ? OnePaneSlotIndex : i;
			if (SlotIndex >= 0 && SlotIndex < MaxViewportSlots)
			{
				bVisibleSlot[SlotIndex] = true;
			}
		}

		// Hide stale split-rect targets: non-visible slots must not keep previous rects,
		// otherwise input routing can still hit old quadrant areas after switching layouts.
		for (int32 SlotIndex = 0; SlotIndex < MaxViewportSlots; ++SlotIndex)
		{
			if (!bVisibleSlot[SlotIndex] && ViewportWindows[SlotIndex])
			{
				ViewportWindows[SlotIndex]->SetRect(FRect{ 0.0f, 0.0f, 0.0f, 0.0f });
			}
		}

		// 모든 ViewportClient의 스크린 rect를 동기화한다.
		// (숨겨진 슬롯도 stale split rect가 남지 않도록 zero-rect까지 반영)
		for (FLevelEditorViewportClient* VC : LevelViewportClients)
		{
			if (VC)
			{
				VC->UpdateLayoutRect();
			}
		}

		// 각 ViewportClient에 이미지 렌더
		for (int32 i = 0; i < ActiveSlotCount; ++i)
		{
			const int32 SlotIndex = (CurrentLayout == EViewportLayout::OnePane) ? OnePaneSlotIndex : i;
			if (SlotIndex < static_cast<int32>(LevelViewportClients.size()))
			{
				FLevelEditorViewportClient* VC = LevelViewportClients[SlotIndex];
				const bool bIsPIEFocusViewport = bPIEViewportMode && VC == PIEFocusedViewportClient;
				VC->RenderViewportImage(VC == ActiveViewportClient, !bIsPIEFocusViewport);
			}
		}

		// 각 뷰포트 패인 상단에 툴바 오버레이 렌더
		for (int32 i = 0; bRenderViewportOverlayUI && i < ActiveSlotCount; ++i)
		{
			const int32 SlotIndex = (CurrentLayout == EViewportLayout::OnePane) ? OnePaneSlotIndex : i;
			RenderPaneToolbar(SlotIndex);
		}

		// 분할 바 렌더 (재귀 수집)
		if (RootSplitter)
		{
			TArray<SSplitter*> AllSplitters;
			SSplitter::CollectSplitters(RootSplitter, AllSplitters);

			ImDrawList* DrawList = ImGui::GetWindowDrawList();
			ImU32 BarColor = IM_COL32(80, 80, 80, 255);

			for (SSplitter* S : AllSplitters)
			{
				const FRect& Bar = S->GetSplitBarRect();
				DrawList->AddRectFilled(
					ImVec2(Bar.X, Bar.Y),
					ImVec2(Bar.X + Bar.Width, Bar.Y + Bar.Height),
					BarColor);
			}
		}

		if (bCoverToOnePane
			&& TransitionFocusSlot >= 0
			&& TransitionFocusSlot < static_cast<int32>(LevelViewportClients.size())
			&& TransitionFocusSlot < MaxViewportSlots
			&& ViewportWindows[TransitionFocusSlot]
			&& LevelViewportClients[TransitionFocusSlot]
			&& LevelViewportClients[TransitionFocusSlot]->GetViewport()
			&& LevelViewportClients[TransitionFocusSlot]->GetViewport()->GetSRV())
		{
			const FRect& FromRect = ViewportWindows[TransitionFocusSlot]->GetRect();
			const float T = Clamp(LayoutTransitionElapsed / LayoutTransitionDuration, 0.0f, 1.0f);
			const float SmoothT = T * T * (3.0f - 2.0f * T);

			const float L = FromRect.X + (ContentRect.X - FromRect.X) * SmoothT;
			const float TT = FromRect.Y + (ContentRect.Y - FromRect.Y) * SmoothT;
			const float R = (FromRect.X + FromRect.Width) + ((ContentRect.X + ContentRect.Width) - (FromRect.X + FromRect.Width)) * SmoothT;
			const float B = (FromRect.Y + FromRect.Height) + ((ContentRect.Y + ContentRect.Height) - (FromRect.Y + FromRect.Height)) * SmoothT;

			ImGui::GetWindowDrawList()->AddImage(
				(ImTextureID)LevelViewportClients[TransitionFocusSlot]->GetViewport()->GetSRV(),
				ImVec2(L, TT),
				ImVec2(R, B));
		}
		else if (bCoverFromOnePane
			&& TransitionFocusSlot >= 0
			&& TransitionFocusSlot < static_cast<int32>(LevelViewportClients.size())
			&& TransitionFocusSlot < MaxViewportSlots
			&& ViewportWindows[TransitionFocusSlot]
			&& LevelViewportClients[TransitionFocusSlot]
			&& LevelViewportClients[TransitionFocusSlot]->GetViewport()
			&& LevelViewportClients[TransitionFocusSlot]->GetViewport()->GetSRV())
		{
			const FRect& ToRect = ViewportWindows[TransitionFocusSlot]->GetRect();
			const float T = Clamp(LayoutTransitionElapsed / LayoutTransitionDuration, 0.0f, 1.0f);
			const float SmoothT = T * T * (3.0f - 2.0f * T);

			const float L = ContentRect.X + (ToRect.X - ContentRect.X) * SmoothT;
			const float TT = ContentRect.Y + (ToRect.Y - ContentRect.Y) * SmoothT;
			const float R = (ContentRect.X + ContentRect.Width) + ((ToRect.X + ToRect.Width) - (ContentRect.X + ContentRect.Width)) * SmoothT;
			const float B = (ContentRect.Y + ContentRect.Height) + ((ToRect.Y + ToRect.Height) - (ContentRect.Y + ContentRect.Height)) * SmoothT;

			ImGui::GetWindowDrawList()->AddImage(
				(ImTextureID)LevelViewportClients[TransitionFocusSlot]->GetViewport()->GetSRV(),
				ImVec2(L, TT),
				ImVec2(R, B));
		}

		// 입력 처리
		const ImVec2 MousePos = ImGui::GetIO().MousePos;
		const FPoint MP = { MousePos.x, MousePos.y };

		// hover window 상태와 무관하게 실제 viewport 인터랙션 영역 여부를 계산한다.
		for (int32 i = 0; i < ActiveSlotCount; ++i)
		{
			const int32 SlotIndex = (CurrentLayout == EViewportLayout::OnePane) ? OnePaneSlotIndex : i;
			if (IsViewportInteractiveHover(SlotIndex, MousePos.x, MousePos.y))
			{
				bMouseOverViewport = true;
				break;
			}
		}

		if (ImGui::IsWindowHovered())
		{

			HandleSplitterInteraction(MousePos, POINT{ static_cast<LONG>(MP.X), static_cast<LONG>(MP.Y) });
			HandleViewportActivationOnClick(MousePos, ActiveSlotCount, OnePaneSlotIndex);

			const bool bCanOpenPlaceActorMenu = CanOpenPlaceActorMenuInViewport(Editor);
			if (bCanOpenPlaceActorMenu)
			{
				constexpr float RightClickPopupThresholdPx = 20.0f;
				const float RightClickPopupThresholdSq = RightClickPopupThresholdPx * RightClickPopupThresholdPx;
				for (int32 i = 0; i < ActiveSlotCount; ++i)
				{
					const int32 SlotIndex = (CurrentLayout == EViewportLayout::OnePane) ? OnePaneSlotIndex : i;
					if (SlotIndex < 0 || SlotIndex >= static_cast<int32>(LevelViewportClients.size()) || !ViewportWindows[SlotIndex])
					{
						continue;
					}
					FLevelEditorViewportClient* VC = LevelViewportClients[SlotIndex];
					if (!VC)
					{
						continue;
					}
					const FViewportInputContext& InputContext = VC->GetRoutedInputContext();

					if (ImGui::IsMouseClicked(1) && InputContext.WasPressed(VK_RBUTTON) && IsViewportInteractiveHover(SlotIndex, MousePos.x, MousePos.y))
					{
						ContextMenuState.RightClickTracking[SlotIndex] = true;
						ContextMenuState.RightClickTravelSq[SlotIndex] = 0.0f;
						POINT PressClientPos =
						{
							static_cast<LONG>(MousePos.x),
							static_cast<LONG>(MousePos.y)
						};
						for (const FInputEvent& E : InputContext.Events)
						{
							if (E.Type == EInputEventType::KeyPressed && E.Key == VK_RBUTTON)
							{
								PressClientPos = E.MouseScreenPos;
								if (Window)
								{
									ScreenToClient(Window->GetHWND(), &PressClientPos);
								}
								break;
							}
						}
						ContextMenuState.RightClickPressPos[SlotIndex] = { static_cast<float>(PressClientPos.x), static_cast<float>(PressClientPos.y) };
					}
					if (ContextMenuState.RightClickTracking[SlotIndex] && InputContext.Frame.IsDown(VK_RBUTTON))
					{
						const LONG Dx = InputContext.Frame.MouseDelta.x;
						const LONG Dy = InputContext.Frame.MouseDelta.y;
						ContextMenuState.RightClickTravelSq[SlotIndex] += static_cast<float>(Dx * Dx + Dy * Dy);
					}
					if (ImGui::IsMouseReleased(1) && InputContext.WasReleased(VK_RBUTTON))
					{
						const bool bRightDragGesture =
							InputContext.Frame.bRightDragging
							|| InputContext.WasPointerDragEnded(EPointerButton::Right);
						const bool bClickCandidate = ContextMenuState.RightClickTracking[SlotIndex]
							&& ContextMenuState.RightClickTravelSq[SlotIndex] <= RightClickPopupThresholdSq
							&& IsViewportInteractiveHover(SlotIndex, MousePos.x, MousePos.y)
							&& !bRightDragGesture;
						ContextMenuState.RightClickTracking[SlotIndex] = false;
						ContextMenuState.RightClickTravelSq[SlotIndex] = 0.0f;
						const bool bHasModifier =
							InputContext.Frame.IsCtrlDown()
							|| InputContext.Frame.IsAltDown()
							|| InputContext.Frame.IsShiftDown();
						if (bClickCandidate)
						{
							if (bHasModifier)
							{
								continue;
							}

							// RMB 컨텍스트 오픈 시 LMB 단일 선택과 동일한 규칙으로 selection 동기화
							if (SelectionManager && VC->GetCamera())
							{
								UWorld* InteractionWorld = InputContext.TargetWorld ? InputContext.TargetWorld : (Editor ? Editor->GetWorld() : nullptr);
								if (InteractionWorld)
								{
									const FRect& ViewRect = VC->GetViewportScreenRect();
									const float LocalMouseX = Clamp(
										ContextMenuState.RightClickPressPos[SlotIndex].X - ViewRect.X,
										0.0f,
										(std::max)(0.0f, ViewRect.Width - 1.0f));
									const float LocalMouseY = Clamp(
										ContextMenuState.RightClickPressPos[SlotIndex].Y - ViewRect.Y,
										0.0f,
										(std::max)(0.0f, ViewRect.Height - 1.0f));
									const float VPWidth = VC->GetViewport() ? static_cast<float>(VC->GetViewport()->GetWidth()) : VC->GetWindowWidth();
									const float VPHeight = VC->GetViewport() ? static_cast<float>(VC->GetViewport()->GetHeight()) : VC->GetWindowHeight();
									const FRay Ray = VC->GetCamera()->DeprojectScreenToWorld(LocalMouseX, LocalMouseY, VPWidth, VPHeight);

									FHitResult HitResult{};
									AActor* BestActor = nullptr;
									const FEditorSettings& Settings = FEditorSettings::Get();
									if (Settings.PickingMode == EEditorPickingMode::RayTriangle)
									{
										InteractionWorld->RaycastPrimitives(Ray, HitResult, BestActor);
									}
									else
									{
										POINT ClickLocal =
										{
											static_cast<LONG>(LocalMouseX),
											static_cast<LONG>(LocalMouseY)
										};
										VC->PickActorByIdAtLocalPoint(ClickLocal, BestActor);
									}
									if (BestActor)
									{
										SelectionManager->Select(BestActor);
									}
									else
									{
										SelectionManager->ClearSelection();
									}
								}
							}

							ContextMenuState.PendingPopupSlot = SlotIndex;
							ContextMenuState.PendingPopupPos = {
								ContextMenuState.RightClickPressPos[SlotIndex].X,
								ContextMenuState.RightClickPressPos[SlotIndex].Y + 2.0f };
							ContextMenuState.PendingSpawnPos = ContextMenuState.RightClickPressPos[SlotIndex];
							ContextMenuState.bForceNextPopupPos = true;
							break;
						}
					}
				}
			}
		}
	}

	RenderPlaceActorPopup(bPlaceActorPopupWasOpen);

	ImGui::End();
	ImGui::PopStyleVar();
}

bool FLevelViewportLayout::IsViewportInteractiveHover(int32 SlotIndex, float MouseX, float MouseY) const
{
	if (SlotIndex < 0 || SlotIndex >= MaxViewportSlots || !ViewportWindows[SlotIndex])
	{
		return false;
	}

	const FPoint MousePoint = { MouseX, MouseY };
	if (!ViewportWindows[SlotIndex]->IsHover(MousePoint))
	{
		return false;
	}

	const FRect& SlotRect = ViewportWindows[SlotIndex]->GetRect();
	return MouseY >= (SlotRect.Y + EditorViewportInputUtils::PaneToolbarHeightF);
}

void FLevelViewportLayout::HandleSplitterInteraction(const ImVec2& MousePos, const POINT& MousePoint)
{
	if (!RootSplitter)
	{
		return;
	}

	if (ImGui::IsMouseClicked(0))
	{
		const FPoint QueryPoint = { static_cast<float>(MousePoint.x), static_cast<float>(MousePoint.y) };
		DraggingSplitter = SSplitter::FindSplitterAtBar(RootSplitter, QueryPoint);
	}

	if (ImGui::IsMouseReleased(0))
	{
		DraggingSplitter = nullptr;
	}

	if (DraggingSplitter)
	{
		const FRect& DragRect = DraggingSplitter->GetRect();
		if (DraggingSplitter->GetOrientation() == ESplitOrientation::Horizontal)
		{
			const float NewRatio = (MousePos.x - DragRect.X) / DragRect.Width;
			DraggingSplitter->SetRatio(Clamp(NewRatio, 0.15f, 0.85f));
			ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
		}
		else
		{
			const float NewRatio = (MousePos.y - DragRect.Y) / DragRect.Height;
			DraggingSplitter->SetRatio(Clamp(NewRatio, 0.15f, 0.85f));
			ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
		}
		return;
	}

	const FPoint HoverPoint = { static_cast<float>(MousePoint.x), static_cast<float>(MousePoint.y) };
	SSplitter* Hovered = SSplitter::FindSplitterAtBar(RootSplitter, HoverPoint);
	if (!Hovered)
	{
		return;
	}

	if (Hovered->GetOrientation() == ESplitOrientation::Horizontal)
	{
		ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
	}
	else
	{
		ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
	}
}

void FLevelViewportLayout::HandleViewportActivationOnClick(const ImVec2& MousePos, int32 ActiveSlotCount, int32 OnePaneSlotIndex)
{
	if (DraggingSplitter || (!ImGui::IsMouseClicked(0) && !ImGui::IsMouseClicked(1)))
	{
		return;
	}

	for (int32 i = 0; i < ActiveSlotCount; ++i)
	{
		const int32 SlotIndex = (CurrentLayout == EViewportLayout::OnePane) ? OnePaneSlotIndex : i;
		if (SlotIndex < static_cast<int32>(LevelViewportClients.size())
			&& SlotIndex < MaxViewportSlots
			&& IsViewportInteractiveHover(SlotIndex, MousePos.x, MousePos.y))
		{
			LastUserActivatedViewportClient = LevelViewportClients[SlotIndex];
			if (LevelViewportClients[SlotIndex] != ActiveViewportClient)
			{
				SetActiveViewport(LevelViewportClients[SlotIndex]);
			}
			break;
		}
	}
}

void FLevelViewportLayout::RenderPlaceActorPopup(bool bPlaceActorPopupWasOpen)
{
	if (ContextMenuState.PendingPopupSlot >= 0)
	{
		if (ContextMenuState.PendingPopupSlot < static_cast<int32>(LevelViewportClients.size()))
		{
			SetActiveViewport(LevelViewportClients[ContextMenuState.PendingPopupSlot]);
		}
		ImGui::SetNextWindowPos(ImVec2(ContextMenuState.PendingPopupPos.X, ContextMenuState.PendingPopupPos.Y), ImGuiCond_Always);
		ImGui::OpenPopup("##ViewportPlaceActorPopup");
		ContextMenuState.PendingPopupSlot = -1;
	}

	ImGui::SetNextWindowSize(ImVec2(110.0f, 0.0f), ImGuiCond_Appearing);
	ImGui::SetNextWindowSizeConstraints(ImVec2(130.0f, 40.f), ImVec2(130.0f, FLT_MAX));
	if (ImGui::BeginPopup("##ViewportPlaceActorPopup"))
	{
		if (ContextMenuState.bForceNextPopupPos)
		{
			ImGui::SetWindowPos(ImVec2(ContextMenuState.PendingPopupPos.X, ContextMenuState.PendingPopupPos.Y), ImGuiCond_Always);
			ContextMenuState.bForceNextPopupPos = false;
		}
		if (ImGui::BeginMenu("Place Actor"))
		{
			if (Editor)
			{
				const int32 SpawnX = static_cast<int32>(ContextMenuState.PendingSpawnPos.X);
				const int32 SpawnY = static_cast<int32>(ContextMenuState.PendingSpawnPos.Y);
				for (const UEditorEngine::FPlaceableActorEntry& Entry : Editor->GetPlaceableActorEntries())
				{
					if (ImGui::MenuItem(Entry.DisplayName.c_str()))
					{
						Editor->PlaceActorFromScreenPointById(Entry.Id, SpawnX, SpawnY);
					}
				}
			}
			ImGui::EndMenu();
		}

		const bool bCanDeleteSelection = SelectionManager && !SelectionManager->IsEmpty();
		if (ImGui::MenuItem("Delete", "Del", false, bCanDeleteSelection))
		{
			const TArray<AActor*> SelectedActors = SelectionManager->GetSelectedActors();
			for (AActor* Actor : SelectedActors)
			{
				if (Actor && Actor->GetWorld())
				{
					Actor->GetWorld()->DestroyActor(Actor);
				}
			}
			SelectionManager->ClearSelection();
		}
		ImGui::EndPopup();
	}

	const bool bPlaceActorPopupIsOpen = ImGui::IsPopupOpen("##ViewportPlaceActorPopup");
	if (bPlaceActorPopupWasOpen
		&& !bPlaceActorPopupIsOpen
		&& (ImGui::IsMouseDown(ImGuiMouseButton_Left) || ImGui::IsMouseDown(ImGuiMouseButton_Right)))
	{
		bRequestViewportMouseSuppress = true;
	}
}

bool FLevelViewportLayout::ConsumeViewportMouseSuppressRequest()
{
	const bool bRequested = bRequestViewportMouseSuppress;
	bRequestViewportMouseSuppress = false;
	return bRequested;
}

// ─── 각 뷰포트 패인 툴바 오버레이 ──────────────────────────

void FLevelViewportLayout::RenderPaneToolbar(int32 SlotIndex)
{
	if (SlotIndex >= MaxViewportSlots || !ViewportWindows[SlotIndex]) return;
	if (SlotIndex >= static_cast<int32>(LevelViewportClients.size())) return;

	const FRect& PaneRect = ViewportWindows[SlotIndex]->GetRect();
	if (PaneRect.Width <= 0 || PaneRect.Height <= 0) return;

	EnsureToolbarIconsLoaded(RendererPtr);
	ID3D11ShaderResourceView** ToolbarIcons = GetToolbarIconTable();

	auto DrawToolbarTextButton = [](const char* Id, const char* Label, bool bPairFirst = false, bool bPairSecond = false) -> bool
	{
		const ImVec2 TextSize = ImGui::CalcTextSize(Label);
		const ImVec2 Padding = ImGui::GetStyle().FramePadding;
		const ImVec2 ButtonSize(TextSize.x + Padding.x * 2.0f, ImGui::GetFrameHeight());
		const bool bPressed = ImGui::InvisibleButton(Id, ButtonSize);
		const ImVec2 Min = ImGui::GetItemRectMin();
		const ImVec2 Max = ImGui::GetItemRectMax();
		const bool bHovered = ImGui::IsItemHovered();
		const bool bHeld = ImGui::IsItemActive();
		const ImU32 BgColor = ImGui::GetColorU32(bHeld ? ImGuiCol_ButtonActive : (bHovered ? ImGuiCol_ButtonHovered : ImGuiCol_Button));
		ImDrawFlags RoundFlags = ImDrawFlags_RoundCornersAll;
		if (bPairFirst) RoundFlags = ImDrawFlags_RoundCornersLeft;
		if (bPairSecond) RoundFlags = ImDrawFlags_RoundCornersRight;
		ImGui::GetWindowDrawList()->AddRectFilled(Min, Max, BgColor, ImGui::GetStyle().FrameRounding, RoundFlags);
		const ImVec2 TextPos(Min.x + (ButtonSize.x - TextSize.x) * 0.5f, Min.y + (ButtonSize.y - TextSize.y) * 0.5f);
		ImGui::GetWindowDrawList()->AddText(TextPos, ImGui::GetColorU32(ImGuiCol_Text), Label);
		ImGui::GetWindowDrawList()->AddText(ImVec2(TextPos.x + 0.8f, TextPos.y), ImGui::GetColorU32(ImGuiCol_Text), Label);
		return bPressed;
	};

	auto DrawToolbarIconButton = [&DrawToolbarTextButton, ToolbarIcons](const char* Id, EToolbarIcon Icon, const char* FallbackLabel, float FallbackIconSize, float MaxIconSize, bool bPairFirst = false, bool bPairSecond = false) -> bool
	{
		ID3D11ShaderResourceView* IconSRV = ToolbarIcons[static_cast<int32>(Icon)];
		if (!IconSRV)
		{
			return DrawToolbarTextButton(Id, FallbackLabel, bPairFirst, bPairSecond);
		}

		const ImVec2 IconSize = GetToolbarIconRenderSize(Icon, FallbackIconSize, MaxIconSize);
		const ImVec2 Padding = ImGui::GetStyle().FramePadding;
		const ImVec2 ButtonSize(IconSize.x + Padding.x * 2.0f, ImGui::GetFrameHeight());
		const bool bPressed = ImGui::InvisibleButton(Id, ButtonSize);
		const ImVec2 Min = ImGui::GetItemRectMin();
		const ImVec2 Max = ImGui::GetItemRectMax();
		const bool bHovered = ImGui::IsItemHovered();
		const bool bHeld = ImGui::IsItemActive();
		const ImU32 BgColor = ImGui::GetColorU32(bHeld ? ImGuiCol_ButtonActive : (bHovered ? ImGuiCol_ButtonHovered : ImGuiCol_Button));
		ImDrawFlags RoundFlags = ImDrawFlags_RoundCornersAll;
		if (bPairFirst) RoundFlags = ImDrawFlags_RoundCornersLeft;
		if (bPairSecond) RoundFlags = ImDrawFlags_RoundCornersRight;
		ImGui::GetWindowDrawList()->AddRectFilled(Min, Max, BgColor, ImGui::GetStyle().FrameRounding, RoundFlags);
		ImGui::GetWindowDrawList()->AddImage(
			(ImTextureID)IconSRV,
			ImVec2(Min.x + Padding.x, Min.y + (ButtonSize.y - IconSize.y) * 0.5f),
			ImVec2(Min.x + Padding.x + IconSize.x, Min.y + (ButtonSize.y + IconSize.y) * 0.5f));
		return bPressed;
	};

	auto DrawToolbarImageButton = [](const char* Id, ID3D11ShaderResourceView* IconSRV, ImVec2 IconSize, bool bPairFirst = false, bool bPairSecond = false) -> bool
	{
		if (!IconSRV)
		{
			return false;
		}

		const ImVec2 Padding = ImGui::GetStyle().FramePadding;
		const ImVec2 ButtonSize(IconSize.x + Padding.x * 2.0f, ImGui::GetFrameHeight());
		const bool bPressed = ImGui::InvisibleButton(Id, ButtonSize);
		const ImVec2 Min = ImGui::GetItemRectMin();
		const ImVec2 Max = ImGui::GetItemRectMax();
		const bool bHovered = ImGui::IsItemHovered();
		const bool bHeld = ImGui::IsItemActive();
		const ImU32 BgColor = ImGui::GetColorU32(bHeld ? ImGuiCol_ButtonActive : (bHovered ? ImGuiCol_ButtonHovered : ImGuiCol_Button));
		ImDrawFlags RoundFlags = ImDrawFlags_RoundCornersAll;
		if (bPairFirst) RoundFlags = ImDrawFlags_RoundCornersLeft;
		if (bPairSecond) RoundFlags = ImDrawFlags_RoundCornersRight;
		ImGui::GetWindowDrawList()->AddRectFilled(Min, Max, BgColor, ImGui::GetStyle().FrameRounding, RoundFlags);
		ImGui::GetWindowDrawList()->AddImage(
			(ImTextureID)IconSRV,
			ImVec2(Min.x + Padding.x, Min.y + (ButtonSize.y - IconSize.y) * 0.5f),
			ImVec2(Min.x + Padding.x + IconSize.x, Min.y + (ButtonSize.y + IconSize.y) * 0.5f));
		return bPressed;
	};

	char OverlayID[64];
	snprintf(OverlayID, sizeof(OverlayID), "##PaneToolbar_%d", SlotIndex);
	constexpr float PaneToolbarHeight = EditorViewportInputUtils::PaneToolbarHeightF;
	ImGui::SetNextWindowPos(ImVec2(PaneRect.X, PaneRect.Y));
	ImGui::SetNextWindowBgAlpha(1.0f);
	ImGui::SetNextWindowSize(ImVec2(PaneRect.Width, PaneToolbarHeight), ImGuiCond_Always);

	const ImGuiWindowFlags OverlayFlags =
		ImGuiWindowFlags_NoDecoration |
		ImGuiWindowFlags_NoSavedSettings |
		ImGuiWindowFlags_NoFocusOnAppearing |
		ImGuiWindowFlags_NoNav |
		ImGuiWindowFlags_NoMove |
		ImGuiWindowFlags_NoScrollbar;

	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.0f, 4.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(5.0f, 2.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
	ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.12f, 0.13f, 0.16f, 0.98f));
	ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.22f, 0.26f, 0.95f));
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.29f, 0.35f, 1.0f));
	ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.16f, 0.30f, 0.53f, 1.0f));
	ImGui::Begin(OverlayID, nullptr, OverlayFlags);
	{
		ImGui::PushID(SlotIndex);

		FLevelEditorViewportClient* VC = LevelViewportClients[SlotIndex];
		FViewportRenderOptions& Opts = VC->GetRenderOptions();
		FEditorViewportController* InputController = VC->GetInputController();
		FEditorNavigationTool* NavTool = InputController ? InputController->GetNavigationTool() : nullptr;
		const bool bOnePane = (CurrentLayout == EViewportLayout::OnePane);
		const bool bPIEPossessed = IsPIEPlayingAndPossessed(Editor);
		UGizmoComponent* Gizmo = Editor ? Editor->GetGizmo() : nullptr;
		FEditorSettings& Settings = FEditorSettings::Get();
		if (bPIEPossessed)
		{
			static const char* ViewModeNames[] = { "Lit", "Unlit", "Wireframe", "Depth", "Scene Depth" };
			int32 ViewModeIdx = static_cast<int32>(Opts.ViewMode);
			if (ViewModeIdx < 0 || ViewModeIdx >= static_cast<int32>(IM_ARRAYSIZE(ViewModeNames)))
			{
				ViewModeIdx = 0;
			}

			char ViewModeButtonLabel[48];
			snprintf(ViewModeButtonLabel, sizeof(ViewModeButtonLabel), "View Mode: %s ▼", ViewModeNames[ViewModeIdx]);
			char ViewModePopupID[64];
			snprintf(ViewModePopupID, sizeof(ViewModePopupID), "##PIEViewModePopup_%d", SlotIndex);
			if (ImGui::Button(ViewModeButtonLabel))
			{
				ImGui::OpenPopup(ViewModePopupID);
			}
			if (ImGui::BeginPopup(ViewModePopupID))
			{
				for (int32 i = 0; i < static_cast<int32>(IM_ARRAYSIZE(ViewModeNames)); ++i)
				{
					const bool bSelected = (i == ViewModeIdx);
					if (ImGui::Selectable(ViewModeNames[i], bSelected))
					{
						Opts.ViewMode = static_cast<EViewMode>(i);
					}
				}
				ImGui::EndPopup();
			}

			ImGui::PopID();
			ImGui::End();
			ImGui::PopStyleColor(4);
			ImGui::PopStyleVar(4);
			return;
		}

		static bool GWorldSpaceState[MaxViewportSlots] = { true, true, true, true };
		static bool GTSnapEnabled[MaxViewportSlots] = { false, false, false, false };
		static bool GRSnapEnabled[MaxViewportSlots] = { false, false, false, false };
		static bool GSSnapEnabled[MaxViewportSlots] = { false, false, false, false };
		static int32 GTSnapIndex[MaxViewportSlots] = { 1, 1, 1, 1 };
		static int32 GRSnapIndex[MaxViewportSlots] = { 1, 1, 1, 1 };
		static int32 GSSnapIndex[MaxViewportSlots] = { 1, 1, 1, 1 };
		const char* TranslateSnapLabels[] = { "1", "5", "10", "50", "100" };
		const char* RotateSnapLabels[] = { "5", "10", "15", "30", "45" };
		const char* ScaleSnapLabels[] = { "0.1", "0.25", "0.5", "1.0", "5.0" };
		const float TranslateSnapValues[] = { 1.0f, 5.0f, 10.0f, 50.0f, 100.0f };
		const float RotateSnapValues[] = { 5.0f, 10.0f, 15.0f, 30.0f, 45.0f };
		const float ScaleSnapValues[] = { 0.1f, 0.25f, 0.5f, 1.0f, 5.0f };

		constexpr float ToolbarFallbackIconSize = 14.0f;
		constexpr float ToolbarMaxIconSize = 16.0f;

		// Gizmo 상태를 UI 캐시와 매 프레임 동기화한다.
		// (X 단축키는 Gizmo 상태만 바꾸므로, 동기화하지 않으면 툴바 아이콘이 갱신되지 않는다.)
		if (Gizmo)
		{
			GWorldSpaceState[SlotIndex] = Gizmo->IsWorldSpace();
		}

		auto DrawTransformIcon = [&](const char* Id, EToolbarIcon Icon, EEditorViewportModeType TargetMode) -> bool
		{
			const bool bSelected = (VC->GetInteractionMode() == TargetMode);
			if (bSelected)
			{
				ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.33f, 0.46f, 0.63f, 1.0f));
				ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.37f, 0.52f, 0.70f, 1.0f));
				ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.27f, 0.39f, 0.54f, 1.0f));
			}
			const bool bPressed = DrawToolbarIconButton(Id, Icon, Id, ToolbarFallbackIconSize, ToolbarMaxIconSize);
			if (bSelected)
			{
				ImGui::PopStyleColor(3);
			}
			return bPressed;
		};

		if (DrawTransformIcon("##SelectTool", EToolbarIcon::Select, EEditorViewportModeType::Select))
		{
			Opts.ShowFlags.bGizmo = false;
			VC->SetInteractionMode(EEditorViewportModeType::Select);
		}
		ImGui::SameLine();
		if (DrawTransformIcon("##TranslateTool", EToolbarIcon::Translate, EEditorViewportModeType::Translate))
		{
			Opts.ShowFlags.bGizmo = true;
			VC->SetInteractionMode(EEditorViewportModeType::Translate);
			if (Gizmo) Gizmo->SetTranslateMode();
		}
		ImGui::SameLine();
		if (DrawTransformIcon("##RotateTool", EToolbarIcon::Rotate, EEditorViewportModeType::Rotate))
		{
			Opts.ShowFlags.bGizmo = true;
			VC->SetInteractionMode(EEditorViewportModeType::Rotate);
			if (Gizmo) Gizmo->SetRotateMode();
		}
		ImGui::SameLine();
		if (DrawTransformIcon("##ScaleTool", EToolbarIcon::Scale, EEditorViewportModeType::Scale))
		{
			Opts.ShowFlags.bGizmo = true;
			VC->SetInteractionMode(EEditorViewportModeType::Scale);
			if (Gizmo) Gizmo->SetScaleMode();
		}

		ImGui::SameLine(0.0f, 10.0f);
		const ImVec2 SeparatorStart = ImGui::GetCursorScreenPos();
		const float SeparatorHeight = ImGui::GetFrameHeight() - 4.0f;
		ImGui::GetWindowDrawList()->AddLine(
			ImVec2(SeparatorStart.x, SeparatorStart.y + 2.0f),
			ImVec2(SeparatorStart.x, SeparatorStart.y + 2.0f + SeparatorHeight),
			IM_COL32(155, 155, 155, 255),
			1.0f);
		ImGui::Dummy(ImVec2(1.0f, ImGui::GetFrameHeight()));

		ImGui::SameLine(0.0f, 10.0f);
		const EToolbarIcon CoordinateIcon = GWorldSpaceState[SlotIndex] ? EToolbarIcon::WorldSpace : EToolbarIcon::LocalSpace;
		if (DrawToolbarIconButton("##CoordinateSpaceToggle", CoordinateIcon, GWorldSpaceState[SlotIndex] ? "World" : "Local", ToolbarFallbackIconSize, ToolbarMaxIconSize))
		{
			GWorldSpaceState[SlotIndex] = !GWorldSpaceState[SlotIndex];
			if (Gizmo) Gizmo->SetWorldSpace(GWorldSpaceState[SlotIndex]);
		}

		auto DrawSnapSection = [&](EToolbarIcon SnapIcon, const char* Prefix, bool& bEnabled, int32& ValueIndex, const char* const* Labels, int32 LabelCount)
		{
			ImGui::SameLine(0.0f, 6.0f);
			const bool bWasEnabled = bEnabled;
			if (bWasEnabled)
			{
				ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.24f, 0.43f, 0.30f, 1.0f));
				ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.30f, 0.50f, 0.36f, 1.0f));
				ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.20f, 0.36f, 0.26f, 1.0f));
			}
			char ToggleID[48];
			snprintf(ToggleID, sizeof(ToggleID), "##%sSnapToggle_%d", Prefix, SlotIndex);
			if (DrawToolbarIconButton(ToggleID, SnapIcon, Prefix, ToolbarFallbackIconSize, ToolbarMaxIconSize, true, false))
			{
				bEnabled = !bEnabled;
			}
			if (bWasEnabled)
			{
				ImGui::PopStyleColor(3);
			}

			ImGui::SameLine(0.0f, 0.0f);
			char ValueButtonLabel[24];
			snprintf(ValueButtonLabel, sizeof(ValueButtonLabel), "%s ▼", Labels[ValueIndex]);
			char PopupID[40];
			snprintf(PopupID, sizeof(PopupID), "##%sSnapPopup_%d", Prefix, SlotIndex);
			char ValueBtnID[48];
			snprintf(ValueBtnID, sizeof(ValueBtnID), "##%sSnapValueBtn_%d", Prefix, SlotIndex);
			if (DrawToolbarTextButton(ValueBtnID, ValueButtonLabel, false, true))
			{
				ImGui::OpenPopup(PopupID);
			}
			if (ImGui::BeginPopup(PopupID))
			{
				for (int32 i = 0; i < LabelCount; ++i)
				{
					const bool bSelected = (ValueIndex == i);
					if (ImGui::RadioButton(Labels[i], bSelected))
					{
						ValueIndex = i;
					}
				}
				ImGui::EndPopup();
			}
		};

		if (bOnePane)
		{
			DrawSnapSection(EToolbarIcon::TranslateSnap, "T", GTSnapEnabled[SlotIndex], GTSnapIndex[SlotIndex], TranslateSnapLabels, IM_ARRAYSIZE(TranslateSnapLabels));
			DrawSnapSection(EToolbarIcon::RotateSnap, "R", GRSnapEnabled[SlotIndex], GRSnapIndex[SlotIndex], RotateSnapLabels, IM_ARRAYSIZE(RotateSnapLabels));
			DrawSnapSection(EToolbarIcon::ScaleSnap, "S", GSSnapEnabled[SlotIndex], GSSnapIndex[SlotIndex], ScaleSnapLabels, IM_ARRAYSIZE(ScaleSnapLabels));
		}
		if (Gizmo && VC == ActiveViewportClient)
		{
			Gizmo->SetTranslateSnap(GTSnapEnabled[SlotIndex], TranslateSnapValues[GTSnapIndex[SlotIndex]]);
			Gizmo->SetRotateSnap(GRSnapEnabled[SlotIndex], RotateSnapValues[GRSnapIndex[SlotIndex]]);
			Gizmo->SetScaleSnap(GSSnapEnabled[SlotIndex], ScaleSnapValues[GSSnapIndex[SlotIndex]]);
		}

		ImGui::SameLine();
		static const char* ViewportTypeNames[] = {
			"Perspective", "Top", "Bottom", "Left", "Right", "Front", "Back", "Free Orthographic"
		};
		const int32 CurrentTypeIdx = static_cast<int32>(Opts.ViewportType);
		const char* CurrentTypeName = ViewportTypeNames[CurrentTypeIdx];
		char VTPopupID[64];
		snprintf(VTPopupID, sizeof(VTPopupID), "ViewportTypePopup_%d", SlotIndex);
		char CameraPopupID[48];
		snprintf(CameraPopupID, sizeof(CameraPopupID), "##CameraSpeedPopup_%d", SlotIndex);
		char SettingsPopupID[64];
		snprintf(SettingsPopupID, sizeof(SettingsPopupID), "SettingsPopup_%d", SlotIndex);
		char LayoutPopupID[64];
		snprintf(LayoutPopupID, sizeof(LayoutPopupID), "LayoutPopup_%d", SlotIndex);

		auto CalcButtonWidth = [ToolbarFallbackIconSize, ToolbarMaxIconSize](const char* Label, EToolbarIcon Icon, bool bIconButton) -> float
		{
			if (bIconButton)
			{
				const ImVec2 IconSize = GetToolbarIconRenderSize(Icon, ToolbarFallbackIconSize, ToolbarMaxIconSize);
				return IconSize.x + ImGui::GetStyle().FramePadding.x * 2.0f;
			}
			return ImGui::CalcTextSize(Label).x + ImGui::GetStyle().FramePadding.x * 2.0f;
		};

		const float RuntimeMultiplier = NavTool ? NavTool->GetRuntimeCameraSpeedMultiplier() : 1.0f;
		char CameraValueLabel[40];
		snprintf(CameraValueLabel, sizeof(CameraValueLabel), "Cam %.1fx ▼", Settings.CameraSpeed * RuntimeMultiplier);

		float RightWidth = 0.0f;
		int32 RightCount = 0;
		auto AddWidth = [&](float W)
		{
			if (RightCount > 0) RightWidth += ImGui::GetStyle().ItemSpacing.x;
			RightWidth += W;
			++RightCount;
		};
		AddWidth(CalcButtonWidth(CurrentTypeName, EToolbarIcon::Menu, false));
		if (bOnePane) AddWidth(CalcButtonWidth(CameraValueLabel, EToolbarIcon::Camera, false));
		AddWidth(CalcButtonWidth("Settings", EToolbarIcon::Setting, true));
		AddWidth(CalcButtonWidth("Layout", EToolbarIcon::Menu, true));
		AddWidth(CalcButtonWidth("Split", EToolbarIcon::Menu, true));

		const float RightStartX = ImGui::GetWindowContentRegionMax().x - RightWidth;
		const float CurrentCursorX = ImGui::GetCursorPosX();
		const float OverflowButtonWidth = CalcButtonWidth("Menu", EToolbarIcon::Menu, true);
		const bool bUseOverflowMenu = (RightStartX <= CurrentCursorX + 6.0f);

		auto DrawSettingsContent = [&]()
		{
			ImGui::Text("View Mode");
			int32 CurrentMode = static_cast<int32>(Opts.ViewMode);
			ImGui::RadioButton("Lit", &CurrentMode, static_cast<int32>(EViewMode::Lit));
			ImGui::SameLine();
			ImGui::RadioButton("Unlit", &CurrentMode, static_cast<int32>(EViewMode::Unlit));
			ImGui::SameLine();
			ImGui::RadioButton("Wireframe", &CurrentMode, static_cast<int32>(EViewMode::Wireframe));
			ImGui::SameLine();
			ImGui::RadioButton("Depth", &CurrentMode, static_cast<int32>(EViewMode::Depth));
			ImGui::SameLine();
			ImGui::RadioButton("Scene Depth", &CurrentMode, static_cast<int32>(EViewMode::SceneDepth));
			Opts.ViewMode = static_cast<EViewMode>(CurrentMode);

			ImGui::Separator();
			ImGui::Text("Show");
			ImGui::Checkbox("Primitives", &Opts.ShowFlags.bPrimitives);
			ImGui::Checkbox("BillboardText", &Opts.ShowFlags.bBillboardText);
			ImGui::Checkbox("Grid", &Opts.ShowFlags.bGrid);
			ImGui::Checkbox("Decal", &Opts.ShowFlags.bDecal);
			ImGui::Checkbox("World Axis", &Opts.ShowFlags.bWorldAxis);
			ImGui::Checkbox("Gizmo", &Opts.ShowFlags.bGizmo);
			ImGui::Checkbox("Bounding Volume", &Opts.ShowFlags.bBoundingVolume);
			ImGui::Checkbox("Debug Draw", &Opts.ShowFlags.bDebugDraw);
			ImGui::Checkbox("Octree", &Opts.ShowFlags.bOctree);

			ImGui::Separator();
			ImGui::Text("Grid");
			ImGui::SliderFloat("Spacing", &Opts.GridSpacing, 0.1f, 10.0f, "%.1f");
			ImGui::SliderInt("Half Line Count", &Opts.GridHalfLineCount, 10, 500);

			ImGui::Separator();
			ImGui::Text("Camera");
			ImGui::SliderFloat("Move Sensitivity", &Opts.CameraMoveSensitivity, 0.1f, 5.0f, "%.1f");
			ImGui::SliderFloat("Rotate Sensitivity", &Opts.CameraRotateSensitivity, 0.1f, 5.0f, "%.1f");
		};

		const EViewportLayout NextToggleLayout = (CurrentLayout == EViewportLayout::OnePane)
			? (LastSplitLayout == EViewportLayout::OnePane ? EViewportLayout::FourPanes2x2 : LastSplitLayout)
			: EViewportLayout::OnePane;
		int32 ToggleIdx = static_cast<int32>(NextToggleLayout);
		if (ToggleIdx < 0 || ToggleIdx >= static_cast<int32>(EViewportLayout::MAX))
		{
			ToggleIdx = (CurrentLayout == EViewportLayout::OnePane)
				? static_cast<int32>(EViewportLayout::FourPanes2x2)
				: static_cast<int32>(EViewportLayout::OnePane);
		}
		const char* ToggleLabel = (CurrentLayout == EViewportLayout::OnePane) ? "Split" : "Merge";

		if (!bUseOverflowMenu)
		{
			if (RightStartX > ImGui::GetCursorPosX())
			{
				ImGui::SetCursorPosX(RightStartX);
			}

			if (DrawToolbarTextButton("##ViewportTypeBtn", CurrentTypeName))
			{
				ImGui::OpenPopup(VTPopupID);
			}
			if (ImGui::BeginPopup(VTPopupID))
			{
				for (int32 t = 0; t < static_cast<int32>(IM_ARRAYSIZE(ViewportTypeNames)); ++t)
				{
					const bool bSelected = (t == CurrentTypeIdx);
					if (ImGui::Selectable(ViewportTypeNames[t], bSelected))
					{
						VC->SetViewportType(static_cast<ELevelViewportType>(t));
					}
				}
				ImGui::EndPopup();
			}

			ImGui::SameLine();
			if (bOnePane && DrawToolbarTextButton("##CameraSpeedBtn", CameraValueLabel))
			{
				ImGui::OpenPopup(CameraPopupID);
			}
			if (bOnePane && ImGui::BeginPopup(CameraPopupID))
			{
				float CameraSpeed = Settings.CameraSpeed * RuntimeMultiplier;
				if (ImGui::SliderFloat("Speed", &CameraSpeed, FEditorNavigationTool::GetMinCameraSpeedValue(), FEditorNavigationTool::GetMaxCameraSpeedValue(), "%.1fx"))
				{
					Settings.CameraSpeed = CameraSpeed;
					if (NavTool)
					{
						NavTool->SetRuntimeCameraSpeedMultiplier(1.0f);
					}
				}
				ImGui::EndPopup();
			}

			ImGui::SameLine(0.0f, 8.0f);
			if (DrawToolbarIconButton("##SettingsIcon", EToolbarIcon::Setting, "Settings", ToolbarFallbackIconSize, ToolbarMaxIconSize))
			{
				ImGui::OpenPopup(SettingsPopupID);
			}
			if (ImGui::BeginPopup(SettingsPopupID))
			{
				DrawSettingsContent();
				ImGui::EndPopup();
			}

			ImGui::SameLine();
			if (DrawToolbarIconButton("##LayoutMenuIcon", EToolbarIcon::Menu, "Layout", ToolbarFallbackIconSize, ToolbarMaxIconSize, true, false))
			{
				ImGui::OpenPopup(LayoutPopupID);
			}
			if (ImGui::BeginPopup(LayoutPopupID))
			{
				constexpr int32 LayoutCount = static_cast<int32>(EViewportLayout::MAX);
				constexpr int32 Columns = 4;
				constexpr float IconSize = 32.0f;
				for (int32 i = 0; i < LayoutCount; ++i)
				{
					ImGui::PushID(i);
					const bool bSelected = (static_cast<EViewportLayout>(i) == CurrentLayout);
					if (bSelected)
					{
						ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.33f, 0.46f, 0.63f, 1.0f));
					}
					bool bClicked = false;
					if (LayoutIcons[i])
					{
						bClicked = ImGui::ImageButton("##LayoutIcon", (ImTextureID)LayoutIcons[i], ImVec2(IconSize, IconSize));
					}
					else
					{
						char Label[8];
						snprintf(Label, sizeof(Label), "%d", i);
						bClicked = ImGui::Button(Label, ImVec2(IconSize + 8, IconSize + 8));
					}
					if (bSelected)
					{
						ImGui::PopStyleColor();
					}
					if (bClicked)
					{
						SetLayout(static_cast<EViewportLayout>(i));
						ImGui::CloseCurrentPopup();
					}
					if ((i + 1) % Columns != 0 && i + 1 < LayoutCount)
					{
						ImGui::SameLine();
					}
					ImGui::PopID();
				}
				ImGui::EndPopup();
			}

			ImGui::SameLine(0.0f, 0.0f);
			if (LayoutIcons[ToggleIdx])
			{
				const ImVec2 IconSize = ImVec2(ToolbarMaxIconSize, ToolbarMaxIconSize);
				if (DrawToolbarImageButton("##SplitToggleIcon", LayoutIcons[ToggleIdx], IconSize, false, true))
				{
					if (LevelViewportClients[SlotIndex] != ActiveViewportClient)
					{
						SetActiveViewport(LevelViewportClients[SlotIndex]);
					}
					ToggleViewportSplit();
				}
			}
			else
			{
				if (DrawToolbarTextButton("##SplitToggleTextBtn", ToggleLabel, false, true))
				{
					if (LevelViewportClients[SlotIndex] != ActiveViewportClient)
					{
						SetActiveViewport(LevelViewportClients[SlotIndex]);
					}
					ToggleViewportSplit();
				}
			}
		}
		else
		{
			char OverflowPopupID[64];
			snprintf(OverflowPopupID, sizeof(OverflowPopupID), "##ToolbarOverflowPopup_%d", SlotIndex);
			const float OverflowStartX = ImGui::GetWindowContentRegionMax().x - OverflowButtonWidth;
			if (OverflowStartX > ImGui::GetCursorPosX())
			{
				ImGui::SetCursorPosX(OverflowStartX);
			}

			if (DrawToolbarIconButton("##ToolbarOverflowIcon", EToolbarIcon::Menu, "Menu", ToolbarFallbackIconSize, ToolbarMaxIconSize))
			{
				ImGui::OpenPopup(OverflowPopupID);
			}

			if (ImGui::BeginPopup(OverflowPopupID))
			{
				if (ImGui::BeginMenu("Viewport Type"))
				{
					for (int32 t = 0; t < static_cast<int32>(IM_ARRAYSIZE(ViewportTypeNames)); ++t)
					{
						const bool bSelected = (t == CurrentTypeIdx);
						if (ImGui::MenuItem(ViewportTypeNames[t], nullptr, bSelected))
						{
							VC->SetViewportType(static_cast<ELevelViewportType>(t));
						}
					}
					ImGui::EndMenu();
				}

				if (bOnePane)
				{
					ImGui::Separator();
					float CameraSpeed = Settings.CameraSpeed * RuntimeMultiplier;
					if (ImGui::SliderFloat("Camera Speed", &CameraSpeed, FEditorNavigationTool::GetMinCameraSpeedValue(), FEditorNavigationTool::GetMaxCameraSpeedValue(), "%.1fx"))
					{
						Settings.CameraSpeed = CameraSpeed;
						if (NavTool)
						{
							NavTool->SetRuntimeCameraSpeedMultiplier(1.0f);
						}
					}
				}

				ImGui::Separator();
				if (ImGui::BeginMenu("Settings"))
				{
					DrawSettingsContent();
					ImGui::EndMenu();
				}

				if (ImGui::BeginMenu("Layout"))
				{
					static const char* LayoutNames[] = {
						"One Pane",
						"Two Panes Horiz",
						"Two Panes Vert",
						"Three Panes Left",
						"Three Panes Right",
						"Three Panes Top",
						"Three Panes Bottom",
						"Four Panes 2x2",
						"Four Panes Left",
						"Four Panes Right",
						"Four Panes Top",
						"Four Panes Bottom"
					};
					constexpr int32 LayoutCount = static_cast<int32>(EViewportLayout::MAX);
					for (int32 i = 0; i < LayoutCount && i < static_cast<int32>(IM_ARRAYSIZE(LayoutNames)); ++i)
					{
						const bool bSelected = (static_cast<EViewportLayout>(i) == CurrentLayout);
						if (ImGui::MenuItem(LayoutNames[i], nullptr, bSelected))
						{
							SetLayout(static_cast<EViewportLayout>(i));
						}
					}
					ImGui::EndMenu();
				}

				ImGui::Separator();
				if (ImGui::MenuItem(ToggleLabel))
				{
					if (LevelViewportClients[SlotIndex] != ActiveViewportClient)
					{
						SetActiveViewport(LevelViewportClients[SlotIndex]);
					}
					ToggleViewportSplit();
					ImGui::CloseCurrentPopup();
				}
				ImGui::EndPopup();
			}
		}

		ImGui::PopID();
	}
	ImGui::End();
	ImGui::PopStyleColor(4);
	ImGui::PopStyleVar(4);
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
		UCameraComponent* Cam = LevelViewportClients[0]->GetCamera();
		if (Cam)
		{
			S.PerspCamLocation = Cam->GetWorldLocation();
			S.PerspCamRotation = Cam->GetRelativeRotation();
			const FCameraState& CS = Cam->GetCameraState();
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
		UCameraComponent* Cam = LevelViewportClients[0]->GetCamera();
		if (Cam)
		{
			Cam->SetRelativeLocation(S.PerspCamLocation);
			Cam->SetRelativeRotation(S.PerspCamRotation);

			FCameraState CS = Cam->GetCameraState();
			CS.FOV = S.PerspCamFOV * (3.14159265358979f / 180.0f); // deg → rad
			CS.NearZ = S.PerspCamNearClip;
			CS.FarZ = S.PerspCamFarClip;
			Cam->SetCameraState(CS);
		}
	}
}

