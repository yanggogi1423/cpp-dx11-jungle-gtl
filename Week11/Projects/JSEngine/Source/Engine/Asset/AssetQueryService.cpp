#include "Asset/AssetQueryService.h"

#include "Asset/AssetFile.h"
#include "Asset/AssetMetaData.h"
#include "Asset/CurveFloatAsset.h"
#include "Asset/StaticMesh.h"
#include "Core/Paths.h"
#include "Render/Resource/Material.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <initializer_list>

namespace
{
    bool ResolveSafeAssetPath(const FString& RelativePath, std::filesystem::path& OutPath)
    {
        if (RelativePath.empty())
        {
            return false;
        }

        std::filesystem::path RawPath(FPaths::ToWide(RelativePath));
        if (RawPath.is_absolute())
        {
            return false;
        }

        std::filesystem::path CleanRelative;
        bool bSkippedLeadingAsset = false;
        for (const std::filesystem::path& Part : RawPath.lexically_normal())
        {
            const std::wstring PartString = Part.wstring();
            if (PartString.empty() || PartString == L".")
            {
                continue;
            }
            if (PartString == L"..")
            {
                return false;
            }
            if (!bSkippedLeadingAsset && CleanRelative.empty() && (PartString == L"Asset" || PartString == L"asset"))
            {
                bSkippedLeadingAsset = true;
                continue;
            }

            CleanRelative /= Part;
        }

        if (CleanRelative.empty())
        {
            return false;
        }

        OutPath = (std::filesystem::path(FPaths::RootDir()) / L"Asset" / CleanRelative).lexically_normal();
        return true;
    }

    FString ToAssetRelativePath(const std::filesystem::path& AbsolutePath)
    {
        std::error_code Ec;
        std::filesystem::path Relative = std::filesystem::relative(AbsolutePath, std::filesystem::path(FPaths::RootDir()), Ec);
        if (Ec)
        {
            Relative = AbsolutePath.lexically_normal();
        }
        return FPaths::ToUtf8(Relative.generic_wstring());
    }

    FString LowerExtension(const std::filesystem::path& Path)
    {
        FString Extension = FPaths::ToUtf8(Path.extension().wstring());
        std::transform(
            Extension.begin(),
            Extension.end(),
            Extension.begin(),
            [](unsigned char Ch)
            {
                return static_cast<char>(std::tolower(Ch));
            });
        return Extension;
    }

    bool ExtensionMatches(const FString& Extension, std::initializer_list<const char*> Candidates)
    {
        for (const char* Candidate : Candidates)
        {
            if (Extension == Candidate)
            {
                return true;
            }
        }
        return false;
    }

    TArray<FString> ListAssetFiles(const std::filesystem::path& SubDirectory, std::initializer_list<const char*> Extensions)
    {
        TArray<FString> Result;

        const std::filesystem::path Root = (std::filesystem::path(FPaths::RootDir()) / L"Asset" / SubDirectory).lexically_normal();
        if (!std::filesystem::exists(Root))
        {
            return Result;
        }

        std::error_code Ec;
        for (const std::filesystem::directory_entry& Entry : std::filesystem::recursive_directory_iterator(Root, Ec))
        {
            if (Ec)
            {
                break;
            }
            if (!Entry.is_regular_file())
            {
                continue;
            }

            const FString Extension = LowerExtension(Entry.path());
            if (ExtensionMatches(Extension, Extensions))
            {
                Result.push_back(ToAssetRelativePath(Entry.path()));
            }
        }

        return Result;
    }

    TArray<FString> ListCurveAssetFiles()
    {
        TArray<FString> Result;

        const std::filesystem::path Root = (std::filesystem::path(FPaths::RootDir()) / L"Asset").lexically_normal();
        if (!std::filesystem::exists(Root))
        {
            return Result;
        }

        std::error_code Ec;
        for (const std::filesystem::directory_entry& Entry : std::filesystem::recursive_directory_iterator(Root, Ec))
        {
            if (Ec)
            {
                break;
            }
            if (!Entry.is_regular_file() || LowerExtension(Entry.path()) != ".uasset")
            {
                continue;
            }

            const FString RelativePath = ToAssetRelativePath(Entry.path());
            FAssetMetaData MetaData;
            if (FAssetFile::LoadMetadataOnly(RelativePath, MetaData) &&
                MetaData.ClassName == UCurveFloatAsset::StaticClass()->ClassName)
            {
                Result.push_back(RelativePath);
            }
        }

        return Result;
    }
}

bool FAssetQueryService::NormalizeAssetPath(const FString& Path, FString& OutRelativePath)
{
    std::filesystem::path AssetPath;
    if (!ResolveSafeAssetPath(Path, AssetPath))
    {
        OutRelativePath.clear();
        return false;
    }

    OutRelativePath = ToAssetRelativePath(AssetPath);
    return true;
}

bool FAssetQueryService::Exists(const FString& Path)
{
    std::filesystem::path AssetPath;
    return ResolveSafeAssetPath(Path, AssetPath) && std::filesystem::exists(AssetPath);
}

TArray<FString> FAssetQueryService::GetTexturePaths()
{
    return ListAssetFiles(L"Texture", { ".png", ".jpg", ".jpeg", ".dds", ".bmp", ".tga" });
}

TArray<FString> FAssetQueryService::GetStaticMeshPaths()
{
    TArray<FString> Result;
    const TArray<FString> UAssets = ListAssetFiles(L"Mesh", { ".uasset" });
    for (const FString& Path : UAssets)
    {
        FAssetMetaData MetaData;
        if (FAssetFile::LoadMetadataOnly(Path, MetaData) &&
            MetaData.ClassName == UStaticMesh::StaticClass()->ClassName)
        {
            Result.push_back(Path);
        }
    }
    return Result;
}

TArray<FString> FAssetQueryService::GetMaterialPaths()
{
	TArray<FString> Result;
	auto UAssets = ListAssetFiles(L"Material", { ".uasset" });

	for (const FString& Path : UAssets)
	{
		FAssetMetaData MetaData;
		if (FAssetFile::LoadMetadataOnly(Path, MetaData))
		{
			if (MetaData.ClassName == UMaterial::StaticClass()->ClassName ||
				MetaData.ClassName == UMaterialInstance::StaticClass()->ClassName)
			{
				Result.push_back(Path);
			}
		}
	}

	return Result;
}

TArray<FString> FAssetQueryService::GetCurvePaths()
{
    return ListCurveAssetFiles();
}

TArray<FString> FAssetQueryService::GetScenePaths()
{
    return ListAssetFiles(L"Scene", { ".scene" });
}

TArray<FString> FAssetQueryService::GetSoundPaths()
{
    return ListAssetFiles(L"Sound", { ".wav", ".ogg", ".mp3" });
}
