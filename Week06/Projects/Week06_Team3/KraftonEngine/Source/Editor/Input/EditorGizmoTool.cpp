#include "Editor/Input/EditorGizmoTool.h"

#include "Editor/Input/EditorViewportInputUtils.h"
#include "Editor/Viewport/EditorViewportClient.h"
#include "Components/CameraComponent.h"
#include "Components/GizmoComponent.h"
#include "Collision/RayUtils.h"
#include "GameFramework/World.h"
#include "Viewport/Viewport.h"

namespace
{
bool HasKeyEvent(const FViewportInputContext& Context, EInputEventType Type, int32 Key)
{
	for (const FInputEvent& Event : Context.Events)
	{
		if (Event.Type == Type && Event.Key == Key)
		{
			return true;
		}
	}
	return false;
}

bool HasPointerEvent(const FViewportInputContext& Context, EInputEventType Type, EPointerButton Button)
{
	for (const FInputEvent& Event : Context.Events)
	{
		if (Event.Type == Type && Event.PointerButton == Button)
		{
			return true;
		}
	}
	return false;
}
}

FEditorGizmoTool::FEditorGizmoTool(FEditorViewportClient* InOwner)
	: Owner(InOwner)
{
}

bool FEditorGizmoTool::HandleInput(float DeltaTime)
{
	(void)DeltaTime;

	if (!Owner || !Owner->GetCamera() || !Owner->GetGizmo() || !Owner->GetInteractionWorld())
	{
		return false;
	}

	UCameraComponent* Camera = Owner->GetCamera();
	UGizmoComponent* Gizmo = Owner->GetGizmo();
	const FViewportInputContext& Context = Owner->GetRoutedInputContext();
	if (!Owner->GetRenderOptions().ShowFlags.bGizmo)
	{
		return false;
	}

	Gizmo->ApplyScreenSpaceScaling(Camera->GetWorldLocation(), Camera->IsOrthogonal(), Camera->GetOrthoWidth());
	Gizmo->UpdateAxisMask(Owner->GetRenderOptions().ViewportType, Camera->IsOrthogonal());

	if (EditorViewportInputUtils::IsInViewportToolbarDeadZone(Context))
	{
		return false;
	}

	if (EditorViewportInputUtils::IsMouseBlockedByImGuiForViewport(Context) && !Gizmo->IsHolding())
	{
		return false;
	}

	// 네비게이션 relative 모드에서는 gizmo hover/press 판정을 완전히 배제한다.
	// (카메라 회전 중 커서가 gizmo를 스치며 입력이 바뀌는 문제 방지)
	if (Context.bRelativeMouseMode && !Gizmo->IsHolding() && !Gizmo->IsPressedOnHandle())
	{
		Gizmo->UpdateHoveredAxis(-1);
		return false;
	}

	float LocalMouseX = static_cast<float>(Context.MouseLocalPos.x);
	float LocalMouseY = static_cast<float>(Context.MouseLocalPos.y);
	float VPWidth = Owner->GetViewport() ? static_cast<float>(Owner->GetViewport()->GetWidth()) : Owner->GetWindowWidth();
	float VPHeight = Owner->GetViewport() ? static_cast<float>(Owner->GetViewport()->GetHeight()) : Owner->GetWindowHeight();
	FRay Ray = Camera->DeprojectScreenToWorld(LocalMouseX, LocalMouseY, VPWidth, VPHeight);

	// Week05 동작과 동일하게 hover 축은 프레임 단위로 갱신한다.
	// (드래그 중에는 현재 선택 축 유지)
	if (!Gizmo->IsHolding())
	{
		FHitResult HoverHit{};
		Gizmo->LineTraceComponent(Ray, HoverHit);
	}

	if (HasKeyEvent(Context, EInputEventType::KeyPressed, VK_LBUTTON))
	{
		FHitResult HitResult{};
		if (FRayUtils::RaycastComponent(Gizmo, Ray, HitResult))
		{
			Gizmo->SetPressedOnHandle(true);
			return true;
		}
	}
	else if (Context.Frame.IsDown(VK_LBUTTON))
	{
		if (Gizmo->IsPressedOnHandle() && !Gizmo->IsHolding())
		{
			Gizmo->SetHolding(true);
		}

		if (Gizmo->IsHolding())
		{
			Gizmo->UpdateDrag(Ray);
			return true;
		}
	}
	else if (HasPointerEvent(Context, EInputEventType::PointerDragEnded, EPointerButton::Left))
	{
		Gizmo->DragEnd();
		Gizmo->SetPressedOnHandle(false);
		return true;
	}
	else if (HasKeyEvent(Context, EInputEventType::KeyReleased, VK_LBUTTON))
	{
		if (Gizmo->IsPressedOnHandle() || Gizmo->IsHolding())
		{
			if (Gizmo->IsHolding())
			{
				Gizmo->DragEnd();
			}
			Gizmo->SetPressedOnHandle(false);
			return true;
		}
	}
	else if (!Context.Frame.IsDown(VK_LBUTTON) && Gizmo->IsPressedOnHandle() && !Gizmo->IsHolding())
	{
		// 입력 이벤트 유실 프레임 보호
		Gizmo->SetPressedOnHandle(false);
	}

	return false;
}

