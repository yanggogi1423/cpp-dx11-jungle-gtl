#include "AudioManager.h"
#include "Core/Logging/Log.h"
#include "Platform/Paths.h"
#include <algorithm>
#include <chrono>

namespace
{
	FMOD_VECTOR ToFMODVector(const FVector& Vector)
	{
		return FMOD_VECTOR { Vector.X, Vector.Y, Vector.Z };
	}

	FVector SafeNormal(const FVector& Vector, const FVector& Fallback)
	{
		return Vector.IsNearlyZero() ? Fallback : Vector.Normalized();
	}
}

bool FAudioManager::Initialize()
{
	if (FMOD::System_Create(&System) != FMOD_OK || !System)
	{
		UE_LOG("Failed to create FMOD system.");
		return false;
	}

	if (System->init(512, FMOD_INIT_NORMAL, nullptr) != FMOD_OK)
	{
		UE_LOG("Failed to initialize FMOD system.");
		Shutdown();
		return false;
	}

	System->getMasterChannelGroup(&MasterGroup);
	if (System->createChannelGroup("BGM", &BGMGroup) == FMOD_OK && MasterGroup)
	{
		MasterGroup->addGroup(BGMGroup);
		BGMGroup->setVolume(BGMVolume);
	}
	if (System->createChannelGroup("SFX", &SFXGroup) == FMOD_OK && MasterGroup)
	{
		MasterGroup->addGroup(SFXGroup);
		SFXGroup->setVolume(SFXVolume);
	}
	SetMasterVolume(MasterVolume);

	LoadDefaultAudios();

	return true;
}

void FAudioManager::Shutdown()
{
	if (!System)
	{
		MasterGroup = nullptr;
		BGMGroup = nullptr;
		SFXGroup = nullptr;
		BGMChannel = nullptr;
		bBGMVolumeFade = false;
		bBGMStopAfterVolumeFade = false;
		LoopChannels.clear();
		ActiveChannels.clear();
		SFXPlaybackPolicies.clear();
		LastSFXPlaybackTimeSeconds.clear();
		Audios.clear();
		return;
	}

	StopBGM();
	StopAllLoops();
	StopAllSounds();
	if (BGMGroup)
	{
		BGMGroup->stop();
		BGMGroup->release();
		BGMGroup = nullptr;
	}
	if (SFXGroup)
	{
		SFXGroup->stop();
		SFXGroup->release();
		SFXGroup = nullptr;
	}
	if (MasterGroup)
	{
		MasterGroup->stop();
		MasterGroup = nullptr;
	}
	System->update();

	for (auto& Pair : Audios)
	{
		if (Pair.second)
		{
			Pair.second->release();
		}
	}
	Audios.clear();
	SFXPlaybackPolicies.clear();
	LastSFXPlaybackTimeSeconds.clear();

	System->update();
	System->close();
	System->release();
	System = nullptr;
	bBGMVolumeFade = false;
	bBGMStopAfterVolumeFade = false;
}

void FAudioManager::Tick()
{
	if (System)
	{
		UpdateBGMFade();
		UpdateFades();
		PruneStoppedChannels();
		System->update();
		UpdateBGMFade();
		UpdateFades();
		PruneStoppedChannels();
	}
}

bool FAudioManager::LoadAudio(const FString& Key, const FString& Path, bool bLoop)
{
	if (!System)
	{
		return false;
	}

	FString FullPath = FPaths::ToUtf8(FPaths::Combine(FPaths::AudioDir(), FPaths::ToWide(Path)));

	FMOD::Sound* Sound = nullptr;
	const FMOD_MODE Mode = FMOD_DEFAULT | (bLoop ? FMOD_LOOP_NORMAL : FMOD_LOOP_OFF);

	if (System->createSound(FullPath.c_str(), Mode, nullptr, &Sound) != FMOD_OK)
	{
		return false;
	}

	if (Audios.contains(Key) && Audios[Key])
	{
		Audios[Key]->release();
	}

	Audios[Key] = Sound;
	return true;
}

void FAudioManager::PlayAudio(const FString& Key, float Volume)
{
	PlayAudioHandle(Key, Volume);
}

FAudioHandle FAudioManager::PlayAudioHandle(const FString& Key, float Volume)
{
	if (!System || !Audios.contains(Key))
	{
		return 0;
	}

	FMOD::Channel* Channel = nullptr;
	System->playSound(Audios[Key], SFXGroup, true, &Channel);

	if (Channel)
	{
		Channel->setMode(FMOD_2D | FMOD_LOOP_OFF);
		Channel->setVolume(std::clamp(Volume, 0.0f, 1.0f));
		Channel->setPaused(false);
		return RegisterChannel(Channel, Key);
	}

	return 0;
}

bool FAudioManager::PlaySFX(const FString& PathOrKey, float VolumeScale)
{
	return PlaySFXHandle(PathOrKey, VolumeScale) != 0;
}

FAudioHandle FAudioManager::PlaySFXHandle(const FString& PathOrKey, float VolumeScale)
{
	return PlaySound2D(PathOrKey, false, VolumeScale);
}

FAudioHandle FAudioManager::PlaySound2D(const FString& PathOrKey, bool bLoop, float VolumeScale)
{
	if (!System || PathOrKey.empty())
	{
		return 0;
	}

	FMOD::Sound* Sound = ResolveSound(PathOrKey, bLoop);
	if (!Sound)
	{
		return 0;
	}

	int32 Priority = 0;
	if (SFXPlaybackPolicies.contains(PathOrKey))
	{
		const FAudioPlaybackPolicy& Policy = SFXPlaybackPolicies[PathOrKey];
		if (!ApplySFXPlaybackPolicy(PathOrKey, Policy))
		{
			return 0;
		}
		Priority = Policy.Priority;
	}

	FMOD::Channel* Channel = nullptr;
	System->playSound(Sound, SFXGroup, true, &Channel);
	if (!Channel)
	{
		return 0;
	}

	Channel->setMode(FMOD_2D | (bLoop ? FMOD_LOOP_NORMAL : FMOD_LOOP_OFF));
	Channel->setVolume(std::clamp(VolumeScale, 0.0f, 1.0f));
	Channel->setPaused(false);
	const FAudioHandle Handle = RegisterChannel(Channel, PathOrKey, Priority);
	if (Handle != 0)
	{
		NoteSFXPlayback(PathOrKey);
	}
	return Handle;
}

FAudioHandle FAudioManager::PlaySound3D(const FString& PathOrKey, const FVector& Position, bool bLoop, float VolumeScale, float MinDistance, float MaxDistance)
{
	if (!System || PathOrKey.empty())
	{
		return 0;
	}

	FMOD::Sound* Sound = ResolveSound(PathOrKey, bLoop);
	if (!Sound)
	{
		return 0;
	}

	int32 Priority = 0;
	if (SFXPlaybackPolicies.contains(PathOrKey))
	{
		const FAudioPlaybackPolicy& Policy = SFXPlaybackPolicies[PathOrKey];
		if (!ApplySFXPlaybackPolicy(PathOrKey, Policy))
		{
			return 0;
		}
		Priority = Policy.Priority;
	}

	FMOD::Channel* Channel = nullptr;
	System->playSound(Sound, SFXGroup, true, &Channel);
	if (!Channel)
	{
		return 0;
	}

	const float ClampedMin = (std::max)(0.01f, MinDistance);
	const float ClampedMax = (std::max)(ClampedMin, MaxDistance);
	FMOD_VECTOR FMODPosition = ToFMODVector(Position);

	Channel->setMode(FMOD_3D | FMOD_3D_LINEARROLLOFF | (bLoop ? FMOD_LOOP_NORMAL : FMOD_LOOP_OFF));
	Channel->set3DMinMaxDistance(ClampedMin, ClampedMax);
	Channel->set3DAttributes(&FMODPosition, nullptr);
	Channel->setVolume(std::clamp(VolumeScale, 0.0f, 1.0f));
	Channel->setPaused(false);
	const FAudioHandle Handle = RegisterChannel(Channel, PathOrKey, Priority);
	if (Handle != 0)
	{
		NoteSFXPlayback(PathOrKey);
	}
	return Handle;
}

FAudioHandle FAudioManager::PlaySFX3D(const FString& PathOrKey, const FVector& Position, float VolumeScale, float MinDistance, float MaxDistance)
{
	return PlaySound3D(PathOrKey, Position, false, VolumeScale, MinDistance, MaxDistance);
}

void FAudioManager::PlayBGM(const FString& Key, float Volume)
{
	if (!System || !Audios.contains(Key))
	{
		return;
	}

	StopBGM();
	System->playSound(Audios[Key], BGMGroup, false, &BGMChannel);

	if (BGMChannel)
	{
		bBGMVolumeFade = false;
		bBGMStopAfterVolumeFade = false;
		BGMChannel->setVolume(std::clamp(Volume, 0.0f, 1.0f));
	}
}

void FAudioManager::StopBGM()
{
	bBGMVolumeFade = false;
	bBGMStopAfterVolumeFade = false;
	if (BGMChannel)
	{
		BGMChannel->stop();
		BGMChannel = nullptr;
	}
}

bool FAudioManager::FadeInBGM(float DurationSeconds, float TargetVolume)
{
	return BeginBGMFade(DurationSeconds, TargetVolume, false);
}

bool FAudioManager::FadeOutBGM(float DurationSeconds)
{
	return BeginBGMFade(DurationSeconds, 0.0f, true);
}

void FAudioManager::PlayLoop(const FString& Key, const FString& LoopName, float Volume, float Pitch)
{
	if (!System || !Audios.contains(Key) || LoopName.empty())
	{
		return;
	}

	if (FMOD::Channel* ExistingChannel = FindPlayingLoopChannel(LoopName))
	{
		ExistingChannel->setVolume(std::clamp(Volume, 0.0f, 1.0f));
		ExistingChannel->setPitch(std::clamp(Pitch, 0.1f, 3.0f));
		return;
	}

	FMOD::Channel* Channel = nullptr;
	System->playSound(Audios[Key], SFXGroup, false, &Channel);

	if (Channel)
	{
		Channel->setMode(FMOD_LOOP_NORMAL);
		Channel->setVolume(std::clamp(Volume, 0.0f, 1.0f));
		Channel->setPitch(std::clamp(Pitch, 0.1f, 3.0f));
		LoopChannels[LoopName] = Channel;
	}
}

void FAudioManager::StopLoop(const FString& LoopName)
{
	if (!LoopChannels.contains(LoopName))
	{
		return;
	}

	if (LoopChannels[LoopName])
	{
		LoopChannels[LoopName]->stop();
	}
	LoopChannels.erase(LoopName);
}

void FAudioManager::StopAllLoops()
{
	for (auto& Pair : LoopChannels)
	{
		if (Pair.second)
		{
			Pair.second->stop();
		}
	}
	LoopChannels.clear();
}

void FAudioManager::SetLoopVolume(const FString& LoopName, float Volume)
{
	if (FMOD::Channel* Channel = FindPlayingLoopChannel(LoopName))
	{
		Channel->setVolume(std::clamp(Volume, 0.0f, 1.0f));
	}
}

void FAudioManager::SetLoopPitch(const FString& LoopName, float Pitch)
{
	if (FMOD::Channel* Channel = FindPlayingLoopChannel(LoopName))
	{
		Channel->setPitch(std::clamp(Pitch, 0.1f, 3.0f));
	}
}

bool FAudioManager::IsLoopPlaying(const FString& LoopName)
{
	return FindPlayingLoopChannel(LoopName) != nullptr;
}

FMOD::Sound* FAudioManager::ResolveSound(const FString& PathOrKey, bool bLoop)
{
	if (!System || PathOrKey.empty())
	{
		return nullptr;
	}

	if (!Audios.contains(PathOrKey) && !LoadAudio(PathOrKey, PathOrKey, bLoop))
	{
		return nullptr;
	}

	return Audios.contains(PathOrKey) ? Audios[PathOrKey] : nullptr;
}

FAudioHandle FAudioManager::RegisterChannel(FMOD::Channel* Channel, const FString& SourceKey, int32 Priority)
{
	if (!Channel)
	{
		return 0;
	}

	const FAudioHandle Handle = NextAudioHandle++;
	if (NextAudioHandle <= 0)
	{
		NextAudioHandle = 1;
	}

	FActiveAudioChannel Info;
	Info.Channel = Channel;
	Info.SourceKey = SourceKey;
	Info.Priority = Priority;
	Info.Sequence = NextAudioSequence++;
	if (NextAudioSequence == 0)
	{
		NextAudioSequence = 1;
	}

	ActiveChannels[Handle] = Info;
	return Handle;
}

FMOD::Channel* FAudioManager::FindActiveChannel(FAudioHandle Handle)
{
	if (Handle <= 0 || !ActiveChannels.contains(Handle))
	{
		return nullptr;
	}

	FMOD::Channel* Channel = ActiveChannels[Handle].Channel;
	bool bIsPlaying = false;
	if (!Channel || Channel->isPlaying(&bIsPlaying) != FMOD_OK || !bIsPlaying)
	{
		ActiveChannels.erase(Handle);
		return nullptr;
	}

	return Channel;
}

FMOD::Channel* FAudioManager::FindPlayingLoopChannel(const FString& LoopName)
{
	if (!LoopChannels.contains(LoopName))
	{
		return nullptr;
	}

	FMOD::Channel* Channel = LoopChannels[LoopName];
	bool bIsPlaying = false;
	if (!Channel || Channel->isPlaying(&bIsPlaying) != FMOD_OK || !bIsPlaying)
	{
		LoopChannels.erase(LoopName);
		return nullptr;
	}

	return Channel;
}

bool FAudioManager::BeginBGMFade(float DurationSeconds, float TargetVolume, bool bStopAfterFade)
{
	if (!BGMChannel)
	{
		return false;
	}

	bool bIsPlaying = false;
	if (BGMChannel->isPlaying(&bIsPlaying) != FMOD_OK || !bIsPlaying)
	{
		BGMChannel = nullptr;
		bBGMVolumeFade = false;
		bBGMStopAfterVolumeFade = false;
		return false;
	}

	if (DurationSeconds <= 0.0f)
	{
		const float ClampedTarget = std::clamp(TargetVolume, 0.0f, 1.0f);
		BGMChannel->setVolume(ClampedTarget);
		if (bStopAfterFade)
		{
			StopBGM();
		}
		return true;
	}

	float CurrentVolume = 1.0f;
	if (BGMChannel->getVolume(&CurrentVolume) != FMOD_OK)
	{
		CurrentVolume = 1.0f;
	}

	bBGMVolumeFade = true;
	bBGMStopAfterVolumeFade = bStopAfterFade;
	BGMVolumeFadeStartTimeSeconds = GetAudioTimeSeconds();
	BGMVolumeFadeDurationSeconds = (std::max)(0.0f, DurationSeconds);
	BGMVolumeFadeStartVolume = std::clamp(CurrentVolume, 0.0f, 1.0f);
	BGMVolumeFadeTargetVolume = std::clamp(TargetVolume, 0.0f, 1.0f);
	return true;
}

bool FAudioManager::BeginChannelFade(FAudioHandle Handle, float DurationSeconds, float TargetVolume, bool bStopAfterFade)
{
	if (DurationSeconds <= 0.0f)
	{
		const bool bWasPlaying = IsSoundPlaying(Handle);
		SetSoundVolume(Handle, TargetVolume);
		if (bStopAfterFade)
		{
			StopSound(Handle);
		}
		return bWasPlaying;
	}

	FMOD::Channel* Channel = FindActiveChannel(Handle);
	if (!Channel || !ActiveChannels.contains(Handle))
	{
		return false;
	}

	float CurrentVolume = 1.0f;
	if (Channel->getVolume(&CurrentVolume) != FMOD_OK)
	{
		CurrentVolume = 1.0f;
	}

	FActiveAudioChannel& Info = ActiveChannels[Handle];
	Info.bVolumeFade = true;
	Info.bStopAfterVolumeFade = bStopAfterFade;
	Info.VolumeFadeStartTimeSeconds = GetAudioTimeSeconds();
	Info.VolumeFadeDurationSeconds = (std::max)(0.0f, DurationSeconds);
	Info.VolumeFadeStartVolume = std::clamp(CurrentVolume, 0.0f, 1.0f);
	Info.VolumeFadeTargetVolume = std::clamp(TargetVolume, 0.0f, 1.0f);
	return true;
}

void FAudioManager::UpdateBGMFade()
{
	if (!bBGMVolumeFade)
	{
		return;
	}

	if (!BGMChannel)
	{
		bBGMVolumeFade = false;
		bBGMStopAfterVolumeFade = false;
		return;
	}

	bool bIsPlaying = false;
	if (BGMChannel->isPlaying(&bIsPlaying) != FMOD_OK || !bIsPlaying)
	{
		BGMChannel = nullptr;
		bBGMVolumeFade = false;
		bBGMStopAfterVolumeFade = false;
		return;
	}

	const float Duration = (std::max)(0.0f, BGMVolumeFadeDurationSeconds);
	if (Duration <= 0.0f)
	{
		BGMChannel->setVolume(BGMVolumeFadeTargetVolume);
		if (bBGMStopAfterVolumeFade)
		{
			StopBGM();
		}
		else
		{
			bBGMVolumeFade = false;
		}
		return;
	}

	const double Elapsed = GetAudioTimeSeconds() - BGMVolumeFadeStartTimeSeconds;
	const float Alpha = std::clamp(static_cast<float>(Elapsed / static_cast<double>(Duration)), 0.0f, 1.0f);
	const float Volume = BGMVolumeFadeStartVolume + (BGMVolumeFadeTargetVolume - BGMVolumeFadeStartVolume) * Alpha;
	BGMChannel->setVolume(std::clamp(Volume, 0.0f, 1.0f));

	if (Alpha >= 1.0f)
	{
		if (bBGMStopAfterVolumeFade)
		{
			StopBGM();
		}
		else
		{
			bBGMVolumeFade = false;
			bBGMStopAfterVolumeFade = false;
		}
	}
}

void FAudioManager::UpdateFades()
{
	const double NowSeconds = GetAudioTimeSeconds();
	for (auto It = ActiveChannels.begin(); It != ActiveChannels.end();)
	{
		FActiveAudioChannel& Info = It->second;
		if (!Info.bVolumeFade)
		{
			++It;
			continue;
		}

		bool bIsPlaying = false;
		if (!Info.Channel || Info.Channel->isPlaying(&bIsPlaying) != FMOD_OK || !bIsPlaying)
		{
			It = ActiveChannels.erase(It);
			continue;
		}

		const float Duration = (std::max)(0.0f, Info.VolumeFadeDurationSeconds);
		if (Duration <= 0.0f)
		{
			Info.Channel->setVolume(Info.VolumeFadeTargetVolume);
			if (Info.bStopAfterVolumeFade)
			{
				Info.Channel->stop();
				It = ActiveChannels.erase(It);
			}
			else
			{
				Info.bVolumeFade = false;
				Info.bStopAfterVolumeFade = false;
				++It;
			}
			continue;
		}

		const double Elapsed = NowSeconds - Info.VolumeFadeStartTimeSeconds;
		const float Alpha = std::clamp(static_cast<float>(Elapsed / static_cast<double>(Duration)), 0.0f, 1.0f);
		const float Volume = Info.VolumeFadeStartVolume + (Info.VolumeFadeTargetVolume - Info.VolumeFadeStartVolume) * Alpha;
		Info.Channel->setVolume(std::clamp(Volume, 0.0f, 1.0f));

		if (Alpha >= 1.0f)
		{
			if (Info.bStopAfterVolumeFade)
			{
				Info.Channel->stop();
				It = ActiveChannels.erase(It);
			}
			else
			{
				Info.bVolumeFade = false;
				Info.bStopAfterVolumeFade = false;
				++It;
			}
			continue;
		}

		++It;
	}
}

void FAudioManager::PruneStoppedChannels()
{
	for (auto It = ActiveChannels.begin(); It != ActiveChannels.end();)
	{
		bool bIsPlaying = false;
		FMOD::Channel* Channel = It->second.Channel;
		if (!Channel || Channel->isPlaying(&bIsPlaying) != FMOD_OK || !bIsPlaying)
		{
			It = ActiveChannels.erase(It);
		}
		else
		{
			++It;
		}
	}
}

double FAudioManager::GetAudioTimeSeconds() const
{
	using FClock = std::chrono::steady_clock;
	static const FClock::time_point StartTime = FClock::now();
	return std::chrono::duration<double>(FClock::now() - StartTime).count();
}

bool FAudioManager::ApplySFXPlaybackPolicy(const FString& SourceKey, const FAudioPlaybackPolicy& Policy)
{
	if (SourceKey.empty())
	{
		return false;
	}

	PruneStoppedChannels();

	if (Policy.CooldownSeconds > 0.0f && LastSFXPlaybackTimeSeconds.contains(SourceKey))
	{
		const double ElapsedSeconds = GetAudioTimeSeconds() - LastSFXPlaybackTimeSeconds[SourceKey];
		if (ElapsedSeconds < static_cast<double>(Policy.CooldownSeconds))
		{
			return false;
		}
	}

	if (Policy.MaxConcurrent <= 0)
	{
		return true;
	}

	int32 ActiveCount = 0;
	FAudioHandle OldestReplaceableHandle = 0;
	uint64 OldestReplaceableSequence = 0;
	for (const auto& Pair : ActiveChannels)
	{
		const FActiveAudioChannel& Info = Pair.second;
		if (Info.SourceKey != SourceKey)
		{
			continue;
		}

		++ActiveCount;
		if (Info.Priority <= Policy.Priority
			&& (OldestReplaceableHandle == 0 || Info.Sequence < OldestReplaceableSequence))
		{
			OldestReplaceableHandle = Pair.first;
			OldestReplaceableSequence = Info.Sequence;
		}
	}

	if (ActiveCount < Policy.MaxConcurrent)
	{
		return true;
	}

	if (!Policy.bStopOldest || OldestReplaceableHandle == 0)
	{
		return false;
	}

	if (FMOD::Channel* Channel = FindActiveChannel(OldestReplaceableHandle))
	{
		Channel->stop();
	}
	ActiveChannels.erase(OldestReplaceableHandle);
	return true;
}

void FAudioManager::NoteSFXPlayback(const FString& SourceKey)
{
	if (!SourceKey.empty())
	{
		LastSFXPlaybackTimeSeconds[SourceKey] = GetAudioTimeSeconds();
	}
}

void FAudioManager::SetMasterVolume(float Volume)
{
	MasterVolume = std::clamp(Volume, 0.0f, 1.0f);
	if (MasterGroup)
	{
		MasterGroup->setVolume(MasterVolume);
	}
}

void FAudioManager::SetBGMVolume(float Volume)
{
	BGMVolume = std::clamp(Volume, 0.0f, 1.0f);
	if (BGMGroup)
	{
		BGMGroup->setVolume(BGMVolume);
	}
}

void FAudioManager::SetSFXVolume(float Volume)
{
	SFXVolume = std::clamp(Volume, 0.0f, 1.0f);
	if (SFXGroup)
	{
		SFXGroup->setVolume(SFXVolume);
	}
}

void FAudioManager::SetListener(const FVector& Position, const FVector& Forward, const FVector& Up)
{
	if (!System)
	{
		return;
	}

	const FVector SafeForward = SafeNormal(Forward, FVector::ForwardVector);
	const FVector SafeUp = SafeNormal(Up, FVector::UpVector);
	FMOD_VECTOR FMODPosition = ToFMODVector(Position);
	FMOD_VECTOR FMODForward = ToFMODVector(SafeForward);
	FMOD_VECTOR FMODUp = ToFMODVector(SafeUp);
	System->set3DListenerAttributes(0, &FMODPosition, nullptr, &FMODForward, &FMODUp);
}

void FAudioManager::StopSound(FAudioHandle Handle)
{
	if (FMOD::Channel* Channel = FindActiveChannel(Handle))
	{
		Channel->stop();
	}
	ActiveChannels.erase(Handle);
}

bool FAudioManager::FadeInSound(FAudioHandle Handle, float DurationSeconds, float TargetVolume)
{
	return BeginChannelFade(Handle, DurationSeconds, TargetVolume, false);
}

bool FAudioManager::FadeOutSound(FAudioHandle Handle, float DurationSeconds)
{
	return BeginChannelFade(Handle, DurationSeconds, 0.0f, true);
}

bool FAudioManager::FadeInSFX(FAudioHandle Handle, float DurationSeconds, float TargetVolume)
{
	return FadeInSound(Handle, DurationSeconds, TargetVolume);
}

bool FAudioManager::FadeOutSFX(FAudioHandle Handle, float DurationSeconds)
{
	return FadeOutSound(Handle, DurationSeconds);
}

void FAudioManager::StopAllSounds()
{
	for (auto& Pair : ActiveChannels)
	{
		if (Pair.second.Channel)
		{
			Pair.second.Channel->stop();
		}
	}
	ActiveChannels.clear();
}

bool FAudioManager::IsSoundPlaying(FAudioHandle Handle)
{
	return FindActiveChannel(Handle) != nullptr;
}

void FAudioManager::SetSoundVolume(FAudioHandle Handle, float Volume)
{
	if (FMOD::Channel* Channel = FindActiveChannel(Handle))
	{
		if (ActiveChannels.contains(Handle))
		{
			ActiveChannels[Handle].bVolumeFade = false;
			ActiveChannels[Handle].bStopAfterVolumeFade = false;
		}
		Channel->setVolume(std::clamp(Volume, 0.0f, 1.0f));
	}
}

void FAudioManager::SetSoundPitch(FAudioHandle Handle, float Pitch)
{
	if (FMOD::Channel* Channel = FindActiveChannel(Handle))
	{
		Channel->setPitch(std::clamp(Pitch, 0.1f, 3.0f));
	}
}

void FAudioManager::SetSoundPosition(FAudioHandle Handle, const FVector& Position)
{
	if (FMOD::Channel* Channel = FindActiveChannel(Handle))
	{
		FMOD_VECTOR FMODPosition = ToFMODVector(Position);
		Channel->set3DAttributes(&FMODPosition, nullptr);
	}
}

void FAudioManager::SetSFXPlaybackPolicy(const FString& PathOrKey, int32 MaxConcurrent, float CooldownSeconds, int32 Priority, bool bStopOldest)
{
	if (PathOrKey.empty())
	{
		return;
	}

	FAudioPlaybackPolicy Policy;
	Policy.MaxConcurrent = (std::max)(0, MaxConcurrent);
	Policy.CooldownSeconds = (std::max)(0.0f, CooldownSeconds);
	Policy.Priority = Priority;
	Policy.bStopOldest = bStopOldest;
	SFXPlaybackPolicies[PathOrKey] = Policy;
}

void FAudioManager::ClearSFXPlaybackPolicy(const FString& PathOrKey)
{
	if (PathOrKey.empty())
	{
		return;
	}

	SFXPlaybackPolicies.erase(PathOrKey);
	LastSFXPlaybackTimeSeconds.erase(PathOrKey);
}

void FAudioManager::ClearAllSFXPlaybackPolicies()
{
	SFXPlaybackPolicies.clear();
	LastSFXPlaybackTimeSeconds.clear();
}

int32 FAudioManager::GetActiveSoundCount(const FString& PathOrKey)
{
	PruneStoppedChannels();
	if (PathOrKey.empty())
	{
		return static_cast<int32>(ActiveChannels.size());
	}

	int32 Count = 0;
	for (const auto& Pair : ActiveChannels)
	{
		if (Pair.second.SourceKey == PathOrKey)
		{
			++Count;
		}
	}
	return Count;
}

void FAudioManager::LoadDefaultAudios()
{

}
