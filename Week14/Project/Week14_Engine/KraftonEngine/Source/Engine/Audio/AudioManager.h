#pragma once

#include "Core/Singleton.h"
#include "Core/Types/CoreTypes.h"
#include "Math/Vector.h"
#include <fmod.hpp>

using FAudioHandle = int32;

struct FAudioPlaybackPolicy
{
	int32 MaxConcurrent = 0;
	float CooldownSeconds = 0.0f;
	int32 Priority = 0;
	bool bStopOldest = true;
};

struct FActiveAudioChannel
{
	FMOD::Channel* Channel = nullptr;
	FString SourceKey;
	int32 Priority = 0;
	uint64 Sequence = 0;
	bool bVolumeFade = false;
	bool bStopAfterVolumeFade = false;
	double VolumeFadeStartTimeSeconds = 0.0;
	float VolumeFadeDurationSeconds = 0.0f;
	float VolumeFadeStartVolume = 1.0f;
	float VolumeFadeTargetVolume = 1.0f;
};

class FAudioManager : public TSingleton<FAudioManager>
{
	friend class TSingleton<FAudioManager>;

public:
	bool Initialize();
	void Shutdown();
	void Tick();

	bool LoadAudio(const FString& Key, const FString& Path, bool bLoop = false);
	void PlayAudio(const FString& Key, float Volume = 1.0f);
	FAudioHandle PlayAudioHandle(const FString& Key, float Volume = 1.0f);
	bool PlaySFX(const FString& PathOrKey, float VolumeScale = 1.0f);
	FAudioHandle PlaySFXHandle(const FString& PathOrKey, float VolumeScale = 1.0f);
	FAudioHandle PlaySound2D(const FString& PathOrKey, bool bLoop = false, float VolumeScale = 1.0f);
	FAudioHandle PlaySound3D(const FString& PathOrKey, const FVector& Position, bool bLoop = false, float VolumeScale = 1.0f, float MinDistance = 1.0f, float MaxDistance = 10000.0f);
	FAudioHandle PlaySFX3D(const FString& PathOrKey, const FVector& Position, float VolumeScale = 1.0f, float MinDistance = 1.0f, float MaxDistance = 10000.0f);
	void PlayBGM(const FString& Key, float Volume = 1.0f);
	void StopBGM();
	bool FadeInBGM(float DurationSeconds, float TargetVolume = 1.0f);
	bool FadeOutBGM(float DurationSeconds);
	void PlayLoop(const FString& Key, const FString& LoopName, float Volume = 1.0f, float Pitch = 1.0f);
	void StopLoop(const FString& LoopName);
	void StopAllLoops();
	void SetLoopVolume(const FString& LoopName, float Volume);
	void SetLoopPitch(const FString& LoopName, float Pitch);
	bool IsLoopPlaying(const FString& LoopName);

	void SetMasterVolume(float Volume);
	float GetMasterVolume() const { return MasterVolume; }
	void SetBGMVolume(float Volume);
	float GetBGMVolume() const { return BGMVolume; }
	void SetSFXVolume(float Volume);
	float GetSFXVolume() const { return SFXVolume; }
	void SetListener(const FVector& Position, const FVector& Forward = FVector::ForwardVector, const FVector& Up = FVector::UpVector);
	void StopSound(FAudioHandle Handle);
	bool FadeInSound(FAudioHandle Handle, float DurationSeconds, float TargetVolume = 1.0f);
	bool FadeOutSound(FAudioHandle Handle, float DurationSeconds);
	bool FadeInSFX(FAudioHandle Handle, float DurationSeconds, float TargetVolume = 1.0f);
	bool FadeOutSFX(FAudioHandle Handle, float DurationSeconds);
	void StopAllSounds();
	bool IsSoundPlaying(FAudioHandle Handle);
	void SetSoundVolume(FAudioHandle Handle, float Volume);
	void SetSoundPitch(FAudioHandle Handle, float Pitch);
	void SetSoundPosition(FAudioHandle Handle, const FVector& Position);
	void SetSFXPlaybackPolicy(const FString& PathOrKey, int32 MaxConcurrent, float CooldownSeconds, int32 Priority = 0, bool bStopOldest = true);
	void ClearSFXPlaybackPolicy(const FString& PathOrKey);
	void ClearAllSFXPlaybackPolicies();
	int32 GetActiveSoundCount(const FString& PathOrKey = {});

private:
	void LoadDefaultAudios();
	FMOD::Sound* ResolveSound(const FString& PathOrKey, bool bLoop);
	FAudioHandle RegisterChannel(FMOD::Channel* Channel, const FString& SourceKey = {}, int32 Priority = 0);
	FMOD::Channel* FindActiveChannel(FAudioHandle Handle);
	FMOD::Channel* FindPlayingLoopChannel(const FString& LoopName);
	bool BeginBGMFade(float DurationSeconds, float TargetVolume, bool bStopAfterFade);
	bool BeginChannelFade(FAudioHandle Handle, float DurationSeconds, float TargetVolume, bool bStopAfterFade);
	void UpdateBGMFade();
	void UpdateFades();
	void PruneStoppedChannels();
	double GetAudioTimeSeconds() const;
	bool ApplySFXPlaybackPolicy(const FString& SourceKey, const FAudioPlaybackPolicy& Policy);
	void NoteSFXPlayback(const FString& SourceKey);

private:
	FAudioManager() = default;
	~FAudioManager() = default;

	FMOD::System* System = nullptr;
	FMOD::ChannelGroup* MasterGroup = nullptr;
	FMOD::ChannelGroup* BGMGroup = nullptr;
	FMOD::ChannelGroup* SFXGroup = nullptr;
	FMOD::Channel* BGMChannel = nullptr;

	TMap<FString, FMOD::Sound*> Audios;
	TMap<FString, FMOD::Channel*> LoopChannels;
	TMap<FAudioHandle, FActiveAudioChannel> ActiveChannels;
	TMap<FString, FAudioPlaybackPolicy> SFXPlaybackPolicies;
	TMap<FString, double> LastSFXPlaybackTimeSeconds;
	FAudioHandle NextAudioHandle = 1;
	uint64 NextAudioSequence = 1;
	bool bBGMVolumeFade = false;
	bool bBGMStopAfterVolumeFade = false;
	double BGMVolumeFadeStartTimeSeconds = 0.0;
	float BGMVolumeFadeDurationSeconds = 0.0f;
	float BGMVolumeFadeStartVolume = 1.0f;
	float BGMVolumeFadeTargetVolume = 1.0f;
	float MasterVolume = 1.0f;
	float BGMVolume = 1.0f;
	float SFXVolume = 1.0f;
};
