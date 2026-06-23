#include "Core/AssetPathPolicy.h"

#include "Asset/AssetFile.h"
#include "Asset/AssetMetaData.h"
#include "Asset/CurveFloatAsset.h"
#include "Core/Paths.h"
#include "Render/Resource/Material.h"

#include <algorithm>
#include <cwctype>
#include <filesystem>

bool FAssetPathPolicy::FileExists(const FString& Path)
{
	std::error_code Ec;
	return std::filesystem::exists(std::filesystem::path(FPaths::ToAbsolute(FPaths::ToWide(Path))), Ec) && !Ec;
}

bool FAssetPathPolicy::IsCurveAssetPath(const FString& Path)
{
	std::filesystem::path FsPath(FPaths::ToWide(FPaths::Normalize(Path)));
	std::wstring Extension = FsPath.extension().wstring();
	std::transform(Extension.begin(), Extension.end(), Extension.begin(), ::towlower);

	if (Extension != L".uasset") return false;

	FAssetMetaData MetaData;
	return FAssetFile::LoadMetadataOnly(FPaths::Normalize(Path), MetaData)
		&& MetaData.ClassName == UCurveFloatAsset::StaticClass()->ClassName;
}

bool FAssetPathPolicy::IsSequenceAssetPath(const FString& Path)
{
	std::filesystem::path FsPath(FPaths::ToWide(FPaths::Normalize(Path)));
	std::wstring Extension = FsPath.extension().wstring();
	std::transform(Extension.begin(), Extension.end(), Extension.begin(), ::towlower);
	return Extension == L".sequence";
}

bool FAssetPathPolicy::IsSerializedMaterialAssetPath(const FString& Path)
{
	std::filesystem::path FsPath(FPaths::ToWide(FPaths::Normalize(Path)));
	std::wstring Extension = FsPath.extension().wstring();
	std::transform(Extension.begin(), Extension.end(), Extension.begin(), ::towlower);
	
	if (Extension != L".uasset") return false;
	
	const FString NormalizedPath = FPaths::Normalize(Path);
	FAssetMetaData MetaData;
	if (!FAssetFile::LoadMetadataOnly(NormalizedPath, MetaData)) return false;

	return MetaData.ClassName == UMaterial::StaticClass()->ClassName ||
		MetaData.ClassName == UMaterialInstance::StaticClass()->ClassName;
}
