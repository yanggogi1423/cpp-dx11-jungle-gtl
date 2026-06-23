#pragma once

#include "GameFramework/AActor.h"
#include "Object/Ptr/WeakObjectPtr.h"

#include "Source/Engine/GameFramework/Actor/DecalActor.generated.h"
class UTextRenderComponent;
class UDecalComponent;
class UBillboardComponent;

UCLASS()
class ADecalActor : public AActor
{
public:
	GENERATED_BODY()
	ADecalActor();

	void Tick(float DeltaTime) override;

	void InitDefaultComponents();
	void InitRuntimeDecal(class UMaterial* Material);
	void SetLifetimeSeconds(float InLifetimeSeconds);

	UFUNCTION(Pure, Category="Actor|Components")
	UDecalComponent* GetDecalComponent() const { return DecalComponent; }

private:
	TWeakObjectPtr<UDecalComponent> DecalComponent;
	TWeakObjectPtr<UBillboardComponent> BillboardComponent = nullptr;
	TWeakObjectPtr<UTextRenderComponent> TextRenderComponent = nullptr;
	
	const FString DefaultDecalMaterialPath = "Content/Material/Editor/DefaultDecal.uasset";
	float LifetimeRemainingSeconds = -1.0f;
};
