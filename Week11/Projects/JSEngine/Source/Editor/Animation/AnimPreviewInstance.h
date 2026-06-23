#pragma once

#include "Animation/AnimSingleNodeInstance.h"

class UAnimPreviewInstance : public UAnimSingleNodeInstance
{
    DECLARE_CLASS(UAnimPreviewInstance, UAnimSingleNodeInstance)

public:
    UAnimPreviewInstance();

    void SetPreviewAnimation(UAnimationAsset* InAsset);
    void SyncPreviewPlayback(bool bInPlaying, bool bInLooping, bool bInReverse, float InPlayRate);
    bool SetPreviewPosition(float NewTime);
};
