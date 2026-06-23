#include "Level.h"
#include "GameFramework/AActor.h"
#include "Object/ObjectFactory.h"
#include <algorithm>

#include "Component/PrimitiveComponent.h"

DEFINE_CLASS(ULevel, UObject)

ULevel::ULevel()
{
	RenderProxy = std::make_unique<FWorldRenderProxy>();
}

ULevel::ULevel(const ULevel& Other)
{
	OwningWorld = Other.OwningWorld;
	Actors = Other.Actors;
	RenderProxy = std::make_unique<FWorldRenderProxy>();
}

ULevel::~ULevel()
{
	EndPlay();
}

void ULevel::DuplicateSubObjects()
{
	TArray<AActor*> NewActors;
	NewActors.reserve(Actors.size());
	for (AActor* Actor : Actors)
	{
		AActor* DuplicatedActor = Actor->Duplicate();
		DuplicatedActor->SetWorld(OwningWorld);
		DuplicatedActor->SetLevel(this);
		NewActors.push_back(DuplicatedActor);
	}
	
	Actors = NewActors;

	// Create new FWorldRenderProxy & Rebuild 
	RenderProxy = std::make_unique<FWorldRenderProxy>();
	for (AActor* Actor : Actors)
	{
		for (UActorComponent* Comp : Actor->GetComponents())
		{
			auto PrimComp = Cast<UPrimitiveComponent>(Comp);
			if (PrimComp)
			{
				RenderProxy->AddProxy(PrimComp->GetProxy());
			}
		}
	}
}

void ULevel::AddActor(AActor* Actor)
{
	if (Actor)
	{
		Actors.push_back(Actor);
		Actor->SetLevel(this);
		Actor->RegisterAllComponents();
		if (bHasBegunPlay)
		{
			Actor->BeginPlay();
		}
	}
}

void ULevel::RemoveActor(AActor* Actor)
{
	if (!Actor) return;

	Actor->UnregisterAllComponents();
	Actor->EndPlay();

	auto it = std::find(Actors.begin(), Actors.end(), Actor);
	if (it != Actors.end())
		Actors.erase(it);

	Actor->SetLevel(nullptr);
}

void ULevel::BeginPlay()
{
	bHasBegunPlay = true;
	for (AActor* Actor : Actors)
	{
		if (Actor)
		{
			Actor->BeginPlay();
		}
	}
}

void ULevel::Tick(float DeltaTime)
{
	for (AActor* Actor : Actors)
	{
		if (Actor)
		{
			Actor->Tick(DeltaTime);
		}
	}
}

void ULevel::EndPlay()
{
	bHasBegunPlay = false;

	for (AActor* Actor : Actors)
	{
		if (Actor)
		{
			Actor->UnregisterAllComponents();
			Actor->EndPlay();
			UObjectManager::Get().DestroyObject(Actor);
		}
	}

	Actors.clear();
}
