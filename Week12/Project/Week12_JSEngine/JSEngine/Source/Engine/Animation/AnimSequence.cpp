#include "Animation/AnimSequence.h"
#include "Animation/AnimNotify.h"
#include "Core/Reflection/ReflectionRegistry.h"
#include "Geometry/Transform.h"
#include "Object/Class.h"
#include "Object/Object.h"
#include "Serialization/Archive.h"

#include <algorithm>
#include <cmath>

namespace
{
	const FName& DefaultNotifyTrackName()
	{
		static const FName Name("Notifies");
		return Name;
	}

	UAnimNotify* CreateAnimNotifyObject(const FString& NotifyClassName)
	{
		if (NotifyClassName.empty())
		{
			return nullptr;
		}

		UClass* Class = FReflectionRegistry::Get().FindClass(NotifyClassName);
		if (!Class || !Class->IsChildOf(UAnimNotify::StaticClass()) || Class->HasAnyClassFlags(CF_Abstract))
		{
			return nullptr;
		}

		return Cast<UAnimNotify>(NewObject(Class));
	}

	void SerializeAnimNotifyEvent(FArchive& Ar, FAnimNotifyStateEvent& Value, int32 PayloadVersion)
	{
		if (PayloadVersion >= 4)
		{
			Ar << "NotifyId" << Value.NotifyId;
		}
		Ar << "TriggerTime" << Value.TriggerTime;
		Ar << "Duration" << Value.Duration;
		if (PayloadVersion >= 4)
		{
			Ar << "TriggerWeightThreshold" << Value.TriggerWeightThreshold;
		}
		Ar << "NotifyName" << Value.NotifyName;
		Ar << "NotifyClassName" << Value.NotifyClassName;
		if (PayloadVersion >= 3)
		{
			Ar << "LuaEventName" << Value.LuaEventName;
			Ar << "LuaTargetScript" << Value.LuaTargetScript;
			Ar << "LuaTargetPolicy" << Value.LuaTargetPolicy;
		}
		if (Ar.IsLoading())
		{
			Value.NotifyObject = CreateAnimNotifyObject(Value.NotifyClassName);
		}
	}

	void SerializeAnimNotifyArray(FArchive& Ar, TArray<FAnimNotifyStateEvent>& Notifies, int32 PayloadVersion)
	{
		int32 Count = static_cast<int32>(Notifies.size());
		Ar << "Notifies";
		Ar.BeginArray(Ar.GetCurrentKey(), Count);
		if (Ar.IsLoading())
		{
			Notifies.resize(Count);
		}
		for (FAnimNotifyStateEvent& Notify : Notifies)
		{
			SerializeAnimNotifyEvent(Ar, Notify, PayloadVersion);
		}
		Ar.EndArray();
	}

	void SerializeAnimNotifyTracks(FArchive& Ar, TArray<FAnimNotifyTrack>& NotifyTracks, int32 PayloadVersion)
	{
		int32 Count = static_cast<int32>(NotifyTracks.size());
		Ar << "NotifyTracks";
		Ar.BeginArray(Ar.GetCurrentKey(), Count);
		if (Ar.IsLoading())
		{
			NotifyTracks.resize(Count);
		}
		for (FAnimNotifyTrack& Track : NotifyTracks)
		{
			Ar << "TrackName" << Track.TrackName;
			SerializeAnimNotifyArray(Ar, Track.Notifies, PayloadVersion);
		}
		Ar.EndArray();
	}
}

const TArray<FBoneAnimationTrack>& UAnimDataModel::GetBoneAnimationTracks() const
{
	return BoneAnimationTracks;
}

TArray<FBoneAnimationTrack>& UAnimDataModel::GetMutableBoneAnimationTracks()
{
	return BoneAnimationTracks;
}

void UAnimDataModel::Serialize(FArchive& Ar, int32 PayloadVersion)
{
	Ar << "BoneAnimationTracks" << BoneAnimationTracks;
	Ar << "PlayLength" << PlayLength;
	Ar << "FrameRate" << FrameRate;
	Ar << "NumberOfFrames" << NumberOfFrames;
	Ar << "NumberOfKeys" << NumberOfKeys;
	if (PayloadVersion >= 4)
	{
		Ar << "CurveData" << CurveData;
	}
}

FArchive& operator<<(FArchive& Ar, FFrameRate& Value)
{
	Ar << "Numerator" << Value.Numerator;
	Ar << "Denominator" << Value.Denominator;
	return Ar;
}

FArchive& operator<<(FArchive& Ar, FRawAnimSequenceTrack& Value)
{
	Ar << "PosKeys" << Value.PosKeys;
	Ar << "RotKeys" << Value.RotKeys;
	Ar << "ScaleKeys" << Value.ScaleKeys;
	return Ar;
}

FArchive& operator<<(FArchive& Ar, FBoneAnimationTrack& Value)
{
	Ar << "Name" << Value.Name;
	Ar << "InternalTrack" << Value.InternalTrack;
	return Ar;
}

FArchive& operator<<(FArchive& Ar, FAnimCurveTrack& Value)
{
	Ar << "CurveName" << Value.CurveName;
	TArray<float> Times;
	TArray<float> Values;
	TArray<int32> InterpModes;
	TArray<int32> TangentModes;
	TArray<float> ArriveTangents;
	TArray<float> LeaveTangents;

	if (Ar.IsSaving())
	{
		Times.reserve(Value.Curve.Keys.size());
		Values.reserve(Value.Curve.Keys.size());
		InterpModes.reserve(Value.Curve.Keys.size());
		TangentModes.reserve(Value.Curve.Keys.size());
		ArriveTangents.reserve(Value.Curve.Keys.size());
		LeaveTangents.reserve(Value.Curve.Keys.size());
		for (const FCurveKey& Key : Value.Curve.Keys)
		{
			Times.push_back(Key.Time);
			Values.push_back(Key.Value);
			InterpModes.push_back(static_cast<int32>(Key.InterpMode));
			TangentModes.push_back(static_cast<int32>(Key.TangentMode));
			ArriveTangents.push_back(Key.ArriveTangent);
			LeaveTangents.push_back(Key.LeaveTangent);
		}
	}

	Ar << "Times" << Times;
	Ar << "Values" << Values;
	Ar << "InterpModes" << InterpModes;
	Ar << "TangentModes" << TangentModes;
	Ar << "ArriveTangents" << ArriveTangents;
	Ar << "LeaveTangents" << LeaveTangents;

	if (Ar.IsLoading())
	{
		Value.Curve.Keys.clear();
		Value.Curve.Keys.reserve(Times.size());
		for (size_t Index = 0; Index < Times.size(); ++Index)
		{
			FCurveKey Key;
			Key.Time = Times[Index];
			Key.Value = Index < Values.size() ? Values[Index] : 0.0f;
			Key.InterpMode = Index < InterpModes.size()
				? static_cast<ECurveInterpMode>(InterpModes[Index])
				: ECurveInterpMode::Cubic;
			Key.TangentMode = Index < TangentModes.size()
				? static_cast<ECurveTangentMode>(TangentModes[Index])
				: ECurveTangentMode::Auto;
			Key.ArriveTangent = Index < ArriveTangents.size() ? ArriveTangents[Index] : 0.0f;
			Key.LeaveTangent = Index < LeaveTangents.size() ? LeaveTangents[Index] : 0.0f;
			Value.Curve.Keys.push_back(Key);
		}
		Value.Curve.SortKeys();
	}

	return Ar;
}

FArchive& operator<<(FArchive& Ar, FAnimationCurveData& Value)
{
	Ar << "FloatCurves" << Value.FloatCurves;
	return Ar;
}

FArchive& operator<<(FArchive& Ar, FAnimNotifyStateEvent& Value)
{
	SerializeAnimNotifyEvent(Ar, Value, 4);
	return Ar;
}

FArchive& operator<<(FArchive& Ar, FAnimNotifyTrack& Value)
{
	Ar << "TrackName" << Value.TrackName;
	Ar << "Notifies" << Value.Notifies;
	return Ar;
}

void FAnimSequenceAssetPayload::Serialize(FArchive& Ar, int32 PayloadVersion)
{
	Ar << "TargetSkeletalMeshPath" << TargetSkeletalMeshPath;
	if (PayloadVersion >= 2)
	{
		Ar << "SourceStackName" << SourceStackName;
	}
	Ar << "SourceAnimStackIndex" << SourceAnimStackIndex;

	if (Ar.IsLoading() && DataModel == nullptr)
	{
		DataModel = UObjectManager::Get().CreateObject<UAnimDataModel>();
	}

	UAnimDataModel EmptyDataModel;
	UAnimDataModel* ModelToSerialize = DataModel ? DataModel : &EmptyDataModel;
	ModelToSerialize->Serialize(Ar, PayloadVersion);

	if (PayloadVersion >= 3)
	{
		SerializeAnimNotifyArray(Ar, Notifies, PayloadVersion);
	}
	else
	{
		Ar << "Notifies" << Notifies;
		if (Ar.IsLoading())
		{
			for (FAnimNotifyStateEvent& Notify : Notifies)
			{
				Notify.NotifyObject = CreateAnimNotifyObject(Notify.NotifyClassName);
			}
		}
	}

	if (PayloadVersion >= 4)
	{
		SerializeAnimNotifyTracks(Ar, NotifyTracks, PayloadVersion);
	}
}

const TArray<FBoneAnimationTrack>& UAnimSequenceBase::GetBoneAnimationTracks() const
{
	static const TArray<FBoneAnimationTrack> EmptyTracks = {};
	return DataModel ? DataModel->GetBoneAnimationTracks() : EmptyTracks;
}

uint32 UAnimSequenceBase::GenerateNotifyId() const
{
	uint32 MaxId = 0;
	for (const FAnimNotifyStateEvent& Notify : Notifies)
	{
		MaxId = std::max(MaxId, Notify.NotifyId);
	}
	for (const FAnimNotifyTrack& Track : NotifyTracks)
	{
		for (const FAnimNotifyStateEvent& Notify : Track.Notifies)
		{
			MaxId = std::max(MaxId, Notify.NotifyId);
		}
	}
	return MaxId + 1;
}

void UAnimSequenceBase::EnsureDefaultNotifyTrack()
{
	if (!NotifyTracks.empty())
	{
		return;
	}

	FAnimNotifyTrack Track;
	Track.TrackName = DefaultNotifyTrackName();
	Track.Notifies = Notifies;
	NotifyTracks.push_back(Track);
}

void UAnimSequenceBase::EnsureNotifyIds()
{
	for (FAnimNotifyTrack& Track : NotifyTracks)
	{
		for (FAnimNotifyStateEvent& Notify : Track.Notifies)
		{
			if (Notify.NotifyId == 0)
			{
				Notify.NotifyId = GenerateNotifyId();
			}
		}
	}
}

void UAnimSequenceBase::RebuildFlatNotifiesFromTracks()
{
	Notifies.clear();
	for (const FAnimNotifyTrack& Track : NotifyTracks)
	{
		Notifies.insert(Notifies.end(), Track.Notifies.begin(), Track.Notifies.end());
	}

	std::ranges::sort(Notifies,
		[](const FAnimNotifyStateEvent& A, const FAnimNotifyStateEvent& B)
		{
			return A.TriggerTime < B.TriggerTime;
		});
}

bool UAnimSequenceBase::FindNotifyByFlatIndex(int32 NotifyIndex, int32& OutTrackIndex, int32& OutNotifyIndex)
{
	OutTrackIndex = -1;
	OutNotifyIndex = -1;
	if (NotifyIndex < 0 || NotifyIndex >= static_cast<int32>(Notifies.size()))
	{
		return false;
	}

	const uint32 NotifyId = Notifies[NotifyIndex].NotifyId;
	if (NotifyId != 0)
	{
		for (int32 TrackIndex = 0; TrackIndex < static_cast<int32>(NotifyTracks.size()); ++TrackIndex)
		{
			TArray<FAnimNotifyStateEvent>& TrackNotifies = NotifyTracks[TrackIndex].Notifies;
			for (int32 TrackNotifyIndex = 0; TrackNotifyIndex < static_cast<int32>(TrackNotifies.size()); ++TrackNotifyIndex)
			{
				if (TrackNotifies[TrackNotifyIndex].NotifyId == NotifyId)
				{
					OutTrackIndex = TrackIndex;
					OutNotifyIndex = TrackNotifyIndex;
					return true;
				}
			}
		}
	}

	int32 LinearIndex = 0;
	for (int32 TrackIndex = 0; TrackIndex < static_cast<int32>(NotifyTracks.size()); ++TrackIndex)
	{
		TArray<FAnimNotifyStateEvent>& TrackNotifies = NotifyTracks[TrackIndex].Notifies;
		for (int32 TrackNotifyIndex = 0; TrackNotifyIndex < static_cast<int32>(TrackNotifies.size()); ++TrackNotifyIndex)
		{
			if (LinearIndex == NotifyIndex)
			{
				OutTrackIndex = TrackIndex;
				OutNotifyIndex = TrackNotifyIndex;
				return true;
			}
			++LinearIndex;
		}
	}

	return false;
}

void UAnimSequenceBase::SetNotifyTracks(const TArray<FAnimNotifyTrack>& InNotifyTracks)
{
	NotifyTracks = InNotifyTracks;
	EnsureDefaultNotifyTrack();
	EnsureNotifyIds();
	RebuildFlatNotifiesFromTracks();
}

int32 UAnimSequenceBase::AddNotifyTrack(const FName& TrackName)
{
	FAnimNotifyTrack Track;
	Track.TrackName = TrackName.IsValid() ? TrackName : DefaultNotifyTrackName();
	NotifyTracks.push_back(Track);
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
	RebuildFlatNotifiesFromTracks();
	return true;
}

int32 UAnimSequenceBase::AddNotifyEvent(int32 TrackIndex, const FAnimNotifyStateEvent& Notify)
{
	EnsureDefaultNotifyTrack();
	if (TrackIndex < 0 || TrackIndex >= static_cast<int32>(NotifyTracks.size()))
	{
		TrackIndex = 0;
	}

	FAnimNotifyStateEvent NotifyToAdd = Notify;
	if (NotifyToAdd.NotifyId == 0)
	{
		NotifyToAdd.NotifyId = GenerateNotifyId();
	}
	NotifyToAdd.NotifyObject = CreateAnimNotifyObject(NotifyToAdd.NotifyClassName);

	NotifyTracks[TrackIndex].Notifies.push_back(NotifyToAdd);
	RebuildFlatNotifiesFromTracks();
	return static_cast<int32>(NotifyTracks[TrackIndex].Notifies.size()) - 1;
}

bool UAnimSequenceBase::RemoveNotifyEvent(int32 TrackIndex, int32 NotifyIndex)
{
	if (TrackIndex < 0 || TrackIndex >= static_cast<int32>(NotifyTracks.size()))
	{
		return false;
	}
	TArray<FAnimNotifyStateEvent>& TrackNotifies = NotifyTracks[TrackIndex].Notifies;
	if (NotifyIndex < 0 || NotifyIndex >= static_cast<int32>(TrackNotifies.size()))
	{
		return false;
	}

	TrackNotifies.erase(TrackNotifies.begin() + NotifyIndex);
	RebuildFlatNotifiesFromTracks();
	return true;
}

bool UAnimSequenceBase::SetNotifyTriggerTime(int32 TrackIndex, int32 NotifyIndex, float TriggerTime)
{
	if (TrackIndex < 0 || TrackIndex >= static_cast<int32>(NotifyTracks.size()))
	{
		return false;
	}
	const TArray<FAnimNotifyStateEvent>& TrackNotifies = NotifyTracks[TrackIndex].Notifies;
	if (NotifyIndex < 0 || NotifyIndex >= static_cast<int32>(TrackNotifies.size()))
	{
		return false;
	}
	return SetNotifyTiming(TrackIndex, NotifyIndex, TriggerTime, TrackNotifies[NotifyIndex].Duration);
}

bool UAnimSequenceBase::SetNotifyTiming(int32 TrackIndex, int32 NotifyIndex, float TriggerTime, float Duration)
{
	if (TrackIndex < 0 || TrackIndex >= static_cast<int32>(NotifyTracks.size()))
	{
		return false;
	}
	TArray<FAnimNotifyStateEvent>& TrackNotifies = NotifyTracks[TrackIndex].Notifies;
	if (NotifyIndex < 0 || NotifyIndex >= static_cast<int32>(TrackNotifies.size()))
	{
		return false;
	}

	const float Length = std::max(0.0f, GetPlayLength());
	FAnimNotifyStateEvent& Notify = TrackNotifies[NotifyIndex];
	Notify.TriggerTime = std::clamp(TriggerTime, 0.0f, Length);
	Notify.Duration = std::clamp(Duration, 0.0f, std::max(0.0f, Length - Notify.TriggerTime));
	RebuildFlatNotifiesFromTracks();
	return true;
}

bool UAnimSequenceBase::SetNotifyName(int32 TrackIndex, int32 NotifyIndex, const FName& InNotifyName)
{
	if (TrackIndex < 0 || TrackIndex >= static_cast<int32>(NotifyTracks.size()))
	{
		return false;
	}

	TArray<FAnimNotifyStateEvent>& TrackNotifies = NotifyTracks[TrackIndex].Notifies;
	if (NotifyIndex < 0 || NotifyIndex >= static_cast<int32>(TrackNotifies.size()) || !InNotifyName.IsValid())
	{
		return false;
	}

	TrackNotifies[NotifyIndex].NotifyName = InNotifyName;
	RebuildFlatNotifiesFromTracks();
	return true;
}

bool UAnimSequenceBase::SetNotifyClassName(int32 TrackIndex, int32 NotifyIndex, const FString& InNotifyClassName)
{
	if (TrackIndex < 0 || TrackIndex >= static_cast<int32>(NotifyTracks.size()))
	{
		return false;
	}

	TArray<FAnimNotifyStateEvent>& TrackNotifies = NotifyTracks[TrackIndex].Notifies;
	if (NotifyIndex < 0 || NotifyIndex >= static_cast<int32>(TrackNotifies.size()))
	{
		return false;
	}

	FAnimNotifyStateEvent& Notify = TrackNotifies[NotifyIndex];
	Notify.NotifyClassName = InNotifyClassName;
	Notify.NotifyObject = CreateAnimNotifyObject(InNotifyClassName);
	RebuildFlatNotifiesFromTracks();
	return true;
}

bool UAnimSequenceBase::SetNotifyLuaEventName(int32 TrackIndex, int32 NotifyIndex, const FString& InLuaEventName)
{
	if (TrackIndex < 0 || TrackIndex >= static_cast<int32>(NotifyTracks.size()))
	{
		return false;
	}

	TArray<FAnimNotifyStateEvent>& TrackNotifies = NotifyTracks[TrackIndex].Notifies;
	if (NotifyIndex < 0 || NotifyIndex >= static_cast<int32>(TrackNotifies.size()))
	{
		return false;
	}

	TrackNotifies[NotifyIndex].LuaEventName = InLuaEventName;
	RebuildFlatNotifiesFromTracks();
	return true;
}

bool UAnimSequenceBase::SetNotifyLuaTargetScript(int32 TrackIndex, int32 NotifyIndex, const FString& InLuaTargetScript)
{
	if (TrackIndex < 0 || TrackIndex >= static_cast<int32>(NotifyTracks.size()))
	{
		return false;
	}

	TArray<FAnimNotifyStateEvent>& TrackNotifies = NotifyTracks[TrackIndex].Notifies;
	if (NotifyIndex < 0 || NotifyIndex >= static_cast<int32>(TrackNotifies.size()))
	{
		return false;
	}

	TrackNotifies[NotifyIndex].LuaTargetScript = InLuaTargetScript;
	RebuildFlatNotifiesFromTracks();
	return true;
}

bool UAnimSequenceBase::SetNotifyLuaTargetPolicy(int32 TrackIndex, int32 NotifyIndex, int32 InLuaTargetPolicy)
{
	if (TrackIndex < 0 || TrackIndex >= static_cast<int32>(NotifyTracks.size()))
	{
		return false;
	}

	TArray<FAnimNotifyStateEvent>& TrackNotifies = NotifyTracks[TrackIndex].Notifies;
	if (NotifyIndex < 0 || NotifyIndex >= static_cast<int32>(TrackNotifies.size()))
	{
		return false;
	}

	TrackNotifies[NotifyIndex].LuaTargetPolicy = std::clamp(InLuaTargetPolicy, 0, 2);
	RebuildFlatNotifiesFromTracks();
	return true;
}

void UAnimSequenceBase::AddNotify(float InTriggerTime, const FName& InNotifyName, float InDuration, const FString& InNotifyClassName)
{
	FAnimNotifyStateEvent NewNotify;

	const float Length = std::max(0.0f, GetPlayLength());
	NewNotify.NotifyId = GenerateNotifyId();
	NewNotify.TriggerTime = std::clamp(InTriggerTime, 0.0f, Length);
	NewNotify.Duration = std::clamp(InDuration, 0.0f, std::max(0.0f, Length - NewNotify.TriggerTime));
	NewNotify.NotifyName = InNotifyName;
	NewNotify.NotifyClassName = InNotifyClassName;
	NewNotify.LuaEventName = InNotifyName.ToString();
	NewNotify.NotifyObject = CreateAnimNotifyObject(NewNotify.NotifyClassName);

	AddNotifyEvent(0, NewNotify);
}

void UAnimSequenceBase::ClearNotifies()
{
	Notifies.clear();
	NotifyTracks.clear();
}

bool UAnimSequenceBase::RemoveNotifyAt(int32 NotifyIndex)
{
	int32 TrackIndex = -1;
	int32 TrackNotifyIndex = -1;
	if (!FindNotifyByFlatIndex(NotifyIndex, TrackIndex, TrackNotifyIndex))
	{
		return false;
	}

	return RemoveNotifyEvent(TrackIndex, TrackNotifyIndex);
}

bool UAnimSequenceBase::SetNotifyName(int32 NotifyIndex, const FName& InNotifyName)
{
	int32 TrackIndex = -1;
	int32 TrackNotifyIndex = -1;
	if (!FindNotifyByFlatIndex(NotifyIndex, TrackIndex, TrackNotifyIndex))
	{
		return false;
	}

	if (!InNotifyName.IsValid())
	{
		return false;
	}

	NotifyTracks[TrackIndex].Notifies[TrackNotifyIndex].NotifyName = InNotifyName;
	RebuildFlatNotifiesFromTracks();
	return true;
}

bool UAnimSequenceBase::SetNotifyClassName(int32 NotifyIndex, const FString& InNotifyClassName)
{
	int32 TrackIndex = -1;
	int32 TrackNotifyIndex = -1;
	if (!FindNotifyByFlatIndex(NotifyIndex, TrackIndex, TrackNotifyIndex))
	{
		return false;
	}

	FAnimNotifyStateEvent& Notify = NotifyTracks[TrackIndex].Notifies[TrackNotifyIndex];
	Notify.NotifyClassName = InNotifyClassName;
	Notify.NotifyObject = CreateAnimNotifyObject(InNotifyClassName);
	RebuildFlatNotifiesFromTracks();
	return true;
}

bool UAnimSequenceBase::SetNotifyLuaEventName(int32 NotifyIndex, const FString& InLuaEventName)
{
	int32 TrackIndex = -1;
	int32 TrackNotifyIndex = -1;
	if (!FindNotifyByFlatIndex(NotifyIndex, TrackIndex, TrackNotifyIndex))
	{
		return false;
	}

	NotifyTracks[TrackIndex].Notifies[TrackNotifyIndex].LuaEventName = InLuaEventName;
	RebuildFlatNotifiesFromTracks();
	return true;
}

bool UAnimSequenceBase::SetNotifyLuaTargetScript(int32 NotifyIndex, const FString& InLuaTargetScript)
{
	int32 TrackIndex = -1;
	int32 TrackNotifyIndex = -1;
	if (!FindNotifyByFlatIndex(NotifyIndex, TrackIndex, TrackNotifyIndex))
	{
		return false;
	}

	NotifyTracks[TrackIndex].Notifies[TrackNotifyIndex].LuaTargetScript = InLuaTargetScript;
	RebuildFlatNotifiesFromTracks();
	return true;
}

bool UAnimSequenceBase::SetNotifyLuaTargetPolicy(int32 NotifyIndex, int32 InLuaTargetPolicy)
{
	int32 TrackIndex = -1;
	int32 TrackNotifyIndex = -1;
	if (!FindNotifyByFlatIndex(NotifyIndex, TrackIndex, TrackNotifyIndex))
	{
		return false;
	}

	NotifyTracks[TrackIndex].Notifies[TrackNotifyIndex].LuaTargetPolicy = std::clamp(InLuaTargetPolicy, 0, 2);
	RebuildFlatNotifiesFromTracks();
	return true;
}

bool UAnimSequenceBase::SetNotifyTriggerTime(int32 NotifyIndex, float InTriggerTime)
{
	int32 TrackIndex = -1;
	int32 TrackNotifyIndex = -1;
	if (!FindNotifyByFlatIndex(NotifyIndex, TrackIndex, TrackNotifyIndex))
	{
		return false;
	}

	return SetNotifyTiming(TrackIndex, TrackNotifyIndex, InTriggerTime, NotifyTracks[TrackIndex].Notifies[TrackNotifyIndex].Duration);
}

bool UAnimSequenceBase::SetNotifyDuration(int32 NotifyIndex, float InDuration)
{
	int32 TrackIndex = -1;
	int32 TrackNotifyIndex = -1;
	if (!FindNotifyByFlatIndex(NotifyIndex, TrackIndex, TrackNotifyIndex))
	{
		return false;
	}

	return SetNotifyTiming(TrackIndex, TrackNotifyIndex, NotifyTracks[TrackIndex].Notifies[TrackNotifyIndex].TriggerTime, InDuration);
}

bool UAnimSequenceBase::SetNotifyTimeRange(int32 NotifyIndex, float InTriggerTime, float InDuration)
{
	int32 TrackIndex = -1;
	int32 TrackNotifyIndex = -1;
	if (!FindNotifyByFlatIndex(NotifyIndex, TrackIndex, TrackNotifyIndex))
	{
		return false;
	}

	return SetNotifyTiming(TrackIndex, TrackNotifyIndex, InTriggerTime, InDuration);
}

bool UAnimSequenceBase::MoveNotifyAt(int32 NotifyIndex, float InTriggerTime, int32* OutNewIndex)
{
	int32 TrackIndex = -1;
	int32 TrackNotifyIndex = -1;
	if (!FindNotifyByFlatIndex(NotifyIndex, TrackIndex, TrackNotifyIndex))
	{
		return false;
	}

	const float Length = std::max(0.0f, GetPlayLength());
	FAnimNotifyStateEvent MovedNotify = NotifyTracks[TrackIndex].Notifies[TrackNotifyIndex];
	MovedNotify.TriggerTime = std::clamp(InTriggerTime, 0.0f, Length);
	MovedNotify.Duration = std::clamp(MovedNotify.Duration, 0.0f, std::max(0.0f, Length - MovedNotify.TriggerTime));

	NotifyTracks[TrackIndex].Notifies.erase(NotifyTracks[TrackIndex].Notifies.begin() + TrackNotifyIndex);
	TArray<FAnimNotifyStateEvent>& TrackNotifies = NotifyTracks[TrackIndex].Notifies;
	const auto InsertIt = std::lower_bound(
		TrackNotifies.begin(),
		TrackNotifies.end(),
		MovedNotify.TriggerTime,
		[](const FAnimNotifyStateEvent& Notify, float TriggerTime)
		{
			return Notify.TriggerTime < TriggerTime;
		});

	TrackNotifies.insert(InsertIt, MovedNotify);
	RebuildFlatNotifiesFromTracks();
	if (OutNewIndex)
	{
		for (int32 FlatIndex = 0; FlatIndex < static_cast<int32>(Notifies.size()); ++FlatIndex)
		{
			if (Notifies[FlatIndex].NotifyId == MovedNotify.NotifyId)
			{
				*OutNewIndex = FlatIndex;
				break;
			}
		}
	}

	return true;
}

namespace
{
	int32 GetTrackKeyCount(const FRawAnimSequenceTrack& Track)
	{
		return static_cast<int32>(std::max({
			Track.PosKeys.size(),
			Track.RotKeys.size(),
			Track.ScaleKeys.size()}));
	}

	FVector3f SampleVectorKey(const TArray<FVector3f>& Keys, int32 KeyIndex, int32 NextKeyIndex, float Alpha, const FVector3f& DefaultValue)
	{
		if (Keys.empty())
		{
			return DefaultValue;
		}

		const int32 LastIndex = static_cast<int32>(Keys.size()) - 1;
		const FVector3f& Start = Keys[std::clamp(KeyIndex, 0, LastIndex)];
		const FVector3f& End = Keys[std::clamp(NextKeyIndex, 0, LastIndex)];
		return Start + (End - Start) * Alpha;
	}

	FQuat4f SampleQuatKey(const TArray<FQuat4f>& Keys, int32 KeyIndex, int32 NextKeyIndex, float Alpha, const FQuat4f& DefaultValue)
	{
		if (Keys.empty())
		{
			return DefaultValue;
		}

		const int32 LastIndex = static_cast<int32>(Keys.size()) - 1;
		const FQuat4f& Start = Keys[std::clamp(KeyIndex, 0, LastIndex)];
		const FQuat4f& End = Keys[std::clamp(NextKeyIndex, 0, LastIndex)];
		return FQuat4f::Slerp(Start, End, Alpha).GetNormalized();
	}
}

float UAnimSequence::GetPlayLength() const
{
	return DataModel ? DataModel->GetPlayLength() : 0.0f;
}

bool UAnimSequence::EvaluateCurve(const FName& CurveName, float TimeSeconds, float& OutValue) const
{
	if (!DataModel)
	{
		return false;
	}

	const FAnimationCurveData& CurveData = DataModel->GetCurveData();
	for (const FAnimCurveTrack& CurveTrack : CurveData.FloatCurves)
	{
		if (CurveTrack.CurveName == CurveName)
		{
			OutValue = CurveTrack.Curve.Evaluate(TimeSeconds);
			return true;
		}
	}

	return false;
}

//3-2. Evaluate Phase(Tick Component의 USkeletalMeshComponent::ApplyAnimationPose로 이어짐)
//진행된 시간에 맞춰 두 샘플링된 키 프레임 사이를 Interpolation, pos 계산
bool UAnimSequence::GetAnimationPose(float Time, FPoseContext& OutPose) const
{
	if (!DataModel)
	{
		return false;
	}

	const TArray<FBoneAnimationTrack>& Tracks = DataModel->GetBoneAnimationTracks();
	if (Tracks.empty())
	{
		return false;
	}

	if (OutPose.LocalPose.empty() && !OutPose.BindPose.empty())
	{
		OutPose.LocalPose = OutPose.BindPose;
	}

	if (OutPose.LocalPose.empty())
	{
		return false;
	}

	int32 KeyCount = DataModel->GetNumberOfKeys();
	if (KeyCount <= 0)
	{
		for (const FBoneAnimationTrack& Track : Tracks)
		{
			KeyCount = std::max(KeyCount, GetTrackKeyCount(Track.InternalTrack));
		}
	}

	if (KeyCount <= 0)
	{
		return true;
	}

	const float Length = std::max(0.0f, DataModel->GetPlayLength());
	const float ClampedTime = Length > 0.0f ? std::clamp(Time, 0.0f, Length) : 0.0f;
	const float KeyPosition = (Length > 0.0f && KeyCount > 1)
		? (ClampedTime / Length) * static_cast<float>(KeyCount - 1)
		: 0.0f;

	const int32 KeyIndex = std::clamp(static_cast<int32>(std::floor(KeyPosition)), 0, KeyCount - 1);
	const int32 NextKeyIndex = std::clamp(KeyIndex + 1, 0, KeyCount - 1);
	const float Alpha = std::clamp(KeyPosition - static_cast<float>(KeyIndex), 0.0f, 1.0f);

	const int32 PoseCount = static_cast<int32>(OutPose.LocalPose.size());
	const int32 TrackCount = static_cast<int32>(Tracks.size());
	for (int32 TrackIndex = 0; TrackIndex < TrackCount; ++TrackIndex)
	{
		int32 BoneIndex = TrackIndex;
		if (TrackIndex < static_cast<int32>(OutPose.TrackToBoneMap.size()))
		{
			BoneIndex = OutPose.TrackToBoneMap[TrackIndex];
		}

		if (BoneIndex < 0 || BoneIndex >= PoseCount)
		{
			continue;
		}

		const FRawAnimSequenceTrack& RawTrack = Tracks[TrackIndex].InternalTrack;

		FTransform BindTransform;
		if (OutPose.BindPose.size() == OutPose.LocalPose.size())
		{
			BindTransform = FTransform(OutPose.BindPose[BoneIndex]);
		}

		const FVector3f Translation = SampleVectorKey(RawTrack.PosKeys, KeyIndex, NextKeyIndex, Alpha, BindTransform.GetTranslation());
		const FQuat4f Rotation = SampleQuatKey(RawTrack.RotKeys, KeyIndex, NextKeyIndex, Alpha, BindTransform.GetRotation());
		const FVector3f Scale = SampleVectorKey(RawTrack.ScaleKeys, KeyIndex, NextKeyIndex, Alpha, BindTransform.GetScale3D());

		OutPose.LocalPose[BoneIndex] = FTransform(Rotation, Translation, Scale).ToMatrixWithScale();
	}

	return true;
}
