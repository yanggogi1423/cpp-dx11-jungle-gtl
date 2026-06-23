#pragma once

#include "Core/Containers/String.h"
#include "Core/CoreTypes.h"

struct FArchive;

struct FAssetMetaData
{
	int32 Version = 1;
	int32 PayloadVersion = 1;
	FString AssetGuid;
	FString ClassName;
	FString DisplayName;
	FString SourceFile;
};

FArchive& operator<<(FArchive& Ar, FAssetMetaData& MetaData);
