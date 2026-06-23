#pragma once

#include "Animation/ActorSequence.h"
#include "Component/ActorComponent.h"

#include "Source/Engine/Component/ActorSequenceComponent.generated.h"

UCLASS()
class UActorSequenceComponent : public UActorComponent
{
public:
	GENERATED_BODY()

	UActorSequenceComponent();

	void BeginPlay() override;
	void EndPlay() override;
	void AddReferencedObjects(FReferenceCollector& Collector) override;
	void OnPreSave(FArchive& Ar) override;
	void OnPostLoad(FArchive& Ar) override;
	void PostDuplicate() override;
	void PostEditProperty(const char* PropertyName) override;

	UFUNCTION(Callable, Category="Actor Sequence")
	void Play();
	UFUNCTION(Callable, Category="Actor Sequence")
	void Pause();
	UFUNCTION(Callable, Category="Actor Sequence")
	void Stop();
	UFUNCTION(Callable, Category="Actor Sequence")
	void SetTickWhenPaused(bool bInTickWhenPaused);
	UFUNCTION(Pure, Category="Actor Sequence")
	bool IsTickWhenPaused() const { return bTickWhenPaused; }

	UFUNCTION(Pure, Category="Actor Sequence")
	UActorSequence* GetSequence();
	UFUNCTION(Pure, Category="Actor Sequence")
	UActorSequencePlayer* GetSequencePlayer();
	UActorSequencePlayer* GetPreviewSequencePlayer();

	UFUNCTION(Callable, Category="Actor Sequence")
	bool AddFloatTrack(
		const FString& TargetObjectName,
		const FString& PropertyName,
		const FString& ChannelName,
		float StartTime,
		float Duration,
		const FString& CurveAssetPath);
	bool AddFloatTrack(
		UObject* TargetObject,
		const FString& PropertyName,
		const FString& ChannelName,
		float StartTime,
		float Duration,
		const FString& CurveAssetPath);

	void PreviewPlay();
	void PreviewPause();
	void PreviewStop();
	float GetPreviewTime() const;
	void SetPreviewTime(float Time);
	void CommitSequenceEditsForSerialization();

protected:
	void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction) override;

private:
	void EnsureSequence();
	void EnsurePlayers();
	void InitializeRuntimePlayer();
	void InitializePreviewPlayer();
	void InitializePlayers();
	void StopRuntimePlayerForPreview();
	UObject* ResolveTargetByName(const FString& TargetObjectName) const;
	void SyncSequenceDataFromRuntime();
	void SyncRuntimeFromSequenceData();

private:
	UPROPERTY(Edit, Save, Category="Actor Sequence", DisplayName="Auto Play")
	bool bAutoPlay = false;
	UPROPERTY(Edit, Save, Category="Actor Sequence", DisplayName="Loop")
	bool bLoop = false;
	UPROPERTY(Edit, Save, Category="Actor Sequence", DisplayName="Pause At End")
	bool bPauseAtEnd = true;
	UPROPERTY(Edit, Save, Category="Actor Sequence", DisplayName="Play Rate", Min=0.0f, Max=10.0f, Speed=0.05f)
	float PlayRate = 1.0f;
	UPROPERTY(Edit, Save, Category="Actor Sequence", DisplayName="Start Offset Seconds", Min=0.0f, Max=60.0f, Speed=0.05f)
	float StartOffsetSeconds = 0.0f;
	UPROPERTY(Edit, Save, Category="Actor Sequence", DisplayName="Tick When Paused")
	bool bTickWhenPaused = false;
	UPROPERTY(Save, Category="Actor Sequence", DisplayName="Sequence Data")
	FString SequenceDataJson;

	UActorSequence* Sequence = nullptr;
	UActorSequencePlayer* SequencePlayer = nullptr;
	UActorSequencePlayer* PreviewSequencePlayer = nullptr;
};
