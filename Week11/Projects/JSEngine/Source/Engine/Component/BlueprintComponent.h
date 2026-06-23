#pragma once

#include "Component/ActorComponent.h"

class UBlueprintAsset;

UCLASS()
class UBlueprintComponent : public UActorComponent
{
	GENERATED_BODY(UBlueprintComponent, UActorComponent)

public:
	void Serialize(FArchive& Ar) override;

	UFUNCTION(BlueprintEvent, Category = "Event", DisplayName = "BeginPlay")
	void BeginPlay() override;

	void TickComponent(float DeltaTime) override;

	UFUNCTION(BlueprintEvent, Category = "Event", DisplayName = "Tick")
	void Tick(float DeltaTime);

	UFUNCTION(BlueprintCallable)
	void TestSelfFunction();

	UFUNCTION(CallInEditor, Category = "Blueprint", DisplayName = "Save Target Test Blueprint")
	void SaveTargetTestBlueprint();

private:
	bool LoadBlueprint();

public:
	UPROPERTY(EditAnywhere, Category = "Blueprint", DisplayName = "Blueprint Asset")
	FString BlueprintAssetPath;

private:
	UBlueprintAsset* Blueprint = nullptr;
};
