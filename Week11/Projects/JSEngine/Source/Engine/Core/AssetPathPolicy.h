#pragma once

#include "Core/Containers/String.h"
#include "Core/CoreTypes.h"

class FAssetPathPolicy
{
public:
	static bool FileExists(const FString& Path);
	static bool IsCurveAssetPath(const FString& Path);
	static bool IsSequenceAssetPath(const FString& Path);
	static bool IsSerializedMaterialAssetPath(const FString& Path);
};
