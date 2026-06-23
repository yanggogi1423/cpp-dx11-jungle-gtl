#pragma once

#include "MovementComponent.h"
#include "Math/Rotator.h"

//TODO: 언리얼에서처럼 더 많은 지원하는 기능을 지원할 지 고민

// 런타임 동안 UpdatedComponent를 일정 각속도로 회전시키는 이동 컴포넌트
class URotatingMovementComponent : public UMovementComponent
{
public:
	DECLARE_CLASS(URotatingMovementComponent, UMovementComponent)

	URotatingMovementComponent() = default;
	~URotatingMovementComponent() override = default;

	void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction) override;
	void GetEditableProperties(TArray<FPropertyDescriptor>& OutProps) override;
	void Serialize(FArchive& Ar) override;

private:
	FRotator RotationRate = FRotator(0.0f, 90.0f, 0.0f);
	bool bRotationInLocalSpace = true;
	FVector PivotTranslation = FVector(0.0f, 0.0f, 0.0f);
};