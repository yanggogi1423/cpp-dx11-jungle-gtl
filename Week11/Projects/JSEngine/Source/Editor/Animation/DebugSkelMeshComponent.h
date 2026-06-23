#pragma once

#include "Component/SkeletalMeshComponent.h"

struct FLuaAnimGraphPreviewClipDesc;
struct FLuaAnimGraphPreviewTransitionDesc;
class ULuaAnimGraphPreviewInstance;
class UAnimPreviewInstance;
class UAnimationAsset;

class UDebugSkelMeshComponent : public USkeletalMeshComponent
{
    DECLARE_CLASS(UDebugSkelMeshComponent, USkeletalMeshComponent)

public:
    UDebugSkelMeshComponent() = default;
    ~UDebugSkelMeshComponent() override = default;

    void SetPreviewAnimation(UAnimationAsset* AnimToPreview);
    bool SetPreviewPosition(float NewTime);
    void ClearPreviewAnimation();
    UAnimPreviewInstance* GetPreviewAnimInstance() const;

    bool SetLuaAnimGraphPreviewState(const FLuaAnimGraphPreviewClipDesc& StateDesc);
    bool SetLuaAnimGraphPreviewTransition(const FLuaAnimGraphPreviewTransitionDesc& TransitionDesc);
    void ClearLuaAnimGraphPreview();
    ULuaAnimGraphPreviewInstance* GetLuaAnimGraphPreviewInstance() const;
};
