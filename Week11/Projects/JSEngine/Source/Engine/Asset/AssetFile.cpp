#include "Asset/AssetFile.h"

#include "Asset/AssetHeader.h"
#include "Core/Paths.h"
#include "Serialization/WindowsBinReader.h"
#include "Serialization/WindowsBinWriter.h"

#include <algorithm>
#include <cctype>
#include <filesystem>

bool FAssetFile::IsAssetPath(const FString& Path)
{
	const FString NormalizedPath = FPaths::Normalize(Path);
	if (NormalizedPath.empty()) return false;

	const std::filesystem::path FsPath(FPaths::ToWide(NormalizedPath));
	FString Extension = FPaths::ToString(FsPath.extension().wstring());
	std::transform(Extension.begin(), Extension.end(), Extension.begin(), [](unsigned char Ch)
	{
		return static_cast<char>(std::tolower(Ch));
	});

	return Extension == ".uasset";
}

bool FAssetFile::Save(const FString& Path, FAssetMetaData& MetaData, const std::function<bool(FArchive&)>& SerializePayload)
{
	const FString NormalizedPath = FPaths::Normalize(Path);
	if (!IsAssetPath(NormalizedPath)) return false;

	const std::filesystem::path FilePath(FPaths::ToWide(NormalizedPath));
	std::error_code ErrorCode;
	std::filesystem::create_directories(FilePath.parent_path(), ErrorCode);

	FWindowsBinWriter Ar(NormalizedPath);
	if (Ar.HasError()) return false;

	FAssetHeader Header;
	Ar << Header;
	Ar << MetaData;

	if (SerializePayload && !SerializePayload(Ar)) return false;

	return !Ar.HasError();
}

bool FAssetFile::Load(const FString& Path, FAssetMetaData& OutMetaData, const std::function<bool(FArchive&)>& SerializePayload)
{
	const FString NormalizedPath = FPaths::Normalize(Path);
	if (!IsAssetPath(NormalizedPath)) return false;

	FWindowsBinReader Ar(NormalizedPath);
	if (Ar.HasError()) return false;

	FAssetHeader Header;
	Ar << Header;

	if (Header.Magic != 0x54455341)
	{
		return false;
	}

	Ar << OutMetaData;

	if (SerializePayload && !SerializePayload(Ar)) return false;

	return !Ar.HasError();
}

bool FAssetFile::LoadMetadataOnly(const FString& Path, FAssetMetaData& OutMetaData)
{
	FWindowsBinReader Ar(Path);
	if (Ar.HasError()) return false;

	FAssetHeader Header;
	Ar << Header;

	if (Header.Magic != FAssetHeader::ExpectedMagic)
	{
		return false;
	}

	if (Header.Version > FAssetHeader::CurrentVersion)
	{
		return false;
	}

	Ar << OutMetaData;
	return !Ar.HasError();
}
