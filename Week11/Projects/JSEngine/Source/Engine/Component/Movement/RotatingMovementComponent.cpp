#include "RotatingMovementComponent.h"
#include "Component/PrimitiveComponent.h"
#include "Object/ObjectFactory.h"

void URotatingMovementComponent::Serialize(FArchive& Ar)
{
    UMovementComponent::Serialize(Ar);

    Ar << "Rotation Rate" << RotationRate;
    Ar << "Pivot Translation" << PivotTranslation;
    Ar << "Rotation In Local Space" << bRotationInLocalSpace;
}

void URotatingMovementComponent::GetEditableProperties(TArray<FPropertyDescriptor>& OutProps)
{
    UMovementComponent::GetEditableProperties(OutProps);
}

void URotatingMovementComponent::TickComponent(float DeltaTime)
{
    if (UpdatedComponent == nullptr)
    {
        return;
    }

    // Primitive Component이고, 화면에 보일 때만 렌더링 업데이트 옵션이 켜져 있는 경우 예외처리
    UPrimitiveComponent* PrimitiveComponent = Cast<UPrimitiveComponent>(UpdatedComponent);
    if (bUpdateOnlyIfRendered && PrimitiveComponent && !PrimitiveComponent->IsVisible())
    {
        return;
    }

    FQuat DeltaQuat = FQuat::MakeFromEuler(RotationRate * DeltaTime);
    DeltaQuat.Normalize();

    // Pivot Offset이 존재하지 않는다면 로컬/월드 공간을 기준으로 한 회전을 수행한다.
    if (PivotTranslation.IsNearlyZero())
    {
        FQuat CurrentQuat = UpdatedComponent->GetRelativeQuat();
        FQuat ResultQuat;

        if (bRotationInLocalSpace)
        {
            ResultQuat = (DeltaQuat * CurrentQuat).GetNormalized();
        }
        else
        {
            ResultQuat = (CurrentQuat * DeltaQuat).GetNormalized();
        }

        UpdatedComponent->SetRelativeRotationQuat(ResultQuat);
    }
    else
    {
        FTransform CurrentTransform = UpdatedComponent->GetRelativeTransform();
        FVector CurrentLocation = CurrentTransform.GetTranslation();

        FVector PivotOffset = CurrentTransform.GetRotation().RotateVector(PivotTranslation);
        FVector NewLocation = (CurrentLocation + PivotOffset) - DeltaQuat.RotateVector(PivotOffset);
        UpdatedComponent->SetRelativeLocation(NewLocation);

        // 피벗 회전도 쿼터니언으로 합성 — bRotationInLocalSpace 존중
        FQuat CurrentQuat = UpdatedComponent->GetRelativeQuat();
        FQuat ResultQuat = bRotationInLocalSpace
            ? (DeltaQuat * CurrentQuat).GetNormalized()
            : (CurrentQuat * DeltaQuat).GetNormalized();
        UpdatedComponent->SetRelativeRotationQuat(ResultQuat);
    }
}
