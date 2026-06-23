#pragma once
#include "AnimSequenceBase.h"

class USkeletalMesh;

struct FAnimSequenceAssetPayload
{
	FString TargetSkeletalMeshPath;
	int32 SourceAnimStackIndex = 0;
	UAnimDataModel* DataModel = nullptr;
	TArray<FAnimNotifyTrack> NotifyTracks;

	void Serialize(FArchive& Ar, int32 PayloadVersion);
};

UCLASS()
class UAnimSequence : public UAnimSequenceBase
{
	GENERATED_BODY(UAnimSequence, UAnimSequenceBase)
public:
    void SetDataModel(UAnimDataModel* InDataModel) { DataModel = InDataModel; }
    bool HasValidData() const
    { return DataModel&& !DataModel->GetBoneAnimationTracks().empty(); }

    // AnimSequence가 가지고 있는 AnimData로 TimeSeconds시간에서의 Bone들의
    // Local Transform을 구함.
    bool EvaluateLocalPose(float TimeSeconds, const USkeletalMesh* SkeletalMesh,
      TArray<FMatrix>& OUT_BoneLocalTransform_OnParentSpace) const;
    bool EvaluateCurve(const FName& CurveName, float TimeSeconds, float& OutValue) const;
};
