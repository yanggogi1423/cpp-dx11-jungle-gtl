#pragma once

#include "GameFramework/AActor.h"

class UBillboardComponent;
class UProjectionDecalComponent;
class UMaterialInterface;

class AProjectionDecalActor : public AActor
{
public:
	DECLARE_CLASS(AProjectionDecalActor, AActor)

	AProjectionDecalActor();

	UProjectionDecalComponent* GetProjectionDecal() const { return ProjectionDecal; }
	UBillboardComponent* GetSpriteComponent() const { return SpriteComponent; }

	void SetProjectionDecalMaterial(UMaterialInterface* NewMaterial);
	void SetProjectionDecalMaterial(const FString& MaterialPath);
	UMaterialInterface* GetProjectionDecalMaterial() const;

	void SetProjectionDecalSize(const FVector& InSize);
	FVector GetProjectionDecalSize() const;

	void BeginPlay() override;
	void Serialize(FArchive& Ar) override;

protected:
	UProjectionDecalComponent* ProjectionDecal = nullptr;
	UBillboardComponent* SpriteComponent = nullptr;
};


