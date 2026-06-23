#pragma once

#include "Core/CoreMinimal.h"

class FResourceManager;
class USkeletalMesh;
struct FSkeletalMesh;

class FSkeletalMeshLoadService
{
public:
	explicit FSkeletalMeshLoadService(FResourceManager& InResourceManager);

	USkeletalMesh* Load(const FString& Path);
	USkeletalMesh* ImportSource(const FString& Path);

private:
	USkeletalMesh* LoadSource(const FString& NormalizedPath);
	USkeletalMesh* LoadUAsset(const FString& NormalizedPath);
	USkeletalMesh* FinalizeLoadedMesh(FSkeletalMesh* MeshData, const FString& ResolvePath, const FString& CacheKey);

	FResourceManager& ResourceManager;
};
