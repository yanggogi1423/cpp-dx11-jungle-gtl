#include "GameFramework/DecalActor.h"
#include "Object/ObjectFactory.h"
#include "Components/DecalComponent.h"
#include "Components/BillboardComponent.h"
#include "Mesh/ObjManager.h"

IMPLEMENT_CLASS(ADecalActor, AActor)

ADecalActor::ADecalActor()
{
    Decal = AddComponent<UDecalComponent>();
	SetRootComponent(Decal);

	SpriteComponent = AddComponent<UBillboardComponent>();
	SpriteComponent->AttachToComponent(Decal);
	SpriteComponent->SetTexture(FName("DecalIcon"));
}

void ADecalActor::SetDecalMaterial(UMaterialInterface* NewDecalMaterial)
{
	if (Decal)
	{
		Decal->SetDecalMaterial(NewDecalMaterial);
	}
}

void ADecalActor::SetDecalMaterial(const FString& MaterialPath)
{
	if (MaterialPath.empty() || MaterialPath == "None")
	{
		SetDecalMaterial(static_cast<UMaterialInterface*>(nullptr));
		return;
	}

	SetDecalMaterial(FObjManager::GetOrLoadMaterial(MaterialPath));
}

UMaterialInterface* ADecalActor::GetDecalMaterial() const
{
	return Decal ? Decal->GetDecalMaterial() : nullptr;
}

void ADecalActor::SetDecalSize(const FVector& InDecalSize)
{
	if (Decal)
	{
		Decal->SetDecalSize(InDecalSize);
	}
}

FVector ADecalActor::GetDecalSize() const
{
	return Decal ? Decal->DecalSize : FVector(0.0f, 0.0f, 0.0f);
}

void ADecalActor::BeginPlay()
{
	AActor::BeginPlay();

    for (UActorComponent* Component : GetComponents())
	{
		if (UBillboardComponent* Billboard = Cast<UBillboardComponent>(Component))
		{
			Billboard->SetVisibility(false);
		}
	}
}

void ADecalActor::Serialize(FArchive& Ar)
{
	AActor::Serialize(Ar);
}
