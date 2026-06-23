#include "ActorSequence.h"

#include "Component/ActorComponent.h"
#include "Component/SceneComponent.h"
#include "FloatCurve/FloatCurveAsset.h"
#include "FloatCurve/FloatCurveManager.h"
#include "GameFramework/AActor.h"
#include "Math/FloatCurve.h"
#include "Object/GarbageCollection.h"
#include "Object/Reflection/UStruct.h"
#include "SimpleJSON/json.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>

namespace
{
	float ResolveSequenceStartTime(const UActorSequence* Sequence)
	{
		return IsValid(Sequence) ? Sequence->GetStartTime() : 0.0f;
	}

	float ResolveSequenceEndTime(const UActorSequence* Sequence)
	{
		return IsValid(Sequence) ? Sequence->GetEndTime() : 0.0f;
	}

	FString MakeActorSequenceId(const char* Prefix)
	{
		static uint32 Counter = 1;
		return FString(Prefix ? Prefix : "Seq") + "_" + std::to_string(Counter++);
	}

	bool EqualsPropertyName(const FProperty& Property, const FString& Name)
	{
		if (Name.empty())
		{
			return false;
		}
		if (Property.Name && Name == Property.Name)
		{
			return true;
		}
		return Property.DisplayName && Name == Property.DisplayName;
	}

	UActorComponent* FindComponentByBinding(AActor* Owner, const FSequenceObjectBinding& Binding)
	{
		if (!IsValid(Owner) || Binding.TargetType != EActorSequenceBindingTarget::Component)
		{
			return nullptr;
		}

		UActorComponent* NameFallback = nullptr;
		for (UActorComponent* Component : Owner->GetComponents())
		{
			if (!IsValid(Component))
			{
				continue;
			}

			const FString& ComponentGuid = Component->GetPersistentGuid();
			if (!Binding.TargetComponentGuid.empty() && ComponentGuid == Binding.TargetComponentGuid)
			{
				return Component;
			}

			if (!Binding.TargetObjectName.empty() && Component->GetFName().ToString() == Binding.TargetObjectName)
			{
				NameFallback = Component;
			}
		}

		return NameFallback;
	}

	void RefreshBindingFromComponent(FSequenceObjectBinding& Binding, UActorComponent* Component)
	{
		if (!IsValid(Component))
		{
			return;
		}

		Binding.TargetType = EActorSequenceBindingTarget::Component;
		Binding.TargetObjectName = Component->GetFName().ToString();
		Binding.TargetComponentGuid = Component->EnsurePersistentGuid();
	}

	FString ToLowerAscii(FString Value)
	{
		std::transform(Value.begin(), Value.end(), Value.begin(), [](unsigned char Ch)
		{
			return static_cast<char>(std::tolower(Ch));
		});
		return Value;
	}

	const FProperty* FindSequencerPropertyByName(UObject* TargetObject, const FString& PropertyName)
	{
		if (!IsValid(TargetObject) || !TargetObject->GetClass())
		{
			return nullptr;
		}

		TArray<const FProperty*> Properties;
		TargetObject->GetClass()->GetPropertyRefs(Properties);
		for (const FProperty* Property : Properties)
		{
			if (Property && Property->IsSequencerScalar() && EqualsPropertyName(*Property, PropertyName))
			{
				return Property;
			}
		}
		return nullptr;
	}

	FString DefaultChannelNameForProperty(const FProperty& Property)
	{
		switch (Property.GetType())
		{
		case EPropertyType::Vec3:
		case EPropertyType::Vec4:
		case EPropertyType::Color4:
			return "x";
		case EPropertyType::Rotator:
			return "pitch";
		default:
			return "Value";
		}
	}

	bool HasLegacyAxisHint(const FString& Text, char Axis)
	{
		const FString Lower = ToLowerAscii(Text);
		const FString NeedleUnderscore = FString("_") + FString(1, Axis);
		const FString NeedleDash = FString("-") + FString(1, Axis);
		const FString NeedleSpace = FString(" ") + FString(1, Axis);
		return Lower.find(NeedleUnderscore) != FString::npos
			|| Lower.find(NeedleDash) != FString::npos
			|| Lower.find(NeedleSpace) != FString::npos;
	}

	FString InferChannelNameForProperty(
		const FProperty& Property,
		const FActorSequenceTrack& Track,
		const FActorSequenceSection& Section,
		const FActorSequenceChannel& Channel)
	{
		const FString HintText = Section.SectionId + " " + Track.PropertyName + " " + Channel.ChannelName;
		switch (Property.GetType())
		{
		case EPropertyType::Vec3:
		case EPropertyType::Vec4:
		case EPropertyType::Color4:
			if (HasLegacyAxisHint(HintText, 'z')) return "z";
			if (HasLegacyAxisHint(HintText, 'y')) return "y";
			if (HasLegacyAxisHint(HintText, 'x')) return "x";
			break;
		case EPropertyType::Rotator:
			if (HasLegacyAxisHint(HintText, 'z')) return "yaw";
			if (HasLegacyAxisHint(HintText, 'y')) return "pitch";
			if (HasLegacyAxisHint(HintText, 'x')) return "roll";
			break;
		default:
			break;
		}
		return DefaultChannelNameForProperty(Property);
	}

	bool CanReadSequenceChannel(UObject* TargetObject, const FProperty& Property, const FString& ChannelName)
	{
		float TestValue = 0.0f;
		return Property.ReadScalarChannelValue(TargetObject, ChannelName, TestValue);
	}

	void RepairSectionForProperty(
		UObject* TargetObject,
		const FProperty& Property,
		FActorSequenceTrack& Track,
		FActorSequenceSection& Section)
	{
		Section.Duration = std::max(0.001f, Section.Duration);
		Section.PlayRate = std::max(0.001f, Section.PlayRate);

		for (FActorSequenceChannel& Channel : Section.Channels)
		{
			if (!CanReadSequenceChannel(TargetObject, Property, Channel.ChannelName))
			{
				Channel.ChannelName = InferChannelNameForProperty(Property, Track, Section, Channel);
			}
		}
	}

	UObject* ResolveBindingObjectForRepair(AActor* OwnerActor, const FSequenceObjectBinding& Binding)
	{
		if (!IsValid(OwnerActor))
		{
			return nullptr;
		}
		if (Binding.TargetType == EActorSequenceBindingTarget::OwnerActor)
		{
			return OwnerActor;
		}
		return FindComponentByBinding(OwnerActor, Binding);
	}

	json::JSON CurveToJson(const UFloatCurveAsset* CurveAsset)
	{
		json::JSON Root = json::Object();
		if (!CurveAsset)
		{
			return Root;
		}

		const FFloatCurve& Curve = CurveAsset->GetCurve();
		Root["DefaultValue"] = static_cast<double>(Curve.DefaultValue);
		Root["PreExtrapMode"] = static_cast<int>(Curve.PreExtrapMode);
		Root["PostExtrapMode"] = static_cast<int>(Curve.PostExtrapMode);

		json::JSON Keys = json::Array();
		for (const FCurveKey& Key : Curve.Keys)
		{
			json::JSON KeyJson = json::Object();
			KeyJson["Time"] = static_cast<double>(Key.Time);
			KeyJson["Value"] = static_cast<double>(Key.Value);
			KeyJson["ArriveTangent"] = static_cast<double>(Key.ArriveTangent);
			KeyJson["LeaveTangent"] = static_cast<double>(Key.LeaveTangent);
			KeyJson["InterpMode"] = static_cast<int>(Key.InterpMode);
			KeyJson["TangentMode"] = static_cast<int>(Key.TangentMode);
			Keys.append(KeyJson);
		}
		Root["Keys"] = Keys;
		return Root;
	}

	void CurveFromJson(json::JSON& Root, UFloatCurveAsset* CurveAsset)
	{
		if (!CurveAsset)
		{
			return;
		}

		FFloatCurve& Curve = CurveAsset->GetCurve();
		Curve.Reset();
		if (Root.hasKey("DefaultValue")) Curve.DefaultValue = static_cast<float>(Root["DefaultValue"].ToFloat());
		if (Root.hasKey("PreExtrapMode")) Curve.PreExtrapMode = static_cast<ECurveExtrapMode>(Root["PreExtrapMode"].ToInt());
		if (Root.hasKey("PostExtrapMode")) Curve.PostExtrapMode = static_cast<ECurveExtrapMode>(Root["PostExtrapMode"].ToInt());

		if (Root.hasKey("Keys"))
		{
			for (auto& KeyJson : Root["Keys"].ArrayRange())
			{
				FCurveKey Key;
				Key.Time = KeyJson.hasKey("Time") ? static_cast<float>(KeyJson["Time"].ToFloat()) : 0.0f;
				Key.Value = KeyJson.hasKey("Value") ? static_cast<float>(KeyJson["Value"].ToFloat()) : 0.0f;
				Key.ArriveTangent = KeyJson.hasKey("ArriveTangent") ? static_cast<float>(KeyJson["ArriveTangent"].ToFloat()) : 0.0f;
				Key.LeaveTangent = KeyJson.hasKey("LeaveTangent") ? static_cast<float>(KeyJson["LeaveTangent"].ToFloat()) : 0.0f;
				Key.InterpMode = KeyJson.hasKey("InterpMode")
					? static_cast<ECurveInterpMode>(KeyJson["InterpMode"].ToInt())
					: ECurveInterpMode::Linear;
				Key.TangentMode = KeyJson.hasKey("TangentMode")
					? static_cast<ECurveTangentMode>(KeyJson["TangentMode"].ToInt())
					: ECurveTangentMode::Auto;
				Curve.Keys.push_back(Key);
			}
		}

		Curve.SortKeys();
	}

	json::JSON PlaybackToJson(const FCurvePlaybackDesc& Playback)
	{
		json::JSON Root = json::Object();
		const FString CurvePath = !Playback.CurveAssetPath.empty()
			? Playback.CurveAssetPath
			: (Playback.Curve ? Playback.Curve->GetSourcePath() : FString());
		Root["CurveAssetPath"] = CurvePath;
		Root["ApplyMode"] = static_cast<int>(Playback.ApplyMode);
		Root["TimeMappingMode"] = static_cast<int>(Playback.TimeMappingMode);

		if (CurvePath.empty() && Playback.Curve)
		{
			Root["CurveData"] = CurveToJson(Playback.Curve);
		}
		return Root;
	}

	void PlaybackFromJson(json::JSON& Root, FCurvePlaybackDesc& Playback, UActorSequence* Owner)
	{
		Playback.Curve = nullptr;
		Playback.CurveAssetPath = Root.hasKey("CurveAssetPath") ? Root["CurveAssetPath"].ToString() : FString();
		Playback.ApplyMode = Root.hasKey("ApplyMode")
			? static_cast<ECurveApplyMode>(Root["ApplyMode"].ToInt())
			: ECurveApplyMode::Absolute;
		Playback.TimeMappingMode = Root.hasKey("TimeMappingMode")
			? static_cast<ECurveTimeMappingMode>(Root["TimeMappingMode"].ToInt())
			: ECurveTimeMappingMode::Seconds;

		if (Root.hasKey("CurveData"))
		{
			Playback.Curve = Owner ? Owner->CreateInlineCurve() : UObjectManager::Get().CreateObject<UFloatCurveAsset>();
			CurveFromJson(Root["CurveData"], Playback.Curve);
		}
		else if (!Playback.CurveAssetPath.empty())
		{
			Playback.Curve = FFloatCurveManager::Get().Load(Playback.CurveAssetPath);
		}
	}

	json::JSON ChannelToJson(const FActorSequenceChannel& Channel)
	{
		json::JSON Root = json::Object();
		Root["ChannelName"] = Channel.ChannelName;
		Root["Playback"] = PlaybackToJson(Channel.Playback);
		return Root;
	}

	void ChannelFromJson(json::JSON& Root, FActorSequenceChannel& Channel, UActorSequence* Owner)
	{
		Channel.ChannelName = Root.hasKey("ChannelName") ? Root["ChannelName"].ToString() : "Value";
		if (Root.hasKey("Playback"))
		{
			PlaybackFromJson(Root["Playback"], Channel.Playback, Owner);
		}
	}

	json::JSON SectionToJson(const FActorSequenceSection& Section)
	{
		json::JSON Root = json::Object();
		Root["SectionId"] = Section.SectionId;
		Root["StartTime"] = static_cast<double>(Section.StartTime);
		Root["Duration"] = static_cast<double>(Section.Duration);
		Root["PlayRate"] = static_cast<double>(Section.PlayRate);
		Root["Loop"] = Section.bLoop;

		json::JSON Channels = json::Array();
		for (const FActorSequenceChannel& Channel : Section.Channels)
		{
			Channels.append(ChannelToJson(Channel));
		}
		Root["Channels"] = Channels;
		return Root;
	}

	void SectionFromJson(json::JSON& Root, FActorSequenceSection& Section, UActorSequence* Owner)
	{
		Section.SectionId = Root.hasKey("SectionId") ? Root["SectionId"].ToString() : MakeActorSequenceId("Section");
		Section.StartTime = Root.hasKey("StartTime") ? static_cast<float>(Root["StartTime"].ToFloat()) : 0.0f;
		Section.Duration = Root.hasKey("Duration") ? static_cast<float>(Root["Duration"].ToFloat()) : 1.0f;
		Section.PlayRate = Root.hasKey("PlayRate") ? static_cast<float>(Root["PlayRate"].ToFloat()) : 1.0f;
		Section.bLoop = Root.hasKey("Loop") ? Root["Loop"].ToBool() : false;
		Section.Channels.clear();

		if (Root.hasKey("Channels"))
		{
			for (auto& ChannelJson : Root["Channels"].ArrayRange())
			{
				FActorSequenceChannel Channel;
				ChannelFromJson(ChannelJson, Channel, Owner);
				Section.Channels.push_back(Channel);
			}
		}
	}
}

void UActorSequence::SetDuration(float InDuration)
{
	Duration = std::max(0.001f, InDuration);
}

void UActorSequence::SetStartTime(float InStartTime)
{
	const float EndTime = GetEndTime();
	StartTime = std::max(0.0f, InStartTime);
	Duration = std::max(0.001f, EndTime - StartTime);
}

void UActorSequence::SetPlaybackRange(float InStartTime, float InEndTime)
{
	StartTime = std::max(0.0f, InStartTime);
	Duration = std::max(0.001f, InEndTime - StartTime);
}

void UActorSequence::Clear()
{
	Bindings.clear();
	StartTime = 0.0f;
	Duration = 1.0f;
}

UFloatCurveAsset* UActorSequence::CreateInlineCurve()
{
	return UObjectManager::Get().CreateObject<UFloatCurveAsset>(this);
}

bool UActorSequence::AddFloatTrack(
	UObject* TargetObject,
	const FString& PropertyName,
	const FString& ChannelName,
	float StartTime,
	float InDuration,
	UFloatCurveAsset* Curve,
	const FString& CurveAssetPath)
{
	if (!IsValid(TargetObject))
	{
		return false;
	}

	const FString EffectiveChannelName = ChannelName.empty() ? "Value" : ChannelName;
	const FProperty* Property = FindSequencerProperty(TargetObject, PropertyName, EffectiveChannelName);
	if (!Property)
	{
		return false;
	}

	float CurrentValue = 0.0f;
	if (!Property->ReadScalarChannelValue(TargetObject, EffectiveChannelName, CurrentValue))
	{
		return false;
	}

	FActorSequenceBinding* Binding = FindOrAddBinding(TargetObject);
	if (!Binding)
	{
		return false;
	}

	FActorSequenceTrack Track;
	Track.PropertyName = Property->Name ? Property->Name : PropertyName;
	Track.TrackType = TrackTypeForProperty(*Property);

	FActorSequenceSection Section;
	Section.SectionId = MakeActorSequenceId("Section");
	Section.StartTime = std::max(0.0f, StartTime);
	Section.Duration = std::max(0.0f, InDuration);
	Section.PlayRate = 1.0f;
	Section.bLoop = false;

	FActorSequenceChannel Channel;
	Channel.ChannelName = EffectiveChannelName;
	Channel.Playback.CurveAssetPath = CurveAssetPath;
	Channel.Playback.Curve = Curve ? Curve : (!CurveAssetPath.empty() ? FFloatCurveManager::Get().Load(CurveAssetPath) : nullptr);
	if (!Channel.Playback.Curve)
	{
		Channel.Playback.Curve = CreateInlineCurve();
		Channel.Playback.Curve->GetCurve().AddKey(0.0f, CurrentValue);
		Channel.Playback.Curve->GetCurve().AddKey(std::max(Section.Duration, 0.001f), CurrentValue);
		Channel.Playback.Curve->GetCurve().SortKeys();
		Channel.Playback.Curve->GetCurve().AutoSetTangents();
	}

	Section.Channels.push_back(Channel);
	Track.Sections.push_back(Section);
	Binding->Tracks.push_back(Track);
	Duration = std::max(Duration, Section.StartTime + Section.Duration - StartTime);
	return true;
}

FString UActorSequence::ExportToJsonString() const
{
	json::JSON Root = json::Object();
	Root["StartTime"] = static_cast<double>(StartTime);
	Root["Duration"] = static_cast<double>(Duration);

	json::JSON BindingsJson = json::Array();
	for (const FActorSequenceBinding& Binding : Bindings)
	{
		json::JSON BindingJson = json::Object();
		BindingJson["BindingId"] = Binding.Binding.BindingId;
		BindingJson["TargetType"] = static_cast<int>(Binding.Binding.TargetType);
		BindingJson["TargetObjectName"] = Binding.Binding.TargetObjectName;
		BindingJson["TargetComponentGuid"] = Binding.Binding.TargetComponentGuid;

		json::JSON TracksJson = json::Array();
		for (const FActorSequenceTrack& Track : Binding.Tracks)
		{
			json::JSON TrackJson = json::Object();
			TrackJson["PropertyName"] = Track.PropertyName;
			TrackJson["TrackType"] = static_cast<int>(Track.TrackType);

			json::JSON SectionsJson = json::Array();
			for (const FActorSequenceSection& Section : Track.Sections)
			{
				SectionsJson.append(SectionToJson(Section));
			}
			TrackJson["Sections"] = SectionsJson;
			TracksJson.append(TrackJson);
		}
		BindingJson["Tracks"] = TracksJson;
		BindingsJson.append(BindingJson);
	}
	Root["Bindings"] = BindingsJson;
	return Root.dump();
}

bool UActorSequence::ImportFromJsonString(const FString& JsonText)
{
	Clear();
	if (JsonText.empty())
	{
		return true;
	}

	json::JSON Root = json::JSON::Load(JsonText);
	StartTime = Root.hasKey("StartTime") ? static_cast<float>(Root["StartTime"].ToFloat()) : 0.0f;
	Duration = Root.hasKey("Duration") ? static_cast<float>(Root["Duration"].ToFloat()) : 1.0f;

	if (Root.hasKey("Bindings"))
	{
		for (auto& BindingJson : Root["Bindings"].ArrayRange())
		{
			FActorSequenceBinding Binding;
			Binding.Binding.BindingId = BindingJson.hasKey("BindingId")
				? BindingJson["BindingId"].ToString()
				: MakeActorSequenceId("Binding");
			Binding.Binding.TargetType = BindingJson.hasKey("TargetType")
				? static_cast<EActorSequenceBindingTarget>(BindingJson["TargetType"].ToInt())
				: EActorSequenceBindingTarget::OwnerActor;
			Binding.Binding.TargetObjectName = BindingJson.hasKey("TargetObjectName")
				? BindingJson["TargetObjectName"].ToString()
				: FString();
			Binding.Binding.TargetComponentGuid = BindingJson.hasKey("TargetComponentGuid")
				? BindingJson["TargetComponentGuid"].ToString()
				: FString();

			if (BindingJson.hasKey("Tracks"))
			{
				for (auto& TrackJson : BindingJson["Tracks"].ArrayRange())
				{
					FActorSequenceTrack Track;
					Track.PropertyName = TrackJson.hasKey("PropertyName") ? TrackJson["PropertyName"].ToString() : FString();
					Track.TrackType = TrackJson.hasKey("TrackType")
						? static_cast<EActorSequenceTrackType>(TrackJson["TrackType"].ToInt())
						: EActorSequenceTrackType::Scalar;

					if (TrackJson.hasKey("Sections"))
					{
						for (auto& SectionJson : TrackJson["Sections"].ArrayRange())
						{
							FActorSequenceSection Section;
							SectionFromJson(SectionJson, Section, this);
							Track.Sections.push_back(Section);
						}
					}
					Binding.Tracks.push_back(Track);
				}
			}
			Bindings.push_back(Binding);
		}
	}

	ClampDurationFromSections();
	return true;
}

void UActorSequence::AddReferencedObjects(FReferenceCollector& Collector)
{
	UObject::AddReferencedObjects(Collector);
	for (FActorSequenceBinding& Binding : Bindings)
	{
		for (FActorSequenceTrack& Track : Binding.Tracks)
		{
			for (FActorSequenceSection& Section : Track.Sections)
			{
				for (FActorSequenceChannel& Channel : Section.Channels)
				{
					Collector.AddReferencedObject(Channel.Playback.Curve, "ActorSequence.Curve");
				}
			}
		}
	}
}

void UActorSequence::RefreshBindingTargetCache(AActor* OwnerActor)
{
	if (!IsValid(OwnerActor))
	{
		return;
	}

	for (FActorSequenceBinding& Binding : Bindings)
	{
		if (Binding.Binding.TargetType == EActorSequenceBindingTarget::OwnerActor)
		{
			USceneComponent* RootComponent = OwnerActor->GetRootComponent();
			int32 OwnerPropertyMatches = 0;
			int32 RootPropertyMatches = 0;

			for (const FActorSequenceTrack& Track : Binding.Tracks)
			{
				if (FindSequencerPropertyByName(OwnerActor, Track.PropertyName))
				{
					++OwnerPropertyMatches;
				}
				else if (RootComponent && FindSequencerPropertyByName(RootComponent, Track.PropertyName))
				{
					++RootPropertyMatches;
				}
			}

			if (RootComponent && RootPropertyMatches > 0 && OwnerPropertyMatches == 0)
			{
				RefreshBindingFromComponent(Binding.Binding, RootComponent);
			}
			else
			{
				Binding.Binding.TargetObjectName = OwnerActor->GetFName().ToString();
				Binding.Binding.TargetComponentGuid.clear();
			}
		}
		else if (UActorComponent* Component = FindComponentByBinding(OwnerActor, Binding.Binding))
		{
			RefreshBindingFromComponent(Binding.Binding, Component);
		}

		UObject* TargetObject = ResolveBindingObjectForRepair(OwnerActor, Binding.Binding);
		if (!IsValid(TargetObject))
		{
			continue;
		}

		for (FActorSequenceTrack& Track : Binding.Tracks)
		{
			const FProperty* Property = FindSequencerPropertyByName(TargetObject, Track.PropertyName);
			if (!Property)
			{
				continue;
			}

			for (FActorSequenceSection& Section : Track.Sections)
			{
				RepairSectionForProperty(TargetObject, *Property, Track, Section);
			}
		}
	}
}

FActorSequenceBinding* UActorSequence::FindOrAddBinding(UObject* TargetObject)
{
	if (!IsValid(TargetObject))
	{
		return nullptr;
	}

	FSequenceObjectBinding NewBinding;
	if (UActorComponent* Component = Cast<UActorComponent>(TargetObject))
	{
		RefreshBindingFromComponent(NewBinding, Component);
	}
	else if (AActor* Actor = Cast<AActor>(TargetObject))
	{
		NewBinding.TargetType = EActorSequenceBindingTarget::OwnerActor;
		NewBinding.TargetObjectName = Actor->GetFName().ToString();
	}
	else
	{
		return nullptr;
	}

	for (FActorSequenceBinding& Binding : Bindings)
	{
		if (Binding.Binding.TargetType != NewBinding.TargetType)
		{
			continue;
		}

		if (NewBinding.TargetType == EActorSequenceBindingTarget::Component)
		{
			if (!NewBinding.TargetComponentGuid.empty()
				&& Binding.Binding.TargetComponentGuid == NewBinding.TargetComponentGuid)
			{
				return &Binding;
			}

			if (Binding.Binding.TargetComponentGuid.empty()
				&& Binding.Binding.TargetObjectName == NewBinding.TargetObjectName)
			{
				Binding.Binding.TargetComponentGuid = NewBinding.TargetComponentGuid;
				return &Binding;
			}
			continue;
		}

		if (Binding.Binding.TargetObjectName == NewBinding.TargetObjectName)
		{
			return &Binding;
		}
	}

	NewBinding.BindingId = MakeActorSequenceId("Binding");
	FActorSequenceBinding Binding;
	Binding.Binding = NewBinding;
	Bindings.push_back(Binding);
	return &Bindings.back();
}

const FProperty* UActorSequence::FindSequencerProperty(
	UObject* TargetObject,
	const FString& PropertyName,
	const FString& ChannelName) const
{
	if (!IsValid(TargetObject))
	{
		return nullptr;
	}

	TArray<const FProperty*> Properties;
	TargetObject->GetClass()->GetPropertyRefs(Properties);
	for (const FProperty* Property : Properties)
	{
		if (!Property || !EqualsPropertyName(*Property, PropertyName))
		{
			continue;
		}

		float TestValue = 0.0f;
		if (Property->ReadScalarChannelValue(TargetObject, ChannelName, TestValue))
		{
			return Property;
		}
	}
	return nullptr;
}

EActorSequenceTrackType UActorSequence::TrackTypeForProperty(const FProperty& Property) const
{
	switch (Property.GetType())
	{
	case EPropertyType::Vec3:
		return EActorSequenceTrackType::Vector3;
	case EPropertyType::Rotator:
		return EActorSequenceTrackType::Rotator;
	case EPropertyType::Vec4:
	case EPropertyType::Color4:
		return EActorSequenceTrackType::Vector4;
	default:
		return EActorSequenceTrackType::Scalar;
	}
}

void UActorSequence::ClampDurationFromSections()
{
	float MaxEndTime = StartTime + Duration;
	for (const FActorSequenceBinding& Binding : Bindings)
	{
		for (const FActorSequenceTrack& Track : Binding.Tracks)
		{
			for (const FActorSequenceSection& Section : Track.Sections)
			{
				MaxEndTime = std::max(MaxEndTime, Section.StartTime + std::max(0.0f, Section.Duration));
			}
		}
	}
	StartTime = std::max(0.0f, StartTime);
	Duration = std::max(0.001f, MaxEndTime - StartTime);
}

void UActorSequencePlayer::Initialize(UActorSequence* InSequence, AActor* InOwnerActor)
{
	RestoreBaseValues();
	Sequence = InSequence;
	OwnerActor.Reset(InOwnerActor);
	ResolvedChannels.clear();
	CurrentTime = ResolveSequenceStartTime(Sequence);
	bPlaying = false;
	bPaused = false;
	bResolveDirty = true;
}

void UActorSequencePlayer::SetPlaybackOptions(bool bInLooping, bool bInPauseAtEnd)
{
	bLooping = bInLooping;
	bPauseAtEnd = bInPauseAtEnd;
}

void UActorSequencePlayer::Play(bool bResetTime)
{
	if (!IsValid(Sequence))
	{
		return;
	}

	if (bResetTime)
	{
		CurrentTime = ResolveSequenceStartTime(Sequence);
	}
	else
	{
		const float StartTime = ResolveSequenceStartTime(Sequence);
		const float EndTime = ResolveSequenceEndTime(Sequence);
		if (CurrentTime < StartTime || CurrentTime >= EndTime)
		{
			CurrentTime = StartTime;
		}
	}

	if (bResolveDirty || ResolvedChannels.empty())
	{
		RebuildResolvedChannels();
	}
	bPlaying = true;
	bPaused = false;
	ApplyAtCurrentTime();
}

void UActorSequencePlayer::Pause()
{
	if (bPlaying)
	{
		bPaused = true;
	}
}

void UActorSequencePlayer::Stop(bool bRestoreBaseValues)
{
	if (bRestoreBaseValues)
	{
		RestoreBaseValues();
	}

	bPlaying = false;
	bPaused = false;
	CurrentTime = ResolveSequenceStartTime(Sequence);
	ResolvedChannels.clear();
	bResolveDirty = true;
}

void UActorSequencePlayer::Tick(float DeltaTime)
{
	if (!bPlaying || bPaused || !IsValid(Sequence))
	{
		return;
	}

	CurrentTime += DeltaTime;
	const float SequenceStart = Sequence->GetStartTime();
	const float SequenceDuration = Sequence->GetDuration();
	const float SequenceEnd = Sequence->GetEndTime();
	if (CurrentTime < SequenceStart)
	{
		CurrentTime = SequenceStart;
	}
	if (SequenceDuration > 0.0001f && CurrentTime > SequenceEnd)
	{
		if (bLooping)
		{
			CurrentTime = SequenceStart + std::fmod(CurrentTime - SequenceStart, SequenceDuration);
			if (CurrentTime < SequenceStart)
			{
				CurrentTime += SequenceDuration;
			}
		}
		else
		{
			CurrentTime = SequenceEnd;
			ApplyAtCurrentTime();
			bPlaying = false;
			bPaused = bPauseAtEnd;
			if (!bPauseAtEnd)
			{
				ResolvedChannels.clear();
			}
			return;
		}
	}

	ApplyAtCurrentTime();
}

void UActorSequencePlayer::SetCurrentTime(float InTime)
{
	const float SequenceStart = ResolveSequenceStartTime(Sequence);
	const float SequenceEnd = ResolveSequenceEndTime(Sequence);
	CurrentTime = SequenceEnd > SequenceStart
		? std::clamp(InTime, SequenceStart, SequenceEnd)
		: SequenceStart;
	if (bResolveDirty || ResolvedChannels.empty())
	{
		RebuildResolvedChannels();
	}
	ApplyAtCurrentTime();
}

float UActorSequencePlayer::GetCurrentTime() const
{
	return CurrentTime;
}

bool UActorSequencePlayer::IsPlaying() const
{
	return bPlaying && !bPaused;
}

bool UActorSequencePlayer::IsPaused() const
{
	return bPaused;
}

void UActorSequencePlayer::MarkResolveDirty()
{
	bResolveDirty = true;
}

void UActorSequencePlayer::AddReferencedObjects(FReferenceCollector& Collector)
{
	UObject::AddReferencedObjects(Collector);
	Collector.AddReferencedObject(Sequence, "ActorSequencePlayer.Sequence");
}

void UActorSequencePlayer::RebuildResolvedChannels()
{
	RestoreBaseValues();
	ResolvedChannels.clear();
	bResolveDirty = false;
	if (!IsValid(Sequence))
	{
		return;
	}

	for (FActorSequenceBinding& Binding : Sequence->GetBindings())
	{
		UObject* Object = ResolveObject(Binding.Binding);
		if (!IsValid(Object))
		{
			continue;
		}

		for (FActorSequenceTrack& Track : Binding.Tracks)
		{
			for (FActorSequenceSection& Section : Track.Sections)
			{
				for (FActorSequenceChannel& Channel : Section.Channels)
				{
					if (!Channel.Playback.Curve && !Channel.Playback.CurveAssetPath.empty())
					{
						Channel.Playback.Curve = FFloatCurveManager::Get().Load(Channel.Playback.CurveAssetPath);
					}

					const FProperty* Property = ResolveProperty(Object, Track, Channel);
					if (!Property)
					{
						continue;
					}

					FResolvedActorSequenceChannel Resolved;
					Resolved.ResolvedObject = Object;
					Resolved.ResolvedProperty = Property;
					Resolved.SourceSection = &Section;
					Resolved.SourceChannel = &Channel;
					Resolved.ResolvedCurve = Channel.Playback.Curve;
					Resolved.bHasBaseValue = Property->ReadScalarChannelValue(
						Object,
						Channel.ChannelName,
						Resolved.BaseValue);
					Resolved.bValid = Resolved.bHasBaseValue && Resolved.ResolvedCurve != nullptr;
					if (Resolved.bValid)
					{
						ResolvedChannels.push_back(Resolved);
					}
				}
			}
		}
	}
}

void UActorSequencePlayer::ApplyAtCurrentTime()
{
	for (FResolvedActorSequenceChannel& Resolved : ResolvedChannels)
	{
		if (!Resolved.bValid || !IsValid(Resolved.ResolvedObject) || !Resolved.ResolvedProperty
			|| !Resolved.SourceSection || !Resolved.SourceChannel || !Resolved.ResolvedCurve)
		{
			continue;
		}

		FCurvePlaybackDesc Playback = Resolved.SourceChannel->Playback;
		Playback.Curve = Resolved.ResolvedCurve;
		Playback.StartTime = Resolved.SourceSection->StartTime;
		Playback.Duration = Resolved.SourceSection->Duration;
		Playback.PlayRate = Resolved.SourceSection->PlayRate;
		Playback.bLoop = Resolved.SourceSection->bLoop;

		const FCurvePlaybackEvalResult Eval = FCurvePlaybackEvaluator::Evaluate(Playback, CurrentTime);
		if (!Eval.bActive)
		{
			continue;
		}

		const float NewValue = Playback.ApplyMode == ECurveApplyMode::Additive
			? Resolved.BaseValue + Eval.Value
			: Eval.Value;
		if (Resolved.ResolvedProperty->WriteScalarChannelValue(
			Resolved.ResolvedObject,
			Resolved.SourceChannel->ChannelName,
			NewValue))
		{
			Resolved.ResolvedObject->PostEditProperty(Resolved.ResolvedProperty->Name);
		}
	}
}

void UActorSequencePlayer::RestoreBaseValues()
{
	for (FResolvedActorSequenceChannel& Resolved : ResolvedChannels)
	{
		if (!Resolved.bValid || !Resolved.bHasBaseValue || !IsValid(Resolved.ResolvedObject)
			|| !Resolved.ResolvedProperty || !Resolved.SourceChannel)
		{
			continue;
		}

		if (Resolved.ResolvedProperty->WriteScalarChannelValue(
			Resolved.ResolvedObject,
			Resolved.SourceChannel->ChannelName,
			Resolved.BaseValue))
		{
			Resolved.ResolvedObject->PostEditProperty(Resolved.ResolvedProperty->Name);
		}
	}
}

UObject* UActorSequencePlayer::ResolveObject(const FSequenceObjectBinding& Binding) const
{
	AActor* Owner = OwnerActor.Get();
	if (!IsValid(Owner))
	{
		return nullptr;
	}

	if (Binding.TargetType == EActorSequenceBindingTarget::OwnerActor)
	{
		if (Binding.TargetObjectName.empty() || Binding.TargetObjectName == Owner->GetFName().ToString())
		{
			return Owner;
		}
		return nullptr;
	}

	if (Binding.TargetType == EActorSequenceBindingTarget::Component)
	{
		return FindComponentByBinding(Owner, Binding);
	}
	return nullptr;
}

const FProperty* UActorSequencePlayer::ResolveProperty(
	UObject* Object,
	const FActorSequenceTrack& Track,
	const FActorSequenceChannel& Channel) const
{
	if (!IsValid(Object))
	{
		return nullptr;
	}

	TArray<const FProperty*> Properties;
	Object->GetClass()->GetPropertyRefs(Properties);
	for (const FProperty* Property : Properties)
	{
		if (!Property || !EqualsPropertyName(*Property, Track.PropertyName))
		{
			continue;
		}

		float TestValue = 0.0f;
		if (Property->ReadScalarChannelValue(Object, Channel.ChannelName, TestValue))
		{
			return Property;
		}
	}
	return nullptr;
}
