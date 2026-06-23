#include "MovementComponent.h"

#include "Component/SceneComponent.h"
#include "Object/ObjectFactory.h"
#include "GameFramework/AActor.h"
#include "GameFramework/World.h"
#include "Spatial/WorldSpatialIndex.h"
#include "Engine/Geometry/Ray.h"
#include "Math/Utils.h"

void UMovementComponent::SetUpdatedComponent(USceneComponent* InComponent)
{
    UpdatedComponent = InComponent;
}

void UMovementComponent::Serialize(FArchive& Ar)
{
    UActorComponent::Serialize(Ar);

    Ar << "Velocity" << Velocity;
    Ar << "Plane Constraint Normal" << PlaneConstraintNormal;
    Ar << "Update Only If Rendered" << bUpdateOnlyIfRendered;
    Ar << "Constrain To Plane" << bConstrainToPlane;
}

void UMovementComponent::GetEditableProperties(TArray<FPropertyDescriptor>& OutProps)
{
    UActorComponent::GetEditableProperties(OutProps);

    // UpdatedComponent 는 SceneComponentRef 타입으로 노출됩니다.
    // CopyPropertiesFrom 은 포인터 복원을 건너뛰며, Actor::Duplicate() 에서 재매핑합니다.
    OutProps.push_back({ "Updated Component",        EPropertyType::SceneComponentRef, &UpdatedComponent });
}

// UpdatedComponent를 Delta만큼 이동시킵니다.
void UMovementComponent::MoveUpdatedComponent(const FVector& Delta)
{
    if (UpdatedComponent == nullptr)
    {
        return;
    }

    FVector ConstrainedDelta = bConstrainToPlane ? ConstrainDirectionToPlane(Delta) : Delta;

    if (ConstrainedDelta.IsNearlyZero())
    {
        return;
    }

    UpdatedComponent->AddWorldOffset(ConstrainedDelta);
}

void UMovementComponent::AddInputVector(const FVector& WorldDirection, float Scale)
{
    PendingInputVector += WorldDirection * Scale;
}

FVector UMovementComponent::ConsumeInputVector()
{
    const FVector Consumed = PendingInputVector;
    PendingInputVector     = FVector::ZeroVector;
    return Consumed;
}

bool UMovementComponent::IsExceedingMaxSpeed(float MaxSpeed) const
{
    if (MaxSpeed < 0.0f)
    {
        return false;
    }

    return Velocity.SizeSquared() > MaxSpeed * MaxSpeed;
}

// 평면에 투영된 법선벡터를 반환한다.
FVector UMovementComponent::ConstrainDirectionToPlane(const FVector& Direction) const
{
    if (!bConstrainToPlane)
    {
        return Direction;
    }

	// 방향벡터를 평면의 정규화된 법선벡터에 내적한다.
    const FVector Normal = PlaneConstraintNormal.GetSafeNormal();
    const float Dot = FVector::DotProduct(Direction, Normal);
    return Direction - Normal * Dot;
}

// 원점 기준 평면에 투영된 점의 위치벡터를 반환한다.
FVector UMovementComponent::ConstrainLocationToPlane(const FVector& Location) const
{
    if (!bConstrainToPlane)
    {
        return Location;
    }
 
    const FVector Normal = PlaneConstraintNormal.GetSafeNormal();
    const float Dot = FVector::DotProduct(Location, Normal);
    return Location - Normal * Dot;
}

void UMovementComponent::OnRegister()
{
    UActorComponent::OnRegister();
    UpdatedComponent = GetOwner()->GetRootComponent();
}

void UMovementComponent::OnUnregister()
{
    UActorComponent::OnUnregister();
    UpdatedComponent = nullptr;
}
