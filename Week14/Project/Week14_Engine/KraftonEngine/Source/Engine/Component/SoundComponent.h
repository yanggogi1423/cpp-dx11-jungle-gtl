#pragma once

#include "Audio/AudioManager.h"
#include "Component/SceneComponent.h"

#include "Source/Engine/Component/SoundComponent.generated.h"

UCLASS()
class USoundComponent : public USceneComponent
{
public:
	GENERATED_BODY()
	USoundComponent() = default;
	~USoundComponent() override = default;

	void BeginPlay() override;
	void EndPlay() override;
	void Activate() override;
	void Deactivate() override;

	UFUNCTION(Callable, Category="Audio")
	void Play();
	UFUNCTION(Callable, Category="Audio")
	void Stop();
	UFUNCTION(Pure, Category="Audio")
	bool IsPlaying() const;

	UFUNCTION(Callable, Category="Audio")
	void SetSoundPath(const FString& InSoundPath);
	UFUNCTION(Pure, Category="Audio")
	FString GetSoundPath() const { return SoundPath; }

	UFUNCTION(Callable, Category="Audio")
	void SetVolume(float InVolume);
	UFUNCTION(Pure, Category="Audio")
	float GetVolume() const { return Volume; }

	UFUNCTION(Callable, Category="Audio")
	void SetPitch(float InPitch);
	UFUNCTION(Pure, Category="Audio")
	float GetPitch() const { return Pitch; }

	UFUNCTION(Callable, Category="Audio")
	void SetLooping(bool bInLooping);
	UFUNCTION(Pure, Category="Audio")
	bool IsLooping() const { return bLooping; }

	UFUNCTION(Callable, Category="Audio")
	void SetPlayOnBeginPlay(bool bInPlayOnBeginPlay) { bPlayOnBeginPlay = bInPlayOnBeginPlay; }
	UFUNCTION(Pure, Category="Audio")
	bool ShouldPlayOnBeginPlay() const { return bPlayOnBeginPlay; }

	UFUNCTION(Callable, Category="Audio")
	void SetSpatialized(bool bInSpatialized);
	UFUNCTION(Pure, Category="Audio")
	bool IsSpatialized() const { return bSpatialized; }

	UFUNCTION(Callable, Category="Audio")
	void Set3DMinMaxDistance(float InMinDistance, float InMaxDistance);
	UFUNCTION(Pure, Category="Audio")
	float GetMinDistance() const { return MinDistance; }
	UFUNCTION(Pure, Category="Audio")
	float GetMaxDistance() const { return MaxDistance; }

	UFUNCTION(Pure, Category="Audio")
	int32 GetActiveHandle() const { return ActiveHandle; }

protected:
	void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction) override;

private:
	void ApplyRuntimeParameters();
	void UpdateSoundPosition();

	UPROPERTY(Edit, Save, Category="Audio", DisplayName="Sound Path")
	FString SoundPath;
	UPROPERTY(Edit, Save, Category="Audio", DisplayName="Play On BeginPlay")
	bool bPlayOnBeginPlay = false;
	UPROPERTY(Edit, Save, Category="Audio", DisplayName="Looping")
	bool bLooping = false;
	UPROPERTY(Edit, Save, Category="Audio", DisplayName="Spatialized")
	bool bSpatialized = true;
	UPROPERTY(Edit, Save, Category="Audio", DisplayName="Volume", Min=0.0f, Max=1.0f, Speed=0.01f)
	float Volume = 1.0f;
	UPROPERTY(Edit, Save, Category="Audio", DisplayName="Pitch", Min=0.1f, Max=3.0f, Speed=0.01f)
	float Pitch = 1.0f;
	UPROPERTY(Edit, Save, Category="Audio|3D", DisplayName="Min Distance", Min=0.01f, Max=1000.0f, Speed=0.1f)
	float MinDistance = 100.0f;
	UPROPERTY(Edit, Save, Category="Audio|3D", DisplayName="Max Distance", Min=0.01f, Max=10000.0f, Speed=1.0f)
	float MaxDistance = 3000.0f;

	FAudioHandle ActiveHandle = 0;
};
