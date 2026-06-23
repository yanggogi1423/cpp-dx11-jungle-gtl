#pragma once

#include "GameFramework/AActor.h"

class UStaticMeshComponent;
class UTextRenderComponent;
class UBillboardComponent;

class AStaticMeshActor : public AActor
{
public:
	DECLARE_CLASS(AStaticMeshActor, AActor)
	AStaticMeshActor() = default;

	void InitDefaultComponents(const FString& UStaticMeshFileName);

private:
	UStaticMeshComponent* StaticMeshComponent = nullptr;
	UTextRenderComponent* TextRenderComponent = nullptr;
	UBillboardComponent* BillboardComponent = nullptr;
};