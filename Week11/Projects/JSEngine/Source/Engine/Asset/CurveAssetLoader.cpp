#include "Asset/CurveAssetLoader.h"

#include "Asset/AssetFile.h"
#include "Asset/AssetMetaData.h"
#include "Asset/CurveFloatAsset.h"
#include "Core/Guid.h"
#include "Core/Logging/Log.h"
#include "Core/Paths.h"
#include "Object/Object.h"

#include <algorithm>
#include <cctype>
#include <filesystem>

namespace
{
    FString NormalizeCurvePath(const FString& Path)
    {
        return FPaths::Normalize(Path);
    }

    bool IsCurveUAssetPath(const FString& Path)
    {
        FString LowerPath = FPaths::Normalize(Path);
        std::transform(
            LowerPath.begin(),
            LowerPath.end(),
            LowerPath.begin(),
            [](unsigned char Ch)
            {
                return static_cast<char>(std::tolower(Ch));
            });

        return std::filesystem::path(FPaths::ToWide(LowerPath)).extension() == L".uasset";
    }
}

UCurveFloatAsset* FCurveAssetLoader::Load(const FString& Path) const
{
    const FString NormalizedPath = NormalizeCurvePath(Path);
    if (NormalizedPath.empty() || !IsCurveUAssetPath(NormalizedPath))
    {
        return nullptr;
    }

    FAssetMetaData MetaData;
    UCurveFloatAsset* Curve = UObjectManager::Get().CreateObject<UCurveFloatAsset>();
    const bool bLoaded = FAssetFile::Load(NormalizedPath, MetaData, [&](FArchive& Ar)
    {
        Curve->Serialize(Ar);
        return true;
    });

    if (!bLoaded || MetaData.ClassName != UCurveFloatAsset::StaticClass()->ClassName)
    {
        UObjectManager::Get().DestroyObject(Curve);
        UE_LOG_ERROR("[CurveAssetLoader] Failed to load curve uasset: %s", NormalizedPath.c_str());
        return nullptr;
    }

    Curve->SetAssetPath(NormalizedPath);
    Curve->GetMutableCurve().SortKeys();
    return Curve;
}

bool FCurveAssetLoader::Save(const FString& Path, const UCurveFloatAsset* Curve) const
{
    if (!Curve)
    {
        return false;
    }

    const FString NormalizedPath = NormalizeCurvePath(Path);
    if (NormalizedPath.empty() || !IsCurveUAssetPath(NormalizedPath))
    {
        return false;
    }

    FAssetMetaData MetaData;
    MetaData.Version = 1;
    MetaData.PayloadVersion = 1;
    MetaData.AssetGuid = FGuid::NewGuid().ToString();
    MetaData.ClassName = UCurveFloatAsset::StaticClass()->ClassName;
    MetaData.DisplayName = FPaths::ToUtf8(std::filesystem::path(FPaths::ToWide(NormalizedPath)).stem().wstring());
    MetaData.SourceFile = "";

    UCurveFloatAsset* MutableCurve = const_cast<UCurveFloatAsset*>(Curve);
    MutableCurve->SetAssetPath(NormalizedPath);
    const bool bSaved = FAssetFile::Save(NormalizedPath, MetaData, [&](FArchive& Ar)
    {
        MutableCurve->Serialize(Ar);
        return true;
    });

    if (!bSaved)
    {
        UE_LOG_ERROR("[CurveAssetLoader] Failed to save curve uasset: %s", NormalizedPath.c_str());
    }
    return bSaved;
}

bool FCurveAssetLoader::SupportsExtension(const FString& Extension) const
{
    return Extension == ".uasset" || Extension == "uasset";
}

FString FCurveAssetLoader::GetLoaderName() const
{
    return "FCurveAssetLoader";
}
