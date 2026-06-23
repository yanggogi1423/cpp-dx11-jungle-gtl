#include "Asset/AssetFile.h"

#include "Asset/AssetHeader.h"
#include "Core/Paths.h"
#include "Serialization/WindowsBinReader.h"
#include "Serialization/WindowsBinWriter.h"

#include <algorithm>
#include <cctype>
#include <filesystem>

namespace
{
	FString GetLowerExtension(const FString& Path)
	{
		std::filesystem::path FsPath(FPaths::ToWide(FPaths::Normalize(Path)));
		FString Extension = FPaths::ToUtf8(FsPath.extension().wstring());
		std::transform(Extension.begin(), Extension.end(), Extension.begin(), [](unsigned char Ch)
		{
			return static_cast<char>(std::tolower(Ch));
		});
		return Extension;
	}

	std::filesystem::path ToAbsoluteFilePath(const FString& Path)
	{
		return std::filesystem::path(FPaths::ToAbsolute(FPaths::ToWide(FPaths::Normalize(Path))));
	}
}

bool FAssetFile::IsAssetPath(const FString& Path)
{
	return GetLowerExtension(Path) == ".uasset";
}

bool FAssetFile::Save(const FString& Path, FAssetMetaData& MetaData, const std::function<bool(FArchive&)>& SerializePayload)
{
	const FString NormalizedPath = FPaths::Normalize(Path);
	if (NormalizedPath.empty() || !IsAssetPath(NormalizedPath))
	{
		return false;
	}

	const std::filesystem::path FilePath = ToAbsoluteFilePath(NormalizedPath);
	std::error_code ErrorCode;
	std::filesystem::create_directories(FilePath.parent_path(), ErrorCode);
	if (ErrorCode)
	{
		return false;
	}

	FWindowsBinWriter Ar(NormalizedPath);
	if (Ar.HasError())
	{
		return false;
	}

	FAssetHeader Header;
	Ar << Header;
	Ar << MetaData;

	if (SerializePayload && !SerializePayload(Ar))
	{
		return false;
	}

	return !Ar.HasError();
}

bool FAssetFile::Load(const FString& Path, FAssetMetaData& OutMetaData, const std::function<bool(FArchive&)>& SerializePayload)
{
	const FString NormalizedPath = FPaths::Normalize(Path);
	if (NormalizedPath.empty() || !IsAssetPath(NormalizedPath))
	{
		return false;
	}

	FWindowsBinReader Ar(NormalizedPath);
	if (Ar.HasError())
	{
		return false;
	}

	FAssetHeader Header;
	Ar << Header;
	if (Ar.HasError() || Header.Magic != FAssetHeader::ExpectedMagic || Header.Version > FAssetHeader::CurrentVersion)
	{
		return false;
	}

	Ar << OutMetaData;
	if (SerializePayload && !SerializePayload(Ar))
	{
		return false;
	}

	return !Ar.HasError();
}

bool FAssetFile::LoadMetadataOnly(const FString& Path, FAssetMetaData& OutMetaData)
{
	const FString NormalizedPath = FPaths::Normalize(Path);
	if (NormalizedPath.empty() || !IsAssetPath(NormalizedPath))
	{
		return false;
	}

	FWindowsBinReader Ar(NormalizedPath);
	if (Ar.HasError())
	{
		return false;
	}

	FAssetHeader Header;
	Ar << Header;
	if (Ar.HasError() || Header.Magic != FAssetHeader::ExpectedMagic || Header.Version > FAssetHeader::CurrentVersion)
	{
		return false;
	}

	Ar << OutMetaData;
	return !Ar.HasError();
}
