#pragma once

#include "Core/CoreTypes.h"
#include "Core/RayTypes.h"
#include "Math/MathUtils.h"
#include "Math/Matrix.h"
#include "Math/Quat.h"
#include "Math/Rotator.h"
#include "Math/Transform.h"
#include "Math/Vector.h"

struct FViewportCameraState
{
	float FOV = 3.14159265358979f / 3.0f;
	float AspectRatio = 16.0f / 9.0f;
	float NearZ = 0.1f;
	float FarZ = 1000.0f;
	float OrthoWidth = 10.0f;
	bool bIsOrthogonal = false;
};

class FViewportCamera
{
public:
	FViewportCamera() = default;

	void LookAt(const FVector& Target);

	void SetCameraState(const FViewportCameraState& NewState) { CameraState = NewState; }
	const FViewportCameraState& GetCameraState() const { return CameraState; }

	void SetFOV(float InFOV) { CameraState.FOV = InFOV; }
	void SetOrthoWidth(float InWidth) { CameraState.OrthoWidth = InWidth; }
	void SetOrthographic(bool bOrtho) { CameraState.bIsOrthogonal = bOrtho; }

	void OnResize(int32 Width, int32 Height);

	FMatrix GetViewMatrix() const;
	FMatrix GetProjectionMatrix() const;
	FMatrix GetWorldMatrix() const { return Transform.ToMatrix(); }

	float GetFOV() const { return CameraState.FOV; }
	float GetNearPlane() const { return CameraState.NearZ; }
	float GetFarPlane() const { return CameraState.FarZ; }
	float GetOrthoWidth() const { return CameraState.OrthoWidth; }
	bool IsOrthogonal() const { return CameraState.bIsOrthogonal; }

	FRay DeprojectScreenToWorld(float MouseX, float MouseY, float ScreenWidth, float ScreenHeight) const;

	void SetWorldLocation(const FVector& NewWorldLocation) { Transform.Location = NewWorldLocation; }
	FVector GetWorldLocation() const { return Transform.Location; }
	void SetRelativeLocation(const FVector& NewLocation) { Transform.Location = NewLocation; }

	void SetRelativeRotation(const FRotator& NewRotation) { Transform.SetRotation(NewRotation); }
	void SetRelativeRotation(const FVector& EulerRotation) { Transform.SetRotation(FRotator(EulerRotation)); }
	FRotator GetRelativeRotation() const { return Transform.GetRotator(); }

	FVector GetForwardVector() const { return Transform.Rotation.GetForwardVector().Normalized(); }
	FVector GetRightVector() const { return Transform.Rotation.GetRightVector().Normalized(); }
	FVector GetUpVector() const { return Transform.Rotation.GetUpVector().Normalized(); }

	void Move(const FVector& Delta) { Transform.Location += Delta; }
	void MoveLocal(const FVector& Delta);
	void Rotate(float DeltaYaw, float DeltaPitch);

private:
	FTransform Transform;
	FViewportCameraState CameraState;
};

