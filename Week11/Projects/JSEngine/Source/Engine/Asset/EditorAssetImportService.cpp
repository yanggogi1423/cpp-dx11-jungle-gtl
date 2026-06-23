#include "EditorAssetImportService.h"

#include "Animation/AnimSequence.h"
#include "Asset/AssetFile.h"
#include "Asset/FbxImportTypes.h"
#include "Asset/FbxImporter.h"
#include "Asset/SkeletalMesh.h"
#include "Core/Guid.h"
#include "Core/Logging/Log.h"
#include "Core/Paths.h"
#include "Core/ResourceManager.h"

#include <algorithm>
#include <cctype>
#include <filesystem>

namespace
{
FString ToLower(FString Value)
{
    std::transform(
        Value.begin(),
        Value.end(),
        Value.begin(),
        [](unsigned char Ch)
        {
            return static_cast<char>(std::tolower(Ch));
        });

    return Value;
}

bool IsStaticMeshSourcePath(const FString& Path)
{
	const std::filesystem::path FsPath(FPaths::ToWide(FPaths::Normalize(Path)));
	const FString Extension = ToLower(FPaths::ToUtf8(FsPath.extension().wstring()));
	return Extension == ".obj" || Extension == ".fbx";
}

bool IsFbxPath(const FString& Path)
{
    const std::filesystem::path FsPath(FPaths::ToWide(FPaths::Normalize(Path)));
    return ToLower(FPaths::ToUtf8(FsPath.extension().wstring())) == ".fbx";
}

FString MakeDisplayNameFromPath(const FString& Path)
{
    const std::filesystem::path FsPath(FPaths::ToWide(FPaths::Normalize(Path)));
    return FPaths::ToUtf8(FsPath.stem().wstring());
}

FString FindExistingOrNewGuid(const FString& DestinationUAssetPath)
{
    FAssetMetaData ExistingMetaData;
    const std::filesystem::path FsPath(FPaths::ToAbsolute(FPaths::ToWide(DestinationUAssetPath)));
    std::error_code ErrorCode;
    if (std::filesystem::exists(FsPath, ErrorCode) &&
        FAssetFile::LoadMetadataOnly(DestinationUAssetPath, ExistingMetaData) &&
        !ExistingMetaData.AssetGuid.empty())
    {
        return ExistingMetaData.AssetGuid;
    }

    return FGuid::NewGuid().ToString();
}

bool HasAnimationStack(const FString& SourceFbxPath, int32 AnimStackIndex, FString& OutClipName)
{
    const TArray<FFbxAnimationClipInfo> Clips = FResourceManager::Get().InspectAnimationClips(SourceFbxPath);
    for (const FFbxAnimationClipInfo& Clip : Clips)
    {
        if (Clip.AnimStackIndex == AnimStackIndex)
        {
            OutClipName = Clip.Name;
            return true;
        }
    }

    return false;
}
}

bool FEditorAssetImportService::ImportStaticMeshFromSource(const FString& SourcePath, const FString& DestinationAssetPath)
{
	const FString NormalizedSourcePath = FPaths::Normalize(SourcePath);
	const FString NormalizedDestinationPath = FPaths::Normalize(DestinationAssetPath);

	if (!IsStaticMeshSourcePath(NormalizedSourcePath))
	{
		UE_LOG_ERROR("[AssetImport] StaticMesh source is not supported format: %s", NormalizedSourcePath.c_str());
		return false;
	}

	if (!FAssetFile::IsAssetPath(NormalizedDestinationPath))
	{
		UE_LOG_ERROR("[AssetImport] StaticMesh destination is not valid asset path: %s", NormalizedDestinationPath.c_str());
		return false;
	}

	FStaticMeshLoadOptions LoadOptions;
	FStaticMesh* MeshData = nullptr;

	if (IsFbxPath(NormalizedSourcePath))
	{
		MeshData = FResourceManager::Get().FbxImporter->Load(NormalizedSourcePath, LoadOptions);
	}
	else
	{
		MeshData = FResourceManager::Get().ObjLoader.Load(NormalizedSourcePath, LoadOptions);
	}

	if (!MeshData)
	{
		UE_LOG_ERROR("[AssetImport] Failed to load static mesh from source: %s", NormalizedSourcePath.c_str());
		return false;
	}

	MeshData->PathFileName = NormalizedDestinationPath;

	FAssetMetaData MetaData;
	MetaData.Version = 1;
	MetaData.PayloadVersion = 1;
	MetaData.AssetGuid = FindExistingOrNewGuid(NormalizedDestinationPath);
	MetaData.ClassName = UStaticMesh::StaticClass()->ClassName;
	MetaData.DisplayName = MakeDisplayNameFromPath(NormalizedDestinationPath);
	MetaData.SourceFile = NormalizedSourcePath;

	const bool bSaved = FAssetFile::Save(NormalizedDestinationPath, MetaData, [&](FArchive& Ar)
	{
		MeshData->Serialize(Ar, MetaData.PayloadVersion);
		return true;
	});

	delete MeshData;
	return bSaved;
}

bool FEditorAssetImportService::ImportSkeletalMeshFromFbx(
    const FString& SourceFbxPath,
    const FString& DestinationUAssetPath)
{
	const FString NormalizedSourcePath = FPaths::Normalize(SourceFbxPath);
	const FString NormalizedDestinationPath = FPaths::Normalize(DestinationUAssetPath);

	if (!IsFbxPath(NormalizedSourcePath))
	{
		UE_LOG_ERROR("[AssetImport] SkeletalMesh source is not supported format: %s", NormalizedSourcePath.c_str());
		return false;
	}

	if (!FAssetFile::IsAssetPath(NormalizedDestinationPath))
	{
		UE_LOG_ERROR("[AssetImport] SkeletalMesh destination is not valid asset path: %s", NormalizedDestinationPath.c_str());
		return false;
	}

	FSkeletalMesh* MeshData = nullptr;

	MeshData = FResourceManager::Get().FbxImporter->LoadSkeletalMesh(NormalizedSourcePath, FStaticMeshLoadOptions());

	if (!MeshData)
	{
		UE_LOG_ERROR("[AssetImport] Failed to load static mesh from source: %s", NormalizedSourcePath.c_str());
		return false;
	}

	MeshData->PathFileName = NormalizedDestinationPath;

	FAssetMetaData MetaData;
	MetaData.Version = 1;
	MetaData.PayloadVersion = 1;
	MetaData.AssetGuid = FindExistingOrNewGuid(NormalizedDestinationPath);
	MetaData.ClassName = USkeletalMesh::StaticClass()->ClassName;
	MetaData.DisplayName = MakeDisplayNameFromPath(NormalizedDestinationPath);
	MetaData.SourceFile = NormalizedSourcePath;

	const bool bSaved = FAssetFile::Save(NormalizedDestinationPath, MetaData, [&](FArchive& Ar)
	{
		MeshData->Serialize(Ar, MetaData.PayloadVersion);
		return true;
	});

	delete MeshData;
	return bSaved;
}

bool FEditorAssetImportService::ImportAnimationSequenceFromFbx(
    const FString& SourceFbxPath,
    const FString& DestinationUAssetPath, //uasset 저장할 위치
    const FString& TargetSkeletalMeshUAssetPath,//사용할 SkeletalMesh
    int32 AnimStackIndex)
{
    const FString NormalizedSourcePath = FPaths::Normalize(SourceFbxPath);
    const FString NormalizedDestinationPath = FPaths::Normalize(DestinationUAssetPath);
    const FString NormalizedTargetMeshPath = FPaths::Normalize(TargetSkeletalMeshUAssetPath);
    if (!IsFbxPath(NormalizedSourcePath))
    {
        UE_LOG_ERROR("[AssetImport] AnimSequence source is not FBX: %s", NormalizedSourcePath.c_str());
        return false;
    }
    if (!FAssetFile::IsAssetPath(NormalizedDestinationPath))
    {
        UE_LOG_ERROR("[AssetImport] AnimSequence destination is not .uasset: %s", NormalizedDestinationPath.c_str());
        return false;
    }
    if (NormalizedTargetMeshPath.empty() || !FAssetFile::IsAssetPath(NormalizedTargetMeshPath))
    {
        UE_LOG_ERROR("[AssetImport] AnimSequence target skeletal mesh is not .uasset: %s", NormalizedTargetMeshPath.c_str());
        return false;
    }

    FAssetMetaData TargetMeshMetaData;
    if (!FAssetFile::LoadMetadataOnly(NormalizedTargetMeshPath, TargetMeshMetaData) ||
        TargetMeshMetaData.ClassName != USkeletalMesh::StaticClass()->ClassName)
    {
        UE_LOG_ERROR("[AssetImport] Invalid target skeletal mesh asset: %s", NormalizedTargetMeshPath.c_str());
        return false;
    }

    FString ClipName;
    if (!HasAnimationStack(NormalizedSourcePath, AnimStackIndex, ClipName))
    {
        UE_LOG_ERROR("[AssetImport] FBX animation stack not found | Path=%s | StackIndex=%d",
            NormalizedSourcePath.c_str(),
            AnimStackIndex);
        return false;
    }

    USkeletalMesh* TargetMesh = FResourceManager::Get().LoadSkeletalMesh(NormalizedTargetMeshPath);
    if (!TargetMesh || !TargetMesh->HasValidMeshData())
    {
        UE_LOG_ERROR("[AssetImport] Failed to load target skeletal mesh asset: %s", NormalizedTargetMeshPath.c_str());
        return false;
    }

    UAnimSequence* ImportedAnimSequence = FResourceManager::Get().LoadAnimSequence(
        NormalizedSourcePath,
        TargetMesh,
        AnimStackIndex);
    if (!ImportedAnimSequence || !ImportedAnimSequence->HasValidData() || !ImportedAnimSequence->GetDataModel())
    {
        UE_LOG_ERROR("[AssetImport] Failed to import animation sequence | Source=%s | StackIndex=%d",
            NormalizedSourcePath.c_str(),
            AnimStackIndex);
        return false;
    }

	FAnimSequenceAssetPayload Payload;
	Payload.TargetSkeletalMeshPath = NormalizedTargetMeshPath;
	Payload.SourceAnimStackIndex = AnimStackIndex;
	Payload.DataModel = ImportedAnimSequence->GetDataModel();
	Payload.NotifyTracks = ImportedAnimSequence->GetNotifyTracks();

	FAssetMetaData MetaData;
	MetaData.Version = 1;
	MetaData.PayloadVersion = 1;
	MetaData.AssetGuid = FindExistingOrNewGuid(NormalizedDestinationPath);
	MetaData.ClassName = UAnimSequence::StaticClass()->ClassName;
	MetaData.DisplayName = ClipName.empty() ? MakeDisplayNameFromPath(NormalizedDestinationPath) : ClipName;
	MetaData.SourceFile = NormalizedSourcePath;

    return FAssetFile::Save(NormalizedDestinationPath, MetaData, [&](FArchive& Ar)
	{
		Payload.Serialize(Ar, MetaData.PayloadVersion);
		return true;
	});
}
