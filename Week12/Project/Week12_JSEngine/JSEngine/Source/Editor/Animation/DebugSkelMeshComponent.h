#pragma once

#include "Component/SkeletalMeshComponent.h"

class UAnimPreviewInstance;
class UAnimationAsset;

UCLASS()
class UDebugSkelMeshComponent : public USkeletalMeshComponent
{
public:
	GENERATED_BODY(UDebugSkelMeshComponent, USkeletalMeshComponent)

	UDebugSkelMeshComponent() = default;
	~UDebugSkelMeshComponent() override = default;

	void SetPreviewAnimation(UAnimationAsset* AnimToPreview);
	bool SetPreviewPosition(float NewTime);
	void ClearPreviewAnimation();
	UAnimPreviewInstance* GetPreviewAnimInstance() const;

private:
	UAnimationAsset* PreviewAnimation = nullptr;
};
