#include "DecalActor.h"
#include "Component/Primitive/DecalComponent.h"
#include "Component/Primitive/BillboardComponent.h"
#include "Component/Primitive/TextRenderComponent.h"
#include "GameFramework/World.h"
#include "Materials/MaterialManager.h"

ADecalActor::ADecalActor()
	: DecalComponent(nullptr)
{
	bNeedsTick = true;
	bTickInEditor = true;
}

void ADecalActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (LifetimeRemainingSeconds < 0.0f)
	{
		return;
	}

	LifetimeRemainingSeconds -= DeltaTime;
	if (LifetimeRemainingSeconds > 0.0f)
	{
		return;
	}

	LifetimeRemainingSeconds = -1.0f;
	if (UWorld* World = GetWorld())
	{
		World->DestroyActor(this);
	}
}

void ADecalActor::InitDefaultComponents()
{
	DecalComponent = AddComponent<UDecalComponent>();
	auto Material = FMaterialManager::Get().GetOrCreateMaterial(DefaultDecalMaterialPath);
	DecalComponent->SetMaterial(Material);
	SetRootComponent(DecalComponent);

	BillboardComponent = DecalComponent->EnsureEditorBillboard();
	
	// UUID 텍스트 표시
	TextRenderComponent = AddComponent<UTextRenderComponent>();
	TextRenderComponent->SetRelativeLocation(FVector(0.0f, 0.0f, 1.3f));
	TextRenderComponent->SetText("UUID : " + TextRenderComponent->GetOwnerUUIDToString());
	TextRenderComponent->AttachToComponent(DecalComponent);
	TextRenderComponent->SetFont(FName("Default"));
}

void ADecalActor::InitRuntimeDecal(UMaterial* Material)
{
	if (!DecalComponent)
	{
		DecalComponent = AddComponent<UDecalComponent>();
	}

	if (UDecalComponent* Decal = DecalComponent.Get())
	{
		if (Material)
		{
			Decal->SetMaterial(Material);
		}
		Decal->SetHiddenInComponentTree(true);
		SetRootComponent(Decal);
	}
}

void ADecalActor::SetLifetimeSeconds(float InLifetimeSeconds)
{
	LifetimeRemainingSeconds = InLifetimeSeconds;
}
