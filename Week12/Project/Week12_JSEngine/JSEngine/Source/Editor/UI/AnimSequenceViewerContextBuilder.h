#pragma once
#include "Animation/AnimSequence.h"
#include "Asset/AssetMetaData.h"
#include "Core/Containers/String.h"

class USkeletalMesh;

struct FAnimSequenceViewerContext
{
    FString AssetPath;
    FAssetMetaData MetaData;

    FString TargetSkeletalMeshPath;
    FString TargetSkeletalMeshGuid;

    USkeletalMesh* PreviewMesh = nullptr;
    UAnimSequence* AnimSequence = nullptr;

    FString ErrorMessage;
};

class FAnimSequenceViewerContextBuilder
{
public:
    static bool BuildFromUAsset(
        const FString& AnimSequenceUAssetPath,
        FAnimSequenceViewerContext& OutContext);
};
