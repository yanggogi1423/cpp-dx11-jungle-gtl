#pragma once

#include "Animation/AnimSingleNodeInstance.h"

class UAnimationAsset;

UCLASS()
class UAnimPreviewInstance : public UAnimSingleNodeInstance
{
public:
	GENERATED_BODY(UAnimPreviewInstance, UAnimSingleNodeInstance)

	UAnimPreviewInstance();

	void SetPreviewAnimation(UAnimationAsset* InAsset);
	void SyncPreviewPlayback(bool bInPlaying, bool bInLooping, bool bInReverse, float InPlayRate);
	bool SetPreviewPosition(float NewTime);
};
