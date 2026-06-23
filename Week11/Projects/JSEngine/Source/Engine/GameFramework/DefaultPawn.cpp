#include "GameFramework/DefaultPawn.h"

#include "Component/CameraComponent.h"
#include "Engine/Input/GameplayInputTypes.h"
#include "GameFramework/PlayerController.h"
#include "Math/Utils.h"

#include <cmath>

DEFINE_CLASS(ADefaultPawn, APawn)
REGISTER_FACTORY(ADefaultPawn)

namespace
{
    bool IsActionActive(const FInputActionState* Action)
    {
        return Action &&
            (Action->TriggerEvent == EInputTriggerEvent::Started ||
             Action->TriggerEvent == EInputTriggerEvent::Triggered);
    }

    float GetViewPitchDegrees(const UCameraComponent* Camera)
    {
        if (!Camera)
        {
            return 0.0f;
        }

        const FVector Forward = Camera->GetForwardVector().GetSafeNormal();
        return MathUtil::RadiansToDegrees(std::asin(MathUtil::Clamp(Forward.Z, -1.0f, 1.0f)));
    }

    float GetViewYawDegrees(const UCameraComponent* Camera)
    {
        if (!Camera)
        {
            return 0.0f;
        }

        const FVector Forward = Camera->GetForwardVector().GetSafeNormal();
        return MathUtil::RadiansToDegrees(std::atan2(Forward.Y, Forward.X));
    }
}

void ADefaultPawn::InitDefaultComponents()
{
    CameraComp = AddComponent<UCameraComponent>();
    SetRootComponent(CameraComp);
    ViewPitchDegrees = GetViewPitchDegrees(CameraComp);
    ViewYawDegrees = GetViewYawDegrees(CameraComp);
    AddTag("DefaultPawn");
}

void ADefaultPawn::BeginPlay()
{
    APawn::BeginPlay();

    if (CameraComp)
    {
        ViewPitchDegrees = GetViewPitchDegrees(CameraComp);
        ViewYawDegrees = GetViewYawDegrees(CameraComp);
    }
}

void ADefaultPawn::Tick(float DeltaTime)
{
    APawn::Tick(DeltaTime);

    if (!CameraComp || DeltaTime <= 0.0f)
    {
        return;
    }

    APlayerController* PlayerController = GetController();
    if (!PlayerController || !PlayerController->IsMouseCaptured())
    {
        return;
    }

    const FGameplayInputSnapshot& Snapshot = PlayerController->GetInputSnapshot();
    const bool bFastMove = IsActionActive(Snapshot.FindAction("Dash"));
    const float EffectiveMoveSpeed = MoveSpeed * (bFastMove ? FastMoveMultiplier : 1.0f);
    const float MoveDistance = EffectiveMoveSpeed * DeltaTime;

    if (const FInputActionState* MoveAction = Snapshot.FindAction("Move"))
    {
        const FVector2& MoveAxis = MoveAction->Value.Axis2D;
        CameraComp->MoveForward(MoveAxis.Y * MoveDistance);
        CameraComp->MoveRight(MoveAxis.X * MoveDistance);
    }

    if (const FInputActionState* MoveVerticalAction = Snapshot.FindAction("MoveVertical"))
    {
        CameraComp->MoveUp(MoveVerticalAction->Value.Axis1D * MoveDistance);
    }

    if (const FInputActionState* LookAction = Snapshot.FindAction("Look"))
    {
        const FVector2& LookAxis = LookAction->Value.Axis2D;
        ViewYawDegrees += LookAxis.X * LookSensitivityDegrees;
        ViewPitchDegrees = MathUtil::Clamp(
            ViewPitchDegrees - LookAxis.Y * LookSensitivityDegrees,
            -89.0f,
            89.0f);

        CameraComp->SetViewWorldRotationDegrees(ViewPitchDegrees, ViewYawDegrees);
    }
}
