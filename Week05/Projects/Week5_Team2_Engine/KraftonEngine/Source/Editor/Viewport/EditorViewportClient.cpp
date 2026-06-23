#include "Editor/Viewport/EditorViewportClient.h"

#include "Editor/EditorEngine.h"
#include "Editor/Input/EditorViewportInputContexts.h"
#include "Editor/Input/EditorViewportInputMapping.h"
#include "Editor/Input/EditorViewportInputUtils.h"
#include "Editor/Input/EditorViewportController.h"
#include "Editor/Settings/EditorSettings.h"
#include "Engine/Runtime/Engine.h"
#include "Engine/Runtime/WindowsWindow.h"

#include "Viewport/Viewport.h"
#include "GameFramework/World.h"
#include "GameFramework/Level.h"
#include "Editor/Gizmo/TransformGizmo.h"
#include "Editor/Selection/SelectionManager.h"
#include "ImGui/imgui.h"
#include "Math/MathUtils.h"
#include <algorithm>

void FEditorViewportClient::Initialize(FWindowsWindow* InWindow)
{
	Window = InWindow;
}

void FEditorViewportClient::SetWorld(UWorld* InWorld)
{
	if (World == InWorld)
	{
		return;
	}

	EndDeferredSpatialIndexInvalidation();

	World = InWorld;
	if (World)
	{
		if (ULevel* ActiveLevel = World->GetActiveLevel())
		{
			ActiveLevel->GetRenderProxy().WarmupSpatialIndices();
		}
		if (ULevel* PersistentLevel = World->GetPersistentLevel())
		{
			if (PersistentLevel != World->GetActiveLevel())
			{
				PersistentLevel->GetRenderProxy().WarmupSpatialIndices();
			}
		}
	}
	ResetIdPickingState();
	if (InputController)
	{
		InputController->ResetInputState();
	}
}

void FEditorViewportClient::CreateCamera()
{
	DestroyCamera();
	Camera = std::make_unique<FViewportCamera>();
}

void FEditorViewportClient::DestroyCamera()
{
	Camera.reset();
}

void FEditorViewportClient::ResetCamera()
{
	if (!Camera || !Settings) return;
	Camera->SetWorldLocation(Settings->InitViewPos);
	Camera->LookAt(Settings->InitLookAt);
	EnsureInputController();
	if (InputController)
	{
		InputController->SyncNavigationFromCamera();
	}
}

void FEditorViewportClient::SetViewportType(ELevelViewportType NewType)
{
	if (!Camera) return;

	RenderOptions.ViewportType = NewType;

	if (NewType == ELevelViewportType::Perspective)
	{
		Camera->SetOrthographic(false);
		return;
	}

	// FreeOrthographic: 현재 카메라 위치/회전 유지, 투영만 Ortho로 전환
	if (NewType == ELevelViewportType::FreeOrthographic)
	{
		Camera->SetOrthographic(true);
		return;
	}

	// 고정 방향 Orthographic: 카메라를 프리셋 방향으로 설정
	Camera->SetOrthographic(true);

	constexpr float OrthoDistance = 50.0f;
	FVector Position = FVector(0, 0, 0);
	FVector Rotation = FVector(0, 0, 0); // (Roll, Pitch, Yaw)

	switch (NewType)
	{
	case ELevelViewportType::Top:
		Position = FVector(0, 0, OrthoDistance);
		Rotation = FVector(0, 90.0f, 0);	// Pitch down (positive pitch = look -Z)
		break;
	case ELevelViewportType::Bottom:
		Position = FVector(0, 0, -OrthoDistance);
		Rotation = FVector(0, -90.0f, 0);	// Pitch up (negative pitch = look +Z)
		break;
	case ELevelViewportType::Front:
		Position = FVector(OrthoDistance, 0, 0);
		Rotation = FVector(0, 0, 180.0f);	// Yaw to look -X
		break;
	case ELevelViewportType::Back:
		Position = FVector(-OrthoDistance, 0, 0);
		Rotation = FVector(0, 0, 0.0f);		// Yaw to look +X
		break;
	case ELevelViewportType::Left:
		Position = FVector(0, -OrthoDistance, 0);
		Rotation = FVector(0, 0, 90.0f);	// Yaw to look +Y
		break;
	case ELevelViewportType::Right:
		Position = FVector(0, OrthoDistance, 0);
		Rotation = FVector(0, 0, -90.0f);	// Yaw to look -Y
		break;
	default:
		break;
	}

	Camera->SetRelativeLocation(Position);
	Camera->SetRelativeRotation(Rotation);
	EnsureInputController();
	if (InputController)
	{
		InputController->SyncNavigationFromCamera();
	}
}

void FEditorViewportClient::SetViewportSize(float InWidth, float InHeight)
{
	if (InWidth > 0.0f)
	{
		WindowWidth = InWidth;
	}

	if (InHeight > 0.0f)
	{
		WindowHeight = InHeight;
	}

	if (Camera)
	{
		Camera->OnResize(static_cast<int32>(WindowWidth), static_cast<int32>(WindowHeight));
	}
}

void FEditorViewportClient::Tick(float DeltaTime)
{
	if (bPIEOutlineFlashActive)
	{
		PIEOutlineFlashElapsed += DeltaTime;
		const float TotalDuration = PIEOutlineFlashHoldDuration + PIEOutlineFlashFadeDuration;
		if (PIEOutlineFlashElapsed >= TotalDuration)
		{
			bPIEOutlineFlashActive = false;
			PIEOutlineFlashElapsed = 0.0f;
		}
	}

	if (!bHasInputContext)
	{
		EnsureInputController();
		if (InputController)
		{
			InputController->TickNavigationSmoothing(DeltaTime);
		}
		return;
	}

	DispatchDeltaTime = DeltaTime;
	EnsureInputController();
	EnsureInputContextStack();

	const bool bPrioritizeNavigation =
		InputContext.bRelativeMouseMode
		&& EditorViewportInputUtils::IsLeftNavigationDragActive(InputContext)
		&& !(Gizmo && (Gizmo->IsHolding() || Gizmo->IsPressedOnHandle()));

	if (bPrioritizeNavigation)
	{
		for (IInputContext* Ctx : InputContextStack)
		{
			if (Ctx == NavigationInputContext.get())
			{
				if (Ctx->HandleInput(InputContext))
				{
					break;
				}
				continue;
			}

			if (Ctx == SelectionInputContext.get())
			{
				continue;
			}

			if (Ctx && Ctx->HandleInput(InputContext))
			{
				break;
			}
		}
	}
	else
	{
		for (IInputContext* Ctx : InputContextStack)
		{
			if (Ctx && Ctx->HandleInput(InputContext))
			{
				break;
			}
		}
	}

	bHasInputContext = false;
	if (InputController)
	{
		InputController->TickNavigationSmoothing(DeltaTime);
	}
}

void FEditorViewportClient::TickPIEOutlineFlashOnly(float DeltaTime)
{
	if (!bPIEOutlineFlashActive)
	{
		return;
	}

	PIEOutlineFlashElapsed += DeltaTime;
	const float TotalDuration = PIEOutlineFlashHoldDuration + PIEOutlineFlashFadeDuration;
	if (PIEOutlineFlashElapsed >= TotalDuration)
	{
		bPIEOutlineFlashActive = false;
		PIEOutlineFlashElapsed = 0.0f;
	}
}

bool FEditorViewportClient::ProcessInput(FViewportInputContext& Context)
{
	InputContext = Context;
	bHasInputContext = true;
	return false;
}

bool FEditorViewportClient::WantsRelativeMouseMode(const FViewportInputContext& Context, POINT& OutRestoreScreenPos) const
{
	OutRestoreScreenPos = Context.Frame.MouseScreenPos;
	bool bGizmoBlocksLeftRelativeDrag = Gizmo && (Gizmo->IsHolding() || Gizmo->IsPressedOnHandle());
	if (!bGizmoBlocksLeftRelativeDrag
		&& Gizmo
		&& Camera
		&& Viewport
		&& Context.Frame.IsDown(VK_LBUTTON))
	{
		const float LocalMouseX = static_cast<float>(Context.MouseLocalPos.x);
		const float LocalMouseY = static_cast<float>(Context.MouseLocalPos.y);
		const float VPWidth = static_cast<float>(Viewport->GetWidth());
		const float VPHeight = static_cast<float>(Viewport->GetHeight());
		const FRay MouseRay = Camera->DeprojectScreenToWorld(LocalMouseX, LocalMouseY, VPWidth, VPHeight);

		FHitResult GizmoHit{};
		bGizmoBlocksLeftRelativeDrag = Gizmo->Raycast(MouseRay, GizmoHit);
	}

	const bool bLeftRelativeDrag = EditorViewportInputUtils::IsLeftNavigationDragActive(Context) && !bGizmoBlocksLeftRelativeDrag;

	if (!Camera || !Viewport)
	{
		return false;
	}

	const bool bMouseOwnedByViewport = Context.bCaptured || Context.bHovered || Context.bRelativeMouseMode;
	if (!bMouseOwnedByViewport)
	{
		return false;
	}

	const bool bImGuiBlocksRelativeAcquire =
		Context.bImGuiCapturedMouse
		&& !Context.bCaptured
		&& !Context.bRelativeMouseMode;
	if (bImGuiBlocksRelativeAcquire)
	{
		return false;
	}

	const bool bResult = EditorViewportInputMapping::IsTriggered(Context, EditorViewportInputMapping::EEditorViewportAction::NavLookRightDown)
		|| EditorViewportInputMapping::IsTriggered(Context, EditorViewportInputMapping::EEditorViewportAction::NavLookMiddleDown)
		|| EditorViewportInputMapping::IsTriggered(Context, EditorViewportInputMapping::EEditorViewportAction::NavOrbitAltLeftDown)
		|| EditorViewportInputMapping::IsTriggered(Context, EditorViewportInputMapping::EEditorViewportAction::NavDollyAltRightDown)
		|| EditorViewportInputMapping::IsTriggered(Context, EditorViewportInputMapping::EEditorViewportAction::NavPanAltMiddleDown)
		|| bLeftRelativeDrag;
	return bResult;
}

void FEditorViewportClient::EnsureInputController()
{
	if (!InputController)
	{
		InputController = std::make_unique<FEditorViewportController>(this);
	}
}

void FEditorViewportClient::EnsureInputContextStack()
{
	if (bInputContextStackInitialized)
	{
		return;
	}

	ViewportCommandContext = std::make_unique<FViewportCommandContext>(this, &DispatchDeltaTime);
	GizmoInputContext = std::make_unique<FEditorGizmoInputContext>(this, &DispatchDeltaTime);
	SelectionInputContext = std::make_unique<FEditorSelectionInputContext>(this, &DispatchDeltaTime);
	NavigationInputContext = std::make_unique<FEditorNavigationInputContext>(this, &DispatchDeltaTime);

	InputContextStack.clear();
	InputContextStack.push_back(ViewportCommandContext.get());
	InputContextStack.push_back(GizmoInputContext.get());
	InputContextStack.push_back(SelectionInputContext.get());
	InputContextStack.push_back(NavigationInputContext.get());
	std::sort(
		InputContextStack.begin(),
		InputContextStack.end(),
		[](IInputContext* Lhs, IInputContext* Rhs)
		{
			if (!Lhs || !Rhs)
			{
				return Lhs != nullptr;
			}
			return Lhs->GetPriority() > Rhs->GetPriority();
		});
	bInputContextStackInitialized = true;
}

bool FEditorViewportClient::TryCycleGizmoMode()
{
	if (!Gizmo)
	{
		return false;
	}

	Gizmo->SetNextMode();
	return true;
}

void FEditorViewportClient::ResetIdPickingState()
{
	EndDeferredSpatialIndexInvalidation();
	EndSelectionMarquee();
	EnsureInputController();
	if (InputController)
	{
		InputController->ResetIdPickingState();
		InputController->ResetInputState();
	}
}

void FEditorViewportClient::BeginSelectionMarquee(const POINT& InLocalStart, bool bInAdditive)
{
	bSelectionMarqueeActive = true;
	bSelectionMarqueeAdditive = bInAdditive;
	SelectionMarqueeStartLocal = InLocalStart;
	SelectionMarqueeCurrentLocal = InLocalStart;
}

void FEditorViewportClient::UpdateSelectionMarquee(const POINT& InLocalCurrent)
{
	if (!bSelectionMarqueeActive)
	{
		return;
	}

	SelectionMarqueeCurrentLocal = InLocalCurrent;
}

void FEditorViewportClient::EndSelectionMarquee()
{
	bSelectionMarqueeActive = false;
	bSelectionMarqueeAdditive = false;
}

void FEditorViewportClient::BeginDeferredSpatialIndexInvalidation()
{
	if (bDeferredSpatialIndexInvalidation || !World)
	{
		return;
	}

	if (ULevel* ActiveLevel = World->GetActiveLevel())
	{
		ActiveLevel->GetRenderProxy().BeginDeferSpatialIndexInvalidation();
	}

	if (ULevel* PersistentLevel = World->GetPersistentLevel())
	{
		if (PersistentLevel != World->GetActiveLevel())
		{
			PersistentLevel->GetRenderProxy().BeginDeferSpatialIndexInvalidation();
		}
	}

	bDeferredSpatialIndexInvalidation = true;
}

void FEditorViewportClient::EndDeferredSpatialIndexInvalidation()
{
	if (!bDeferredSpatialIndexInvalidation || !World)
	{
		return;
	}

	if (ULevel* ActiveLevel = World->GetActiveLevel())
	{
		ActiveLevel->GetRenderProxy().EndDeferSpatialIndexInvalidation();
		ActiveLevel->GetRenderProxy().WarmupSpatialIndices();
	}

	if (ULevel* PersistentLevel = World->GetPersistentLevel())
	{
		if (PersistentLevel != World->GetActiveLevel())
		{
			PersistentLevel->GetRenderProxy().EndDeferSpatialIndexInvalidation();
			PersistentLevel->GetRenderProxy().WarmupSpatialIndices();
		}
	}

	bDeferredSpatialIndexInvalidation = false;
}

void FEditorViewportClient::UpdateLayoutRect()
{
	if (!LayoutWindow) return;

	const FRect& R = LayoutWindow->GetRect();
	ViewportScreenRect = R;

	// FViewport 리사이즈 요청 (슬롯 크기와 RT 크기 동기화)
	if (Viewport)
	{
		uint32 SlotW = static_cast<uint32>(R.Width);
		uint32 SlotH = static_cast<uint32>(R.Height);
		if (SlotW > 0 && SlotH > 0 && (SlotW != Viewport->GetWidth() || SlotH != Viewport->GetHeight()))
		{
			Viewport->RequestResize(SlotW, SlotH);
		}
	}
}

void FEditorViewportClient::RenderViewportImage(bool bIsActiveViewport, bool bDrawActiveOutline)
{
	if (!Viewport || !Viewport->GetSRV()) return;

	const FRect& R = ViewportScreenRect;
	if (R.Width <= 0 || R.Height <= 0) return;

	ImDrawList* DrawList = ImGui::GetWindowDrawList();
	ImVec2 Min(R.X, R.Y);
	ImVec2 Max(R.X + R.Width, R.Y + R.Height);
	constexpr float ToolbarBorderOffsetY = 34.0f;
	const ImVec2 OutlineMin(R.X, R.Y + ToolbarBorderOffsetY);

	DrawList->AddImage((ImTextureID)Viewport->GetSRV(), Min, Max);

	// 활성 뷰포트 테두리 강조
	if (bIsActiveViewport && bDrawActiveOutline)
	{
		DrawList->AddRect(OutlineMin, Max, IM_COL32(255, 200, 0, 200), 0.0f, 0, 2.0f);
	}

	if (bPIEOutlineFlashActive && PIEOutlineFlashFadeDuration > 0.0f)
	{
		float Alpha01 = 1.0f;
		if (PIEOutlineFlashElapsed > PIEOutlineFlashHoldDuration)
		{
			const float FadeElapsed = PIEOutlineFlashElapsed - PIEOutlineFlashHoldDuration;
			Alpha01 = 1.0f - Clamp(FadeElapsed / PIEOutlineFlashFadeDuration, 0.0f, 1.0f);
		}
		const int32 Alpha = static_cast<int32>(Alpha01 * 255.0f);
		DrawList->AddRect(OutlineMin, Max, IM_COL32(80, 255, 120, Alpha), 0.0f, 0, 3.0f);
	}

	if (bSelectionMarqueeActive)
	{
		const float StartX = R.X + static_cast<float>(SelectionMarqueeStartLocal.x);
		const float StartY = R.Y + static_cast<float>(SelectionMarqueeStartLocal.y);
		const float CurrentX = R.X + static_cast<float>(SelectionMarqueeCurrentLocal.x);
		const float CurrentY = R.Y + static_cast<float>(SelectionMarqueeCurrentLocal.y);
		const float Left = (std::min)(StartX, CurrentX);
		const float Top = (std::min)(StartY, CurrentY);
		const float Right = (std::max)(StartX, CurrentX);
		const float Bottom = (std::max)(StartY, CurrentY);
		const ImU32 FillColor = IM_COL32(255, 255, 255, 48);
		const ImU32 BorderColor = IM_COL32(255, 255, 255, 210);
		DrawList->AddRectFilled(ImVec2(Left, Top), ImVec2(Right, Bottom), FillColor);
		DrawList->AddRect(ImVec2(Left, Top), ImVec2(Right, Bottom), BorderColor, 0.0f, 0, 1.5f);
	}
}

void FEditorViewportClient::TriggerPIEStartOutlineFlash(float HoldSeconds, float FadeSeconds)
{
	PIEOutlineFlashHoldDuration = HoldSeconds > 0.0f ? HoldSeconds : 1.0f;
	PIEOutlineFlashFadeDuration = FadeSeconds > 0.0f ? FadeSeconds : 2.0f;
	PIEOutlineFlashElapsed = 0.0f;
	bPIEOutlineFlashActive = true;
}

void FEditorViewportClient::ClearPIEStartOutlineFlash()
{
	bPIEOutlineFlashActive = false;
	PIEOutlineFlashElapsed = 0.0f;
}
