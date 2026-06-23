#pragma once
#include "Asset/CurveFloatAsset.h"
#include "Core/CoreMinimal.h"
#include "Object/Object.h"

//Bone 하나의 애니메이션 키 데이터
struct FRawAnimSequenceTrack
{
    TArray<FVector> PosKeys;
    TArray<FQuat> RotKeys;
    TArray<FVector> ScaleKeys;
};

//어느 Bone의 Raw Track인지 알려주는 Wrapper
struct FBoneAnimationTrack
{
    FName Name;
    int32 BoneIndex = -1;
    FRawAnimSequenceTrack InternalTrack;
};

//1초당 Key를 몇개 가지는지 (Numerator + 1)
//Key 1Frame당 하나
struct FFrameRate
{
    int32 Numerator = 30;
    int32 Denominator = 1;

    //Default Rate is 30/fps
    float AsDecimal() const
    {
        return Denominator != 0
        ? static_cast<float>(Numerator) / static_cast<float>(Denominator)
        :30.f;
    }
};

struct FAnimCurveTrack
{
    FName CurveName;
    FFloatCurve Curve;
};

struct FAnimationCurveData
{
    TArray<FAnimCurveTrack> FloatCurves;
};

/**
 * Bone들이 각 Key에서 어떻게 되는지를 보관.
 */
UCLASS()
class UAnimDataModel : public UObject
{
	GENERATED_BODY(UAnimDataModel, UObject)
public:
    const TArray<FBoneAnimationTrack>& GetBoneAnimationTracks() const { return BoneAnimationTracks; }
    TArray<FBoneAnimationTrack>& GetMutableBoneAnimationTracks() { return BoneAnimationTracks; }

    float GetPlayLength() const { return PlayLength; }
    FFrameRate GetFrameRate() const { return FrameRate; }
    int32 GetNumberOfFrames() const { return NumberOfFrames; }
    int32 GetNumberOfKeys() const { return NumberOfKeys; }
    const FAnimationCurveData& GetCurveData() const { return CurveData; }
    FAnimationCurveData& GetMutableCurveData() { return CurveData; }


    void SetTiming(
    float InPlayLength,
    const FFrameRate& InFrameRate,
    int32 InNumberOfFrames,
    int32 InNumberOfKeys)
    {
        PlayLength = InPlayLength;
        FrameRate = InFrameRate;
        NumberOfFrames = InNumberOfFrames;
        NumberOfKeys = InNumberOfKeys;
    }

	void Serialize(FArchive& Ar, int32 PayloadVersion);

private:
    /**2차원 배열이라고 생각하면 편함.
     * Frame마다 1Key
     * Bone 0 : [PosKey0, RotKey0,ScaleKey0] [PosKey1, RotKey1,ScaleKey1]...
     * Bone 1 : [PosKey0, RotKey0,ScaleKey0] [PosKey1, RotKey1,ScaleKey1]...
     * Bone 2 : [PosKey0, RotKey0,ScaleKey0] [PosKey1, RotKey1,ScaleKey1]...
     */
    TArray<FBoneAnimationTrack> BoneAnimationTracks;
    //플레이 길이
    float PlayLength = 0.f;
    // 1초당 몇개의 키가 있는가 = Frame Rate
    FFrameRate FrameRate;
    int32 NumberOfFrames = 0;
    int32 NumberOfKeys = 0;
    FAnimationCurveData CurveData;
};

FArchive& operator<<(FArchive& Ar, FRawAnimSequenceTrack& Track);
FArchive& operator<<(FArchive& Ar, FBoneAnimationTrack& Track);
FArchive& operator<<(FArchive& Ar, FFrameRate& FrameRate);
FArchive& operator<<(FArchive& Ar, FAnimCurveTrack& CurveTrack);
FArchive& operator<<(FArchive& Ar, FAnimationCurveData& CurveData);
