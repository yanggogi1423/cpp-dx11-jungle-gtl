#include "RotatingMovementComponent.h"

#include "Object/ObjectFactory.h"
#include "SceneComponent.h"
#include "Serialization/Archive.h"
#include "Math/Vector.h"

IMPLEMENT_CLASS(URotatingMovementComponent, UMovementComponent)

void URotatingMovementComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction)
{
	UMovementComponent::TickComponent(DeltaTime, TickType, ThisTickFunction);

	USceneComponent* UpdatedSceneComponent = GetUpdatedComponent();
	if (!UpdatedSceneComponent)
	{
		return;
	}

	const FRotator DeltaRotation = RotationRate * DeltaTime;
	if (DeltaRotation.IsNearlyZero())
	{
		return;
	}

	const FQuat DeltaQuat = DeltaRotation.ToQuaternion();
	const FVector OldWorldLocation = UpdatedSceneComponent->GetWorldLocation();
	const FQuat OldWorldQuat = UpdatedSceneComponent->GetWorldQuat();
	const bool bHasPivotTranslation = PivotTranslation.Length() > 0.0f;
	const FVector OldPivotOffsetWorld = bHasPivotTranslation
		? OldWorldQuat.RotateVector(PivotTranslation)
		: FVector(0.0f, 0.0f, 0.0f);

	FQuat NewWorldQuat = OldWorldQuat;
	FVector NewWorldLocation = OldWorldLocation;

	if (bRotationInLocalSpace)
	{
		// 로컬 축 기준 회전
		UpdatedSceneComponent->AddLocalRotation(DeltaQuat);
		NewWorldQuat = UpdatedSceneComponent->GetWorldQuat();
		if (bHasPivotTranslation)
		{
			const FVector NewPivotOffsetWorld = NewWorldQuat.RotateVector(PivotTranslation);
			const FVector PivotWorldLocation = OldWorldLocation - OldPivotOffsetWorld;
			NewWorldLocation = PivotWorldLocation + NewPivotOffsetWorld;
			UpdatedSceneComponent->SetWorldLocation(NewWorldLocation);
		}
	}
	else
	{
		// 월드 축 기준 회전
		NewWorldQuat = (DeltaQuat * OldWorldQuat).GetNormalized();
		if (bHasPivotTranslation)
		{
			const FVector NewPivotOffsetWorld = NewWorldQuat.RotateVector(PivotTranslation);
			const FVector PivotWorldLocation = OldWorldLocation - OldPivotOffsetWorld;
			NewWorldLocation = PivotWorldLocation + NewPivotOffsetWorld;
		}

		UpdatedSceneComponent->SetWorldRotation(NewWorldQuat);
		UpdatedSceneComponent->SetWorldLocation(NewWorldLocation);
	}
}

void URotatingMovementComponent::Serialize(FArchive& Ar)
{
	UMovementComponent::Serialize(Ar);
	Ar << RotationRate.Pitch;
	Ar << RotationRate.Yaw;
	Ar << RotationRate.Roll;
	Ar << bRotationInLocalSpace;
	Ar << PivotTranslation;
}

void URotatingMovementComponent::GetEditableProperties(TArray<FPropertyDescriptor>& OutProps)
{
	UMovementComponent::GetEditableProperties(OutProps);
	OutProps.push_back({ "Rotation Rate", EPropertyType::Rotator, &RotationRate, 0.0f, 0.0f, 0.1f });
	OutProps.push_back({ "Rotation In Local Space", EPropertyType::Bool, &bRotationInLocalSpace, 0.0f, 0.0f, 0.0f });
	OutProps.push_back({ "Pivot Translation", EPropertyType::Vec3, &PivotTranslation, 0.0f, 0.0f, 0.1f });
}
