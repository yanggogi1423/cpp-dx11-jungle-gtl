#pragma once
#include "Core/CoreTypes.h"
#include "Core/Containers/String.h"

class FEditorAssetImportService
{
public:
	bool ImportStaticMeshFromSource(const FString& SourcePath, const FString& DestinationAssetPath);

    bool ImportSkeletalMeshFromFbx(const FString& SourceFbxPath, const FString& DestinationUAssetPath);
    bool ImportAnimationSequenceFromFbx(
        const FString& SourceFbxPath,
        const FString& DestinationUAssetPath,
        const FString& TargetSkeletalMeshUAssetPath,
        int32 AnimStackIndex);
    
};
