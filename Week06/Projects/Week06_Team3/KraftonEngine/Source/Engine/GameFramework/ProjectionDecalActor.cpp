#include "GameFramework/ProjectionDecalActor.h"

#include "Object/ObjectFactory.h"
#include "Components/BillboardComponent.h"
#include "Components/ProjectionDecalComponent.h"
#include "Mesh/ObjManager.h"

IMPLEMENT_CLASS(AProjectionDecalActor, AActor)

AProjectionDecalActor::AProjectionDecalActor()
{
	ProjectionDecal = AddComponent<UProjectionDecalComponent>();
	SetRootComponent(ProjectionDecal);

	SpriteComponent = AddComponent<UBillboardComponent>();
	SpriteComponent->AttachToComponent(ProjectionDecal);
	SpriteComponent->SetTexture(FName("DecalIcon"));
}

void AProjectionDecalActor::SetProjectionDecalMaterial(UMaterialInterface* NewMaterial)
{
	if (ProjectionDecal)
	{
		ProjectionDecal->SetProjectionDecalMaterial(NewMaterial);
	}
}

void AProjectionDecalActor::SetProjectionDecalMaterial(const FString& MaterialPath)
{
	if (MaterialPath.empty() || MaterialPath == "None")
	{
		SetProjectionDecalMaterial(static_cast<UMaterialInterface*>(nullptr));
		return;
	}
	SetProjectionDecalMaterial(FObjManager::GetOrLoadMaterial(MaterialPath));
}

UMaterialInterface* AProjectionDecalActor::GetProjectionDecalMaterial() const
{
	return ProjectionDecal ? ProjectionDecal->GetProjectionDecalMaterial() : nullptr;
}

void AProjectionDecalActor::SetProjectionDecalSize(const FVector& InSize)
{
	if (ProjectionDecal)
	{
		ProjectionDecal->SetProjectionDecalSize(InSize);
	}
}

FVector AProjectionDecalActor::GetProjectionDecalSize() const
{
	return ProjectionDecal ? ProjectionDecal->GetProjectionDecalSize() : FVector(0.0f, 0.0f, 0.0f);
}

void AProjectionDecalActor::BeginPlay()
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

void AProjectionDecalActor::Serialize(FArchive& Ar)
{
	AActor::Serialize(Ar);
}


