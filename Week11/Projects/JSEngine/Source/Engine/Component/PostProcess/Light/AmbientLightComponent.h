#pragma once
#include "LightComponent.h"

UCLASS()
class UAmbientLightComponent : public ULightComponent {
	GENERATED_BODY(UAmbientLightComponent, ULightComponent)
public:
	UAmbientLightComponent() = default;
};