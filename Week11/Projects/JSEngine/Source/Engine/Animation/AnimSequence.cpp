#include "AnimSequence.h"
#include "Asset/SkeletalMesh.h"
#include "Object/ObjectFactory.h"

#include <cmath>

namespace
{
template <typename T>
T SampleKeyArray(const TArray<T>& Keys, float FrameFloat, const T& Fallback)
{
    // FrameFloat: 프레임 위치.
    if (Keys.empty())
    {
        return Fallback;
    }

    if (Keys.size() == 1)
    {
        return Keys[0];
    }

    const float ClampedFrame = MathUtil::Clamp(FrameFloat, 0.0f, static_cast<float>(Keys.size() - 1));
    const int32 Key0 = static_cast<int32>(std::floor(ClampedFrame));
    const int32 Key1 = MathUtil::Clamp(Key0 + 1, 0, static_cast<int32>(Keys.size() - 1));
    const float Alpha = ClampedFrame - static_cast<float>(Key0); //3.0프레임 - 2.65프레임 = 0.35프레임

    return T::Lerp(Keys[Key0], Keys[Key1], Alpha); //3.0프레임과 2.0프레임을 0.36만큼 보간
}

template <>
FQuat SampleKeyArray<FQuat>(const TArray<FQuat>& Keys, float FrameFloat, const FQuat& Fallback)
{
    if (Keys.empty())
    {
        return Fallback;
    }

    if (Keys.size() == 1)
    {
        return Keys[0].GetNormalized();
    }

    const float ClampedFrame = MathUtil::Clamp(FrameFloat, 0.0f, static_cast<float>(Keys.size() - 1));
    const int32 Key0 = static_cast<int32>(std::floor(ClampedFrame));
    const int32 Key1 = MathUtil::Clamp(Key0 + 1, 0, static_cast<int32>(Keys.size() - 1));
    const float Alpha = ClampedFrame - static_cast<float>(Key0);

    return FQuat::Slerp(Keys[Key0], Keys[Key1], Alpha).GetNormalized();
}
}

void FAnimSequenceAssetPayload::Serialize(FArchive& Ar, int32 PayloadVersion)
{
	Ar << TargetSkeletalMeshPath;
	Ar << SourceAnimStackIndex;

	if (Ar.IsLoading() && DataModel == nullptr)
	{
		DataModel = UObjectManager::Get().CreateObject<UAnimDataModel>();
	}

	DataModel->Serialize(Ar, PayloadVersion);
	Ar << NotifyTracks;
}

bool UAnimSequence::EvaluateLocalPose(
    float TimeSeconds,
    const USkeletalMesh* SkeletalMesh,
    OUT TArray<FMatrix>& OUT_BoneLocalTransform_OnParentSpace) const
{
    if (!DataModel || !SkeletalMesh || !SkeletalMesh->HasValidMeshData())
    {
        return false;
    }

    const TArray<FBoneInfo>& Bones = SkeletalMesh->GetBones();
    if (Bones.empty())
    {
        return false;
    }

    // Animation Track 자체가 없는 Bone은 기본 BindPose로 Fallback을 채움.
    OUT_BoneLocalTransform_OnParentSpace.resize(Bones.size());
    for (int32 BoneIndex = 0; BoneIndex < static_cast<int32>(Bones.size()); ++BoneIndex)
    {
        OUT_BoneLocalTransform_OnParentSpace[BoneIndex] = Bones[BoneIndex].LocalBindTransform;
    }

    const FFrameRate FrameRate = DataModel->GetFrameRate();
    const float FPS = FrameRate.AsDecimal();
    if (FPS <= 0.0f)
    {
        return false;
    }

    const float PlayLength = DataModel->GetPlayLength();
    const float ClampedTime = PlayLength > 0.0f
        ? MathUtil::Clamp(TimeSeconds, 0.0f, PlayLength)
        : TimeSeconds;

    // Time * FPS.
    // EX) FPS = 10, Time = 0.25 -> FrameFloat = 2.5, 즉 2.5번째 Key.
    const float FrameFloat = ClampedTime * FPS;

    // BoneAnimTrack이 있는 Bone들을 채워줌.
    const TArray<FBoneAnimationTrack>& BoneAnimTracks = DataModel->GetBoneAnimationTracks();
    for (const FBoneAnimationTrack& BoneAnimTrack : BoneAnimTracks)
    {
        const int32 BoneIndex = BoneAnimTrack.BoneIndex;
        if (BoneIndex < 0 || BoneIndex >= static_cast<int32>(Bones.size()))
        {
            continue;
        }

        // Track은 있으나 특정 채널(Translation, Rotation, Scale)이 없는 경우 fallback.
        FVector BindTranslation = FVector::ZeroVector;
        FMatrix BindRotationMatrix = FMatrix::Identity;
        FVector BindScale = FVector::OneVector;
        Bones[BoneIndex].LocalBindTransform.Decompose(BindTranslation, BindRotationMatrix, BindScale);

        const FQuat BindRotation(BindRotationMatrix);
        const FRawAnimSequenceTrack& Track = BoneAnimTrack.InternalTrack;

        // FrameFloat로 Key를 가져옴.
        // EX) FrameFloat = 2.5 -> Key2와 Key3 사이를 보간한 값. 두 키 사이의 값으로 보간한다
        const FVector Translation = SampleKeyArray(Track.PosKeys, FrameFloat, BindTranslation);
        const FQuat Rotation = SampleKeyArray(Track.RotKeys, FrameFloat, BindRotation);
        const FVector Scale = SampleKeyArray(Track.ScaleKeys, FrameFloat, BindScale);

        //한 프레임 Bone들의 LocalTransform
        OUT_BoneLocalTransform_OnParentSpace[BoneIndex] =
            FMatrix::MakeTRS(Translation, Rotation.ToMatrix(), Scale);
    }

    return true;
}

bool UAnimSequence::EvaluateCurve(const FName& CurveName, float TimeSeconds, float& OutValue) const
{
    if (!DataModel)
    {
        return false;
    }

    const FString TargetName = CurveName.ToString();
    for (const FAnimCurveTrack& CurveTrack : DataModel->GetCurveData().FloatCurves)
    {
        if (CurveTrack.CurveName.ToString() == TargetName)
        {
            OutValue = CurveTrack.Curve.Evaluate(TimeSeconds);
            return true;
        }
    }

    return false;
}
