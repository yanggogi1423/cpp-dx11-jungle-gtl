#pragma once

#include "Animation/ActorSequence.h"
#include "Component/ActorComponent.h"

UCLASS()
class UActorSequenceComponent : public UActorComponent
{
	GENERATED_BODY(UActorSequenceComponent, UActorComponent)
public:
    void BeginPlay() override;
    void EndPlay() override;
    void ExecutePreviewTick(float DeltaTime);

    void Play();
    void Pause();
    void Stop();
    void PlayPreview();
    void PausePreview();
    void StopPreview();
    void SetPreviewTime(float InTime);
    bool AddFloatTrack(const FActorSequenceFloatTrackDesc& Desc);

    UActorSequence* GetSequence();
    UActorSequencePlayer* GetSequencePlayer();
    UActorSequencePlayer* GetPreviewSequencePlayer();

    bool IsAutoPlay() const { return bAutoPlay; }
    void SetAutoPlay(bool bInAutoPlay) { bAutoPlay = bInAutoPlay; }
    bool IsLooping() const;
    void SetLooping(bool bInLooping);
    float GetPlayRate() const { return PlayRate; }
    void SetPlayRate(float InPlayRate);
    bool ShouldPauseAtEnd() const { return bPauseAtEnd; }
    void SetPauseAtEnd(bool bInPauseAtEnd);
    float GetStartOffsetSeconds() const { return StartOffsetSeconds; }
    void SetStartOffsetSeconds(float InStartOffsetSeconds);

    void MarkSequenceDirty();

    void GetEditableProperties(TArray<FPropertyDescriptor>& OutProps) override;
    void Serialize(FArchive& Ar) override;
    void PostDuplicate(UObject* Original) override;

protected:
    void TickComponent(float DeltaTime) override;

private:
    void EnsureSequence();
    void EnsureSequencePlayer(ESequencePlayerContext Context);
    void EnsurePreviewSequencePlayer();
    void ApplyPlaybackSettings(UActorSequencePlayer* Player);

private:
    UActorSequence* Sequence = nullptr;
    UActorSequencePlayer* SequencePlayer = nullptr;
    UActorSequencePlayer* PreviewSequencePlayer = nullptr;

	UPROPERTY(EditAnywhere, Category = "Sequence", DisplayName = "Auto Play")
    bool bAutoPlay = true;
	UPROPERTY(EditAnywhere, Category = "Sequence", DisplayName = "Pause At End")
    bool bPauseAtEnd = false;
	UPROPERTY(EditAnywhere, Category = "Sequence", DisplayName = "Play Rate")
    float PlayRate = 1.0f;
	UPROPERTY(EditAnywhere, Category = "Sequence", DisplayName = "Start Offset Seconds")
    float StartOffsetSeconds = 0.0f;
};
