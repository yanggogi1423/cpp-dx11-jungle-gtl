#include "Viewport/GameViewportClient.h"

#include "Component/CameraComponent.h"
#include "Component/SceneComponent.h"
#include "Component/StaticMeshComponent.h"
#include "Editor/EditorEngine.h"
#include "Editor/Viewport/LevelEditorViewportClient.h"
#include "Editor/Viewport/ViewportCamera.h"
#include "Engine/Input/InputBinding.h"
#include "Engine/Input/InputSystem.h"
#include "Engine/Runtime/Engine.h"
#include "GameFramework/AActor.h"
#include "GameFramework/World.h"
#include "Mesh/ObjManager.h"
#include "Object/Object.h"
#include "Viewport/Viewport.h"
#include "Math/MathUtils.h"

#include <algorithm>
#include <cmath>
#include <d3d11.h>
#include <memory>

namespace
{
enum class EPIEGlobalAction : int32
{
	EndPIE,
	TogglePossessEject,
	ReleaseMouseCapture
};

enum class EPIEPlayerAction : int32
{
	MoveForward,
	MoveLeft,
	MoveBackward,
	MoveRight
};

const TArray<FInputBinding>& GetPIEGlobalBindings()
{
	static const TArray<FInputBinding> Bindings =
	{
		{ static_cast<int32>(EPIEGlobalAction::EndPIE), EInputBindingTrigger::Released, { VK_ESCAPE, false, false, false }, EInputEventType::KeyReleased, 300 },
		{ static_cast<int32>(EPIEGlobalAction::TogglePossessEject), EInputBindingTrigger::Released, { VK_F8, false, false, false }, EInputEventType::KeyReleased, 280 },
		{ static_cast<int32>(EPIEGlobalAction::ReleaseMouseCapture), EInputBindingTrigger::Released, { VK_F1, false, false, true }, EInputEventType::KeyReleased, 260 }
	};
	return Bindings;
}

const TArray<FInputBinding>& GetPIEPlayerBindings()
{
	static const TArray<FInputBinding> Bindings =
	{
		{ static_cast<int32>(EPIEPlayerAction::MoveForward), EInputBindingTrigger::Down, { 'W', false, false, false }, EInputEventType::KeyPressed, 0 },
		{ static_cast<int32>(EPIEPlayerAction::MoveLeft), EInputBindingTrigger::Down, { 'A', false, false, false }, EInputEventType::KeyPressed, 0 },
		{ static_cast<int32>(EPIEPlayerAction::MoveBackward), EInputBindingTrigger::Down, { 'S', false, false, false }, EInputEventType::KeyPressed, 0 },
		{ static_cast<int32>(EPIEPlayerAction::MoveRight), EInputBindingTrigger::Down, { 'D', false, false, false }, EInputEventType::KeyPressed, 0 }
	};
	return Bindings;
}

bool IsPlayerActionTriggered(const FViewportInputContext& Context, EPIEPlayerAction Action)
{
	return InputBindingUtils::IsActionTriggered(Context, GetPIEPlayerBindings(), static_cast<int32>(Action));
}

class IGameViewportTool
{
public:
	virtual ~IGameViewportTool() = default;
	virtual bool HandleInput(UGameViewportClient* Owner, float DeltaTime) = 0;
};

class FPIEGlobalTool final : public IGameViewportTool
{
public:
	bool HandleInput(UGameViewportClient* Owner, float DeltaTime) override
	{
		return Owner ? Owner->HandleGlobalInput(DeltaTime) : false;
	}
};

class FPIEPlayerTool final : public IGameViewportTool
{
public:
	bool HandleInput(UGameViewportClient* Owner, float DeltaTime) override
	{
		return Owner ? Owner->HandlePlayerInput(DeltaTime) : false;
	}
};

class FPIEGizmoTool final : public IGameViewportTool
{
public:
	bool HandleInput(UGameViewportClient* Owner, float DeltaTime) override
	{
		return Owner ? Owner->HandleGizmoInput(DeltaTime) : false;
	}
};

class FPIEGlobalInputContext final : public IInputContext
{
public:
	FPIEGlobalInputContext(UGameViewportClient* InOwner, float* InDeltaTime)
		: Owner(InOwner), DeltaTimePtr(InDeltaTime)
	{
	}

	int32 GetPriority() const override { return 300; }
	bool HandleInput(FViewportInputContext& Context) override
	{
		(void)Context;
		return Owner && DeltaTimePtr && Tool ? Tool->HandleInput(Owner, *DeltaTimePtr) : false;
	}

	std::unique_ptr<IGameViewportTool> Tool = std::make_unique<FPIEGlobalTool>();

private:
	UGameViewportClient* Owner = nullptr;
	float* DeltaTimePtr = nullptr;
};

class FPIEPlayerInputContext final : public IInputContext
{
public:
	FPIEPlayerInputContext(UGameViewportClient* InOwner, float* InDeltaTime)
		: Owner(InOwner), DeltaTimePtr(InDeltaTime)
	{
	}

	int32 GetPriority() const override { return 200; }
	bool HandleInput(FViewportInputContext& Context) override
	{
		(void)Context;
		return Owner && DeltaTimePtr && Tool ? Tool->HandleInput(Owner, *DeltaTimePtr) : false;
	}

	std::unique_ptr<IGameViewportTool> Tool = std::make_unique<FPIEPlayerTool>();

private:
	UGameViewportClient* Owner = nullptr;
	float* DeltaTimePtr = nullptr;
};

class FPIEGizmoInputContext final : public IInputContext
{
public:
	FPIEGizmoInputContext(UGameViewportClient* InOwner, float* InDeltaTime)
		: Owner(InOwner), DeltaTimePtr(InDeltaTime)
	{
	}

	int32 GetPriority() const override { return 100; }
	bool HandleInput(FViewportInputContext& Context) override
	{
		(void)Context;
		return Owner && DeltaTimePtr && Tool ? Tool->HandleInput(Owner, *DeltaTimePtr) : false;
	}

	std::unique_ptr<IGameViewportTool> Tool = std::make_unique<FPIEGizmoTool>();

private:
	UGameViewportClient* Owner = nullptr;
	float* DeltaTimePtr = nullptr;
};
}

class FGameViewportController
{
public:
	explicit FGameViewportController(UGameViewportClient* InOwner)
		: Owner(InOwner)
	{
		GlobalInputContext = std::make_unique<FPIEGlobalInputContext>(Owner, &DispatchDeltaTime);
		PlayerInputContext = std::make_unique<FPIEPlayerInputContext>(Owner, &DispatchDeltaTime);
		GizmoInputContext = std::make_unique<FPIEGizmoInputContext>(Owner, &DispatchDeltaTime);

		InputContexts.push_back(GlobalInputContext.get());
		InputContexts.push_back(PlayerInputContext.get());
		InputContexts.push_back(GizmoInputContext.get());
		std::sort(
			InputContexts.begin(),
			InputContexts.end(),
			[](IInputContext* Lhs, IInputContext* Rhs)
			{
				if (!Lhs || !Rhs)
				{
					return Lhs != nullptr;
				}
				return Lhs->GetPriority() > Rhs->GetPriority();
			});
	}

	bool HandleInput(FViewportInputContext& Context, float DeltaTime)
	{
		DispatchDeltaTime = DeltaTime;
		for (IInputContext* InputContext : InputContexts)
		{
			if (InputContext && InputContext->HandleInput(Context))
			{
				return true;
			}
		}
		return false;
	}

private:
	UGameViewportClient* Owner = nullptr;
	float DispatchDeltaTime = 0.0f;
	std::unique_ptr<FPIEGlobalInputContext> GlobalInputContext;
	std::unique_ptr<FPIEPlayerInputContext> PlayerInputContext;
	std::unique_ptr<FPIEGizmoInputContext> GizmoInputContext;
	TArray<IInputContext*> InputContexts;
};

DEFINE_CLASS(UGameViewportClient, UObject)

UGameViewportClient::~UGameViewportClient()
{
	delete Controller;
	Controller = nullptr;
	ReleasePIEPlayer();
}

void UGameViewportClient::Draw(FViewport* InViewport, float DeltaTime)
{
	(void)InViewport;
	DispatchDeltaTime = DeltaTime;
	EnsurePIEPlayer();
}

bool UGameViewportClient::ProcessInput(FViewportInputContext& Context)
{
	InputContext = Context;
	bHasInputContext = false;
	DispatchDeltaTime = 1.0f / 60.0f;

	if (Context.WasPressed(VK_LBUTTON) && !Context.bHovered)
	{
		bPIEInputArmed = false;
	}
	if (Context.WasPressed(VK_LBUTTON) && Context.bHovered)
	{
		bPIEInputArmed = true;
	}

	EnsureController();
	EnsurePIEPlayer();

	UEditorEngine* EditorEngine = Cast<UEditorEngine>(GEngine);
	const bool bPossessedMode =
		EditorEngine
		&& EditorEngine->IsPIEEnabled()
		&& EditorEngine->GetPIEControlMode() == UEditorEngine::EPIEControlMode::Possessed;
	const bool bInputOwnership = Context.bFocused && (Context.bCaptured || Context.bRelativeMouseMode);

	if (Controller)
	{
		const bool bConsumed = Controller->HandleInput(InputContext, DispatchDeltaTime);
		return bConsumed || (bPossessedMode && bInputOwnership);
	}

	return bPossessedMode && bInputOwnership;
}

bool UGameViewportClient::WantsRelativeMouseMode(const FViewportInputContext& Context, POINT& OutRestoreScreenPos) const
{
	OutRestoreScreenPos = Context.Frame.MouseScreenPos;
	if (Context.bImGuiCapturedMouse)
	{
		return false;
	}

	UEditorEngine* EditorEngine = Cast<UEditorEngine>(GEngine);
	if (!EditorEngine || !EditorEngine->IsPIEEnabled() || EditorEngine->GetPIEControlMode() != UEditorEngine::EPIEControlMode::Possessed)
	{
		return false;
	}

	if (!Context.bFocused || (!Context.bCaptured && !Context.bRelativeMouseMode))
	{
		return false;
	}

	if (!bPIEInputArmed)
	{
		return false;
	}

	return true;
}

void UGameViewportClient::SyncPlayerViewToEditorViewport()
{
	UEditorEngine* EditorEngine = Cast<UEditorEngine>(GEngine);
	if (!EditorEngine || !PIEPlayerCamera)
	{
		return;
	}

	FLevelEditorViewportClient* EntryVC = EditorEngine->GetPIEEntryViewportClient();
	if (!EntryVC || !EntryVC->GetCamera())
	{
		return;
	}

	FViewportCamera* TargetCamera = EntryVC->GetCamera();
	if (Viewport && PIEPlayerCamera)
	{
		PIEPlayerCamera->OnResize(static_cast<int32>(Viewport->GetWidth()), static_cast<int32>(Viewport->GetHeight()));
	}
	TargetCamera->SetWorldLocation(PIEPlayerCamera->GetWorldLocation());
	TargetCamera->SetRelativeRotation(PIEPlayerCamera->GetRelativeRotation());
	FViewportCameraState CameraState = TargetCamera->GetCameraState();
	CameraState.FOV = PIEPlayerCamera->GetFOV();
	CameraState.NearZ = PIEPlayerCamera->GetNearPlane();
	CameraState.FarZ = PIEPlayerCamera->GetFarPlane();
	CameraState.bIsOrthogonal = PIEPlayerCamera->IsOrthogonal();
	CameraState.OrthoWidth = PIEPlayerCamera->GetOrthoWidth();
	TargetCamera->SetCameraState(CameraState);
}

bool UGameViewportClient::HandleGlobalInput(float DeltaTime)
{
	(void)DeltaTime;
	if (InputContext.bImGuiCapturedKeyboard)
	{
		return false;
	}

	int32 ActionId = 0;
	const bool bTriggered = InputBindingUtils::TryGetHighestPriorityTriggeredAction(
		InputContext,
		GetPIEGlobalBindings(),
		{},
		ActionId);
	if (!bTriggered)
	{
		return false;
	}

	UEditorEngine* EditorEngine = Cast<UEditorEngine>(GEngine);
	if (!EditorEngine)
	{
		return false;
	}

	switch (static_cast<EPIEGlobalAction>(ActionId))
	{
	case EPIEGlobalAction::EndPIE:
		EditorEngine->EndPIE();
		return true;
	case EPIEGlobalAction::TogglePossessEject:
		return EditorEngine->TogglePIEControlMode();
	case EPIEGlobalAction::ReleaseMouseCapture:
		InputSystem::Get().EndRelativeMouseMode();
		bPIEInputArmed = false;
		return true;
	default:
		return false;
	}
}

bool UGameViewportClient::HandlePlayerInput(float DeltaTime)
{
	if (!PIEPlayerActor || !PIEPlayerCamera)
	{
		return false;
	}

	UEditorEngine* EditorEngine = Cast<UEditorEngine>(GEngine);
	if (!EditorEngine || EditorEngine->GetPIEControlMode() != UEditorEngine::EPIEControlMode::Possessed)
	{
		return false;
	}

	if (!InputContext.bFocused || (!InputContext.bCaptured && !InputContext.bRelativeMouseMode))
	{
		return false;
	}

	const bool bKeyboardBlocked = InputContext.bImGuiCapturedKeyboard;
	const bool bMouseBlocked = InputContext.bImGuiCapturedMouse && !InputContext.bCaptured;

	FVector MoveInput = FVector(0.0f, 0.0f, 0.0f);
	if (!bKeyboardBlocked)
	{
		if (IsPlayerActionTriggered(InputContext, EPIEPlayerAction::MoveForward)) MoveInput.X += 1.0f;
		if (IsPlayerActionTriggered(InputContext, EPIEPlayerAction::MoveBackward)) MoveInput.X -= 1.0f;
		if (IsPlayerActionTriggered(InputContext, EPIEPlayerAction::MoveLeft)) MoveInput.Y -= 1.0f;
		if (IsPlayerActionTriggered(InputContext, EPIEPlayerAction::MoveRight)) MoveInput.Y += 1.0f;
	}

	if (MoveInput.Length() > 0.0f)
	{
		MoveInput = MoveInput.Normalized();
		const float MoveSpeed = 0.3;
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

		const FVector WorldDelta = FlatForward * MoveInput.X + FlatRight * MoveInput.Y;
		PIEPlayerActor->AddActorWorldOffset(WorldDelta * (MoveSpeed * DeltaTime));
	}

	const bool bCanLook = InputContext.bRelativeMouseMode && !bMouseBlocked;
	if (bCanLook)
	{
		const float LookSensitivity = 0.055f;
		const float DeltaYaw = static_cast<float>(InputContext.Frame.MouseDelta.x) * LookSensitivity * 1.0f;
		const float DeltaPitch = static_cast<float>(InputContext.Frame.MouseDelta.y) * LookSensitivity * -1.0f;
		PIECameraYaw += DeltaYaw;
		PIECameraPitch = Clamp(PIECameraPitch + DeltaPitch, -89.0f, 89.0f);

		FRotator ActorYawRotation = PIEPlayerActor->GetActorRotation();
		ActorYawRotation.Pitch = 0.0f;
		ActorYawRotation.Roll = 0.0f;
		ActorYawRotation.Yaw = PIECameraYaw;
		PIEPlayerActor->SetActorRotation(ActorYawRotation);
	}

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
	SyncPlayerViewToEditorViewport();

	return MoveInput.Length() > 0.0f || bCanLook;
}

bool UGameViewportClient::HandleGizmoInput(float DeltaTime)
{
	(void)DeltaTime;
	return false;
}

void UGameViewportClient::OnBeginPIE()
{
	EnsurePIEPlayer();
}

void UGameViewportClient::OnEndPIE()
{
	ReleasePIEPlayer();
	InputSystem::Get().EndRelativeMouseMode();
	bPIEInputArmed = false;
}

void UGameViewportClient::EnsureController()
{
	if (!Controller)
	{
		Controller = new FGameViewportController(this);
	}
}

void UGameViewportClient::EnsurePIEPlayer()
{
	UEditorEngine* EditorEngine = Cast<UEditorEngine>(GEngine);
	if (!EditorEngine || !EditorEngine->IsPIEEnabled())
	{
		ReleasePIEPlayer();
		return;
	}

	UWorld* World = EditorEngine->GetWorld();
	if (!World)
	{
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

	FVector SpawnLocation = EditorEngine->GetCamera() ? EditorEngine->GetCamera()->GetWorldLocation() : FVector(0.0f, 0.0f, 0.0f);
	SpawnLocation.Z *= 0.5f;
	PIEPlayerActor->SetActorLocation(SpawnLocation);
	PIECameraYaw = EditorEngine->GetCamera() ? EditorEngine->GetCamera()->GetRelativeRotation().Yaw : 0.0f;
	PIECameraPitch = -20.0f;
	SyncPlayerViewToEditorViewport();
}

void UGameViewportClient::ReleasePIEPlayer()
{
	UEditorEngine* EditorEngine = Cast<UEditorEngine>(GEngine);
	UWorld* World = EditorEngine ? EditorEngine->GetWorld() : nullptr;
	if (World && PIEPlayerActor)
	{
		World->DestroyActor(PIEPlayerActor);
	}

	PIEPlayerActor = nullptr;
	PIEPlayerActorUUID = 0u;
	PIEPlayerMesh = nullptr;
	PIEPlayerCamera = nullptr;
}
