#pragma once

#include "Core/CoreMinimal.h"
#include "SimpleJSON/json.hpp"

class AActor;
class UActorComponent;
class UWorld;

struct FActorLoadOptions
{
	bool bPreserveUUIDs = true;
	bool bPreserveName = true;
	bool bMakeNameUnique = false;
	bool bCallBeginPlayIfWorldBegunPlay = true;
};

namespace FActorSerialization
{
	json::JSON BuildActorJson(AActor* Actor);
	json::JSON BuildComponentJson(UActorComponent* Component);
	AActor* SpawnActorFromJson(UWorld* World, json::JSON& ActorData, const FActorLoadOptions& Options = FActorLoadOptions());
	bool ApplyActorJson(AActor* Actor, json::JSON& ActorData, bool bPreserveUUID = true);
	UActorComponent* AddComponentFromJson(AActor* Owner, json::JSON& ComponentData, bool bPreserveUUID = true);
	bool ApplyComponentJson(UActorComponent* Component, json::JSON& ComponentData, bool bPreserveUUID = true);
}
