#include "GameFramework/GameMode/GameModeBase.h"
#include "GameFramework/GameMode/GameStateBase.h"
#include "GameFramework/GameMode/PlayerController.h"
#include "GameFramework/Pawn/Pawn.h"
#include "GameFramework/World.h"
#include "Object/Reflection/UClass.h"
#include "Core/Logging/Log.h"
#include "Core/ProjectSettings.h"
#include "Serialization/PrefabManager.h"

AGameModeBase::AGameModeBase()
{
	// 기본값 — 서브클래스 생성자가 더 구체 클래스로 덮어쓸 수 있다.
	GameStateClass = AGameStateBase::StaticClass();
	PlayerControllerClass = APlayerController::StaticClass();
}

void AGameModeBase::BeginPlay()
{
	AActor::BeginPlay();

	// GameState spawn — World 경유로 등록되어 BeginPlay/Tick에 편입된다.
	if (UWorld* World = GetWorld())
	{
		UClass* StateClass = GameStateClass ? GameStateClass : AGameStateBase::StaticClass();
		AActor* Spawned = World->SpawnActorByClass(StateClass);
		GameState = Cast<AGameStateBase>(Spawned);
	}
}

void AGameModeBase::EndPlay()
{
	GameState = nullptr;
	PlayerController = nullptr;
	AActor::EndPlay();
}

void AGameModeBase::StartMatch()
{
	// PlayerController spawn — Editor 월드에선 GameMode 자체가 안 만들어지므로 안전.
	if (UWorld* World = GetWorld())
	{
		UClass* PCClass = PlayerControllerClass ? PlayerControllerClass : APlayerController::StaticClass();
		AActor* Spawned = World->SpawnActorByClass(PCClass);
		PlayerController = Cast<APlayerController>(Spawned);
	}

	if (!AutoPossessFirstPawn())
	{
		SpawnAndPossessDefaultPawnPrefab();
	}
}

void AGameModeBase::EndMatch()
{
	if (PlayerController)
	{
		PlayerController->UnPossess();
	}
}

UClass* AGameModeBase::ResolveClassFromProjectSettings(UClass* InDefault)
{
	UClass* Result = InDefault;
	const FString& ConfiguredName = FProjectSettings::Get().Game.GameModeClassName;
	if (ConfiguredName.empty())
	{
		return Result;
	}

	UClass* Found = UClass::FindByName(ConfiguredName.c_str());
	if (Found && Found->IsA(AGameModeBase::StaticClass()))
	{
		return Found;
	}

	UE_LOG("[GameMode] GameModeClassName '%s' not found or not a AGameModeBase subclass — using default %s",
		ConfiguredName.c_str(), Result ? Result->GetName() : "(null)");
	return Result;
}

bool AGameModeBase::AutoPossessFirstPawn()
{
	if (!PlayerController) return false;

	UWorld* World = GetWorld();
	if (!World) return false;

	for (AActor* Actor : World->GetActors())
	{
		if (!Actor) continue;
		APawn* Pawn = Cast<APawn>(Actor);
		if (!Pawn) continue;
		if (!Pawn->GetAutoPossessPlayer()) continue;

		PlayerController->Possess(Pawn);
		UE_LOG("[GameMode] Auto-possessed Pawn: %s", Pawn->GetName().c_str());
		return true;
	}

	// 매칭 Pawn 없음 — PC만 살아있고 PossessedPawn은 nullptr.
	return false;
}

bool AGameModeBase::SpawnAndPossessDefaultPawnPrefab()
{
	if (!PlayerController) return false;

	UWorld* World = GetWorld();
	if (!World) return false;

	FString PrefabPath = World->GetWorldSettings().DefaultPawnPrefabPath;
	if (PrefabPath.empty())
	{
		PrefabPath = FProjectSettings::Get().Game.DefaultPawnPrefabPath;
	}
	if (PrefabPath.empty())
	{
		return false;
	}

	AActor* SpawnedActor = FPrefabManager::SpawnActorFromPrefab(World, PrefabPath);
	if (!IsValid(SpawnedActor))
	{
		UE_LOG("[GameMode] Failed to spawn DefaultPawnPrefab: %s", PrefabPath.c_str());
		return false;
	}

	APawn* Pawn = Cast<APawn>(SpawnedActor);
	if (!IsValid(Pawn))
	{
		UE_LOG("[GameMode] DefaultPawnPrefab is not a Pawn: %s", PrefabPath.c_str());
		World->DestroyActor(SpawnedActor);
		return false;
	}

	PlayerController->Possess(Pawn);
	UE_LOG("[GameMode] Spawned and possessed DefaultPawnPrefab: %s", PrefabPath.c_str());
	return true;
}
