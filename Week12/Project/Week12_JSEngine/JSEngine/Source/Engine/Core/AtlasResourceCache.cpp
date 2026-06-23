#include "Core/AtlasResourceCache.h"

#include "Core/Logging/Log.h"
#include "Core/Paths.h"

#include <unordered_set>

bool FAtlasResourceCache::LoadGPUResources(ID3D11Device* Device)
{
	if (!Device)
	{
		return false;
	}

	for (auto& [Key, Resource] : FontResources)
	{
		if (Resource.Texture != nullptr && Resource.Texture->GetSRV() != nullptr)
		{
			continue;
		}

		if (!FontLoader.Load(Resource.Name, Resource.Path, Resource.Columns, Resource.Rows, Device, Resource))
		{
			UE_LOG_WARNING("Failed to load Font atlas: %s", Resource.Path.c_str());
			return false;
		}
	}

	for (auto& [Key, Resource] : SubUVResources)
	{
		if (Resource.Texture != nullptr && Resource.Texture->GetSRV() != nullptr)
		{
			continue;
		}

		if (!SubUVLoader.Load(Resource.Name, Resource.Path, Resource.Columns, Resource.Rows, Device, Resource))
		{
			UE_LOG_WARNING("Failed to load SubUV atlas: %s", Resource.Path.c_str());
			return false;
		}
	}

	return true;
}

FFontResource* FAtlasResourceCache::FindFont(const FName& FontName)
{
	if (FontResources.empty())
	{
		return nullptr;
	}

	auto It = FontResources.find(FontName.ToString());
	return (It != FontResources.end()) ? &It->second : &FontResources.begin()->second;
}

const FFontResource* FAtlasResourceCache::FindFont(const FName& FontName) const
{
	if (FontResources.empty())
	{
		return nullptr;
	}

	auto It = FontResources.find(FontName.ToString());
	return (It != FontResources.end()) ? &It->second : &FontResources.begin()->second;
}

void FAtlasResourceCache::RegisterFont(const FName& FontName, const FString& InPath, uint32 Columns, uint32 Rows)
{
	FFontResource Resource;
	Resource.Name = FontName;
	Resource.Path = FPaths::Normalize(InPath);
	Resource.Columns = Columns;
	Resource.Rows = Rows;
	Resource.Texture = UObjectManager::Get().CreateObject<UTexture>();
	FontResources[FontName.ToString()] = Resource;
}

FTextureAtlasResource* FAtlasResourceCache::FindSubUV(const FName& SubUVName)
{
	if (SubUVResources.empty())
	{
		return nullptr;
	}

	auto It = SubUVResources.find(SubUVName.ToString());
	return (It != SubUVResources.end()) ? &It->second : &SubUVResources.begin()->second;
}

FTextureAtlasResource* FAtlasResourceCache::FindSubUVExact(const FName& SubUVName)
{
	auto It = SubUVResources.find(SubUVName.ToString());
	return (It != SubUVResources.end()) ? &It->second : nullptr;
}

const FTextureAtlasResource* FAtlasResourceCache::FindSubUV(const FName& SubUVName) const
{
	if (SubUVResources.empty())
	{
		return nullptr;
	}

	auto It = SubUVResources.find(SubUVName.ToString());
	return (It != SubUVResources.end()) ? &It->second : &SubUVResources.begin()->second;
}

const FTextureAtlasResource* FAtlasResourceCache::FindSubUVExact(const FName& SubUVName) const
{
	auto It = SubUVResources.find(SubUVName.ToString());
	return (It != SubUVResources.end()) ? &It->second : nullptr;
}

void FAtlasResourceCache::RegisterSubUV(const FName& SubUVName, const FString& InPath, uint32 Columns, uint32 Rows)
{
	FTextureAtlasResource Resource;
	Resource.Name = SubUVName;
	Resource.Path = FPaths::Normalize(InPath);
	Resource.Columns = Columns;
	Resource.Rows = Rows;
	Resource.Texture = UObjectManager::Get().CreateObject<UTexture>();
	SubUVResources[SubUVName.ToString()] = Resource;
}

void FAtlasResourceCache::Clear()
{
	Release();
}

void FAtlasResourceCache::Release()
{
	std::unordered_set<UObject*> DestroyedObjects;
	auto DestroyUniqueObject = [&DestroyedObjects](UObject* Object)
	{
		if (Object && DestroyedObjects.insert(Object).second)
		{
			UObjectManager::Get().DestroyObject(Object);
		}
	};

	for (auto& [Key, Font] : FontResources)
	{
		DestroyUniqueObject(Font.Texture);
	}
	FontResources.clear();

	for (auto& [Key, SubUV] : SubUVResources)
	{
		DestroyUniqueObject(SubUV.Texture);
	}
	SubUVResources.clear();
}
