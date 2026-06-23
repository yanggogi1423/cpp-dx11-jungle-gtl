#include "Editor/Input/EditorNavigationTool.h"

#include "Editor/Input/EditorViewportInputMapping.h"
#include "Editor/Input/EditorViewportInputUtils.h"
#include "Editor/Gizmo/TransformGizmo.h"
#include "Editor/Selection/SelectionManager.h"
#include "Editor/Settings/EditorSettings.h"
#include "Editor/Viewport/EditorViewportClient.h"
#include "Editor/Viewport/ViewportCamera.h"
#include "GameFramework/AActor.h"
#include "Math/MathUtils.h"
#include "Math/Quat.h"

#include <cmath>

FEditorNavigationTool::FEditorNavigationTool(FEditorViewportClient* InOwner)
	: Owner(InOwner)
{
}

bool FEditorNavigationTool::HandleInput(float DeltaTime)
{
	if (!Owner || !Owner->Camera)
	{
		return false;
	}

	const bool bWasActive = IsInputActiveNow();
	const bool bLeftDragLookRaw = EditorViewportInputUtils::IsLeftNavigationDragActive(Owner->InputContext);
	const bool bGizmoDragging = Owner->Gizmo && (Owner->Gizmo->IsHolding() || Owner->Gizmo->IsPressedOnHandle());
	const bool bLeftDragLookFrame = bLeftDragLookRaw && !bGizmoDragging;
	const bool bAltOrbitFrame =
		EditorViewportInputMapping::IsTriggered(Owner->InputContext, EditorViewportInputMapping::EEditorViewportAction::NavOrbitAltLeftDown);
	const bool bAltDollyFrame =
		EditorViewportInputMapping::IsTriggered(Owner->InputContext, EditorViewportInputMapping::EEditorViewportAction::NavDollyAltRightDown);
	const bool bAltPanFrame =
		EditorViewportInputMapping::IsTriggered(Owner->InputContext, EditorViewportInputMapping::EEditorViewportAction::NavPanAltMiddleDown);
	const bool bMiddlePanFrame =
		EditorViewportInputMapping::IsTriggered(Owner->InputContext, EditorViewportInputMapping::EEditorViewportAction::NavLookMiddleDown);
	const bool bRightLookFrame =
		EditorViewportInputMapping::IsTriggered(Owner->InputContext, EditorViewportInputMapping::EEditorViewportAction::NavLookRightDown);
	const bool bMouseLookFrame = bLeftDragLookFrame || bRightLookFrame || bAltOrbitFrame;
	const bool bPanFrame = bMiddlePanFrame || bAltPanFrame;
	const bool bAnyMouseNavigationFrame = bMouseLookFrame || bAltDollyFrame || bPanFrame;
	const bool bKeyboardNavigationBlocked = Owner->InputContext.bImGuiCapturedKeyboard && !bAnyMouseNavigationFrame;

	const FViewportCameraState& CameraState = Owner->Camera->GetCameraState();
	const bool bIsOrtho = CameraState.bIsOrthogonal;
	const float MoveSensitivity = Owner->RenderOptions.CameraMoveSensitivity;
	const float CameraSpeed = GetEffectiveCameraSpeed() * MoveSensitivity;
	const float MouseDeltaX = static_cast<float>(Owner->InputContext.Frame.MouseDelta.x);
	const float MouseDeltaY = static_cast<float>(Owner->InputContext.Frame.MouseDelta.y);
	const float ScrollNotches = Owner->InputContext.Frame.WheelNotches;
	const bool bAdjustSpeedFrame = bRightLookFrame && ScrollNotches != 0.0f;
	if (bAdjustSpeedFrame)
	{
		AdjustRuntimeCameraSpeed(ScrollNotches);
	}
	const bool bWheelDollyFrame =
		ScrollNotches != 0.0f
		&& !bAdjustSpeedFrame
		&& EditorViewportInputMapping::IsTriggered(Owner->InputContext, EditorViewportInputMapping::EEditorViewportAction::NavWheelScroll);
	constexpr float WheelDollyVirtualMouseDelta = 50.0f;
	float DollyInputY = 0.0f;
	if (bAltDollyFrame)
	{
		DollyInputY += MouseDeltaY;
	}
	if (bWheelDollyFrame)
	{
		// Reuse the same dolly path as Alt+RMB by feeding a small virtual mouse delta.
		DollyInputY += -ScrollNotches * WheelDollyVirtualMouseDelta;
	}

	if (!bIsOrtho)
	{
		FVector Move = FVector(0, 0, 0);
		const bool bAllowKeyboardFlyMove = (bRightLookFrame || bLeftDragLookFrame) && !bKeyboardNavigationBlocked;
		if (bAllowKeyboardFlyMove)
		{
			if (EditorViewportInputMapping::IsTriggered(Owner->InputContext, EditorViewportInputMapping::EEditorViewportAction::NavMoveForward)) Move.X += CameraSpeed;
			if (EditorViewportInputMapping::IsTriggered(Owner->InputContext, EditorViewportInputMapping::EEditorViewportAction::NavMoveLeft)) Move.Y -= CameraSpeed;
			if (EditorViewportInputMapping::IsTriggered(Owner->InputContext, EditorViewportInputMapping::EEditorViewportAction::NavMoveBackward)) Move.X -= CameraSpeed;
			if (EditorViewportInputMapping::IsTriggered(Owner->InputContext, EditorViewportInputMapping::EEditorViewportAction::NavMoveRight)) Move.Y += CameraSpeed;
			if (EditorViewportInputMapping::IsTriggered(Owner->InputContext, EditorViewportInputMapping::EEditorViewportAction::NavMoveDown)) Move.Z -= CameraSpeed;
			if (EditorViewportInputMapping::IsTriggered(Owner->InputContext, EditorViewportInputMapping::EEditorViewportAction::NavMoveUp)) Move.Z += CameraSpeed;
		}
		Move *= DeltaTime;
		AddCameraMoveInputLocal(Move);

		if (bPanFrame)
		{
			const float PanScale = 0.03f * MoveSensitivity;
			AddCameraMoveInputLocal(FVector(0.0f, MouseDeltaX * PanScale, -MouseDeltaY * PanScale));
		}

		if (DollyInputY != 0.0f)
		{
			const float DollyScale = 0.001f * CameraSpeed;
			AddCameraMoveInputLocal(FVector(-DollyInputY * DollyScale, 0.0f, 0.0f));
		}

		if (bAltOrbitFrame)
		{
			FVector Pivot = Owner->Camera->GetWorldLocation() + Owner->Camera->GetForwardVector() * 10.0f;
			if (Owner->SelectionManager)
			{
				if (AActor* Primary = Owner->SelectionManager->GetPrimarySelection())
				{
					Pivot = Primary->GetActorLocation();
				}
			}
			OrbitCameraAroundPivot(Pivot, MouseDeltaX, MouseDeltaY, Owner->RenderOptions.CameraRotateSensitivity);
			Owner->Camera->SetWorldLocation(CameraTargetLocation);
			Owner->Camera->SetRelativeRotation(CameraTargetRotation);
		}

		FVector Rotation = FVector(0, 0, 0);
		const float RotateSensitivity = Owner->RenderOptions.CameraRotateSensitivity;
		const float AngleVelocity = (Owner->Settings ? Owner->Settings->CameraRotationSpeed : 60.f) * RotateSensitivity;
		if (!bKeyboardNavigationBlocked)
		{
			if (EditorViewportInputMapping::IsTriggered(Owner->InputContext, EditorViewportInputMapping::EEditorViewportAction::NavRotateUp)) Rotation.Z -= AngleVelocity;
			if (EditorViewportInputMapping::IsTriggered(Owner->InputContext, EditorViewportInputMapping::EEditorViewportAction::NavRotateLeft)) Rotation.Y -= AngleVelocity;
			if (EditorViewportInputMapping::IsTriggered(Owner->InputContext, EditorViewportInputMapping::EEditorViewportAction::NavRotateDown)) Rotation.Z += AngleVelocity;
			if (EditorViewportInputMapping::IsTriggered(Owner->InputContext, EditorViewportInputMapping::EEditorViewportAction::NavRotateRight)) Rotation.Y += AngleVelocity;
		}

		FVector MouseRotation = FVector(0, 0, 0);
		const float MouseRotationSpeed = 0.15f * RotateSensitivity;
		const bool bLookInputDown = bMouseLookFrame && !bAltDollyFrame && !bPanFrame && !bAltOrbitFrame;
		if (bLookInputDown)
		{
			MouseRotation.Y += MouseDeltaX * MouseRotationSpeed; // Yaw
			MouseRotation.Z += MouseDeltaY * MouseRotationSpeed; // Pitch
			// Yaw (Y) should NOT be clamped to 89. Clipping Y and Z together was reported as a bug.
			// Only Pitch (Z) is clamped.
			MouseRotation.Z = Clamp(MouseRotation.Z, -89.0f, 89.0f);
		}

		Rotation *= DeltaTime;
		AddCameraRotateInput(Rotation.Y + MouseRotation.Y, Rotation.Z + MouseRotation.Z);
	}
	else
	{
		const bool bOrthoRightDragPanFrame = bRightLookFrame;

		if (bPanFrame || bOrthoRightDragPanFrame)
		{
			const float PanScale = CameraState.OrthoWidth * 0.002f * MoveSensitivity;
			AddCameraMoveInputLocal(FVector(0, -MouseDeltaX * PanScale, MouseDeltaY * PanScale));
		}

		if (DollyInputY != 0.0f)
		{
			const float NextOrthoWidth = Owner->Camera->GetOrthoWidth() + DollyInputY * CameraState.OrthoWidth * 0.003f;
			Owner->Camera->SetOrthoWidth(Clamp(NextOrthoWidth, 0.1f, 5000.0f));
		}
	}

	return bWasActive || IsInputActiveNow();
}

bool FEditorNavigationTool::IsInputActiveNow() const
{
	if (!Owner)
	{
		return false;
	}

	const bool bGizmoDragging = Owner->Gizmo && (Owner->Gizmo->IsHolding() || Owner->Gizmo->IsPressedOnHandle());
	const bool bLeftNavigationActive = EditorViewportInputUtils::IsLeftNavigationDragActive(Owner->InputContext) && !bGizmoDragging;

	return EditorViewportInputMapping::IsTriggered(Owner->InputContext, EditorViewportInputMapping::EEditorViewportAction::NavLookRightDown)
		|| EditorViewportInputMapping::IsTriggered(Owner->InputContext, EditorViewportInputMapping::EEditorViewportAction::NavLookMiddleDown)
		|| EditorViewportInputMapping::IsTriggered(Owner->InputContext, EditorViewportInputMapping::EEditorViewportAction::NavOrbitAltLeftDown)
		|| EditorViewportInputMapping::IsTriggered(Owner->InputContext, EditorViewportInputMapping::EEditorViewportAction::NavDollyAltRightDown)
		|| EditorViewportInputMapping::IsTriggered(Owner->InputContext, EditorViewportInputMapping::EEditorViewportAction::NavPanAltMiddleDown)
		|| bLeftNavigationActive
		|| EditorViewportInputMapping::IsTriggered(Owner->InputContext, EditorViewportInputMapping::EEditorViewportAction::NavMoveForward)
		|| EditorViewportInputMapping::IsTriggered(Owner->InputContext, EditorViewportInputMapping::EEditorViewportAction::NavMoveLeft)
		|| EditorViewportInputMapping::IsTriggered(Owner->InputContext, EditorViewportInputMapping::EEditorViewportAction::NavMoveBackward)
		|| EditorViewportInputMapping::IsTriggered(Owner->InputContext, EditorViewportInputMapping::EEditorViewportAction::NavMoveRight)
		|| EditorViewportInputMapping::IsTriggered(Owner->InputContext, EditorViewportInputMapping::EEditorViewportAction::NavMoveDown)
		|| EditorViewportInputMapping::IsTriggered(Owner->InputContext, EditorViewportInputMapping::EEditorViewportAction::NavMoveUp)
		|| EditorViewportInputMapping::IsTriggered(Owner->InputContext, EditorViewportInputMapping::EEditorViewportAction::NavRotateUp)
		|| EditorViewportInputMapping::IsTriggered(Owner->InputContext, EditorViewportInputMapping::EEditorViewportAction::NavRotateDown)
		|| EditorViewportInputMapping::IsTriggered(Owner->InputContext, EditorViewportInputMapping::EEditorViewportAction::NavRotateLeft)
		|| EditorViewportInputMapping::IsTriggered(Owner->InputContext, EditorViewportInputMapping::EEditorViewportAction::NavRotateRight)
		|| EditorViewportInputMapping::IsTriggered(Owner->InputContext, EditorViewportInputMapping::EEditorViewportAction::NavWheelScroll);
}

void FEditorNavigationTool::TickSmoothing(float DeltaTime)
{
	if (!Owner || !Owner->Camera)
	{
		return;
	}

	if (!bSmoothingInitialized)
	{
		SyncCameraTargetFromCurrent();
	}

	const bool bEnableSmoothing = Owner->Settings ? Owner->Settings->bEnableCameraSmoothing : true;
	const float MoveSmoothSpeed = Clamp(Owner->Settings ? Owner->Settings->CameraMoveSmoothSpeed : CameraMoveSmoothSpeed, 0.01f, 100.0f);
	const float RotateSmoothSpeed = Clamp(Owner->Settings ? Owner->Settings->CameraRotateSmoothSpeed : CameraRotateSmoothSpeed, 0.01f, 100.0f);

	if (!bEnableSmoothing)
	{
		Owner->Camera->SetWorldLocation(CameraTargetLocation);
		Owner->Camera->SetRelativeRotation(CameraTargetRotation);
		return;
	}

	const float MoveAlpha = 1.0f - std::exp(-MoveSmoothSpeed * DeltaTime);
	const float RotateAlpha = 1.0f - std::exp(-RotateSmoothSpeed * DeltaTime);

	const FVector CurrentLocation = Owner->Camera->GetWorldLocation();
	const FVector NextLocation = CurrentLocation + (CameraTargetLocation - CurrentLocation) * MoveAlpha;
	Owner->Camera->SetWorldLocation(NextLocation);

	FRotator CurrentRotation = Owner->Camera->GetRelativeRotation();
	auto InterpAngle = [RotateAlpha](float Current, float Target)
	{
		float Delta = std::fmod(Target - Current + 540.0f, 360.0f) - 180.0f;
		return Current + Delta * RotateAlpha;
	};
	CurrentRotation.Yaw = InterpAngle(CurrentRotation.Yaw, CameraTargetRotation.Yaw);
	CurrentRotation.Pitch = InterpAngle(CurrentRotation.Pitch, CameraTargetRotation.Pitch);
	CurrentRotation.Roll = 0.0f;
	Owner->Camera->SetRelativeRotation(CurrentRotation);
}

void FEditorNavigationTool::FocusOnTarget(const FVector& Target, float DesiredDistance)
{
	if (!Owner || !Owner->Camera)
	{
		return;
	}

	if (!bSmoothingInitialized)
	{
		SyncCameraTargetFromCurrent();
	}

	if (Owner->Camera->IsOrthogonal())
	{
		// Ortho focus must behave like panning only: keep current rotation/depth.
		const FQuat TargetQuat = FQuat::FromRotator(CameraTargetRotation).GetNormalized();
		const FVector Forward = TargetQuat.GetForwardVector().Normalized();
		const FVector Right = TargetQuat.GetRightVector().Normalized();
		const FVector Up = TargetQuat.GetUpVector().Normalized();

		const FVector ToTarget = Target - CameraTargetLocation;
		const float PanRight = ToTarget.Dot(Right);
		const float PanUp = ToTarget.Dot(Up);
		CameraTargetLocation = CameraTargetLocation + Right * PanRight + Up * PanUp;
		return;
	}

	FVector ToCamera = CameraTargetLocation - Target;
	float Distance = ToCamera.Length();
	if (Distance < 1.0f)
	{
		const FQuat TargetQuat = FQuat::FromRotator(CameraTargetRotation).GetNormalized();
		ToCamera = TargetQuat.GetForwardVector() * -1.0f;
		Distance = 10.0f;
	}
	if (DesiredDistance > 0.0f)
	{
		Distance = DesiredDistance;
	}

	Distance = Clamp(Distance, 3.0f, 400.0f);
	const FVector Direction = ToCamera.Normalized();
	CameraTargetLocation = Target + Direction * Distance;
	CameraTargetRotation = MakeLookAtRotation(CameraTargetLocation, Target);
}

void FEditorNavigationTool::SyncFromCamera()
{
	SyncCameraTargetFromCurrent();
}

void FEditorNavigationTool::SyncCameraTargetFromCurrent()
{
	if (!Owner || !Owner->Camera)
	{
		return;
	}

	CameraTargetLocation = Owner->Camera->GetWorldLocation();
	CameraTargetRotation = Owner->Camera->GetRelativeRotation();
	bSmoothingInitialized = true;
}

FRotator FEditorNavigationTool::MakeLookAtRotation(const FVector& From, const FVector& To) const
{
	FVector Diff = (To - From).Normalized();
	FRotator LookRotation;
	LookRotation.Pitch = -asinf(Diff.Z) * RAD_TO_DEG;
	if (std::fabs(Diff.Z) < 0.999f)
	{
		LookRotation.Yaw = atan2f(Diff.Y, Diff.X) * RAD_TO_DEG;
	}
	LookRotation.Roll = 0.0f;
	return LookRotation;
}

void FEditorNavigationTool::AddCameraMoveInputLocal(const FVector& DeltaLocal)
{
	if (!Owner || !Owner->Camera)
	{
		return;
	}

	if (!bSmoothingInitialized)
	{
		SyncCameraTargetFromCurrent();
	}

	const FQuat TargetQuat = FQuat::FromRotator(CameraTargetRotation).GetNormalized();
	const FVector Forward = TargetQuat.GetForwardVector().Normalized();
	const FVector Right = TargetQuat.GetRightVector().Normalized();
	const FVector Up = TargetQuat.GetUpVector().Normalized();
	CameraTargetLocation = CameraTargetLocation + Forward * DeltaLocal.X + Right * DeltaLocal.Y + Up * DeltaLocal.Z;
}

void FEditorNavigationTool::AddCameraRotateInput(float DeltaYaw, float DeltaPitch)
{
	if (!Owner || !Owner->Camera)
	{
		return;
	}

	if (!bSmoothingInitialized)
	{
		SyncCameraTargetFromCurrent();
	}
	
	constexpr float PsuedoScale = 0.65f;
	DeltaYaw *= PsuedoScale;
	DeltaPitch *= PsuedoScale;

	CameraTargetRotation.Yaw += DeltaYaw;
	CameraTargetRotation.Pitch = Clamp(CameraTargetRotation.Pitch + DeltaPitch, -89.9f, 89.9f);
	CameraTargetRotation.Roll = 0.0f;
}

void FEditorNavigationTool::OrbitCameraAroundPivot(const FVector& Pivot, float DeltaMouseX, float DeltaMouseY, float OrbitSensitivity)
{
	if (!Owner || !Owner->Camera)
	{
		return;
	}

	if (!bSmoothingInitialized)
	{
		SyncCameraTargetFromCurrent();
	}

	const FVector Offset = CameraTargetLocation - Pivot;
	float Radius = Offset.Length();
	if (Radius < 1.0f)
	{
		Radius = 10.0f;
	}

	float Azimuth = atan2f(Offset.Y, Offset.X);
	float Elevation = asinf(Clamp(Offset.Z / Radius, -1.0f, 1.0f));
	const float OrbitSpeed = 0.0035f * OrbitSensitivity;
	Azimuth += DeltaMouseX * OrbitSpeed;
	Elevation = Clamp(Elevation + DeltaMouseY * OrbitSpeed, -1.4f, 1.4f);

	FVector NextOffset;
	const float CosElevation = cosf(Elevation);
	NextOffset.X = Radius * CosElevation * cosf(Azimuth);
	NextOffset.Y = Radius * CosElevation * sinf(Azimuth);
	NextOffset.Z = Radius * sinf(Elevation);

	CameraTargetLocation = Pivot + NextOffset;
	CameraTargetRotation = MakeLookAtRotation(CameraTargetLocation, Pivot);
}

void FEditorNavigationTool::AdjustRuntimeCameraSpeed(float WheelNotches)
{
	if (WheelNotches == 0.0f)
	{
		return;
	}

	const float BaseSpeed = Owner && Owner->Settings
		? Owner->Settings->CameraSpeed
		: FEditorSettings::Get().CameraSpeed;
	const float SafeBaseSpeed = (BaseSpeed > 0.0001f) ? BaseSpeed : 0.0001f;
	const float MinMultiplier = GetMinCameraSpeedValue() / SafeBaseSpeed;
	const float MaxMultiplier = GetMaxCameraSpeedValue() / SafeBaseSpeed;
	const float Step = 0.1f;
	RuntimeCameraSpeedMultiplier = Clamp(RuntimeCameraSpeedMultiplier + WheelNotches * Step, MinMultiplier, MaxMultiplier);
}

void FEditorNavigationTool::SetRuntimeCameraSpeedMultiplier(float InMultiplier)
{
	const float BaseSpeed = Owner && Owner->Settings
		? Owner->Settings->CameraSpeed
		: FEditorSettings::Get().CameraSpeed;
	const float SafeBaseSpeed = (BaseSpeed > 0.0001f) ? BaseSpeed : 0.0001f;
	const float MinMultiplier = GetMinCameraSpeedValue() / SafeBaseSpeed;
	const float MaxMultiplier = GetMaxCameraSpeedValue() / SafeBaseSpeed;
	RuntimeCameraSpeedMultiplier = Clamp(InMultiplier, MinMultiplier, MaxMultiplier);
}

float FEditorNavigationTool::GetEffectiveCameraSpeed() const
{
	const float BaseSpeed = Owner && Owner->Settings
		? Owner->Settings->CameraSpeed
		: FEditorSettings::Get().CameraSpeed;
	return BaseSpeed * RuntimeCameraSpeedMultiplier;
}
