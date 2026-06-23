#include "Editor/Gizmo/TransformGizmo.h"

#include "Collision/RayUtils.h"
#include "Component/GizmoComponent.h"
#include "GameFramework/AActor.h"
#include "GameFramework/Level.h"
#include "GameFramework/World.h"
#include "Object/Object.h"
#include "Render/Pipeline/WorldRenderProxy.h"

void FTransformGizmo::Initialize(UWorld* InWorld)
{
	if (GizmoComponent)
	{
		return;
	}

	GizmoComponent = UObjectManager::Get().CreateObject<UGizmoComponent>();
	GizmoComponent->SetWorldLocation(FVector(0.0f, 0.0f, 0.0f));
	GizmoComponent->Deactivate();
	SetWorld(InWorld);
}

void FTransformGizmo::Shutdown()
{
	if (!GizmoComponent)
	{
		return;
	}

	UObjectManager::Get().DestroyObject(GizmoComponent);
	GizmoComponent = nullptr;
}

void FTransformGizmo::SetWorld(UWorld* InWorld)
{
	if (!GizmoComponent)
	{
		return;
	}

	// EnsureProxyRegistered에서 새 WorldRenderProxy에 등록하기 전에 기존 WorldRenderProxy에서 제거 
	// => 모든 WorldRenderProxy에 걸쳐 1개만 존재하도록
	UWorld* OldWorld = GizmoComponent->GetWorld();
	if (OldWorld && OldWorld != InWorld)
	{
		if (ULevel* OldLevel = OldWorld->GetPersistentLevel())
		{
			OldLevel->GetRenderProxy().RemoveProxy(GizmoComponent->GetProxy());
		}
	}
 
	GizmoComponent->Deactivate();
	GizmoComponent->SetExplicitWorld(InWorld);
	if (InWorld)
	{
		EnsureProxyRegistered();
	}
}

void FTransformGizmo::EnsureProxyRegistered()
{
	if (!GizmoComponent)
	{
		return;
	}

	UWorld* World = GizmoComponent->GetWorld();
	if (!World)
	{
		return;
	}

	if (!GizmoComponent->GetProxy())
	{
		GizmoComponent->OnRegister();
	}

	if (ULevel* PersistentLevel = World->GetPersistentLevel())
	{
		PersistentLevel->GetRenderProxy().AddProxy(GizmoComponent->GetProxy());
	}
}

UWorld* FTransformGizmo::GetWorld() const
{
	return GizmoComponent ? GizmoComponent->GetWorld() : nullptr;
}

uint32 FTransformGizmo::GetUUID() const
{
	return GizmoComponent ? GizmoComponent->GetUUID() : 0u;
}

EGizmoMode FTransformGizmo::GetMode() const
{
	return GizmoComponent ? GizmoComponent->GetMode() : EGizmoMode::Translate;
}

void FTransformGizmo::SetTranslateMode()
{
	if (GizmoComponent)
	{
		GizmoComponent->SetTranslateMode();
	}
}

void FTransformGizmo::SetRotateMode()
{
	if (GizmoComponent)
	{
		GizmoComponent->SetRotateMode();
	}
}

void FTransformGizmo::SetScaleMode()
{
	if (GizmoComponent)
	{
		GizmoComponent->SetScaleMode();
	}
}

void FTransformGizmo::SetNextMode()
{
	if (GizmoComponent)
	{
		GizmoComponent->SetNextMode();
	}
}

void FTransformGizmo::SetWorldSpace(bool bWorldSpace)
{
	if (GizmoComponent)
	{
		GizmoComponent->SetWorldSpace(bWorldSpace);
	}
}

bool FTransformGizmo::IsWorldSpace() const
{
	return GizmoComponent ? GizmoComponent->IsWorldSpace() : true;
}

void FTransformGizmo::ToggleCoordinateSpace()
{
	if (GizmoComponent)
	{
		GizmoComponent->ToggleWorldSpace();
	}
}

void FTransformGizmo::SetTranslateSnapEnabled(bool bEnabled)
{
	if (GizmoComponent)
	{
		GizmoComponent->SetTranslateSnapEnabled(bEnabled);
	}
}

bool FTransformGizmo::IsTranslateSnapEnabled() const
{
	return GizmoComponent ? GizmoComponent->IsTranslateSnapEnabled() : false;
}

void FTransformGizmo::SetTranslateSnapValue(float InValue)
{
	if (GizmoComponent)
	{
		GizmoComponent->SetTranslateSnapValue(InValue);
	}
}

float FTransformGizmo::GetTranslateSnapValue() const
{
	return GizmoComponent ? GizmoComponent->GetTranslateSnapValue() : 1.0f;
}

void FTransformGizmo::SetRotateSnapEnabled(bool bEnabled)
{
	if (GizmoComponent)
	{
		GizmoComponent->SetRotateSnapEnabled(bEnabled);
	}
}

bool FTransformGizmo::IsRotateSnapEnabled() const
{
	return GizmoComponent ? GizmoComponent->IsRotateSnapEnabled() : false;
}

void FTransformGizmo::SetRotateSnapValueDegrees(float InValue)
{
	if (GizmoComponent)
	{
		GizmoComponent->SetRotateSnapValueDegrees(InValue);
	}
}

float FTransformGizmo::GetRotateSnapValueDegrees() const
{
	return GizmoComponent ? GizmoComponent->GetRotateSnapValueDegrees() : 5.0f;
}

void FTransformGizmo::SetScaleSnapEnabled(bool bEnabled)
{
	if (GizmoComponent)
	{
		GizmoComponent->SetScaleSnapEnabled(bEnabled);
	}
}

bool FTransformGizmo::IsScaleSnapEnabled() const
{
	return GizmoComponent ? GizmoComponent->IsScaleSnapEnabled() : false;
}

void FTransformGizmo::SetScaleSnapValue(float InValue)
{
	if (GizmoComponent)
	{
		GizmoComponent->SetScaleSnapValue(InValue);
	}
}

float FTransformGizmo::GetScaleSnapValue() const
{
	return GizmoComponent ? GizmoComponent->GetScaleSnapValue() : 0.1f;
}

void FTransformGizmo::SetTarget(AActor* NewTarget)
{
	if (GizmoComponent)
	{
		GizmoComponent->SetTarget(NewTarget);
	}
}

void FTransformGizmo::SetSelectedActors(const TArray<AActor*>* InSelectedActors)
{
	if (GizmoComponent)
	{
		GizmoComponent->SetSelectedActors(InSelectedActors);
	}
}

void FTransformGizmo::Deactivate()
{
	if (GizmoComponent)
	{
		GizmoComponent->Deactivate();
	}
}

void FTransformGizmo::UpdateGizmoTransform()
{
	if (GizmoComponent)
	{
		GizmoComponent->UpdateGizmoTransform();
	}
}

void FTransformGizmo::ApplyScreenSpaceScaling(const FVector& CameraLocation, bool bIsOrtho, float OrthoWidth)
{
	if (GizmoComponent)
	{
		GizmoComponent->ApplyScreenSpaceScaling(CameraLocation, bIsOrtho, OrthoWidth);
	}
}

void FTransformGizmo::UpdateAxisMask(ELevelViewportType ViewportType)
{
	if (GizmoComponent)
	{
		GizmoComponent->UpdateAxisMask(ViewportType);
	}
}

void FTransformGizmo::SetPressedOnHandle(bool bPressed)
{
	if (GizmoComponent)
	{
		GizmoComponent->SetPressedOnHandle(bPressed);
	}
}

bool FTransformGizmo::IsPressedOnHandle() const
{
	return GizmoComponent ? GizmoComponent->IsPressedOnHandle() : false;
}

void FTransformGizmo::SetHolding(bool bHolding)
{
	if (GizmoComponent)
	{
		GizmoComponent->SetHolding(bHolding);
	}
}

bool FTransformGizmo::IsHolding() const
{
	return GizmoComponent ? GizmoComponent->IsHolding() : false;
}

void FTransformGizmo::UpdateDrag(const FRay& Ray)
{
	if (GizmoComponent)
	{
		GizmoComponent->UpdateDrag(Ray);
	}
}

void FTransformGizmo::DragEnd()
{
	if (GizmoComponent)
	{
		GizmoComponent->DragEnd();
	}
}

bool FTransformGizmo::Raycast(const FRay& Ray, FHitResult& OutHitResult) const
{
	if (!GizmoComponent)
	{
		return false;
	}

	return FRayUtils::RaycastComponent(GizmoComponent, Ray, OutHitResult);
}
