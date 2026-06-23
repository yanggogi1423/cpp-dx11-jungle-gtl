#pragma once

#include "Core/CoreMinimal.h"

class FResourceManager;
class UStaticMesh;
struct FStaticMesh;

class FStaticMeshLoadService
{
public:
	explicit FStaticMeshLoadService(FResourceManager& InResourceManager);

	UStaticMesh* Load(const FString& Path);

private:
	UStaticMesh* LoadAsset(const FString& NormalizedPath);
	UStaticMesh* FinalizeLoadedMesh(FStaticMesh* MeshData, const FString& ResolvePath, const FString& CacheKey);

	FResourceManager& ResourceManager;
};
