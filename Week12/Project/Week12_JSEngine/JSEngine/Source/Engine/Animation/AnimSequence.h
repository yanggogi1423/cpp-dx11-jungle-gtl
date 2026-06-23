#pragma once
#include "AnimTypes.h"
#include "Asset/CurveFloatAsset.h"
#include "Core/CoreMinimal.h"
#include "Object/Object.h"

struct FArchive;

using FVector3f = FVector;
using FQuat4f = FQuat;

struct FFrameRate
{
	int32 Numerator = 30;
	int32 Denominator = 1;

	float AsDecimal() const
	{
		return Denominator != 0 ? static_cast<float>(Numerator) / static_cast<float>(Denominator) : 0.0f;
	}
};

struct FRawAnimSequenceTrack
{
	TArray<FVector3f> PosKeys;
	TArray<FQuat4f> RotKeys;
	TArray<FVector3f> ScaleKeys;
};

struct FBoneAnimationTrack
{
	FName Name;
	FRawAnimSequenceTrack InternalTrack;
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

struct FAnimNotifyTrack
{
	FName TrackName;
	TArray<FAnimNotifyStateEvent> Notifies;
};

FArchive& operator<<(FArchive& Ar, FFrameRate& Value);
FArchive& operator<<(FArchive& Ar, FRawAnimSequenceTrack& Value);
FArchive& operator<<(FArchive& Ar, FBoneAnimationTrack& Value);
FArchive& operator<<(FArchive& Ar, FAnimCurveTrack& Value);
FArchive& operator<<(FArchive& Ar, FAnimationCurveData& Value);
FArchive& operator<<(FArchive& Ar, FAnimNotifyStateEvent& Value);
FArchive& operator<<(FArchive& Ar, FAnimNotifyTrack& Value);

UCLASS()
class UAnimationAsset : public UObject
{
public:
	GENERATED_BODY(UAnimationAsset, UObject)

	UAnimationAsset() = default;
	~UAnimationAsset() override = default;
};

UCLASS()
class UAnimDataModel : public UObject
{
public:
	GENERATED_BODY(UAnimDataModel, UObject)

	UAnimDataModel() = default;
	~UAnimDataModel() override = default;

	virtual const TArray<FBoneAnimationTrack>& GetBoneAnimationTracks() const;
	TArray<FBoneAnimationTrack>& GetMutableBoneAnimationTracks();

	float GetPlayLength() const { return PlayLength; }
	void SetPlayLength(float InPlayLength) { PlayLength = InPlayLength; }

	const FFrameRate& GetFrameRate() const { return FrameRate; }
	void SetFrameRate(const FFrameRate& InFrameRate) { FrameRate = InFrameRate; }

	int32 GetNumberOfFrames() const { return NumberOfFrames; }
	void SetNumberOfFrames(int32 InNumberOfFrames) { NumberOfFrames = InNumberOfFrames; }

	int32 GetNumberOfKeys() const { return NumberOfKeys; }
	void SetNumberOfKeys(int32 InNumberOfKeys) { NumberOfKeys = InNumberOfKeys; }

	const FAnimationCurveData& GetCurveData() const { return CurveData; }
	FAnimationCurveData& GetMutableCurveData() { return CurveData; }
	void Serialize(FArchive& Ar, int32 PayloadVersion);

private:
	TArray<FBoneAnimationTrack> BoneAnimationTracks;
	float PlayLength = 0.0f;
	FFrameRate FrameRate;
	int32 NumberOfFrames = 0;
	int32 NumberOfKeys = 0;
	FAnimationCurveData CurveData;
};

struct FAnimSequenceAssetPayload
{
	FString TargetSkeletalMeshPath;
	FString SourceStackName;
	int32 SourceAnimStackIndex = 0;
	UAnimDataModel* DataModel = nullptr;
	TArray<FAnimNotifyStateEvent> Notifies;
	TArray<FAnimNotifyTrack> NotifyTracks;

	void Serialize(FArchive& Ar, int32 PayloadVersion);
};

UCLASS()
class UAnimSequenceBase : public UAnimationAsset
{
public:
	GENERATED_BODY(UAnimSequenceBase, UAnimationAsset)
	virtual ~UAnimSequenceBase() = default;

	UAnimDataModel* GetDataModel() const { return DataModel; }
	void SetDataModel(UAnimDataModel* InDataModel) { DataModel = InDataModel; }

	virtual float GetPlayLength() const { return PlayLength; }
	virtual const TArray<FAnimNotifyStateEvent>& GetNotifies() const { return Notifies; }
	const TArray<FAnimNotifyTrack>& GetNotifyTracks() const { return NotifyTracks; }
	TArray<FAnimNotifyTrack>& GetMutableNotifyTracks() { return NotifyTracks; }
	virtual const TArray<FBoneAnimationTrack>& GetBoneAnimationTracks() const;
	virtual bool GetAnimationPose(float Time, FPoseContext& OutPose) const { return false; }
	void SetNotifyTracks(const TArray<FAnimNotifyTrack>& InNotifyTracks);
	int32 AddNotifyTrack(const FName& TrackName);
	bool RemoveNotifyTrack(int32 TrackIndex);
	int32 AddNotifyEvent(int32 TrackIndex, const FAnimNotifyStateEvent& Notify);
	bool RemoveNotifyEvent(int32 TrackIndex, int32 NotifyIndex);
	bool SetNotifyTriggerTime(int32 TrackIndex, int32 NotifyIndex, float TriggerTime);
	bool SetNotifyTiming(int32 TrackIndex, int32 NotifyIndex, float TriggerTime, float Duration);
	bool SetNotifyName(int32 TrackIndex, int32 NotifyIndex, const FName& InNotifyName);
	bool SetNotifyClassName(int32 TrackIndex, int32 NotifyIndex, const FString& InNotifyClassName);
	bool SetNotifyLuaEventName(int32 TrackIndex, int32 NotifyIndex, const FString& InLuaEventName);
	bool SetNotifyLuaTargetScript(int32 TrackIndex, int32 NotifyIndex, const FString& InLuaTargetScript);
	bool SetNotifyLuaTargetPolicy(int32 TrackIndex, int32 NotifyIndex, int32 InLuaTargetPolicy);
	void AddNotify(float InTriggerTime, const FName& InNotifyName, float InDuration = 0.0f, const FString& InNotifyClassName = "");
	void AddNotifyState(float InTriggerTime, float InDuration, const FName& InNotifyName, const FString& InNotifyClassName = "") { AddNotify(InTriggerTime, InNotifyName, InDuration, InNotifyClassName); }
	void ClearNotifies();
	bool RemoveNotifyAt(int32 NotifyIndex);
	bool SetNotifyName(int32 NotifyIndex, const FName& InNotifyName);
	bool SetNotifyClassName(int32 NotifyIndex, const FString& InNotifyClassName);
	bool SetNotifyLuaEventName(int32 NotifyIndex, const FString& InLuaEventName);
	bool SetNotifyLuaTargetScript(int32 NotifyIndex, const FString& InLuaTargetScript);
	bool SetNotifyLuaTargetPolicy(int32 NotifyIndex, int32 InLuaTargetPolicy);
	bool SetNotifyTriggerTime(int32 NotifyIndex, float InTriggerTime);
	bool SetNotifyDuration(int32 NotifyIndex, float InDuration);
	bool SetNotifyTimeRange(int32 NotifyIndex, float InTriggerTime, float InDuration);
	bool MoveNotifyAt(int32 NotifyIndex, float InTriggerTime, int32* OutNewIndex = nullptr);

	void SetPreviewMeshPath(const FString& InPreviewMeshPath) { PreviewMeshPath = InPreviewMeshPath; }
	const FString& GetPreviewMeshPath() const { return PreviewMeshPath; }

protected:
	float PlayLength = 5.0f;
	TArray<FAnimNotifyStateEvent> Notifies;
	UAnimDataModel* DataModel = nullptr;
	FString PreviewMeshPath;
	TArray<FAnimNotifyTrack> NotifyTracks;

private:
	uint32 GenerateNotifyId() const;
	void EnsureDefaultNotifyTrack();
	void EnsureNotifyIds();
	void RebuildFlatNotifiesFromTracks();
	bool FindNotifyByFlatIndex(int32 NotifyIndex, int32& OutTrackIndex, int32& OutNotifyIndex);
};

UCLASS()
class UAnimSequence : public UAnimSequenceBase
{
public:
	GENERATED_BODY(UAnimSequence, UAnimSequenceBase)
	UAnimSequence() = default;
	~UAnimSequence() override = default;

	float GetPlayLength() const override;
	bool GetAnimationPose(float Time, FPoseContext& OutPose) const override;
	bool EvaluateCurve(const FName& CurveName, float TimeSeconds, float& OutValue) const;

	void SetAssetPath(const FString& InAssetPath) { AssetPath = InAssetPath; }
	const FString& GetAssetPath() const { return AssetPath; }

	void SetSourceFilePath(const FString& InSourceFilePath) { SourceFilePath = InSourceFilePath; }
	const FString& GetSourceFilePath() const { return SourceFilePath; }

	void SetSourceStackName(const FString& InSourceStackName) { SourceStackName = InSourceStackName; }
	const FString& GetSourceStackName() const { return SourceStackName; }

	void SetSourceFileWriteTimeTicks(uint64 InSourceFileWriteTimeTicks) { SourceFileWriteTimeTicks = InSourceFileWriteTimeTicks; }
	uint64 GetSourceFileWriteTimeTicks() const { return SourceFileWriteTimeTicks; }

	void SetSourceFileSizeBytes(uint64 InSourceFileSizeBytes) { SourceFileSizeBytes = InSourceFileSizeBytes; }
	uint64 GetSourceFileSizeBytes() const { return SourceFileSizeBytes; }

	void SetSourceFileContentHash(const FString& InSourceFileContentHash) { SourceFileContentHash = InSourceFileContentHash; }
	const FString& GetSourceFileContentHash() const { return SourceFileContentHash; }

	void SetDerivedDataCachePath(const FString& InDerivedDataCachePath) { DerivedDataCachePath = InDerivedDataCachePath; }
	const FString& GetDerivedDataCachePath() const { return DerivedDataCachePath; }

	void SetDerivedDataCacheVersion(int32 InDerivedDataCacheVersion) { DerivedDataCacheVersion = InDerivedDataCacheVersion; }
	int32 GetDerivedDataCacheVersion() const { return DerivedDataCacheVersion; }

	void SetJsonTracksEmbedded(bool bInJsonTracksEmbedded) { bJsonTracksEmbedded = bInJsonTracksEmbedded; }
	bool AreJsonTracksEmbedded() const { return bJsonTracksEmbedded; }

private:
	FString AssetPath;
	FString SourceFilePath;
	FString SourceStackName;
	uint64 SourceFileWriteTimeTicks = 0;
	uint64 SourceFileSizeBytes = 0;
	FString SourceFileContentHash;
	FString DerivedDataCachePath;
	int32 DerivedDataCacheVersion = 0;
	bool bJsonTracksEmbedded = false;
};
