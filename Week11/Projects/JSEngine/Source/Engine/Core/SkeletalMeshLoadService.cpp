#include "Core/SkeletalMeshLoadService.h"

#include "Asset/AssetFile.h"
#include "Asset/AssetMetaData.h"
#include "Asset/FbxImporter.h"
#include "Asset/SkeletalMeshTypes.h"
#include "Core/FbxMaterialLoadService.h"
#include "Core/Logging/Log.h"
#include "Core/Paths.h"
#include "Core/ResourceManager.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>

namespace
{
struct FSkeletalMeshLoadTrianglePositionKey
{
    std::array<int32, 9> QuantizedPositions = {};

    bool operator==(const FSkeletalMeshLoadTrianglePositionKey& Other) const
    {
        return QuantizedPositions == Other.QuantizedPositions;
    }
};

struct FSkeletalMeshLoadTrianglePositionKeyHasher
{
    size_t operator()(const FSkeletalMeshLoadTrianglePositionKey& Key) const
    {
        size_t Hash = 0;
        for (int32 Value : Key.QuantizedPositions)
        {
            Hash ^= static_cast<size_t>(static_cast<uint32>(Value)) + 0x9e3779b9u + (Hash << 6) + (Hash >> 2);
        }
        return Hash;
    }
};

static int32 QuantizeSkeletalMeshLoadPosition(float Value)
{
    constexpr float PositionScale = 10000.0f;
    return static_cast<int32>(std::round(Value * PositionScale));
}

static std::array<int32, 3> MakeSkeletalMeshLoadPositionKey(const FVector& Position)
{
    return {
        QuantizeSkeletalMeshLoadPosition(Position.X),
        QuantizeSkeletalMeshLoadPosition(Position.Y),
        QuantizeSkeletalMeshLoadPosition(Position.Z),
    };
}

static FSkeletalMeshLoadTrianglePositionKey MakeSkeletalMeshLoadTriangleKey(
    const TArray<FSkeletalMeshVertex>& Vertices,
    uint32 Index0,
    uint32 Index1,
    uint32 Index2)
{
    std::array<std::array<int32, 3>, 3> Positions = {
        MakeSkeletalMeshLoadPositionKey(Vertices[Index0].Position),
        MakeSkeletalMeshLoadPositionKey(Vertices[Index1].Position),
        MakeSkeletalMeshLoadPositionKey(Vertices[Index2].Position),
    };

    std::sort(Positions.begin(), Positions.end());

    FSkeletalMeshLoadTrianglePositionKey Key;
    for (int32 PositionIndex = 0; PositionIndex < 3; ++PositionIndex)
    {
        for (int32 Axis = 0; Axis < 3; ++Axis)
        {
            Key.QuantizedPositions[PositionIndex * 3 + Axis] = Positions[PositionIndex][Axis];
        }
    }
    return Key;
}

static void RemoveDuplicateLoadedSkeletalMeshTriangles(FSkeletalMesh* Mesh)
{
    if (!Mesh || Mesh->Vertices.empty() || Mesh->Indices.empty() || Mesh->Sections.empty())
    {
        return;
    }

    TMap<FSkeletalMeshLoadTrianglePositionKey, bool, FSkeletalMeshLoadTrianglePositionKeyHasher> SeenTriangles;
    SeenTriangles.reserve(Mesh->Indices.size() / 3);

    TArray<uint32> NewIndices;
    NewIndices.reserve(Mesh->Indices.size());

    size_t RemovedTriangleCount = 0;

    for (FStaticMeshSection& Section : Mesh->Sections)
    {
        if (Section.IndexCount == 0)
        {
            Section.StartIndex = static_cast<uint32>(NewIndices.size());
            continue;
        }

        const uint32 OldStartIndex = Section.StartIndex;
        const uint32 OldIndexCount = Section.IndexCount;
        if (OldStartIndex + OldIndexCount > Mesh->Indices.size() ||
            OldStartIndex % 3 != 0 ||
            OldIndexCount % 3 != 0)
        {
            UE_LOG_WARNING("[SkeletalMeshLoad] Skip triangle dedup due to invalid section range");
            return;
        }

        Section.StartIndex = static_cast<uint32>(NewIndices.size());

        for (uint32 IndexOffset = 0; IndexOffset < OldIndexCount; IndexOffset += 3)
        {
            const uint32 SourceIndex = OldStartIndex + IndexOffset;
            const uint32 Index0 = Mesh->Indices[SourceIndex + 0];
            const uint32 Index1 = Mesh->Indices[SourceIndex + 1];
            const uint32 Index2 = Mesh->Indices[SourceIndex + 2];

            if (Index0 >= Mesh->Vertices.size() ||
                Index1 >= Mesh->Vertices.size() ||
                Index2 >= Mesh->Vertices.size())
            {
                UE_LOG_WARNING("[SkeletalMeshLoad] Skip triangle dedup due to invalid triangle index");
                return;
            }

            const FSkeletalMeshLoadTrianglePositionKey Key =
                MakeSkeletalMeshLoadTriangleKey(Mesh->Vertices, Index0, Index1, Index2);
            if (SeenTriangles.find(Key) != SeenTriangles.end())
            {
                ++RemovedTriangleCount;
                continue;
            }

            SeenTriangles.emplace(Key, true);
            NewIndices.push_back(Index0);
            NewIndices.push_back(Index1);
            NewIndices.push_back(Index2);
        }

        Section.IndexCount = static_cast<uint32>(NewIndices.size()) - Section.StartIndex;
    }

    if (RemovedTriangleCount == 0)
    {
        return;
    }

    Mesh->Indices = std::move(NewIndices);
    UE_LOG("[SkeletalMeshLoad] Removed %zu duplicate skeletal triangles during load", RemovedTriangleCount);
}
}

FSkeletalMeshLoadService::FSkeletalMeshLoadService(FResourceManager& InResourceManager)
	: ResourceManager(InResourceManager)
{
}

USkeletalMesh* FSkeletalMeshLoadService::Load(const FString& Path)
{
	const FString NormalizedPath = FPaths::Normalize(Path);

	if (USkeletalMesh* FoundMesh = ResourceManager.FindSkeletalMesh(NormalizedPath))
	{
		return FoundMesh;
	}

    if (FAssetFile::IsAssetPath(NormalizedPath))
    {
        return LoadUAsset(NormalizedPath);
    }

	FFbxMaterialLoadService(ResourceManager).Load(NormalizedPath, EMaterialShaderType::SurfaceLit, nullptr);

	return LoadSource(NormalizedPath);
}

USkeletalMesh* FSkeletalMeshLoadService::LoadSource(const FString& NormalizedPath)
{
	FStaticMeshLoadOptions LoadOptions;

	FSkeletalMesh* LoadedMeshData = nullptr;
	double SourceLoadSec = 0.0;

	const auto SourceStart = std::chrono::steady_clock::now();
	LoadedMeshData = ResourceManager.FbxImporter->LoadSkeletalMesh(NormalizedPath, LoadOptions);
	const auto SourceEnd = std::chrono::steady_clock::now();
	SourceLoadSec = std::chrono::duration<double>(SourceEnd - SourceStart).count();

	if (!LoadedMeshData)
	{
		UE_LOG_ERROR("[SkeletalMeshLoad] Failed | Path=%s | FbxSec=%.6f",
			NormalizedPath.c_str(), SourceLoadSec);
		return nullptr;
	}

	UE_LOG("[SkeletalMeshLoad] Source=FBX | Path=%s | FbxSec=%.6f",
		NormalizedPath.c_str(), SourceLoadSec);

	return FinalizeLoadedMesh(LoadedMeshData, NormalizedPath, NormalizedPath);
}

USkeletalMesh* FSkeletalMeshLoadService::ImportSource(const FString& Path)
{
	const FString NormalizedPath = FPaths::Normalize(Path);
	FStaticMeshLoadOptions LoadOptions;

	FFbxMaterialLoadService(ResourceManager).Load(NormalizedPath, EMaterialShaderType::SurfaceLit, nullptr);

	const auto SourceStart = std::chrono::steady_clock::now();
	FSkeletalMesh* LoadedMeshData = ResourceManager.FbxImporter->LoadSkeletalMesh(NormalizedPath, LoadOptions);
	const auto SourceEnd = std::chrono::steady_clock::now();
	const double SourceLoadSec = std::chrono::duration<double>(SourceEnd - SourceStart).count();

	if (!LoadedMeshData)
	{
		UE_LOG_ERROR("[SkeletalMeshLoad] Forced source import failed | Path=%s | FbxSec=%.6f",
			NormalizedPath.c_str(), SourceLoadSec);
		return nullptr;
	}

	UE_LOG("[SkeletalMeshLoad] Source=FBXForced | Path=%s | FbxSec=%.6f",
		NormalizedPath.c_str(), SourceLoadSec);

	return FinalizeLoadedMesh(LoadedMeshData, NormalizedPath, NormalizedPath);
}

USkeletalMesh* FSkeletalMeshLoadService::LoadUAsset(const FString& NormalizedPath)
{
    FAssetMetaData MetaData;
	FSkeletalMesh* MeshData = new FSkeletalMesh();

	const bool bLoaded = FAssetFile::Load(NormalizedPath, MetaData, [&](FArchive& Ar)
	{
		MeshData->Serialize(Ar, MetaData.PayloadVersion);
		return true;
	});

	if (!bLoaded)
	{
		delete MeshData;
		UE_LOG_ERROR("[SkeletalMeshLoad] Failed to load skeletal mesh asset: %s", NormalizedPath.c_str());
		return nullptr;
	}

	if (MetaData.ClassName != USkeletalMesh::StaticClass()->ClassName)
	{
		delete MeshData;
		UE_LOG_ERROR("[SkeletalMeshLoad] UAsset is not SkeletalMesh | Path=%s | Class=%s", NormalizedPath.c_str(), MetaData.ClassName.c_str());
		return nullptr;
	}

	const FString ResolvePath = MetaData.SourceFile.empty() ? NormalizedPath : MetaData.SourceFile;

	if (!MetaData.SourceFile.empty())
	{
		FFbxMaterialLoadService(ResourceManager).Load(ResolvePath, EMaterialShaderType::SurfaceLit, nullptr);
	}

	MeshData->PathFileName = NormalizedPath;
    return FinalizeLoadedMesh(MeshData, ResolvePath, NormalizedPath);
}

USkeletalMesh* FSkeletalMeshLoadService::FinalizeLoadedMesh(FSkeletalMesh* MeshData, const FString& ResolvePath, const FString& CacheKey)
{
	RemoveDuplicateLoadedSkeletalMeshTriangles(MeshData);

	ResourceManager.ResolveSkeletalMeshMaterialSlots(ResolvePath, MeshData);
	MeshData->PathFileName = CacheKey;

	USkeletalMesh* LoadedMesh = UObjectManager::Get().CreateObject<USkeletalMesh>();
	LoadedMesh->SetMeshData(MeshData);

	ResourceManager.SkeletalMeshMap[CacheKey] = LoadedMesh;
	if (std::find(ResourceManager.SkeletalMeshFilePaths.begin(), ResourceManager.SkeletalMeshFilePaths.end(), CacheKey)
		== ResourceManager.SkeletalMeshFilePaths.end())
	{
		ResourceManager.SkeletalMeshFilePaths.push_back(CacheKey);
	}

	UE_LOG("[SkeletalMeshLoad] Loaded | Path=%s | Vertices=%zu | Indices=%zu | Bones=%zu | Sections=%zu",
	       CacheKey.c_str(),
	       LoadedMesh->GetVertices().size(),
	       LoadedMesh->GetIndices().size(),
	       LoadedMesh->GetBones().size(),
	       LoadedMesh->GetSections().size());

	return LoadedMesh;
}
