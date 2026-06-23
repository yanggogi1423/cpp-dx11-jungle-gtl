#include "Editor/Animation/DebugSkelMeshComponent.h"

#include "Editor/Animation/AnimGraphPreviewInstance.h"
#include "Editor/Animation/AnimPreviewInstance.h"

DEFINE_CLASS(UDebugSkelMeshComponent, USkeletalMeshComponent)

void UDebugSkelMeshComponent::SetPreviewAnimation(UAnimationAsset* AnimToPreview)
{
    if (!bEnableAnimation)
    {
        return;
    }

    UAnimPreviewInstance* PreviewInstance = Cast<UAnimPreviewInstance>(AnimInstance);
    if (!PreviewInstance)
    {
        ClearAnimScriptInstance();

        PreviewInstance = UObjectManager::Get().CreateObject<UAnimPreviewInstance>();
        if (!PreviewInstance)
        {
            return;
        }

        PreviewInstance->InitializeAnimation(this);
        AnimInstance = PreviewInstance;
    }

    PreviewInstance->SetPreviewAnimation(AnimToPreview);
    RefreshBoneTransformsFromAnimation();
    MarkSkinningDirty();
}

bool UDebugSkelMeshComponent::SetPreviewPosition(float NewTime)
{
    UAnimPreviewInstance* PreviewInstance = GetPreviewAnimInstance();
    if (!PreviewInstance)
    {
        return false;
    }

    if (!PreviewInstance->SetPreviewPosition(NewTime))
    {
        return false;
    }

    RefreshBoneTransformsFromAnimation();
    return true;
}

void UDebugSkelMeshComponent::ClearPreviewAnimation()
{
    if (!GetPreviewAnimInstance())
    {
        return;
    }

    ClearAnimScriptInstance();
    ResetToBindPose();
}

UAnimPreviewInstance* UDebugSkelMeshComponent::GetPreviewAnimInstance() const
{
    return Cast<UAnimPreviewInstance>(AnimInstance);
}

bool UDebugSkelMeshComponent::SetLuaAnimGraphPreviewState(const FLuaAnimGraphPreviewClipDesc& StateDesc)
{
    if (!bEnableAnimation)
    {
        return false;
    }

    ULuaAnimGraphPreviewInstance* PreviewInstance = GetLuaAnimGraphPreviewInstance();
    if (!PreviewInstance)
    {
        ClearAnimScriptInstance();

        PreviewInstance = UObjectManager::Get().CreateObject<ULuaAnimGraphPreviewInstance>();
        if (!PreviewInstance)
        {
            return false;
        }

        PreviewInstance->InitializeAnimation(this);
        AnimInstance = PreviewInstance;
    }

    if (!PreviewInstance->SetPreviewState(StateDesc))
    {
        RefreshBoneTransformsFromAnimation();
        MarkSkinningDirty();
        return false;
    }

    RefreshBoneTransformsFromAnimation();
    MarkSkinningDirty();
    return true;
}

bool UDebugSkelMeshComponent::SetLuaAnimGraphPreviewTransition(const FLuaAnimGraphPreviewTransitionDesc& TransitionDesc)
{
    if (!bEnableAnimation)
    {
        return false;
    }

    ULuaAnimGraphPreviewInstance* PreviewInstance = GetLuaAnimGraphPreviewInstance();
    if (!PreviewInstance)
    {
        ClearAnimScriptInstance();

        PreviewInstance = UObjectManager::Get().CreateObject<ULuaAnimGraphPreviewInstance>();
        if (!PreviewInstance)
        {
            return false;
        }

        PreviewInstance->InitializeAnimation(this);
        AnimInstance = PreviewInstance;
    }

    if (!PreviewInstance->SetPreviewTransition(TransitionDesc))
    {
        RefreshBoneTransformsFromAnimation();
        MarkSkinningDirty();
        return false;
    }

    RefreshBoneTransformsFromAnimation();
    MarkSkinningDirty();
    return true;
}

void UDebugSkelMeshComponent::ClearLuaAnimGraphPreview()
{
    ULuaAnimGraphPreviewInstance* PreviewInstance = GetLuaAnimGraphPreviewInstance();
    if (!PreviewInstance)
    {
        return;
    }

    PreviewInstance->ClearPreview();
    ResetToBindPose();
    MarkSkinningDirty();
}

ULuaAnimGraphPreviewInstance* UDebugSkelMeshComponent::GetLuaAnimGraphPreviewInstance() const
{
    return Cast<ULuaAnimGraphPreviewInstance>(AnimInstance);
}
