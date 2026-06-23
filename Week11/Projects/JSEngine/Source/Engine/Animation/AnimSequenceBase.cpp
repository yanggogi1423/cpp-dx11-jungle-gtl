#include "AnimSequenceBase.h"
#include "Object/ObjectFactory.h"

#include <algorithm>

namespace
{
const FName& DefaultNotifyTrackName()
{
    static const FName Name("Notifies");
    return Name;
}

float GetNotifyEndTime(const FAnimNotifyEvent& Notify, float PlayLength)
{
    return MathUtil::Clamp(Notify.TriggerTime + std::max(Notify.Duration, 0.0f), 0.0f, PlayLength);
}

template <typename TPredicate>
void AppendForwardRange(
    const TArray<FAnimNotifyEvent>& Notifies,
    float RangeStart,
    float RangeEnd,
    TArray<FAnimNotifyEvent>& OutNotifies,
    TPredicate Predicate)
{
    for (const FAnimNotifyEvent& Notify : Notifies)
    {
        const float Time = Predicate(Notify);
        if (Time > RangeStart && Time <= RangeEnd)
        {
            OutNotifies.push_back(Notify);
        }
    }
}

template <typename TPredicate>
void AppendReverseRange(
    const TArray<FAnimNotifyEvent>& Notifies,
    float RangeStart,
    float RangeEnd,
    TArray<FAnimNotifyEvent>& OutNotifies,
    TPredicate Predicate)
{
    for (auto It = Notifies.rbegin(); It != Notifies.rend(); ++It)
    {
        const FAnimNotifyEvent& Notify = *It;
        const float Time = Predicate(Notify);
        if (Time >= RangeEnd && Time < RangeStart)
        {
            OutNotifies.push_back(Notify);
        }
    }
}
}

UAnimSequenceBase::~UAnimSequenceBase()
{
	if (DataModel)
	{
		UObjectManager::Get().DestroyObject(DataModel);
		DataModel = nullptr;
	}
}

const TArray<FAnimNotifyEvent>& UAnimSequenceBase::GetNotifyEvents() const
{
    if (!bNotifyCacheDirty)
    {
        return FlattenedNotifyEvents;
    }

    FlattenedNotifyEvents.clear();
    for (const FAnimNotifyTrack& Track : NotifyTracks)
    {
        FlattenedNotifyEvents.insert(
            FlattenedNotifyEvents.end(),
            Track.Notifies.begin(),
            Track.Notifies.end());
    }
    std::sort(
        FlattenedNotifyEvents.begin(),
        FlattenedNotifyEvents.end(),
        [](const FAnimNotifyEvent& A, const FAnimNotifyEvent& B)
        {
            return A.TriggerTime < B.TriggerTime;
        });
    bNotifyCacheDirty = false;
    return FlattenedNotifyEvents;
}

void UAnimSequenceBase::SetNotifyTracks(const TArray<FAnimNotifyTrack>& InNotifyTracks)
{
    NotifyTracks = InNotifyTracks;
    EnsureDefaultNotifyTrack();
    EnsureNotifyIds();
    MarkNotifyCacheDirty();
}

int32 UAnimSequenceBase::AddNotifyTrack(const FName& TrackName)
{
    FAnimNotifyTrack Track;
    Track.TrackName = TrackName.ToString().empty() ? DefaultNotifyTrackName() : TrackName;
    NotifyTracks.push_back(Track);
    MarkNotifyCacheDirty();
    return static_cast<int32>(NotifyTracks.size()) - 1;
}

bool UAnimSequenceBase::RemoveNotifyTrack(int32 TrackIndex)
{
    if (TrackIndex < 0 || TrackIndex >= static_cast<int32>(NotifyTracks.size()))
    {
        return false;
    }

    NotifyTracks.erase(NotifyTracks.begin() + TrackIndex);
    EnsureDefaultNotifyTrack();
    MarkNotifyCacheDirty();
    return true;
}

int32 UAnimSequenceBase::AddNotifyEvent(int32 TrackIndex, const FAnimNotifyEvent& Notify)
{
    EnsureDefaultNotifyTrack();
    if (TrackIndex < 0 || TrackIndex >= static_cast<int32>(NotifyTracks.size()))
    {
        TrackIndex = 0;
    }

    FAnimNotifyEvent NotifyToAdd = Notify;
    if (NotifyToAdd.NotifyId == 0)
    {
        NotifyToAdd.NotifyId = GenerateNotifyId();
    }

    NotifyTracks[TrackIndex].Notifies.push_back(NotifyToAdd);
    MarkNotifyCacheDirty();
    return static_cast<int32>(NotifyTracks[TrackIndex].Notifies.size()) - 1;
}

bool UAnimSequenceBase::RemoveNotifyEvent(int32 TrackIndex, int32 NotifyIndex)
{
    if (TrackIndex < 0 || TrackIndex >= static_cast<int32>(NotifyTracks.size()))
    {
        return false;
    }

    TArray<FAnimNotifyEvent>& Notifies = NotifyTracks[TrackIndex].Notifies;
    if (NotifyIndex < 0 || NotifyIndex >= static_cast<int32>(Notifies.size()))
    {
        return false;
    }

    Notifies.erase(Notifies.begin() + NotifyIndex);
    MarkNotifyCacheDirty();
    return true;
}

bool UAnimSequenceBase::SetNotifyTriggerTime(int32 TrackIndex, int32 NotifyIndex, float TriggerTime)
{
    if (TrackIndex < 0 || TrackIndex >= static_cast<int32>(NotifyTracks.size()))
    {
        return false;
    }

    TArray<FAnimNotifyEvent>& Notifies = NotifyTracks[TrackIndex].Notifies;
    if (NotifyIndex < 0 || NotifyIndex >= static_cast<int32>(Notifies.size()))
    {
        return false;
    }

    return SetNotifyTiming(TrackIndex, NotifyIndex, TriggerTime, Notifies[NotifyIndex].Duration);
}

bool UAnimSequenceBase::SetNotifyTiming(int32 TrackIndex, int32 NotifyIndex, float TriggerTime, float Duration)
{
    if (TrackIndex < 0 || TrackIndex >= static_cast<int32>(NotifyTracks.size()))
    {
        return false;
    }

    TArray<FAnimNotifyEvent>& Notifies = NotifyTracks[TrackIndex].Notifies;
    if (NotifyIndex < 0 || NotifyIndex >= static_cast<int32>(Notifies.size()))
    {
        return false;
    }

    const float ClampedPlayLength = std::max(0.0f, GetPlayLength());
    const float ClampedTriggerTime = MathUtil::Clamp(TriggerTime, 0.0f, ClampedPlayLength);
    const float MaxDuration = std::max(0.0f, ClampedPlayLength - ClampedTriggerTime);
    Notifies[NotifyIndex].TriggerTime = ClampedTriggerTime;
    Notifies[NotifyIndex].Duration = MathUtil::Clamp(Duration, 0.0f, MaxDuration);
    MarkNotifyCacheDirty();
    return true;
}

void UAnimSequenceBase::GetAnimNotifiesFromDeltaPositions(
    float PreviousPosition,
    float CurrentPosition,
    bool bLoop,
    bool bLooped,
    bool bReverse,
    TArray<FAnimNotifyEvent>& OutNotifies) const
{
    const TArray<FAnimNotifyEvent>& FlattendNotifys = GetNotifyEvents();
    const float PlayLength = GetPlayLength();
    if (FlattendNotifys.empty() || PlayLength <= 0.0f)
    {
        return;
    }

    auto Predicate = [](const FAnimNotifyEvent& Notify) { return Notify.TriggerTime; };
    if (!bReverse)
    {
        if (bLoop && bLooped)
        {
            AppendForwardRange(FlattendNotifys, PreviousPosition, PlayLength, OutNotifies, Predicate);
            AppendForwardRange(FlattendNotifys, 0.0f, CurrentPosition, OutNotifies, Predicate);
            return;
        }

        AppendForwardRange(FlattendNotifys, PreviousPosition, CurrentPosition, OutNotifies, Predicate);
        return;
    }

    if (bLoop && bLooped)
    {
        AppendReverseRange(FlattendNotifys, PreviousPosition, 0.0f, OutNotifies, Predicate);
        AppendReverseRange(FlattendNotifys, PlayLength, CurrentPosition, OutNotifies, Predicate);
        return;
    }

    AppendReverseRange(FlattendNotifys, PreviousPosition, CurrentPosition, OutNotifies, Predicate);
}

void UAnimSequenceBase::GetAnimNotifyStateEndsFromDeltaPositions(
    float PreviousPosition,
    float CurrentPosition,
    bool bLoop,
    bool bLooped,
    bool bReverse,
    TArray<FAnimNotifyEvent>& OutNotifies) const
{
    const TArray<FAnimNotifyEvent>& Notifies = GetNotifyEvents();
    const float PlayLength = GetPlayLength();
    if (Notifies.empty() || PlayLength <= 0.0f)
    {
        return;
    }

    auto EndTime = [PlayLength](const FAnimNotifyEvent& Notify)
    {
        return GetNotifyEndTime(Notify, PlayLength);
    };

    if (!bReverse)
    {
        if (bLoop && bLooped)
        {
            AppendForwardRange(Notifies, PreviousPosition, PlayLength, OutNotifies, EndTime);
            AppendForwardRange(Notifies, 0.0f, CurrentPosition, OutNotifies, EndTime);
            return;
        }

        AppendForwardRange(Notifies, PreviousPosition, CurrentPosition, OutNotifies, EndTime);
        return;
    }

    if (bLoop && bLooped)
    {
        AppendReverseRange(Notifies, PreviousPosition, 0.0f, OutNotifies, EndTime);
        AppendReverseRange(Notifies, PlayLength, CurrentPosition, OutNotifies, EndTime);
        return;
    }

    AppendReverseRange(Notifies, PreviousPosition, CurrentPosition, OutNotifies, EndTime);
}

bool UAnimSequenceBase::IsNotifyStateActiveAtTime(const FAnimNotifyEvent& Notify, float TimeSeconds) const
{
    if (Notify.Duration <= 0.0f)
    {
        return false;
    }

    const float PlayLength = GetPlayLength();
    if (PlayLength <= 0.0f)
    {
        return false;
    }

    const float StartTime = MathUtil::Clamp(Notify.TriggerTime, 0.0f, PlayLength);
    const float EndTime = GetNotifyEndTime(Notify, PlayLength);
    return TimeSeconds >= StartTime && TimeSeconds < EndTime;
}

uint32 UAnimSequenceBase::GenerateNotifyId() const
{
    uint32 MaxId = 0;
    for (const FAnimNotifyTrack& Track : NotifyTracks)
    {
        for (const FAnimNotifyEvent& Notify : Track.Notifies)
        {
            MaxId = std::max(MaxId, Notify.NotifyId);
        }
    }

    return MaxId + 1;
}

void UAnimSequenceBase::EnsureNotifyIds()
{
    TArray<uint32> UsedIds;
    for (FAnimNotifyTrack& Track : NotifyTracks)
    {
        for (FAnimNotifyEvent& Notify : Track.Notifies)
        {
            const bool bNeedsNewId = Notify.NotifyId == 0
                || std::find(UsedIds.begin(), UsedIds.end(), Notify.NotifyId) != UsedIds.end();
            if (bNeedsNewId)
            {
                Notify.NotifyId = GenerateNotifyId();
            }

            UsedIds.push_back(Notify.NotifyId);
        }
    }
}

void UAnimSequenceBase::EnsureDefaultNotifyTrack()
{
    if (!NotifyTracks.empty())
    {
        return;
    }

    FAnimNotifyTrack DefaultTrack;
    DefaultTrack.TrackName = DefaultNotifyTrackName();
    NotifyTracks.push_back(DefaultTrack);
    MarkNotifyCacheDirty();
}

FArchive& operator<<(FArchive& Ar, FAnimNotifyEvent& Event)
{
	Ar << Event.NotifyId;
	Ar << Event.TriggerTime;
	Ar << Event.Duration;
	Ar << Event.TriggerWeightThreshold;
	Ar << Event.NotifyName;
	return Ar;
}

FArchive& operator<<(FArchive& Ar, FAnimNotifyTrack& Track)
{
	Ar << Track.TrackName;
	Ar << Track.Notifies;
	return Ar;
}
