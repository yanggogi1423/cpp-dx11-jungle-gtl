#include "Asset/CurveAssetLoader.h"

#include "Asset/AssetFile.h"
#include "Asset/AssetMetaData.h"
#include "Asset/CurveFloatAsset.h"
#include "Core/Guid.h"
#include "Core/Logging/Log.h"
#include "Core/Paths.h"
#include "Object/Class.h"
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

}

UCurveFloatAsset* FCurveAssetLoader::Load(const FString& Path) const
{
    const FString NormalizedPath = NormalizeCurvePath(Path);
    if (NormalizedPath.empty())
    {
        return nullptr;
    }

    if (FAssetFile::IsAssetPath(NormalizedPath))
    {
        FAssetMetaData MetaData;
        UCurveFloatAsset* Curve = UObjectManager::Get().CreateObject<UCurveFloatAsset>();
        const bool bLoaded = FAssetFile::Load(NormalizedPath, MetaData, [&](FArchive& Ar)
        {
            Curve->Serialize(Ar);
            return true;
        });

        if (!bLoaded || MetaData.ClassName != UCurveFloatAsset::StaticClass()->GetName())
        {
            UObjectManager::Get().DestroyObject(Curve);
            UE_LOG_ERROR("[CurveAssetLoader] Failed to load curve uasset: %s", NormalizedPath.c_str());
            return nullptr;
        }

        Curve->SetAssetPath(NormalizedPath);
        Curve->GetMutableCurve().SortKeys();
        return Curve;
    }

    return nullptr;
}

bool FCurveAssetLoader::Save(const FString& Path, const UCurveFloatAsset* Curve) const
{
    if (!Curve)
    {
        return false;
    }

    const FString NormalizedPath = NormalizeCurvePath(Path);
    if (NormalizedPath.empty())
    {
        return false;
    }

    if (FAssetFile::IsAssetPath(NormalizedPath))
    {
        FAssetMetaData ExistingMetaData;
        FAssetMetaData MetaData;
        MetaData.Version = 1;
        MetaData.PayloadVersion = 1;
        MetaData.AssetGuid = FAssetFile::LoadMetadataOnly(NormalizedPath, ExistingMetaData) && !ExistingMetaData.AssetGuid.empty()
            ? ExistingMetaData.AssetGuid
            : FGuid::NewGuid().ToString();
        MetaData.ClassName = UCurveFloatAsset::StaticClass()->GetName();
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

    return false;
}

bool FCurveAssetLoader::SupportsExtension(const FString& Extension) const
{
    return Extension == ".uasset" || Extension == "uasset";
}
