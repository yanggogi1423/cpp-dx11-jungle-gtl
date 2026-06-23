#include "AnimSequenceViewerContextBuilder.h"

#include "Asset/AssetFile.h"
#include "Core/ResourceManager.h"
#include "Object/Class.h"

bool FAnimSequenceViewerContextBuilder::BuildFromUAsset(const FString& AnimSequenceUAssetPath, FAnimSequenceViewerContext& OutContext)
{
    OutContext = FAnimSequenceViewerContext{};
    OutContext.AssetPath = AnimSequenceUAssetPath;

	if (!FAssetFile::LoadMetadataOnly(AnimSequenceUAssetPath, OutContext.MetaData))
    {
        OutContext.ErrorMessage = "Failed to read AnimSequence .uasset metadata.";
        return false;
    }

    if (OutContext.MetaData.ClassName != UAnimSequence::StaticClass()->GetName())
    {
        OutContext.ErrorMessage = "Selected .uasset is not an AnimSequence.";
        return false;
    }

	FAnimSequenceAssetPayload Payload;
	const bool bLoaded = FAssetFile::Load(AnimSequenceUAssetPath, OutContext.MetaData,
		[&](FArchive& Ar)
	{
		Payload.Serialize(Ar, OutContext.MetaData.PayloadVersion);
		return true;
	});

	if (!bLoaded)
	{
		OutContext.ErrorMessage = "Failed to load AnimSequence .uasset payload.";
		return false;
	}

	if (Payload.TargetSkeletalMeshPath.empty())
	{
		OutContext.ErrorMessage = "AnimSequence has no target SkeletalMesh path.";
		return false;
	}

	OutContext.TargetSkeletalMeshPath = Payload.TargetSkeletalMeshPath;

	OutContext.PreviewMesh = FResourceManager::Get().LoadSkeletalMesh(Payload.TargetSkeletalMeshPath);
	if (!OutContext.PreviewMesh || !OutContext.PreviewMesh->HasValidMeshData())
	{
		OutContext.ErrorMessage = "Failed to load target SkeletalMesh for AnimSequence.";
		return false;
	}

	OutContext.AnimSequence = FResourceManager::Get().LoadAnimSequence(AnimSequenceUAssetPath);
	if (!OutContext.AnimSequence || !OutContext.AnimSequence->GetDataModel())
	{
		OutContext.ErrorMessage = "Failed to load AnimSequence data.";
		return false;
	}

    return true;
}
