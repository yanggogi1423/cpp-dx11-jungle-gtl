#include "Editor/Input/EditorSelectionTool.h"

#include "Editor/Input/EditorViewportInputMapping.h"
#include "Component/PrimitiveComponent.h"
#include "Editor/EditorEngine.h"
#include "Editor/Gizmo/TransformGizmo.h"
#include "Editor/Selection/PickingService.h"
#include "Editor/Selection/SelectionManager.h"
#include "Editor/Viewport/EditorViewportClient.h"
#include "Engine/Runtime/Engine.h"
#include "GameFramework/AActor.h"
#include "GameFramework/World.h"
#include "Math/Matrix.h"
#include "Object/Object.h"
#include "Viewport/Viewport.h"

#include <algorithm>
#include <cfloat>
#include <cmath>

FEditorSelectionTool::FEditorSelectionTool(FEditorViewportClient* InOwner)
	: Owner(InOwner)
{
}

bool FEditorSelectionTool::HandleInput(float DeltaTime)
{
	if (!Owner)
	{
		return false;
	}

	(void)DeltaTime;
	const bool bConsumedByPending = ProcessPendingIdPickResult();
	if ((Owner->InputContext.bImGuiCapturedMouse && !Owner->InputContext.bCaptured) || !Owner->Camera || !Owner->Viewport)
	{
		return bConsumedByPending;
	}

	bool bBoxAdditive = false;
	const bool bBoxSelectDown = IsBoxSelectionChordDown(bBoxAdditive);
	if (bBoxSelectDown)
	{
		const POINT CurrentLocal = Owner->InputContext.MouseLocalPos;
		if (!Owner->HasSelectionMarquee())
		{
			Owner->BeginSelectionMarquee(CurrentLocal, bBoxAdditive);
		}
		else
		{
			Owner->UpdateSelectionMarquee(CurrentLocal);
		}

		if (Owner->InputContext.WasPointerDragStarted(EPointerButton::Left))
		{
			return true;
		}
	}

	if (Owner->HasSelectionMarquee())
	{
		const bool bBoxReleased =
			Owner->InputContext.WasPointerDragEnded(EPointerButton::Left) || !bBoxSelectDown;
		if (bBoxReleased)
		{
			ApplySelectionMarquee(Owner->IsSelectionMarqueeAdditive());
			Owner->EndSelectionMarquee();
			return true;
		}
		return true;
	}

	const bool bSelectionClickTriggered =
		EditorViewportInputMapping::IsTriggered(Owner->InputContext, EditorViewportInputMapping::EEditorViewportAction::SelectPrimaryReleased)
		|| EditorViewportInputMapping::IsTriggered(Owner->InputContext, EditorViewportInputMapping::EEditorViewportAction::SelectToggleReleased)
		|| EditorViewportInputMapping::IsTriggered(Owner->InputContext, EditorViewportInputMapping::EEditorViewportAction::SelectAddReleased);
	if (!bSelectionClickTriggered)
	{
		return bConsumedByPending;
	}
	if (Owner->InputContext.WasPointerDragEnded(EPointerButton::Left))
	{
		return true;
	}

	const float LocalMouseX = static_cast<float>(Owner->InputContext.MouseLocalPos.x);
	const float LocalMouseY = static_cast<float>(Owner->InputContext.MouseLocalPos.y);
	const float VPWidth = static_cast<float>(Owner->Viewport->GetWidth());
	const float VPHeight = static_cast<float>(Owner->Viewport->GetHeight());
	const FRay Ray = Owner->Camera->DeprojectScreenToWorld(LocalMouseX, LocalMouseY, VPWidth, VPHeight);
	HandleSelectionClick(Ray, LocalMouseX, LocalMouseY);
	return true;
}

bool FEditorSelectionTool::ProcessPendingIdPickResult()
{
	if (!IdPickState.bHasResult || !Owner->SelectionManager)
	{
		return false;
	}

	IdPickState.bHasResult = false;

	if (IdPickState.PickedObjectId == 0u)
	{
		if (!IdPickState.bCtrlHeld && !IdPickState.bShiftHeld)
		{
			Owner->SelectionManager->ClearSelection();
		}
		return true;
	}

	if (Owner->Gizmo && IdPickState.PickedObjectId == Owner->Gizmo->GetUUID())
	{
		Owner->Gizmo->SetPressedOnHandle(true);
		return true;
	}

	UObject* PickedObject = FPickingService::ResolveObjectFromPickingIdInWorld(IdPickState.PickedObjectId, Owner->World);
	AActor* PickedActor = Cast<AActor>(PickedObject);
	if (!PickedActor)
	{
		if (UPrimitiveComponent* PickedComp = Cast<UPrimitiveComponent>(PickedObject))
		{
			PickedActor = PickedComp->GetOwner();
		}
	}

	if (!PickedActor)
	{
		if (!IdPickState.bCtrlHeld && !IdPickState.bShiftHeld)
		{
			Owner->SelectionManager->ClearSelection();
		}
		return true;
	}

	if (IdPickState.bCtrlHeld)
	{
		Owner->SelectionManager->ToggleSelect(PickedActor);
	}
	else if (IdPickState.bShiftHeld)
	{
		Owner->SelectionManager->AddSelect(PickedActor);
	}
	else
	{
		Owner->SelectionManager->Select(PickedActor);
	}

	return true;
}

void FEditorSelectionTool::HandleSelectionClick(const FRay& Ray, float LocalMouseX, float LocalMouseY)
{
	if (!Owner)
	{
		return;
	}

	EPickingMode PickingMode = EPickingMode::RayTriangleBVH;
	if (UEditorEngine* Editor = Cast<UEditorEngine>(GEngine))
	{
		PickingMode = Editor->GetPickingMode();
	}

	if (PickingMode == EPickingMode::IDBuffer)
	{
		CancelPendingIdPickReadback();
		IdPickState.bCtrlHeld = Owner->InputContext.Frame.IsCtrlDown();
		IdPickState.bShiftHeld = Owner->InputContext.Frame.IsShiftDown();
		const float MaxX = (Owner->Viewport && Owner->Viewport->GetWidth() > 0)
			? static_cast<float>(Owner->Viewport->GetWidth() - 1) : 0.0f;
		const float MaxY = (Owner->Viewport && Owner->Viewport->GetHeight() > 0)
			? static_cast<float>(Owner->Viewport->GetHeight() - 1) : 0.0f;
		const uint32 PickX = static_cast<uint32>(Clamp(LocalMouseX, 0.0f, MaxX));
		const uint32 PickY = static_cast<uint32>(Clamp(LocalMouseY, 0.0f, MaxY));
		IdPickState.PickX = PickX;
		IdPickState.PickY = PickY;
		IdPickState.bPendingRequest = true;
		return;
	}

	const bool bCtrlHeld = Owner->InputContext.Frame.IsCtrlDown();
	const bool bShiftHeld = Owner->InputContext.Frame.IsShiftDown();
	const float MaxX = (Owner->Viewport && Owner->Viewport->GetWidth() > 0)
		? static_cast<float>(Owner->Viewport->GetWidth() - 1) : 0.0f;
	const float MaxY = (Owner->Viewport && Owner->Viewport->GetHeight() > 0)
		? static_cast<float>(Owner->Viewport->GetHeight() - 1) : 0.0f;
	const uint32 ClickX = static_cast<uint32>(Clamp(LocalMouseX, 0.0f, MaxX));
	const uint32 ClickY = static_cast<uint32>(Clamp(LocalMouseY, 0.0f, MaxY));

	if (!bCtrlHeld
		&& IsRayPickCacheValidForCurrentCamera()
		&& ClickX == CachedRayPickX
		&& ClickY == CachedRayPickY)
	{
		AActor* CachedActor = Cast<AActor>(FPickingService::ResolveObjectFromPickingIdInWorld(CachedRayPickedActorId, Owner->World));
		if (CachedActor && CachedActor->IsVisible())
		{
			Owner->SelectionManager->Select(CachedActor);
			return;
		}
		if (CachedRayPickedActorId == 0u)
		{
			Owner->SelectionManager->ClearSelection();
			return;
		}
	}

	float ClosestDistance = FLT_MAX;
	AActor* BestActor = FPickingService::PickActor(Owner->World, Ray, PickingMode, ClosestDistance);
	if (!BestActor)
	{
		if (!bCtrlHeld && !bShiftHeld)
		{
			Owner->SelectionManager->ClearSelection();
		}
	}
	else
	{
		if (bCtrlHeld)
		{
			Owner->SelectionManager->ToggleSelect(BestActor);
		}
		else if (bShiftHeld)
		{
			Owner->SelectionManager->AddSelect(BestActor);
		}
		else
		{
			Owner->SelectionManager->Select(BestActor);
		}
	}

	UpdateRayPickCache(ClickX, ClickY, BestActor);
}

void FEditorSelectionTool::ResetInputState()
{
	InvalidateRayPickCache();
}

bool FEditorSelectionTool::HasPendingIdPickRequest() const
{
	return IdPickState.bPendingRequest;
}

void FEditorSelectionTool::GetPendingIdPickCoord(uint32& OutX, uint32& OutY) const
{
	OutX = IdPickState.PickX;
	OutY = IdPickState.PickY;
}

bool FEditorSelectionTool::HasPendingIdPickReadback() const
{
	return IdPickState.bPendingReadback;
}

uint32 FEditorSelectionTool::GetPendingIdPickReadbackRequestId() const
{
	return IdPickState.PendingReadbackRequestId;
}

void FEditorSelectionTool::BeginPendingIdPickReadback(uint32 InRequestId)
{
	IdPickState.bPendingRequest = false;
	IdPickState.bPendingReadback = true;
	IdPickState.PendingReadbackRequestId = InRequestId;
}

void FEditorSelectionTool::CancelPendingIdPickReadback()
{
	if (!Owner)
	{
		return;
	}
	if (Owner->Viewport && IdPickState.PendingReadbackRequestId != 0u)
	{
		Owner->Viewport->CancelPickingIdReadback(IdPickState.PendingReadbackRequestId);
	}
	IdPickState.bPendingReadback = false;
	IdPickState.PendingReadbackRequestId = 0u;
}

void FEditorSelectionTool::SetIdPickResult(uint32 InId)
{
	IdPickState.PickedObjectId = InId;
	IdPickState.bHasResult = true;
	IdPickState.bPendingRequest = false;
	IdPickState.bPendingReadback = false;
	IdPickState.PendingReadbackRequestId = 0u;
}

void FEditorSelectionTool::ResetIdPickingState()
{
	CancelPendingIdPickReadback();
	IdPickState.bPendingRequest = false;
	IdPickState.bHasResult = false;
	IdPickState.bCtrlHeld = false;
	IdPickState.bShiftHeld = false;
	IdPickState.PickX = 0u;
	IdPickState.PickY = 0u;
	IdPickState.PickedObjectId = 0u;
}

bool FEditorSelectionTool::IsRayPickCacheValidForCurrentCamera() const
{
	if (!bHasCachedRayPickResult || !Owner || !Owner->Camera)
	{
		return false;
	}

	constexpr float PositionEpsilon = 0.001f;
	constexpr float ForwardEpsilon = 0.0001f;
	constexpr float FOVEpsilon = 0.0001f;
	constexpr float OrthoWidthEpsilon = 0.0001f;

	const bool bSameProjection = (Owner->Camera->IsOrthogonal() == bCachedRayPickCameraOrtho)
		&& (std::abs(Owner->Camera->GetFOV() - CachedRayPickCameraFOV) <= FOVEpsilon)
		&& (std::abs(Owner->Camera->GetOrthoWidth() - CachedRayPickCameraOrthoWidth) <= OrthoWidthEpsilon);
	const bool bSameView = FVector::Distance(Owner->Camera->GetWorldLocation(), CachedRayPickCameraLocation) <= PositionEpsilon
		&& FVector::Distance(Owner->Camera->GetForwardVector(), CachedRayPickCameraForward) <= ForwardEpsilon;
	return bSameProjection && bSameView;
}

void FEditorSelectionTool::UpdateRayPickCache(uint32 InX, uint32 InY, AActor* InActor)
{
	if (!Owner || !Owner->Camera)
	{
		InvalidateRayPickCache();
		return;
	}

	bHasCachedRayPickResult = true;
	CachedRayPickX = InX;
	CachedRayPickY = InY;
	CachedRayPickedActorId = InActor ? InActor->GetUUID() : 0u;
	CachedRayPickCameraLocation = Owner->Camera->GetWorldLocation();
	CachedRayPickCameraForward = Owner->Camera->GetForwardVector();
	bCachedRayPickCameraOrtho = Owner->Camera->IsOrthogonal();
	CachedRayPickCameraFOV = Owner->Camera->GetFOV();
	CachedRayPickCameraOrthoWidth = Owner->Camera->GetOrthoWidth();
}

void FEditorSelectionTool::InvalidateRayPickCache()
{
	bHasCachedRayPickResult = false;
	CachedRayPickedActorId = 0u;
	CachedRayPickX = 0u;
	CachedRayPickY = 0u;
}

bool FEditorSelectionTool::IsBoxSelectionChordDown(bool& bOutAdditive) const
{
	bOutAdditive = false;
	if (!Owner)
	{
		return false;
	}

	if (EditorViewportInputMapping::IsTriggered(
		Owner->InputContext,
		EditorViewportInputMapping::EEditorViewportAction::BoxSelectAdditiveDown))
	{
		bOutAdditive = true;
		return true;
	}

	return EditorViewportInputMapping::IsTriggered(
		Owner->InputContext,
		EditorViewportInputMapping::EEditorViewportAction::BoxSelectReplaceDown);
}

bool FEditorSelectionTool::TryProjectActorToViewportLocal(AActor* Actor, float& OutX, float& OutY) const
{
	if (!Owner || !Owner->Camera || !Owner->Viewport || !Actor)
	{
		return false;
	}

	const FVector WorldPos = Actor->GetActorLocation();
	const FMatrix ViewProjection = Owner->Camera->GetViewMatrix() * Owner->Camera->GetProjectionMatrix();

	const float ClipX = WorldPos.X * ViewProjection.M[0][0] + WorldPos.Y * ViewProjection.M[1][0] + WorldPos.Z * ViewProjection.M[2][0] + ViewProjection.M[3][0];
	const float ClipY = WorldPos.X * ViewProjection.M[0][1] + WorldPos.Y * ViewProjection.M[1][1] + WorldPos.Z * ViewProjection.M[2][1] + ViewProjection.M[3][1];
	const float ClipZ = WorldPos.X * ViewProjection.M[0][2] + WorldPos.Y * ViewProjection.M[1][2] + WorldPos.Z * ViewProjection.M[2][2] + ViewProjection.M[3][2];
	const float ClipW = WorldPos.X * ViewProjection.M[0][3] + WorldPos.Y * ViewProjection.M[1][3] + WorldPos.Z * ViewProjection.M[2][3] + ViewProjection.M[3][3];

	if (std::abs(ClipW) < 1e-6f)
	{
		return false;
	}

	const float InvW = 1.0f / ClipW;
	const float NdcX = ClipX * InvW;
	const float NdcY = ClipY * InvW;
	const float NdcZ = ClipZ * InvW;

	if (NdcX < -1.0f || NdcX > 1.0f || NdcY < -1.0f || NdcY > 1.0f || NdcZ < 0.0f || NdcZ > 1.0f)
	{
		return false;
	}

	const float Width = static_cast<float>(Owner->Viewport->GetWidth());
	const float Height = static_cast<float>(Owner->Viewport->GetHeight());
	if (Width <= 0.0f || Height <= 0.0f)
	{
		return false;
	}

	OutX = (NdcX * 0.5f + 0.5f) * Width;
	OutY = (1.0f - (NdcY * 0.5f + 0.5f)) * Height;
	return true;
}

void FEditorSelectionTool::ApplySelectionMarquee(bool bAdditive)
{
	if (!Owner || !Owner->SelectionManager || !Owner->World || !Owner->HasSelectionMarquee())
	{
		return;
	}

	const POINT& Start = Owner->GetSelectionMarqueeStartLocal();
	const POINT& Current = Owner->GetSelectionMarqueeCurrentLocal();
	const float Left = static_cast<float>((std::min)(Start.x, Current.x));
	const float Right = static_cast<float>((std::max)(Start.x, Current.x));
	const float Top = static_cast<float>((std::min)(Start.y, Current.y));
	const float Bottom = static_cast<float>((std::max)(Start.y, Current.y));
	const float Width = Right - Left;
	const float Height = Bottom - Top;
	if (Width < 2.0f || Height < 2.0f)
	{
		return;
	}

	TArray<AActor*> Hits;
	const TArray<AActor*>& Actors = Owner->World->GetActors();
	for (AActor* Actor : Actors)
	{
		if (!Actor || !Actor->IsVisible())
		{
			continue;
		}

		float ScreenX = 0.0f;
		float ScreenY = 0.0f;
		if (!TryProjectActorToViewportLocal(Actor, ScreenX, ScreenY))
		{
			continue;
		}

		if (ScreenX >= Left && ScreenX <= Right && ScreenY >= Top && ScreenY <= Bottom)
		{
			Hits.push_back(Actor);
		}
	}

	if (!bAdditive)
	{
		Owner->SelectionManager->ClearSelection();
	}

	for (AActor* HitActor : Hits)
	{
		Owner->SelectionManager->AddSelect(HitActor);
	}
}
