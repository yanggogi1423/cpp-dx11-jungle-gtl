#include "Component/RotateComponent.h"
#include "GameFramework/AActor.h"

IMPLEMENT_CLASS(URotateComponent, UActorComponent)

void URotateComponent::TickComponent(float DeltaTime)
{
	AActor* Owner = GetOwner();
	if (!Owner) return;

	FRotator CurrentRotation = Owner->GetActorRotation();
	CurrentRotation.Pitch += RotationSpeed.Pitch * DeltaTime;
	CurrentRotation.Yaw += RotationSpeed.Yaw * DeltaTime;
	CurrentRotation.Roll += RotationSpeed.Roll * DeltaTime;
	Owner->SetActorRotation(CurrentRotation);
}
