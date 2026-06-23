#include "Viewport/GameViewportClient.h"

#include "Components/CameraComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Runtime/Engine.h"
#include "GameFramework/AActor.h"
#include "GameFramework/World.h"
#include "Mesh/ObjManager.h"
#include "Object/Object.h"
#include "Viewport/Viewport.h"

#include <d3d11.h>
#include <cmath>

DEFINE_CLASS(UGameViewportClient, UObject)

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
}

bool UGameViewportClient::ProcessInput(FViewportInputContext& Context)
{
	EnsurePIEPlayer();
	if (!PIEPlayerActor || !PIEPlayerCamera)
	{
		return false;
	}

	if (!bPIEPossessedInputEnabled)
	{
		return false;
	}

	bool bConsumedByArmToggle = false;
	UpdatePIEInputArmedState(Context, bConsumedByArmToggle);
	if (bConsumedByArmToggle)
	{
		return true;
	}

	const bool bInputOwnership = Context.bFocused && (Context.bCaptured || Context.bRelativeMouseMode);
	if (!bInputOwnership)
	{
		return false;
	}

	const bool bKeyboardBlocked = Context.bImGuiCapturedKeyboard;
	const bool bMouseBlocked = Context.bImGuiCapturedMouse && !Context.bCaptured;

	const float DeltaTime = (Context.DeltaSeconds > 0.0f) ? Context.DeltaSeconds : (1.0f / 60.0f);
	const FVector MoveInput = BuildMoveInput(Context, bKeyboardBlocked);
	const bool bMoved = ApplyMoveInput(MoveInput, DeltaTime);
	const bool bCanLook = ApplyLookInput(Context, bMouseBlocked);
	UpdatePlayerCameraFromOrbitState();
	SyncPlayerViewToEditorViewport();

	return bMoved || bCanLook;
}

bool UGameViewportClient::WantsRelativeMouseMode(const FViewportInputContext& Context, POINT& OutRestoreScreenPos) const
{
	OutRestoreScreenPos = Context.Frame.MouseScreenPos;
	if (Context.bImGuiCapturedMouse && !Context.bRelativeMouseMode)
	{
		return false;
	}

	if (!Context.bFocused || (!Context.bCaptured && !Context.bRelativeMouseMode))
	{
		return false;
	}

	return bPIEInputArmed && bPIEPossessedInputEnabled;
}

void UGameViewportClient::UpdatePIEInputArmedState(const FViewportInputContext& Context, bool& bOutConsumedByArmToggle)
{
	bOutConsumedByArmToggle = false;
	if (HasKeyEvent(Context, EInputEventType::KeyPressed, VK_LBUTTON) && Context.bHovered)
	{
		bPIEInputArmed = true;
	}
	if (HasKeyEvent(Context, EInputEventType::KeyPressed, VK_LBUTTON)
		&& !Context.bHovered
		&& !Context.bCaptured
		&& !Context.bRelativeMouseMode)
	{
		bPIEInputArmed = false;
	}
	if (HasKeyEvent(Context, EInputEventType::KeyPressed, VK_F1) && Context.Frame.IsDown(VK_SHIFT))
	{
		bPIEInputArmed = false;
		bOutConsumedByArmToggle = true;
	}
}

FVector UGameViewportClient::BuildMoveInput(const FViewportInputContext& Context, bool bKeyboardBlocked) const
{
	FVector MoveInput = FVector(0.0f, 0.0f, 0.0f);
	if (bKeyboardBlocked)
	{
		return MoveInput;
	}

	if (Context.Frame.IsDown('W')) MoveInput.X += 1.0f;
	if (Context.Frame.IsDown('S')) MoveInput.X -= 1.0f;
	if (Context.Frame.IsDown('A')) MoveInput.Y -= 1.0f;
	if (Context.Frame.IsDown('D')) MoveInput.Y += 1.0f;
	return MoveInput;
}

bool UGameViewportClient::ApplyMoveInput(const FVector& MoveInput, float DeltaTime)
{
	if (MoveInput.Length() <= 0.0f)
	{
		return false;
	}

	const FVector NormalizedMoveInput = MoveInput.Normalized();
	constexpr float MoveSpeedUnitsPerSecond = 10.f;
	FVector FlatForward = PIEPlayerActor->GetActorForward();
	FVector FlatRight = PIEPlayerActor->GetRootComponent()
		? PIEPlayerActor->GetRootComponent()->GetRightVector()
		: PIEPlayerCamera->GetRightVector();
	FlatForward.Z = 0.0f;
	FlatRight.Z = 0.0f;
	if (FlatForward.Length() > 0.0f)
	{
		FlatForward = FlatForward.Normalized();
	}
	if (FlatRight.Length() > 0.0f)
	{
		FlatRight = FlatRight.Normalized();
	}

	const FVector WorldDelta = FlatForward * NormalizedMoveInput.X + FlatRight * NormalizedMoveInput.Y;
	PIEPlayerActor->AddActorWorldOffset(WorldDelta * (MoveSpeedUnitsPerSecond * DeltaTime));
	return true;
}

bool UGameViewportClient::ApplyLookInput(const FViewportInputContext& Context, bool bMouseBlocked)
{
	const bool bCanLook = Context.bRelativeMouseMode && !bMouseBlocked;
	if (!bCanLook)
	{
		return false;
	}

	const float LookSensitivity = 0.055f;
	const float DeltaYaw = static_cast<float>(Context.Frame.MouseDelta.x) * LookSensitivity;
	const float DeltaPitch = static_cast<float>(Context.Frame.MouseDelta.y) * LookSensitivity * -1.0f;
	PIECameraYaw += DeltaYaw;
	PIECameraPitch = Clamp(PIECameraPitch + DeltaPitch, -89.0f, 89.0f);

	FRotator ActorYawRotation = PIEPlayerActor->GetActorRotation();
	ActorYawRotation.Pitch = 0.0f;
	ActorYawRotation.Roll = 0.0f;
	ActorYawRotation.Yaw = PIECameraYaw;
	PIEPlayerActor->SetActorRotation(ActorYawRotation);
	return true;
}

void UGameViewportClient::UpdatePlayerCameraFromOrbitState()
{
	const float YawRad = PIECameraYaw * DEG_TO_RAD;
	const float PitchRad = PIECameraPitch * DEG_TO_RAD;
	const float CosPitch = cosf(PitchRad);
	const FVector CameraOffset =
	{
		PIECameraBoomLength * CosPitch * cosf(YawRad),
		PIECameraBoomLength * CosPitch * sinf(YawRad),
		PIECameraBoomLength * sinf(PitchRad)
	};

	const FVector Focus = PIEPlayerActor->GetActorLocation() + FVector(0.0f, 0.0f, 1.2f);
	PIEPlayerCamera->SetWorldLocation(Focus - CameraOffset);
	PIEPlayerCamera->LookAt(Focus);
}

void UGameViewportClient::OnBeginPIE()
{
	bPIEPossessedInputEnabled = false;
	EnsurePIEPlayer();
}

void UGameViewportClient::OnEndPIE()
{
	bPIEPossessedInputEnabled = false;
	ReleasePIEPlayer();
	bPIEInputArmed = false;
}

void UGameViewportClient::EnsurePIEPlayer()
{
	if (!GEngine)
	{
		ReleasePIEPlayer();
		return;
	}

	UWorld* World = GEngine->GetWorld();
	if (!World)
	{
		ReleasePIEPlayer();
		return;
	}

	if (PIEPlayerActor && PIEPlayerActorUUID != 0u)
	{
		UObject* Found = UObjectManager::Get().FindByUUID(PIEPlayerActorUUID);
		if (Found != PIEPlayerActor)
		{
			PIEPlayerActor = nullptr;
			PIEPlayerMesh = nullptr;
			PIEPlayerCamera = nullptr;
			PIEPlayerActorUUID = 0u;
		}
	}

	if (PIEPlayerActor && PIEPlayerActor->GetWorld() != World)
	{
		ReleasePIEPlayer();
	}

	if (PIEPlayerActor)
	{
		return;
	}

	PIEPlayerActor = World->SpawnActor<AActor>();
	if (!PIEPlayerActor)
	{
		return;
	}
	PIEPlayerActorUUID = PIEPlayerActor->GetUUID();

	USceneComponent* Root = PIEPlayerActor->AddComponent<USceneComponent>();
	PIEPlayerActor->SetRootComponent(Root);

	PIEPlayerMesh = PIEPlayerActor->AddComponent<UStaticMeshComponent>();
	PIEPlayerMesh->AttachToComponent(Root);
	PIEPlayerMesh->SetRelativeScale(FVector(0.6f, 0.6f, 1.2f));
	// Ejected 모드에서 PIE 플레이어는 관찰 대상이므로 GPU occlusion false-positive로 인한
	// 깜빡임/outline 소실을 막기 위해 오클루전 테스트에서 제외한다.
	PIEPlayerMesh->SetSkipGPUOcclusion(true);

	if (ID3D11Device* Device = GEngine->GetRenderer().GetFD3DDevice().GetDevice())
	{
		if (UStaticMesh* CubeMesh = FObjManager::LoadObjStaticMesh("Data/BasicShape/Cube.OBJ", Device))
		{
			PIEPlayerMesh->SetStaticMesh(CubeMesh);
		}
	}

	PIEPlayerCamera = PIEPlayerActor->AddComponent<UCameraComponent>();
	PIEPlayerCamera->AttachToComponent(Root);
	PIEPlayerCamera->SetRelativeLocation(FVector(0.0f, 0.0f, 1.8f));
	if (Viewport)
	{
		PIEPlayerCamera->OnResize(static_cast<int32>(Viewport->GetWidth()), static_cast<int32>(Viewport->GetHeight()));
	}

	FVector SpawnLocation = DrivingCamera ? DrivingCamera->GetWorldLocation() : FVector(0.0f, 0.0f, 0.0f);
	SpawnLocation.Z *= 0.5f;
	PIEPlayerActor->SetActorLocation(SpawnLocation);
	PIECameraYaw = DrivingCamera ? DrivingCamera->GetRelativeRotation().Yaw : 0.0f;
	PIECameraPitch = -20.0f;
	SyncPlayerViewToEditorViewport();
}

void UGameViewportClient::ReleasePIEPlayer()
{
	if (PIEPlayerActor)
	{
		// PIE 종료 순서/예외 경로에서 Actor가 이미 해제됐을 수 있으므로 UUID로 유효성 확인.
		UObject* Found = (PIEPlayerActorUUID != 0u)
			? UObjectManager::Get().FindByUUID(PIEPlayerActorUUID)
			: nullptr;

		if (Found == PIEPlayerActor)
		{
			if (UWorld* OwnerWorld = PIEPlayerActor->GetWorld())
			{
				OwnerWorld->DestroyActor(PIEPlayerActor);
			}
		}
	}

	PIEPlayerActor = nullptr;
	PIEPlayerActorUUID = 0u;
	PIEPlayerMesh = nullptr;
	PIEPlayerCamera = nullptr;
}

void UGameViewportClient::SyncPlayerViewToEditorViewport()
{
	if (!DrivingCamera || !PIEPlayerCamera)
	{
		return;
	}

	if (Viewport)
	{
		PIEPlayerCamera->OnResize(static_cast<int32>(Viewport->GetWidth()), static_cast<int32>(Viewport->GetHeight()));
	}

	DrivingCamera->SetWorldLocation(PIEPlayerCamera->GetWorldLocation());
	DrivingCamera->SetRelativeRotation(PIEPlayerCamera->GetRelativeRotation());
	FCameraState CameraState = DrivingCamera->GetCameraState();
	CameraState.FOV = PIEPlayerCamera->GetFOV();
	CameraState.NearZ = PIEPlayerCamera->GetNearPlane();
	CameraState.FarZ = PIEPlayerCamera->GetFarPlane();
	CameraState.bIsOrthogonal = PIEPlayerCamera->IsOrthogonal();
	CameraState.OrthoWidth = PIEPlayerCamera->GetOrthoWidth();
	DrivingCamera->SetCameraState(CameraState);
}
