#include "Editor/Viewport/ViewportCamera.h"

#include <cmath>

void FViewportCamera::LookAt(const FVector& Target)
{
	const FVector Position = GetWorldLocation();
	const FVector Diff = (Target - Position).Normalized();

	constexpr float Rad2Deg = 180.0f / 3.14159265358979f;

	FRotator LookRotation = GetRelativeRotation();
	LookRotation.Pitch = -asinf(Diff.Z) * Rad2Deg;

	if (fabsf(Diff.Z) < 0.999f)
	{
		LookRotation.Yaw = atan2f(Diff.Y, Diff.X) * Rad2Deg;
	}
	LookRotation.Roll = 0.0f;

	SetRelativeRotation(LookRotation);
}

void FViewportCamera::OnResize(int32 Width, int32 Height)
{
	CameraState.AspectRatio = static_cast<float>(Width) / static_cast<float>(Height);
}

FMatrix FViewportCamera::GetViewMatrix() const
{
	const FVector F = GetForwardVector();
	const FVector R = GetRightVector();
	const FVector U = GetUpVector();
	const FVector E = GetWorldLocation();

	return FMatrix(
		R.X, U.X, F.X, 0,
		R.Y, U.Y, F.Y, 0,
		R.Z, U.Z, F.Z, 0,
		-E.Dot(R), -E.Dot(U), -E.Dot(F), 1
	);
}

FMatrix FViewportCamera::GetProjectionMatrix() const
{
	const float Cot = 1.0f / tanf(CameraState.FOV * 0.5f);
	const float Denom = CameraState.FarZ - CameraState.NearZ;

	if (!CameraState.bIsOrthogonal)
	{
		// Reversed-Z: NearZ -> 1, FarZ -> 0
		return FMatrix(
			Cot / CameraState.AspectRatio, 0, 0, 0,
			0, Cot, 0, 0,
			0, 0, -CameraState.NearZ / Denom, 1,
			0, 0, (CameraState.FarZ * CameraState.NearZ) / Denom, 0
		);
	}

	const float HalfW = CameraState.OrthoWidth * 0.5f;
	const float HalfH = HalfW / CameraState.AspectRatio;
	// Reversed-Z: NearZ -> 1, FarZ -> 0
	return FMatrix(
		1.0f / HalfW, 0, 0, 0,
		0, 1.0f / HalfH, 0, 0,
		0, 0, -1.0f / Denom, 0,
		0, 0, CameraState.FarZ / Denom, 1
	);
}

FRay FViewportCamera::DeprojectScreenToWorld(float MouseX, float MouseY, float ScreenWidth, float ScreenHeight) const
{
	const float NdcX = (2.0f * MouseX) / ScreenWidth - 1.0f;
	const float NdcY = 1.0f - (2.0f * MouseY) / ScreenHeight;

	const FVector NdcNear(NdcX, NdcY, 1.0f);
	const FVector NdcFar(NdcX, NdcY, 0.0f);

	const FMatrix ViewProj = GetViewMatrix() * GetProjectionMatrix();
	const FMatrix InverseViewProjection = ViewProj.GetInverse();

	const FVector WorldNear = InverseViewProjection.TransformPositionWithW(NdcNear);
	const FVector WorldFar = InverseViewProjection.TransformPositionWithW(NdcFar);

	FRay Ray;
	Ray.Origin = WorldNear;

	const FVector Dir = WorldFar - WorldNear;
	const float Length = std::sqrt(Dir.X * Dir.X + Dir.Y * Dir.Y + Dir.Z * Dir.Z);
	Ray.Direction = (Length > 1e-4f) ? Dir / Length : FVector(1, 0, 0);
	return Ray;
}

void FViewportCamera::MoveLocal(const FVector& Delta)
{
	const FVector Forward = GetForwardVector();
	const FVector Right = GetRightVector();
	const FVector Up = GetUpVector();

	SetRelativeLocation(GetWorldLocation()
		+ Forward * Delta.X
		+ Right * Delta.Y
		+ Up * Delta.Z);
}

void FViewportCamera::Rotate(float DeltaYaw, float DeltaPitch)
{
	// Camera-style rotation:
	// - Yaw around world up (+Z)
	// - Pitch around current local right (+Y)

	// Convert current rotation to Euler to easily clamp Pitch and avoid flipping/singularity issues
	FRotator Rot = Transform.Rotation.ToRotator();

	Rot.Yaw += DeltaYaw;
	Rot.Pitch = Clamp(Rot.Pitch + DeltaPitch, -89.9f, 89.9f);
	Rot.Roll = 0.0f;

	SetRelativeRotation(Rot);
}

