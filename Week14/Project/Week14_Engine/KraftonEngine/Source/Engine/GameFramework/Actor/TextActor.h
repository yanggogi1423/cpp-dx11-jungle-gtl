#pragma once

#include "GameFramework/AActor.h"
#include "Object/Ptr/WeakObjectPtr.h"

#include "Source/Engine/GameFramework/Actor/TextActor.generated.h"

class UTextRenderComponent;

UCLASS()
class ATextActor : public AActor
{
public:
	GENERATED_BODY()

	ATextActor();

	void InitDefaultComponents();

	UFUNCTION(Pure, Category="Actor|Components")
	UTextRenderComponent* GetTextRenderComponent() const { return TextRenderComponent; }

private:
	TWeakObjectPtr<UTextRenderComponent> TextRenderComponent = nullptr;
};
