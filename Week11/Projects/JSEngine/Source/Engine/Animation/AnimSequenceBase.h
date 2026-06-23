#pragma once
#include "AnimationAsset.h"
#include "AnimDataModel.h"

class UAnimDataModel;

struct FAnimNotifyEvent
{
    uint32 NotifyId = 0;
    float TriggerTime = 0.f;
    float Duration = 0.f;
    float TriggerWeightThreshold = 0.1f;
    FName NotifyName;
};

struct FAnimNotifyTrack
{
    FName TrackName;
    TArray<FAnimNotifyEvent> Notifies;
};

UCLASS()
class UAnimSequenceBase : public UAnimationAsset
{
	GENERATED_BODY(UAnimSequenceBase, UAnimationAsset)
public:
	~UAnimSequenceBase() override;

public:
    UAnimDataModel* GetDataModel() const { return DataModel; }
    float GetPlayLength() const { return DataModel ? DataModel->GetPlayLength() : 0.0f; }
    const TArray<FAnimNotifyTrack>& GetNotifyTracks() const { return NotifyTracks; }
    TArray<FAnimNotifyTrack>& GetMutableNotifyTracks() { return NotifyTracks; }
    const TArray<FAnimNotifyEvent>& GetNotifyEvents() const;
    void SetNotifyTracks(const TArray<FAnimNotifyTrack>& InNotifyTracks);
    int32 AddNotifyTrack(const FName& TrackName);
    bool RemoveNotifyTrack(int32 TrackIndex);
    int32 AddNotifyEvent(int32 TrackIndex, const FAnimNotifyEvent& Notify);
    bool RemoveNotifyEvent(int32 TrackIndex, int32 NotifyIndex);
    bool SetNotifyTriggerTime(int32 TrackIndex, int32 NotifyIndex, float TriggerTime);
    bool SetNotifyTiming(int32 TrackIndex, int32 NotifyIndex, float TriggerTime, float Duration);
    void GetAnimNotifiesFromDeltaPositions(float PreviousPosition, float CurrentPosition, bool bLoop, bool bLooped, bool bReverse, TArray<FAnimNotifyEvent>& OutNotifies) const;
    void GetAnimNotifyStateEndsFromDeltaPositions(float PreviousPosition, float CurrentPosition, bool bLoop, bool bLooped, bool bReverse, TArray<FAnimNotifyEvent>& OutNotifies) const;
    bool IsNotifyStateActiveAtTime(const FAnimNotifyEvent& Notify, float TimeSeconds) const;

protected:
    uint32 GenerateNotifyId() const;
    void EnsureNotifyIds();
    void EnsureDefaultNotifyTrack();
    void MarkNotifyCacheDirty() const { bNotifyCacheDirty = true; }

protected:
    UAnimDataModel* DataModel = nullptr;
    TArray<FAnimNotifyTrack> NotifyTracks;
    mutable TArray<FAnimNotifyEvent> FlattenedNotifyEvents;
    mutable bool bNotifyCacheDirty = true;
};

FArchive& operator<<(FArchive& Ar, FAnimNotifyEvent& Event);
FArchive& operator<<(FArchive& Ar, FAnimNotifyTrack& Track);
