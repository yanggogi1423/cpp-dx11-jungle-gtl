#include "Editor/Animation/AnimPreviewInstance.h"

DEFINE_CLASS(UAnimPreviewInstance, UAnimSingleNodeInstance)

UAnimPreviewInstance::UAnimPreviewInstance()
{
    SetNotifyDispatchEnabled(false);
}

void UAnimPreviewInstance::SetPreviewAnimation(UAnimationAsset* InAsset)
{
    SetAnimationAsset(InAsset);
}

void UAnimPreviewInstance::SyncPreviewPlayback(
    bool bInPlaying,
    bool bInLooping,
    bool bInReverse,
    float InPlayRate)
{
    SetLooping(bInLooping);
    SetReverse(bInReverse);
    SetPlayRate(InPlayRate);
    SetPlaying(bInPlaying);
}

bool UAnimPreviewInstance::SetPreviewPosition(float NewTime)
{
    SetPosition(NewTime, false);
    return true;
}
