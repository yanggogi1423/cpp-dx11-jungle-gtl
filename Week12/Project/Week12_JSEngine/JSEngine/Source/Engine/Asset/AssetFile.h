#pragma once

#include "Asset/AssetMetaData.h"
#include "Core/Containers/String.h"

#include <functional>

struct FArchive;

class FAssetFile
{
public:
	static bool IsAssetPath(const FString& Path);
	static bool Save(const FString& Path, FAssetMetaData& MetaData, const std::function<bool(FArchive&)>& SerializePayload);
	static bool Load(const FString& Path, FAssetMetaData& OutMetaData, const std::function<bool(FArchive&)>& SerializePayload);
	static bool LoadMetadataOnly(const FString& Path, FAssetMetaData& OutMetaData);
};
