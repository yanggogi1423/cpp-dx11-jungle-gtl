#pragma once

#include "Asset/IAssetLoader.h"
#include "Asset/FbxImportTypes.h"
#include "Asset/SkeletalMeshTypes.h"
#include "Asset/StaticMeshTypes.h"
#include "Core/ResourceTypes.h"

namespace fbxsdk
{
class FbxManager;
class FbxIOSettings;
class FbxMesh;
class FbxNode;
class FbxScene;
} // namespace fbxsdk

using fbxsdk::FbxManager;
using fbxsdk::FbxIOSettings;
using fbxsdk::FbxMesh;
using fbxsdk::FbxNode;
using fbxsdk::FbxScene;

enum class ESkeletalMeshImportPass
{
    SkinnedMeshes,
    RigidAttachedMeshes
};

class UAnimSequence;
class USkeletalMesh;

class FFbxImporter : public IAssetLoader
{
public:
    FFbxImporter() = default;
    ~FFbxImporter() override;

    FStaticMesh* Load(const FString& Path, const FStaticMeshLoadOptions& LoadOptions);

    bool SupportsExtension(const FString& Extension) const override;
    FString GetLoaderName() const override;

    FSkeletalMesh* LoadSkeletalMesh(const FString& Path, const FStaticMeshLoadOptions& LoadOptions);

    FFbxMeshContentInfo InspectMeshContent(const FString& Path);

    // Animation Related
    UAnimSequence* LoadAnimSequence(const FString& Path, const USkeletalMesh* TargetMesh);
    UAnimSequence* LoadAnimSequence(const FString& Path, const USkeletalMesh* TargetMesh, int32 AnimStackIndex);
    TArray<FFbxAnimationClipInfo> InspectAnimationClips(const FString& Path);

    bool InspectMeshAndAnimClips(const FString& Path, FFbxMeshContentInfo& OutInfo, TArray<FFbxAnimationClipInfo>& OutAnimationClips);

private:
    FbxManager* GetOrCreateManager();
    FbxScene* GetOrCreateAnimationScene(const FString& Path);
    void ClearCachedAnimationScene();
    bool ImportScene(const FString& Path, FbxManager* Manager, FbxScene* Scene, bool bConvertScene = true);

    // Scene -> StaticMesh (mesh node를 재귀로 순회)
    void CollectMeshes(FbxNode* Node, FStaticMesh* InStaticMesh);
    void ProcessMesh(FbxMesh* Mesh, FStaticMesh* InStaticMesh);

    int32 GetOrAddMaterialSlot(FStaticMesh* InStaticMesh, const FString& MaterialName);
    FAABB BuildLocalBounds(FStaticMesh* InStaticMesh) const;

    void NormalizePositionsToUnitCube(FStaticMesh* InStaticMesh);
    void ComputeTangents(FStaticMesh* InStaticMesh);

    void CollectSkeletalMeshes(
        FbxNode* Node,
        FSkeletalMesh* InSkeletalMesh,
        ESkeletalMeshImportPass Pass,
        TMap<FbxNode*, int32>& BoneNodeToIndex,
        bool& bHasImportedSkinnedMesh);

    void ProcessSkeletalMesh(
        FbxMesh* Mesh,
        FSkeletalMesh* InSkeletalMesh,
        ESkeletalMeshImportPass Pass,
        TMap<FbxNode*, int32>& BoneNodeToIndex,
        bool& bHasImportedSkinnedMesh);

    void ProcessRigidAttachedMesh(
        FbxMesh* Mesh,
        FSkeletalMesh* InSkeletalMesh,
        TMap<FbxNode*, int32>& BoneNodeToIndex,
        bool bHasImportedSkinnedMesh);

    int32 GetOrAddMaterialSlot(FSkeletalMesh* InSkeletalMesh, const FString& MaterialName);
    FAABB BuildLocalBounds(FSkeletalMesh* InSkeletalMesh) const;
    void ComputeTangents(FSkeletalMesh* InSkeletalMesh);

    // Animation Related
    void CollectAnimationBoneNodes(FbxNode* Node, const TMap<FString, int32>& BoneNameToIndex, TMap<FString, FbxNode*>& OutBoneNameToNode) const;
	TArray<FFbxAnimationClipInfo> CollectAnimationClipInfos(const FString& Path);
    TArray<FFbxAnimationClipInfo> CollectAnimationClipInfos(FbxScene* Scene) const;

    FbxManager* CachedManager = nullptr;
    FbxIOSettings* CachedIOSettings = nullptr;
    FbxScene* CachedAnimationScene = nullptr;
    FString CachedAnimationScenePath;
};
