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
	bool IsInputActiveNow() const;
	void TickSmoothing(float DeltaTime);
	void FocusOnTarget(const FVector& Target, float DesiredDistance = -1.0f);
	void SyncFromCamera();
	float GetRuntimeCameraSpeedMultiplier() const { return RuntimeCameraSpeedMultiplier; }
	void SetRuntimeCameraSpeedMultiplier(float InMultiplier);
	static constexpr float GetMinCameraSpeedValue() { return 0.1f; }
	static constexpr float GetMaxCameraSpeedValue() { return 32.0f; }

private:
	void SyncCameraTargetFromCurrent();
	FRotator MakeLookAtRotation(const FVector& From, const FVector& To) const;
	void AddCameraMoveInputLocal(const FVector& DeltaLocal);
	void AddCameraRotateInput(float DeltaYaw, float DeltaPitch);
	void OrbitCameraAroundPivot(const FVector& Pivot, float DeltaMouseX, float DeltaMouseY, float OrbitSensitivity);
	void AdjustRuntimeCameraSpeed(float WheelNotches);
	float GetEffectiveCameraSpeed() const;

private:
	FEditorViewportClient* Owner = nullptr;
	bool bSmoothingInitialized = false;
	FVector CameraTargetLocation = FVector(0.0f, 0.0f, 0.0f);
	FRotator CameraTargetRotation = FRotator();
	float CameraMoveSmoothSpeed = 10.0f;
	float CameraRotateSmoothSpeed = 12.0f;
	float RuntimeCameraSpeedMultiplier = 1.0f;
};
