#include "Component/SoundComponent.h"

#include <algorithm>

void USoundComponent::BeginPlay()
{
	USceneComponent::BeginPlay();

	if (bPlayOnBeginPlay)
	{
		Play();
	}
}

void USoundComponent::EndPlay()
{
	Stop();
	USceneComponent::EndPlay();
}

void USoundComponent::Activate()
{
	USceneComponent::Activate();

	if (bPlayOnBeginPlay && ActiveHandle == 0)
	{
		Play();
	}
}

void USoundComponent::Deactivate()
{
	Stop();
	USceneComponent::Deactivate();
}

void USoundComponent::Play()
{
	if (SoundPath.empty())
	{
		return;
	}

	Stop();

	if (bSpatialized)
	{
		ActiveHandle = FAudioManager::Get().PlaySound3D(
			SoundPath,
			GetWorldLocation(),
			bLooping,
			Volume,
			MinDistance,
			MaxDistance);
	}
	else
	{
		ActiveHandle = FAudioManager::Get().PlaySound2D(SoundPath, bLooping, Volume);
	}

	ApplyRuntimeParameters();
}

void USoundComponent::Stop()
{
	if (ActiveHandle != 0)
	{
		FAudioManager::Get().StopSound(ActiveHandle);
		ActiveHandle = 0;
	}
}

bool USoundComponent::IsPlaying() const
{
	return ActiveHandle != 0 && FAudioManager::Get().IsSoundPlaying(ActiveHandle);
}

void USoundComponent::SetSoundPath(const FString& InSoundPath)
{
	const bool bWasPlaying = IsPlaying();
	if (bWasPlaying)
	{
		Stop();
	}

	SoundPath = InSoundPath;

	if (bWasPlaying && !SoundPath.empty())
	{
		Play();
	}
}

void USoundComponent::SetVolume(float InVolume)
{
	Volume = std::clamp(InVolume, 0.0f, 1.0f);
	if (ActiveHandle != 0)
	{
		FAudioManager::Get().SetSoundVolume(ActiveHandle, Volume);
	}
}

void USoundComponent::SetPitch(float InPitch)
{
	Pitch = std::clamp(InPitch, 0.1f, 3.0f);
	if (ActiveHandle != 0)
	{
		FAudioManager::Get().SetSoundPitch(ActiveHandle, Pitch);
	}
}

void USoundComponent::SetLooping(bool bInLooping)
{
	if (bLooping == bInLooping)
	{
		return;
	}

	const bool bWasPlaying = IsPlaying();
	bLooping = bInLooping;
	if (bWasPlaying)
	{
		Play();
	}
}

void USoundComponent::SetSpatialized(bool bInSpatialized)
{
	if (bSpatialized == bInSpatialized)
	{
		return;
	}

	const bool bWasPlaying = IsPlaying();
	bSpatialized = bInSpatialized;
	if (bWasPlaying)
	{
		Play();
	}
}

void USoundComponent::Set3DMinMaxDistance(float InMinDistance, float InMaxDistance)
{
	MinDistance = (std::max)(0.01f, InMinDistance);
	MaxDistance = (std::max)(MinDistance, InMaxDistance);

	if (bSpatialized && IsPlaying())
	{
		Play();
	}
}

void USoundComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction)
{
	USceneComponent::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (ActiveHandle == 0)
	{
		return;
	}

	if (!FAudioManager::Get().IsSoundPlaying(ActiveHandle))
	{
		ActiveHandle = 0;
		return;
	}

	ApplyRuntimeParameters();
	UpdateSoundPosition();
}

void USoundComponent::ApplyRuntimeParameters()
{
	if (ActiveHandle == 0)
	{
		return;
	}

	FAudioManager::Get().SetSoundVolume(ActiveHandle, Volume);
	FAudioManager::Get().SetSoundPitch(ActiveHandle, Pitch);
}

void USoundComponent::UpdateSoundPosition()
{
	if (ActiveHandle != 0 && bSpatialized)
	{
		FAudioManager::Get().SetSoundPosition(ActiveHandle, GetWorldLocation());
	}
}
