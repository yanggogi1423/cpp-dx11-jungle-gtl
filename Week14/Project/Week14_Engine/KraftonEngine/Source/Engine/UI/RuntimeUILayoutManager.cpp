#include "UI/RuntimeUILayoutManager.h"

#include "Asset/AssetPackage.h"
#include "Object/Object.h"
#include "Platform/Paths.h"
#include "Serialization/WindowsArchive.h"
#include "UI/RuntimeUILayoutAsset.h"

#include <filesystem>

namespace
{
	FString MakeGeneratedSiblingPath(const FString& AssetPath, const wchar_t* Extension)
	{
		std::filesystem::path Path(FPaths::ToWide(AssetPath));
		Path.replace_extension(Extension);
		return FPaths::ToUtf8(Path.generic_wstring());
	}
}

URuntimeUILayoutAsset* FRuntimeUILayoutManager::Load(const FString& Path)
{
	const FString NormalizedPath = FPaths::MakeProjectRelative(Path);

	auto It = LoadedLayouts.find(NormalizedPath);
	if (It != LoadedLayouts.end())
	{
		if (IsValid(It->second))
		{
			return It->second;
		}
		LoadedLayouts.erase(It);
	}

	if (!FAssetPackage::IsAssetPackagePath(NormalizedPath))
	{
		return nullptr;
	}

	FWindowsBinReader Ar(NormalizedPath);
	if (!Ar.IsValid())
	{
		return nullptr;
	}

	FAssetPackageHeader Header;
	FAssetImportMetadata Metadata;
	if (!FAssetPackage::ReadPackagePrelude(Ar, EAssetPackageType::RuntimeUILayout, Header, Metadata))
	{
		return nullptr;
	}

	URuntimeUILayoutAsset* NewAsset = UObjectManager::Get().CreateObject<URuntimeUILayoutAsset>();
	NewAsset->Serialize(Ar);

	if (!Ar.IsValid())
	{
		UObjectManager::Get().DestroyObject(NewAsset);
		return nullptr;
	}

	NewAsset->SetAssetPath(NormalizedPath);
	if (NewAsset->GetGeneratedRmlPath().empty() || NewAsset->GetGeneratedRcssPath().empty())
	{
		NewAsset->SetGeneratedPaths(
			MakeGeneratedSiblingPath(NormalizedPath, L".rml"),
			MakeGeneratedSiblingPath(NormalizedPath, L".rcss"));
	}

	LoadedLayouts.emplace(NormalizedPath, NewAsset);
	return NewAsset;
}

URuntimeUILayoutAsset* FRuntimeUILayoutManager::Find(const FString& Path) const
{
	const FString NormalizedPath = FPaths::MakeProjectRelative(Path);

	auto It = LoadedLayouts.find(NormalizedPath);
	if (It == LoadedLayouts.end())
	{
		return nullptr;
	}
	if (IsValid(It->second))
	{
		return It->second;
	}
	return nullptr;
}

bool FRuntimeUILayoutManager::Save(URuntimeUILayoutAsset* Asset)
{
	if (!Asset)
	{
		return false;
	}

	const FString& Path = Asset->GetAssetPath();
	if (Path.empty())
	{
		return false;
	}

	FWindowsBinWriter Ar(FPaths::MakeProjectRelative(Path));
	if (!Ar.IsValid())
	{
		return false;
	}

	FAssetImportMetadata Metadata;
	if (!FAssetPackage::WritePackagePrelude(Ar, EAssetPackageType::RuntimeUILayout, Metadata))
	{
		return false;
	}

	Asset->Serialize(Ar);
	return Ar.IsValid();
}

void FRuntimeUILayoutManager::AddReferencedObjects(FReferenceCollector& Collector)
{
	for (auto& Pair : LoadedLayouts)
	{
		Collector.AddReferencedObject(Pair.second, "FRuntimeUILayoutManager.LoadedLayouts");
	}
}

void FRuntimeUILayoutManager::ClearCache()
{
	LoadedLayouts.clear();
}
