#include "Asset/AssetMetaData.h"

#include "Serialization/Archive.h"

FArchive& operator<<(FArchive& Ar, FAssetMetaData& MetaData)
{
	Ar << "Version" << MetaData.Version;
	Ar << "PayloadVersion" << MetaData.PayloadVersion;
	Ar << "AssetGuid" << MetaData.AssetGuid;
	Ar << "ClassName" << MetaData.ClassName;
	Ar << "DisplayName" << MetaData.DisplayName;
	Ar << "SourceFile" << MetaData.SourceFile;
	return Ar;
}
