#pragma once

#include "Object/Object.h"
#include "GameFramework/AActor.h"
#include "Render/Pipeline/WorldRenderProxy.h"
#include <memory>

// === Forward Declaration
class FViewportCamera;
class ULevel;

class UWorld : public UObject {
public:
	DECLARE_CLASS(UWorld, UObject)
	UWorld();
	~UWorld() override;
	void DuplicateSubObjects() override;

	// Actor lifecycle
	template<typename T>
	T* SpawnActor();
	void DestroyActor(AActor* Actor);

	const TArray<AActor*>& GetActors() const;

	void InitWorld();      // Set up the world before gameplay begins
	void BeginPlay();      // Triggers BeginPlay on all actors
	void EndPlay();        // Cleanup before world is destroyed

	bool HasBegunPlay() const { return bHasBegunPlay; }

	ULevel* GetActiveLevel() const { return ActiveLevel; }
	ULevel* GetPersistentLevel() const { return PersistentLevel; }

private:
	ULevel* ActiveLevel = nullptr;
	ULevel* PersistentLevel = nullptr;
	bool bHasBegunPlay = false;
};

#include "GameFramework/Level.h"

template<typename T>
inline T* UWorld::SpawnActor()
{
	// create and register an actor
	T* Actor = UObjectManager::Get().CreateObject<T>();
	Actor->SetWorld(this);
	
	// Default to ActiveLevel
	ActiveLevel->AddActor(Actor);

	return Actor;
}
