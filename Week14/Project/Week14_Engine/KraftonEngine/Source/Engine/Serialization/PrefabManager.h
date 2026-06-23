#pragma once

#include <string>

#include "Core/Types/CoreTypes.h"

class AActor;
class UWorld;

class FPrefabManager
{
public:
	static constexpr const wchar_t* PrefabExtension = L".prefab";

	static std::wstring GetPrefabDirectory();
	static bool SaveActorPrefab(AActor* Actor, const FString& Path);
	static AActor* SpawnActorFromPrefab(UWorld* World, const FString& Path);
};
