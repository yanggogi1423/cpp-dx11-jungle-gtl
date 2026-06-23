#pragma once

#include "GameFramework/AActor.h"

class APlayerController;
struct FInputActionState;

UCLASS(Placeable, DisplayName = "Pawn", Category = "Gameplay")
class APawn : public AActor
{
public:
	GENERATED_BODY(APawn, AActor)

	void InitDefaultComponents() override;

	APlayerController* GetController() const { return Controller; }
	void PossessedBy(APlayerController* NewController);
	void UnPossessed();

	virtual void OnInputAction(const FInputActionState& Action);

private:
	APlayerController* Controller = nullptr;
};
