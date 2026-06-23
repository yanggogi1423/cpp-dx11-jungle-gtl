#include "Core/StaticMeshLoadService.h"

#include "Asset/AssetMetaData.h"
#include "Asset/AssetFile.h"
#include "Core/Logging/Log.h"
#include "Core/Paths.h"
#include "Core/ResourceManager.h"

namespace fs = std::filesystem;

FStaticMeshLoadService::FStaticMeshLoadService(FResourceManager& InResourceManager)
	: ResourceManager(InResourceManager)
{
}

UStaticMesh* FStaticMeshLoadService::Load(const FString& Path)
{
	const FString NormalizedPath = FPaths::Normalize(Path);

	if (!FAssetFile::IsAssetPath(NormalizedPath))
	{
		UE_LOG_WARNING("[StaticMeshLoad] Runtime static mesh load only supports .uasset: %s", NormalizedPath.c_str());
		return nullptr;
	}

	if (UStaticMesh* FoundMesh = ResourceManager.FindStaticMesh(NormalizedPath))
	{
		return FoundMesh;
	}

	return LoadAsset(NormalizedPath);
}

UStaticMesh* FStaticMeshLoadService::LoadAsset(const FString& NormalizedPath)
{
	FAssetMetaData MetaData;
	FStaticMesh* MeshData = new FStaticMesh();

	const bool bLoaded = FAssetFile::Load(NormalizedPath, MetaData, [&](FArchive& Ar)
	{
		MeshData->Serialize(Ar, MetaData.PayloadVersion);
		return true;
	});

	if (!bLoaded)
	{
		delete MeshData;
		UE_LOG_ERROR("[StaticMeshLoad] Failed to load static mesh asset: %s", NormalizedPath.c_str());
		return nullptr;
	}

	if (MetaData.ClassName != UStaticMesh::StaticClass()->ClassName)
	{
		delete MeshData;
		UE_LOG_ERROR("[StaticMeshLoad] UAsset class mismatch | Path=%s | Class=%s",
			NormalizedPath.c_str(),
			MetaData.ClassName.c_str());
		return nullptr;
	}

	MeshData->PathFileName = NormalizedPath;
	const FString ResolvePath = MetaData.SourceFile.empty() ? NormalizedPath : MetaData.SourceFile;

	return FinalizeLoadedMesh(MeshData, ResolvePath, NormalizedPath);
}

UStaticMesh* FStaticMeshLoadService::FinalizeLoadedMesh(
	FStaticMesh* MeshData,
	const FString& ResolvePath,
	const FString& CacheKey)
{
	ResourceManager.ResolveStaticMeshMaterialSlots(ResolvePath, MeshData);

	UStaticMesh* LoadedMesh = ResourceManager.CreateStaticMeshFromLoadedData(MeshData, ResolvePath, true, true);

	ResourceManager.StaticMeshCache.RegisterLoaded(CacheKey, LoadedMesh);
	return LoadedMesh;
}
