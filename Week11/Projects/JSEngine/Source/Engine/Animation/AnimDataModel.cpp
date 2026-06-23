#include "AnimDataModel.h"
#include "Object/ObjectFactory.h"

void UAnimDataModel::Serialize(FArchive& Ar, int32 PayloadVersion)
{
	Ar << BoneAnimationTracks;
	Ar << PlayLength;
	Ar << FrameRate.Numerator;
	Ar << FrameRate.Denominator;
	Ar << NumberOfFrames;
	Ar << NumberOfKeys;
	Ar << CurveData.FloatCurves;
}

FArchive& operator<<(FArchive& Ar, FRawAnimSequenceTrack& Track)
{
	Ar << Track.PosKeys;
	Ar << Track.RotKeys;
	Ar << Track.ScaleKeys;
	return Ar;
}

FArchive& operator<<(FArchive& Ar, FBoneAnimationTrack& BoneTrack)
{
	Ar << BoneTrack.Name;
	Ar << BoneTrack.BoneIndex;
	Ar << BoneTrack.InternalTrack;
	return Ar;
}

FArchive& operator<<(FArchive& Ar, FFrameRate& FrameRate)
{
	Ar << FrameRate.Numerator;
	Ar << FrameRate.Denominator;
	return Ar;
}

FArchive& operator<<(FArchive& Ar, FAnimCurveTrack& CurveTrack)
{
	Ar << CurveTrack.CurveName;
	Ar << CurveTrack.Curve.Keys;
	return Ar;
}

FArchive& operator<<(FArchive& Ar, FAnimationCurveData& CurveData)
{
	Ar << CurveData.FloatCurves;
	return Ar;
}
