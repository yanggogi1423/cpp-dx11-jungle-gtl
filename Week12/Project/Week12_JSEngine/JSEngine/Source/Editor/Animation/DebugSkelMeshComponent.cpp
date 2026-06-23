#include "Editor/Animation/DebugSkelMeshComponent.h"

#include "Editor/Animation/AnimPreviewInstance.h"
#include "Object/Object.h"

void UDebugSkelMeshComponent::SetPreviewAnimation(UAnimationAsset* AnimToPreview)
{
	UAnimPreviewInstance* PreviewInstance = Cast<UAnimPreviewInstance>(GetAnimInstance());
	if (PreviewInstance && PreviewAnimation == AnimToPreview)
	{
		return;
	}

	if (!PreviewInstance)
	{
		PreviewInstance = UObjectManager::Get().CreateObject<UAnimPreviewInstance>();
		if (!PreviewInstance)
		{
			return;
		}

		PreviewInstance->Initialize(this);
		SetAnimInstance(PreviewInstance);
	}

	PreviewAnimation = AnimToPreview;
	PreviewInstance->SetPreviewAnimation(AnimToPreview);
	ResetToBindPose();
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

	FPoseContext PoseContext;
	if (PreviewInstance->EvaluatePose(PoseContext))
	{
		ApplyAnimationPose(PoseContext);
	}
	MarkSkinningDirty();
	return true;
}

void UDebugSkelMeshComponent::ClearPreviewAnimation()
{
	if (!GetPreviewAnimInstance())
	{
		return;
	}

	SetAnimInstance(nullptr);
	PreviewAnimation = nullptr;
	ResetToBindPose();
}

UAnimPreviewInstance* UDebugSkelMeshComponent::GetPreviewAnimInstance() const
{
	return Cast<UAnimPreviewInstance>(GetAnimInstance());
}
