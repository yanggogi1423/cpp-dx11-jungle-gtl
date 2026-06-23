#pragma once

#include "Core/CoreTypes.h"
#include "Core/RayTypes.h"
#include "Render/Types/ViewTypes.h"
#include "Component/GizmoComponent.h"

class AActor;
class UGizmoComponent;
class UWorld;
struct FHitResult;

class FTransformGizmo
{
public:
	void Initialize(UWorld* InWorld);
	void Shutdown();
	void SetWorld(UWorld* InWorld);
	void EnsureProxyRegistered();

	UWorld* GetWorld() const;
	uint32 GetUUID() const;
	EGizmoMode GetMode() const;
	void SetTranslateMode();
	void SetRotateMode();
	void SetScaleMode();
	void SetNextMode();
	void SetWorldSpace(bool bWorldSpace);
	bool IsWorldSpace() const;
	void ToggleCoordinateSpace();
	void SetTranslateSnapEnabled(bool bEnabled);
	bool IsTranslateSnapEnabled() const;
	void SetTranslateSnapValue(float InValue);
	float GetTranslateSnapValue() const;
	void SetRotateSnapEnabled(bool bEnabled);
	bool IsRotateSnapEnabled() const;
	void SetRotateSnapValueDegrees(float InValue);
	float GetRotateSnapValueDegrees() const;
	void SetScaleSnapEnabled(bool bEnabled);
	bool IsScaleSnapEnabled() const;
	void SetScaleSnapValue(float InValue);
	float GetScaleSnapValue() const;

	void SetTarget(AActor* NewTarget);
	void SetSelectedActors(const TArray<AActor*>* InSelectedActors);
	void Deactivate();
	void UpdateGizmoTransform();

	void ApplyScreenSpaceScaling(const FVector& CameraLocation, bool bIsOrtho = false, float OrthoWidth = 10.0f);
	void UpdateAxisMask(ELevelViewportType ViewportType);
	void SetPressedOnHandle(bool bPressed);
	bool IsPressedOnHandle() const;
	void SetHolding(bool bHolding);
	bool IsHolding() const;
	void UpdateDrag(const FRay& Ray);
	void DragEnd();

	bool Raycast(const FRay& Ray, FHitResult& OutHitResult) const;

private:
	//	렌더링을 위해 임시로 남겨둠 나중에는 렌더 데이터만 수정할 예정
	UGizmoComponent* GizmoComponent = nullptr;
};
