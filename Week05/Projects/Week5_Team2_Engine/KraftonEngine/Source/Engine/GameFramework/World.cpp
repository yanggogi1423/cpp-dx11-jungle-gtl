#include "GameFramework/World.h"
#include "GameFramework/Level.h"
#include "Object/ObjectFactory.h"
#include <algorithm>
#include <memory>

IMPLEMENT_CLASS(UWorld, UObject)

UWorld::UWorld()
{
	InitWorld();
}

UWorld::~UWorld()
{
	if (bHasBegunPlay)
	{
		EndPlay();
	}

	if (ActiveLevel)
	{
		UObjectManager::Get().DestroyObject(ActiveLevel);
		ActiveLevel = nullptr;
	}
	if (PersistentLevel)
	{
		UObjectManager::Get().DestroyObject(PersistentLevel);
		PersistentLevel = nullptr;
	}
}

void UWorld::DuplicateSubObjects()
{
	ActiveLevel = ActiveLevel->Duplicate();
	ActiveLevel->SetWorld(this);
	PersistentLevel = PersistentLevel->Duplicate();
	PersistentLevel->SetWorld(this);
}

void UWorld::DestroyActor(AActor* Actor)
{
	if (!Actor) return;

	if (ULevel* Level = Actor->GetLevel())
	{
		Level->RemoveActor(Actor);
	}

	// Mark for garbage collection
	UObjectManager::Get().DestroyObject(Actor);
}

const TArray<AActor*>& UWorld::GetActors() const
{
	// NOTE: For compatibility, we return PersistentLevel's actors.
	// In the future, we might want to return a combined list or change the API.
	return ActiveLevel->GetActors();
}

void UWorld::InitWorld()
{
	if (!ActiveLevel)
	{
		ActiveLevel = UObjectManager::Get().CreateObject<ULevel>();
		ActiveLevel->SetWorld(this);
	}
	if (!PersistentLevel)
	{
		PersistentLevel = UObjectManager::Get().CreateObject<ULevel>();
		PersistentLevel->SetWorld(this);
	}
}

void UWorld::BeginPlay()
{
	bHasBegunPlay = true;

	if (ActiveLevel) ActiveLevel->BeginPlay();
	if (PersistentLevel) PersistentLevel->BeginPlay();
}

void UWorld::EndPlay()
{
	bHasBegunPlay = false;

	if (ActiveLevel) ActiveLevel->EndPlay();
	if (PersistentLevel) PersistentLevel->EndPlay();
}
