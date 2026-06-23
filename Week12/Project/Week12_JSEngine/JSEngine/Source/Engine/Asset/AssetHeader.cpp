#include "Asset/AssetHeader.h"

#include "Serialization/Archive.h"

FArchive& operator<<(FArchive& Ar, FAssetHeader& Header)
{
	Ar << "Magic" << Header.Magic;
	Ar << "Version" << Header.Version;
	return Ar;
}
