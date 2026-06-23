#include "GameFramework/GameMode/PlayerController.h"
#include "GameFramework/Pawn/Pawn.h"
#include "GameFramework/World.h"
#include "GameFramework/Camera/PlayerCameraManager.h"
#include "Component/ActorComponent.h"
#include "Component/Camera/CameraComponent.h"
#include "Runtime/Engine.h"
#include "UI/CursorSystem.h"
#include "Viewport/GameViewportClient.h"

namespace
{
	bool IsRuntimeWorld(const APlayerController* Controller)
	{
		const UWorld* World = Controller ? Controller->GetWorld() : nullptr;
		if (!World)
		{
			return false;
		}

		const EWorldType WorldType = World->GetWorldType();
		return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
	}

	UGameViewportClient* GetRuntimeGameViewportClient(const APlayerController* Controller)
	{
		if (!IsRuntimeWorld(Controller) || !GEngine)
		{
			return nullptr;
		}

		UGameViewportClient* ViewportClient = GEngine->GetGameViewportClient();
		return ViewportClient && ViewportClient->IsPossessed() ? ViewportClient : nullptr;
	}

	void SetViewportInputMode(const APlayerController* Controller, EGameInputMode InputMode)
	{
		if (UGameViewportClient* ViewportClient = GetRuntimeGameViewportClient(Controller))
		{
			ViewportClient->SetInputMode(InputMode);
		}
	}
}

void APlayerController::BeginPlay()
{
	Super::BeginPlay();
	// E.2/3: PC 가 PlayerCameraManager 의 owner — UE 패턴.
	// PC 가 GameMode->StartMatch 에서 spawn 되는 시점엔 다른 액터들이 이미 BeginPlay 완료.
	// 그 사이에 BeginPlay 한 카메라 컴포넌트들은 PC 가 없어 등록 못 했으므로 여기서 catch up.
	if (UWorld* World = GetWorld())
	{
		PlayerCameraManager = World->SpawnActor<APlayerCameraManager>();
		if (PlayerCameraManager)
		{
			// 한 액터가 여러 카메라 컴포넌트(예: CarPawn 의 First/Third Person)를 가질 수
			// 있으므로 GetComponents 전체를 순회. GetComponentByClass 는 첫 번째만 반환하므로 부족.
			for (AActor* Actor : World->GetActors())
			{
				if (!Actor) continue;
				for (UActorComponent* Comp : Actor->GetComponents())
				{
					if (UCameraComponent* Cam = Cast<UCameraComponent>(Comp))
					{
						PlayerCameraManager->RegisterCamera(Cam);
					}
				}
			}
			PlayerCameraManager->AutoPossessDefaultCamera();
		}
	}
}

void APlayerController::SetViewTargetWithBlend(
	AActor* NewViewTarget,
	float BlendTime,
	EViewTargetBlendFunction BlendFunc,
	float BlendExp,
	bool bLockOutgoing)
{
	APlayerCameraManager* CM = GetPlayerCameraManager();
	if (!CM) return;

	FViewTargetTransitionParams Params;
	Params.BlendTime = BlendTime;
	Params.BlendFunction = BlendFunc;
	Params.BlendExp = BlendExp;
	Params.bLockOutgoing = bLockOutgoing;

	CM->SetViewTarget(NewViewTarget, Params);
}

void APlayerController::Possess(APawn* Pawn)
{
	if (!Pawn || PossessedPawn.Get() == Pawn) return;

	if (PossessedPawn)
	{
		UnPossess();
	}

	PossessedPawn = Pawn;
	Pawn->PossessedBy(this);
}

void APlayerController::UnPossess()
{
	if (!PossessedPawn) return;

	APawn* OldPawn = PossessedPawn;
	PossessedPawn = nullptr;
	OldPawn->UnPossessed();
}

void APlayerController::ProcessPlayerInput(const FInputSystemSnapshot& Snapshot, float DeltaTime)
{
	APawn* Pawn = GetPossessedPawn();
	if (!Pawn)
	{
		return;
	}

	Pawn->ProcessPlayerInput(Snapshot, DeltaTime);
}

void APlayerController::SetInputModeGameOnly()
{
	SetViewportInputMode(this, EGameInputMode::GameOnly);
}

void APlayerController::SetInputModeUIOnly()
{
	SetViewportInputMode(this, EGameInputMode::UIOnly);
}

void APlayerController::SetInputModeGameAndUI()
{
	SetViewportInputMode(this, EGameInputMode::GameAndUI);
}

void APlayerController::SetShowMouseCursor(bool bShow)
{
	if (UGameViewportClient* ViewportClient = GetRuntimeGameViewportClient(this))
	{
		ViewportClient->SetCursorVisible(bShow);
	}
}

bool APlayerController::IsShowMouseCursor() const
{
	if (const UGameViewportClient* ViewportClient = GetRuntimeGameViewportClient(this))
	{
		return ViewportClient->IsCursorVisible();
	}

	return false;
}

void APlayerController::SetCursorLocked(bool bLocked)
{
	if (UGameViewportClient* ViewportClient = GetRuntimeGameViewportClient(this))
	{
		ViewportClient->SetCursorLocked(bLocked);
	}
}

bool APlayerController::IsCursorLocked() const
{
	if (const UGameViewportClient* ViewportClient = GetRuntimeGameViewportClient(this))
	{
		return ViewportClient->IsCursorLocked();
	}

	return false;
}

void APlayerController::SetSoftwareCursorVisible(bool bVisible)
{
	if (GetRuntimeGameViewportClient(this))
	{
		FCursorSystem::Get().SetSoftwareCursorVisible(bVisible);
	}
}

bool APlayerController::IsSoftwareCursorVisible() const
{
	return IsRuntimeWorld(this) && FCursorSystem::Get().IsSoftwareCursorVisible();
}

bool APlayerController::SetCursorImage(const FString& TexturePath, float Width, float Height, float InHotSpotX, float InHotSpotY)
{
	if (!GetRuntimeGameViewportClient(this))
	{
		return false;
	}

	return FCursorSystem::Get().SetCursorImage(TexturePath, Width, Height, InHotSpotX, InHotSpotY);
}

void APlayerController::ClearCursorImage()
{
	if (GetRuntimeGameViewportClient(this))
	{
		FCursorSystem::Get().ClearCursorImage();
	}
}

void APlayerController::SetCursorHotSpot(float X, float Y)
{
	if (GetRuntimeGameViewportClient(this))
	{
		FCursorSystem::Get().SetCursorHotSpot(X, Y);
	}
}

void APlayerController::SetCursorSize(float Width, float Height)
{
	if (GetRuntimeGameViewportClient(this))
	{
		FCursorSystem::Get().SetCursorSize(Width, Height);
	}
}

void APlayerController::SetCursorHitBox(float OffsetX, float OffsetY, float Width, float Height)
{
	if (GetRuntimeGameViewportClient(this))
	{
		FCursorSystem::Get().SetCursorHitBox(OffsetX, OffsetY, Width, Height);
	}
}

bool APlayerController::IsCursorOverRect(float X, float Y, float Width, float Height) const
{
	return IsRuntimeWorld(this) && FCursorSystem::Get().IsCursorOverRect(X, Y, Width, Height);
}
