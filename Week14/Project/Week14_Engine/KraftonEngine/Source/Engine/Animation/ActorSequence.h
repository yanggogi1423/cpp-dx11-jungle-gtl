#pragma once

#include "Animation/CurvePlayback.h"
#include "Core/Types/CoreTypes.h"
#include "Object/Object.h"
#include "Object/Ptr/WeakObjectPtr.h"

class AActor;
class FArchive;
class FReferenceCollector;
struct FProperty;
class UActorComponent;
class UFloatCurveAsset;

enum class EActorSequenceBindingTarget : uint8
{
	OwnerActor,
	Component,
};

enum class EActorSequenceTrackType : uint8
{
	Scalar,
	Vector3,
	Rotator,
	Vector4,
};

struct FSequenceObjectBinding
{
	FString BindingId;
	EActorSequenceBindingTarget TargetType = EActorSequenceBindingTarget::OwnerActor;
	FString TargetObjectName;
	FString TargetComponentGuid;
};

struct FActorSequenceChannel
{
	FString ChannelName = "Value";
	FCurvePlaybackDesc Playback;
};

struct FActorSequenceSection
{
	FString SectionId;
	float StartTime = 0.0f;
	float Duration = 1.0f;
	float PlayRate = 1.0f;
	bool bLoop = false;
	TArray<FActorSequenceChannel> Channels;
};

struct FActorSequenceTrack
{
	FString PropertyName;
	EActorSequenceTrackType TrackType = EActorSequenceTrackType::Scalar;
	TArray<FActorSequenceSection> Sections;
};

struct FActorSequenceBinding
{
	FSequenceObjectBinding Binding;
	TArray<FActorSequenceTrack> Tracks;
};

struct FResolvedActorSequenceChannel
{
	UObject* ResolvedObject = nullptr;
	const FProperty* ResolvedProperty = nullptr;
	FActorSequenceSection* SourceSection = nullptr;
	FActorSequenceChannel* SourceChannel = nullptr;
	UFloatCurveAsset* ResolvedCurve = nullptr;
	float BaseValue = 0.0f;
	bool bHasBaseValue = false;
	bool bValid = false;
};

#include "Source/Engine/Animation/ActorSequence.generated.h"

UCLASS()
class UActorSequence : public UObject
{
public:
	GENERATED_BODY()

	TArray<FActorSequenceBinding>& GetBindings() { return Bindings; }
	const TArray<FActorSequenceBinding>& GetBindings() const { return Bindings; }

	float GetStartTime() const { return StartTime; }
	float GetDuration() const { return Duration; }
	float GetEndTime() const { return StartTime + Duration; }
	void SetStartTime(float InStartTime);
	void SetDuration(float InDuration);
	void SetPlaybackRange(float InStartTime, float InEndTime);
	void Clear();

	bool AddFloatTrack(
		UObject* TargetObject,
		const FString& PropertyName,
		const FString& ChannelName,
		float StartTime,
		float InDuration,
		UFloatCurveAsset* Curve = nullptr,
		const FString& CurveAssetPath = FString());

	UFloatCurveAsset* CreateInlineCurve();
	void RefreshBindingTargetCache(AActor* OwnerActor);
	FString ExportToJsonString() const;
	bool ImportFromJsonString(const FString& JsonText);

	void AddReferencedObjects(FReferenceCollector& Collector) override;

private:
	FActorSequenceBinding* FindOrAddBinding(UObject* TargetObject);
	const FProperty* FindSequencerProperty(UObject* TargetObject, const FString& PropertyName, const FString& ChannelName) const;
	EActorSequenceTrackType TrackTypeForProperty(const FProperty& Property) const;
	void ClampDurationFromSections();

private:
	TArray<FActorSequenceBinding> Bindings;
	float StartTime = 0.0f;
	float Duration = 1.0f;
};

UCLASS()
class UActorSequencePlayer : public UObject
{
public:
	GENERATED_BODY()

	void Initialize(UActorSequence* InSequence, AActor* InOwnerActor);
	void SetPlaybackOptions(bool bInLooping, bool bInPauseAtEnd);

	void Play(bool bResetTime = true);
	void Pause();
	void Stop(bool bRestoreBaseValues = true);
	void Tick(float DeltaTime);

	void SetCurrentTime(float InTime);
	float GetCurrentTime() const;
	bool IsPlaying() const;
	bool IsPaused() const;
	void MarkResolveDirty();

	void AddReferencedObjects(FReferenceCollector& Collector) override;

private:
	void RebuildResolvedChannels();
	void ApplyAtCurrentTime();
	void RestoreBaseValues();
	UObject* ResolveObject(const FSequenceObjectBinding& Binding) const;
	const FProperty* ResolveProperty(UObject* Object, const FActorSequenceTrack& Track, const FActorSequenceChannel& Channel) const;

private:
	UActorSequence* Sequence = nullptr;
	TWeakObjectPtr<AActor> OwnerActor;
	TArray<FResolvedActorSequenceChannel> ResolvedChannels;
	float CurrentTime = 0.0f;
	bool bPlaying = false;
	bool bPaused = false;
	bool bLooping = false;
	bool bPauseAtEnd = false;
	bool bResolveDirty = true;
};
