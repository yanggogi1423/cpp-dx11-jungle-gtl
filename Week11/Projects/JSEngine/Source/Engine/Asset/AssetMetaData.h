#pragma once
#include "Core/CoreTypes.h"
#include "Core/Containers/Array.h"
#include "Core/Containers/String.h"

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
