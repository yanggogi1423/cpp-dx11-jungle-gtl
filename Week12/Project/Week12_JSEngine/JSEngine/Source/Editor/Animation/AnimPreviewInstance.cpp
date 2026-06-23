#include "Editor/Animation/AnimPreviewInstance.h"

#include <cmath>

UAnimPreviewInstance::UAnimPreviewInstance()
{
	SetNotifyDispatchEnabled(false);
}

void UAnimPreviewInstance::SetPreviewAnimation(UAnimationAsset* InAsset)
{
	SetAnimation(Cast<UAnimSequenceBase>(InAsset));
}

void UAnimPreviewInstance::SyncPreviewPlayback(
	bool bInPlaying,
	bool bInLooping,
	bool bInReverse,
	float InPlayRate)
{
	SetLooping(bInLooping);
	SetPlayRate(bInReverse ? -std::abs(InPlayRate) : std::abs(InPlayRate));
	if (bInPlaying)
	{
		Play(bInLooping);
	}
	else
	{
		Pause();
	}
}

bool UAnimPreviewInstance::SetPreviewPosition(float NewTime)
{
	SetPosition(NewTime);
	return true;
}
