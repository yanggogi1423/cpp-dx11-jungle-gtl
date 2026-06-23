#include "ActorSequenceComponent.h"

#include "FloatCurve/FloatCurveAsset.h"
#include "FloatCurve/FloatCurveManager.h"
#include "GameFramework/AActor.h"
#include "Object/GarbageCollection.h"
#include "Object/Object.h"
#include "Serialization/Archive.h"

#include <algorithm>
#include <cstring>

UActorSequenceComponent::UActorSequenceComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	PrimaryComponentTick.bTickEnabled = true;
	PrimaryComponentTick.bTickEvenWhenPaused = bTickWhenPaused;
}

void UActorSequenceComponent::BeginPlay()
{
	UActorComponent::BeginPlay();
	EnsurePlayers();
	InitializePlayers();

	if (bAutoPlay)
	{
		Play();
	}
}

void UActorSequenceComponent::EndPlay()
{
	if (SequencePlayer)
	{
		SequencePlayer->Stop(true);
	}
	if (PreviewSequencePlayer)
	{
		PreviewSequencePlayer->Stop(true);
	}
	UActorComponent::EndPlay();
}

void UActorSequenceComponent::AddReferencedObjects(FReferenceCollector& Collector)
{
	UActorComponent::AddReferencedObjects(Collector);
	Collector.AddReferencedObject(Sequence, "ActorSequenceComponent.Sequence");
	Collector.AddReferencedObject(SequencePlayer, "ActorSequenceComponent.SequencePlayer");
	Collector.AddReferencedObject(PreviewSequencePlayer, "ActorSequenceComponent.PreviewSequencePlayer");
}

void UActorSequenceComponent::OnPreSave(FArchive& Ar)
{
	UActorComponent::OnPreSave(Ar);
	SyncSequenceDataFromRuntime();
}

void UActorSequenceComponent::OnPostLoad(FArchive& Ar)
{
	UActorComponent::OnPostLoad(Ar);
	PrimaryComponentTick.bTickEvenWhenPaused = bTickWhenPaused;
	SyncRuntimeFromSequenceData();
	EnsurePlayers();
	InitializePlayers();
}

void UActorSequenceComponent::PostDuplicate()
{
	UActorComponent::PostDuplicate();
	PrimaryComponentTick.bTickEvenWhenPaused = bTickWhenPaused;
	SyncRuntimeFromSequenceData();
	EnsurePlayers();
	InitializePlayers();
}

void UActorSequenceComponent::PostEditProperty(const char* PropertyName)
{
	UActorComponent::PostEditProperty(PropertyName);
	if (PropertyName && std::strcmp(PropertyName, "SequenceDataJson") == 0)
	{
		SyncRuntimeFromSequenceData();
	}
	PlayRate = std::max(0.0f, PlayRate);
	StartOffsetSeconds = std::max(0.0f, StartOffsetSeconds);
	PrimaryComponentTick.bTickEvenWhenPaused = bTickWhenPaused;
}

void UActorSequenceComponent::Play()
{
	EnsurePlayers();
	if (!SequencePlayer)
	{
		return;
	}

	InitializeRuntimePlayer();
	SequencePlayer->SetPlaybackOptions(bLoop, bPauseAtEnd);
	const float PlaybackStart = Sequence ? Sequence->GetStartTime() : 0.0f;
	SequencePlayer->SetCurrentTime(PlaybackStart + StartOffsetSeconds);
	SequencePlayer->Play(false);
}

void UActorSequenceComponent::Pause()
{
	if (SequencePlayer)
	{
		SequencePlayer->Pause();
	}
}

void UActorSequenceComponent::Stop()
{
	if (SequencePlayer)
	{
		SequencePlayer->Stop(true);
	}
}

void UActorSequenceComponent::SetTickWhenPaused(bool bInTickWhenPaused)
{
	bTickWhenPaused = bInTickWhenPaused;
	PrimaryComponentTick.bTickEvenWhenPaused = bTickWhenPaused;
}

UActorSequence* UActorSequenceComponent::GetSequence()
{
	EnsureSequence();
	if (Sequence)
	{
		Sequence->RefreshBindingTargetCache(GetOwner());
	}
	return Sequence;
}

UActorSequencePlayer* UActorSequenceComponent::GetSequencePlayer()
{
	EnsurePlayers();
	return SequencePlayer;
}

UActorSequencePlayer* UActorSequenceComponent::GetPreviewSequencePlayer()
{
	EnsurePlayers();
	return PreviewSequencePlayer;
}

bool UActorSequenceComponent::AddFloatTrack(
	const FString& TargetObjectName,
	const FString& PropertyName,
	const FString& ChannelName,
	float StartTime,
	float Duration,
	const FString& CurveAssetPath)
{
	EnsureSequence();
	UObject* TargetObject = ResolveTargetByName(TargetObjectName);
	return AddFloatTrack(TargetObject, PropertyName, ChannelName, StartTime, Duration, CurveAssetPath);
}

bool UActorSequenceComponent::AddFloatTrack(
	UObject* TargetObject,
	const FString& PropertyName,
	const FString& ChannelName,
	float StartTime,
	float Duration,
	const FString& CurveAssetPath)
{
	EnsureSequence();
	if (!IsValid(TargetObject) || !Sequence)
	{
		return false;
	}

	UFloatCurveAsset* Curve = !CurveAssetPath.empty() ? FFloatCurveManager::Get().Load(CurveAssetPath) : nullptr;
	const bool bAdded = Sequence->AddFloatTrack(
		TargetObject,
		PropertyName,
		ChannelName,
		StartTime,
		Duration,
		Curve,
		CurveAssetPath);
	if (bAdded)
	{
		SyncSequenceDataFromRuntime();
		if (SequencePlayer)
		{
			SequencePlayer->MarkResolveDirty();
		}
		if (PreviewSequencePlayer)
		{
			PreviewSequencePlayer->MarkResolveDirty();
		}
	}
	return bAdded;
}

void UActorSequenceComponent::PreviewPlay()
{
	EnsurePlayers();
	if (!PreviewSequencePlayer)
	{
		return;
	}

	StopRuntimePlayerForPreview();
	InitializePreviewPlayer();
	PreviewSequencePlayer->SetPlaybackOptions(bLoop, true);
	PreviewSequencePlayer->Play(false);
}

void UActorSequenceComponent::PreviewPause()
{
	if (PreviewSequencePlayer)
	{
		PreviewSequencePlayer->Pause();
	}
}

void UActorSequenceComponent::PreviewStop()
{
	if (PreviewSequencePlayer)
	{
		PreviewSequencePlayer->Stop(true);
	}
}

float UActorSequenceComponent::GetPreviewTime() const
{
	return PreviewSequencePlayer ? PreviewSequencePlayer->GetCurrentTime() : 0.0f;
}

void UActorSequenceComponent::SetPreviewTime(float Time)
{
	EnsurePlayers();
	if (PreviewSequencePlayer)
	{
		StopRuntimePlayerForPreview();
		InitializePreviewPlayer();
		PreviewSequencePlayer->SetCurrentTime(Time);
	}
}

void UActorSequenceComponent::CommitSequenceEditsForSerialization()
{
	SyncSequenceDataFromRuntime();
	if (SequencePlayer)
	{
		SequencePlayer->MarkResolveDirty();
	}
	if (PreviewSequencePlayer)
	{
		PreviewSequencePlayer->MarkResolveDirty();
	}
}

void UActorSequenceComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction)
{
	UActorComponent::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (SequencePlayer && SequencePlayer->IsPlaying())
	{
		SequencePlayer->Tick(DeltaTime * std::max(0.0f, PlayRate));
	}
	if (PreviewSequencePlayer && PreviewSequencePlayer->IsPlaying())
	{
		PreviewSequencePlayer->Tick(DeltaTime * std::max(0.0f, PlayRate));
	}
}

void UActorSequenceComponent::EnsureSequence()
{
	if (!IsValid(Sequence))
	{
		Sequence = UObjectManager::Get().CreateObject<UActorSequence>(this);
	}
}

void UActorSequenceComponent::EnsurePlayers()
{
	EnsureSequence();

	AActor* Owner = GetOwner();
	if (!IsValid(SequencePlayer))
	{
		SequencePlayer = UObjectManager::Get().CreateObject<UActorSequencePlayer>(this);
		if (SequencePlayer)
		{
			SequencePlayer->Initialize(Sequence, Owner);
		}
	}
	if (!IsValid(PreviewSequencePlayer))
	{
		PreviewSequencePlayer = UObjectManager::Get().CreateObject<UActorSequencePlayer>(this);
		if (PreviewSequencePlayer)
		{
			PreviewSequencePlayer->Initialize(Sequence, Owner);
		}
	}
}

void UActorSequenceComponent::InitializeRuntimePlayer()
{
	AActor* Owner = GetOwner();
	if (SequencePlayer)
	{
		SequencePlayer->Initialize(Sequence, Owner);
	}
}

void UActorSequenceComponent::InitializePreviewPlayer()
{
	AActor* Owner = GetOwner();
	if (PreviewSequencePlayer)
	{
		PreviewSequencePlayer->Initialize(Sequence, Owner);
	}
}

void UActorSequenceComponent::InitializePlayers()
{
	InitializeRuntimePlayer();
	InitializePreviewPlayer();
}

void UActorSequenceComponent::StopRuntimePlayerForPreview()
{
	if (SequencePlayer)
	{
		SequencePlayer->Stop(true);
	}
}

UObject* UActorSequenceComponent::ResolveTargetByName(const FString& TargetObjectName) const
{
	AActor* Owner = GetOwner();
	if (!IsValid(Owner))
	{
		return nullptr;
	}

	if (TargetObjectName.empty() || TargetObjectName == "Owner" || TargetObjectName == Owner->GetFName().ToString())
	{
		return Owner;
	}

	for (UActorComponent* Component : Owner->GetComponents())
	{
		if (!IsValid(Component))
		{
			continue;
		}

		if (Component->GetFName().ToString() == TargetObjectName
			|| Component->GetPersistentGuid() == TargetObjectName)
		{
			return Component;
		}
	}
	return nullptr;
}

void UActorSequenceComponent::SyncSequenceDataFromRuntime()
{
	if (IsValid(Sequence))
	{
		Sequence->RefreshBindingTargetCache(GetOwner());
		SequenceDataJson = Sequence->ExportToJsonString();
	}
	else
	{
		SequenceDataJson.clear();
	}
}

void UActorSequenceComponent::SyncRuntimeFromSequenceData()
{
	EnsureSequence();
	if (Sequence)
	{
		Sequence->ImportFromJsonString(SequenceDataJson);
		Sequence->RefreshBindingTargetCache(GetOwner());
	}
	if (SequencePlayer)
	{
		SequencePlayer->MarkResolveDirty();
	}
	if (PreviewSequencePlayer)
	{
		PreviewSequencePlayer->MarkResolveDirty();
	}
}
