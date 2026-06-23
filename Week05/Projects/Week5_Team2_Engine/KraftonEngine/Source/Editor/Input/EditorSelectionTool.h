#pragma once

#include "Editor/Input/EditorViewportTools.h"
#include "Core/RayTypes.h"
#include "Math/Vector.h"

class FEditorViewportClient;
class AActor;

class FEditorSelectionTool final : public IEditorViewportTool
{
public:
	explicit FEditorSelectionTool(FEditorViewportClient* InOwner);
	bool HandleInput(float DeltaTime) override;
	bool HasPendingIdPickRequest() const;
	void GetPendingIdPickCoord(uint32& OutX, uint32& OutY) const;
	bool HasPendingIdPickReadback() const;
	uint32 GetPendingIdPickReadbackRequestId() const;
	void BeginPendingIdPickReadback(uint32 InRequestId);
	void CancelPendingIdPickReadback();
	void SetIdPickResult(uint32 InId);
	void ResetInputState();
	void ResetIdPickingState();

private:
	bool IsBoxSelectionChordDown(bool& bOutAdditive) const;
	void ApplySelectionMarquee(bool bAdditive);
	bool TryProjectActorToViewportLocal(AActor* Actor, float& OutX, float& OutY) const;
	void HandleSelectionClick(const FRay& Ray, float LocalMouseX, float LocalMouseY);
	bool ProcessPendingIdPickResult();
	bool IsRayPickCacheValidForCurrentCamera() const;
	void UpdateRayPickCache(uint32 InX, uint32 InY, AActor* InActor);
	void InvalidateRayPickCache();

	struct FPendingIdPickState
	{
		bool bPendingRequest = false;
		bool bPendingReadback = false;
		bool bHasResult = false;
		bool bCtrlHeld = false;
		bool bShiftHeld = false;
		uint32 PickX = 0u;
		uint32 PickY = 0u;
		uint32 PendingReadbackRequestId = 0u;
		uint32 PickedObjectId = 0u;
	};

	FEditorViewportClient* Owner = nullptr;
	bool bHasCachedRayPickResult = false;
	uint32 CachedRayPickX = 0u;
	uint32 CachedRayPickY = 0u;
	uint32 CachedRayPickedActorId = 0u;
	FVector CachedRayPickCameraLocation = FVector(0.0f, 0.0f, 0.0f);
	FVector CachedRayPickCameraForward = FVector(0.0f, 0.0f, 0.0f);
	bool bCachedRayPickCameraOrtho = false;
	float CachedRayPickCameraFOV = 0.0f;
	float CachedRayPickCameraOrthoWidth = 0.0f;
	FPendingIdPickState IdPickState;
};
