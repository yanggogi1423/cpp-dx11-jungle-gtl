#include "Core/FbxMaterialLoadService.h"

#include "Core/AssetPathPolicy.h"
#include "Core/ImportedMaterialPolicy.h"
#include "Core/Logging/Log.h"
#include "Core/Paths.h"
#include "Core/ResourceManager.h"
#include "Render/Resource/FbxMaterialLoader.h"

#include <filesystem>

namespace fs = std::filesystem;

namespace
{
    FString MakeFbxMaterialAssetPath(const FString& NormalizedFbxPath, int32 Index)
    {
        const fs::path AutoMaterialDir = fs::path(L"Asset") / L"Material" / L"Auto";
        const FString MatName = FImportedMaterialPolicy::MakeImportedMaterialAssetName(NormalizedFbxPath, Index);
        const fs::path RelativeMatPath = AutoMaterialDir / FPaths::ToWide(MatName + ".uasset");
        return FPaths::Normalize(FPaths::ToUtf8(RelativeMatPath.generic_wstring()));
    }
}

FFbxMaterialLoadService::FFbxMaterialLoadService(FResourceManager& InResourceManager)
    : ResourceManager(InResourceManager)
{
}

bool FFbxMaterialLoadService::Load(const FString& FbxFilePath, EMaterialShaderType ShaderType, ID3D11Device* Device)
{
    const FString NormalizedFbxPath = FPaths::Normalize(FbxFilePath);
    if (NormalizedFbxPath.empty())
    {
        return false;
    }

    // Cache hit early return (in-memory): 媛숈? FBX??泥?material key媛 ?대? 罹먯떆???덉쑝硫?利됱떆 諛섑솚.
    const FString FirstMaterialKey = MakeFbxMaterialAssetPath(NormalizedFbxPath, 0);
    if (ResourceManager.MaterialCache.ContainsMaterialKey(FirstMaterialKey))
    {
        UE_LOG("[FbxMaterialLoadService] Skipped (already cached): %s", NormalizedFbxPath.c_str());
        return true;
    }

    // Disk cache fallback: imported materials are stored as .uasset files.
    if (FAssetPathPolicy::FileExists(FirstMaterialKey))
    {
        int32 LoadedCount = 0;
        for (int32 Index = 0; ; ++Index)
        {
            const FString MatAssetPath = MakeFbxMaterialAssetPath(NormalizedFbxPath, Index);
            if (!FAssetPathPolicy::FileExists(MatAssetPath))
            {
                break;
            }
            if (!ResourceManager.DeserializeMaterial(MatAssetPath))
            {
                UE_LOG_WARNING("[FbxMaterialLoadService] Failed to deserialize cached material: %s", MatAssetPath.c_str());
                continue;
            }
            // Restore slot aliases from ImportedName to the material asset key.
            if (UMaterial* Mat = ResourceManager.GetMaterial(MatAssetPath))
            {
                if (!Mat->ImportedName.empty())
                {
                    ResourceManager.MaterialCache.SetMaterialSlotAlias(
                        FImportedMaterialPolicy::MakeMaterialSlotAliasKey(NormalizedFbxPath, Mat->ImportedName),
                        MatAssetPath);
                }
            }
            ++LoadedCount;
        }
        if (LoadedCount > 0)
        {
            UE_LOG("[FbxMaterialLoadService] Loaded %d materials from disk cache: %s", LoadedCount, NormalizedFbxPath.c_str());
            return true;
        }
    }

    TMap<FString, UMaterial*> Parsed;
    TArray<FString> MaterialOrder;
    if (!FFbxMaterialLoader::Load(NormalizedFbxPath, Parsed, Device, &MaterialOrder))
    {
        UE_LOG_WARNING("[FbxMaterialLoadService] FbxMaterialLoader failed: %s", NormalizedFbxPath.c_str());
        return false;
    }

    if (Parsed.empty())
    {
        // FBX??surface material 0媛쒖뿬???몄텧 ?먯껜???깃났 (resolve ?④퀎媛 DefaultWhite fallback).
        UE_LOG("[FbxMaterialLoadService] No materials in FBX: %s", NormalizedFbxPath.c_str());
        return true;
    }

    // Ensure the imported material cache directory exists.
    std::error_code Ec;
    fs::create_directories(fs::path(L"Asset") / L"Material" / L"Auto", Ec);

    for (int32 MaterialIndex = 0; MaterialIndex < static_cast<int32>(MaterialOrder.size()); ++MaterialIndex)
    {
        const FString& Name = MaterialOrder[MaterialIndex];
        auto ParsedIt = Parsed.find(Name);
        if (ParsedIt == Parsed.end()) continue;

        UMaterial* Mat = ParsedIt->second;
        if (!Mat) continue;

        const FString MaterialAssetPath = MakeFbxMaterialAssetPath(NormalizedFbxPath, MaterialIndex);
        const FString MaterialKey = MaterialAssetPath;
        const FString MaterialName = FImportedMaterialPolicy::MakeImportedMaterialAssetName(NormalizedFbxPath, MaterialIndex);

        Mat->Name = MaterialName;
        if (Mat->ImportedName.empty()) Mat->ImportedName = Name;
        Mat->FilePath = MaterialAssetPath;
        Mat->SetShaderType(ShaderType);

        // 以묐났 ?깅줉 媛?? ?대? 媛숈? key媛 ?덈떎硫??ъ궗?⑺븯怨???媛앹껜???먭린.
        UMaterial* ExistingMaterial = ResourceManager.MaterialCache.FindMaterialByKey(MaterialKey);
        if (ExistingMaterial)
        {
            if (ExistingMaterial != Mat)
            {
                UObjectManager::Get().DestroyObject(Mat);
                Mat = ExistingMaterial;
            }
        }
        else
        {
            ResourceManager.MaterialCache.RegisterMaterial(MaterialKey, Mat);
        }

        // 蹂댁“ ???깅줉 (?대쫫 湲곕컲 lookup 吏??
        if (!ResourceManager.MaterialCache.ContainsMaterialKey(Mat->Name))
        {
            ResourceManager.MaterialCache.RegisterMaterial(Mat->Name, Mat);
        }
        if (!ResourceManager.MaterialCache.ContainsMaterialKey(Name))
        {
            ResourceManager.MaterialCache.RegisterMaterial(Name, Mat);
        }

        // Slot alias: (fbxPath, FbxName) ??MaterialKey
        // ??ResolveStaticMeshMaterialSlots媛 ??alias濡?吏꾩쭨 UMaterial??李얠쓬
        ResourceManager.MaterialCache.SetMaterialSlotAlias(
            FImportedMaterialPolicy::MakeMaterialSlotAliasKey(NormalizedFbxPath, Name),
            MaterialKey);

        // ?붿뒪????????ㅼ쓬 import ??disk cache fallback??FBX ?ы뙆???뚰뵾?섎룄濡?
        if (!ResourceManager.SerializeMaterial(MaterialAssetPath, Mat))
        {
            UE_LOG_WARNING("[FbxMaterialLoadService] Failed to serialize material to disk: %s", MaterialAssetPath.c_str());
        }

        UE_LOG("[FbxMaterialLoadService] Registered: %s ??%s", Name.c_str(), MaterialKey.c_str());
    }

    UE_LOG("[FbxMaterialLoadService] Loaded %zu materials from %s",
        MaterialOrder.size(), NormalizedFbxPath.c_str());

    return true;
}
