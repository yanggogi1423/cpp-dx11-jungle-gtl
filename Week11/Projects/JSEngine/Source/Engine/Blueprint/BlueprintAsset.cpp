#include "Blueprint/BlueprintAsset.h"

#include "Asset/AssetFile.h"
#include "Asset/AssetMetaData.h"
#include "Core/Guid.h"
#include "Core/Paths.h"
#include "Serialization/Archive.h"

#include <filesystem>

void UBlueprintAsset::Serialize(FArchive& Ar)
{
	Serialize(Ar, CurrentPayloadVersion);
}

void UBlueprintAsset::Serialize(FArchive& Ar, int32 PayloadVersion)
{
	UObject::Serialize(Ar);

	Ar << "AssetPath" << AssetPath;
	Graph.Serialize(Ar, PayloadVersion);
}

bool UBlueprintAsset::SaveToFile(const FString& Path)
{
	const FString NormalizedPath = FPaths::Normalize(Path);
	if (!FAssetFile::IsAssetPath(NormalizedPath)) return false;

	AssetPath = NormalizedPath;
	const FString FilePath = FPaths::Normalize(FPaths::ToAbsoluteString(FPaths::ToWide(NormalizedPath)));

	FAssetMetaData MetaData;
	MetaData.Version = 1;
	MetaData.PayloadVersion = CurrentPayloadVersion;

	FAssetMetaData ExistingMetaData;
	MetaData.AssetGuid = FAssetFile::LoadMetadataOnly(FilePath, ExistingMetaData) && !ExistingMetaData.AssetGuid.empty()
		? ExistingMetaData.AssetGuid
		: FGuid::NewGuid().ToString();

	MetaData.ClassName = UBlueprintAsset::StaticClass()->ClassName;
	MetaData.DisplayName = std::filesystem::path(FPaths::ToWide(NormalizedPath)).stem().string();
	MetaData.SourceFile.clear();

	return FAssetFile::Save(FilePath, MetaData, [this](FArchive& Ar)
	{
		Serialize(Ar, CurrentPayloadVersion);
		return true;
	}); 
}

bool UBlueprintAsset::LoadFromFile(const FString& Path)
{
	const FString NormalizedPath = FPaths::Normalize(Path);
	const FString FilePath = FPaths::Normalize(FPaths::ToAbsoluteString(FPaths::ToWide(NormalizedPath)));

	FAssetMetaData MetaData;
	const bool bLoaded = FAssetFile::Load(FilePath, MetaData, [this, &MetaData](FArchive& Ar)
	{
		if (MetaData.ClassName != UBlueprintAsset::StaticClass()->ClassName)
		{
			return false;
		}

		Serialize(Ar, MetaData.PayloadVersion);
		return true;
	});

	if (!bLoaded || MetaData.ClassName != UBlueprintAsset::StaticClass()->ClassName)
	{
		return false;
	}

	AssetPath = NormalizedPath;
	return true;
}
