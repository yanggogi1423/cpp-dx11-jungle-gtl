#include "ParticleModuleRequired.h"

#include "Materials/Material.h"
#include "Materials/MaterialManager.h"

void UParticleModuleRequired::SetToSensibleDefaults(UParticleEmitter* Owner)
{
	MaterialSlot = "None";
	CachedMaterial = nullptr;
	CachedMaterialPath.clear();
	CachedMaterialGeneration = FMaterialManager::Get().GetCacheGeneration();

	bUseLocalSpace = false;

	SubImagesHorizontal = 1;
	SubImagesVertical = 1;

	EmitterDuration = 1.0f;
	EmitterLoops = 0;

	SortMode = ESortMode::None;
	ScreenAlignment = EScreenAlignment::Square;
}

UMaterial* UParticleModuleRequired::ResolveMaterial()
{
	const FString& Path = MaterialSlot.ToString();
	FMaterialManager& MaterialManager = FMaterialManager::Get();
	const uint64 CurrentGeneration = MaterialManager.GetCacheGeneration();
	if (Path.empty() || Path == "None")
	{
		CachedMaterial = nullptr;
		CachedMaterialPath.clear();
		CachedMaterialGeneration = CurrentGeneration;
		return nullptr;
	}

	if (CachedMaterial && CachedMaterialPath == Path && CachedMaterialGeneration == CurrentGeneration)
	{
		return CachedMaterial;
	}

	CachedMaterial = MaterialManager.GetOrCreateMaterial(Path);
	CachedMaterialPath = Path;
	CachedMaterialGeneration = CurrentGeneration;
	return CachedMaterial;
}
