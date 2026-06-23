#pragma once
#include "Component/ActorComponent.h"
#include "Math/Rotator.h"

class URotateComponent : public UActorComponent
{
public:
	DECLARE_CLASS(URotateComponent, UActorComponent)
	URotateComponent() = default;

	virtual void TickComponent(float DeltaTime) override;

	void SetRotationSpeed(const FRotator& InSpeed) { RotationSpeed = InSpeed; }
	const FRotator& GetRotationSpeed() const { return RotationSpeed; }

protected:
	FRotator RotationSpeed = FRotator(0.0f, 90.0f, 0.0f);
};
