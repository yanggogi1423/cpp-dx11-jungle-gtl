#pragma once

#include "Editor/Input/EditorViewportTools.h"
#include "Math/Rotator.h"
#include "Math/Vector.h"

class FEditorViewportClient;

class FEditorNavigationTool final : public IEditorViewportTool
{
public:
	explicit FEditorNavigationTool(FEditorViewportClient* InOwner);
	bool HandleInput(float DeltaTime) override;
	void SyncTargetFromCameraImmediate();
	void FocusOnTarget(const FVector& Target, float DesiredDistance = -1.0f);
	float GetRuntimeCameraSpeedMultiplier() const { return RuntimeCameraSpeedMultiplier; }
	void SetRuntimeCameraSpeedMultiplier(float InMultiplier);
	static constexpr float GetMinCameraSpeedValue() { return 0.1f; }
	static constexpr float GetMaxCameraSpeedValue() { return 32.0f; }

private:
	void SyncCameraTargetFromCurrent();
	void ApplyCameraSmoothing(float DeltaTime, bool bBypassSmoothing);
	float InterpAngle(float Current, float Target, float Alpha) const;
	FRotator MakeLookAtRotation(const FVector& From, const FVector& To) const;
	void AddCameraMoveInputLocal(const FVector& DeltaLocal);
	void AddCameraRotateInput(float DeltaYaw, float DeltaPitch);
	void OrbitCameraAroundPivot(const FVector& Pivot, float DeltaMouseX, float DeltaMouseY, float OrbitSensitivity);
	void AdjustRuntimeCameraSpeed(float WheelNotches);
	float GetEffectiveCameraSpeed() const;

	FEditorViewportClient* Owner = nullptr;
	bool bCameraTargetInitialized = false;
	FVector CameraTargetLocation = FVector(0.0f, 0.0f, 0.0f);
	FRotator CameraTargetRotation = FRotator();
	float RuntimeCameraSpeedMultiplier = 1.0f;
};

