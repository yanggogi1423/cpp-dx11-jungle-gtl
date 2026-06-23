#pragma once

#include "Animation/AnimInstance.h"
#include "Animation/AnimSequencePlayerRuntime.h"
#include "Animation/LuaAnimInstance.h"

class UAnimSequence;

#ifdef GetCurrentTime
#undef GetCurrentTime
#endif

enum class ELuaAnimGraphPreviewMode : uint8
{
    None,
    State,
    Transition
};

struct FLuaAnimGraphPreviewClipDesc
{
    FString DebugName;
    FString AnimationPath;
    bool bLoop = true;
    float PlayRate = 1.0f;
};

struct FLuaAnimGraphPreviewTransitionDesc
{
    FLuaAnimGraphPreviewClipDesc From;
    FLuaAnimGraphPreviewClipDesc To;

    float BlendTime = 0.15f;
    bool bResetTime = true;
    EAnimLuaBlendMode BlendMode = EAnimLuaBlendMode::Linear;
};

UCLASS()
class ULuaAnimGraphPreviewInstance : public UAnimInstance
{
    GENERATED_BODY(ULuaAnimGraphPreviewInstance, UAnimInstance)

public:
    ULuaAnimGraphPreviewInstance();

    bool SetPreviewState(const FLuaAnimGraphPreviewClipDesc& StateDesc);

    bool SetPreviewTransition(const FLuaAnimGraphPreviewTransitionDesc& TransitionDesc);

    void ClearPreview();

    void SetPlaying(bool bInPlaying) { bPlaying = bInPlaying; }
    bool IsPlaying() const { return bPlaying; }

    void ResetPreview();

    ELuaAnimGraphPreviewMode GetPreviewMode() const { return PreviewMode; }

    float GetCurrentTime() const;
    float GetCurrentNormalizedTime() const;
    float GetTransitionRawAlpha() const;
    float GetTransitionBlendAlpha() const;

    float GetCurrentStateTime() const;
    float GetCurrentStateNormalizedTime() const;

protected:
    void UpdateAnimGraph(float DeltaTime) override;
    bool EvaluateAnimation(const USkeletalMesh* SkeletalMesh, TArray<FMatrix>& OutLocalPose) const override;

private:
    bool BuildPlayerFromClip(const FLuaAnimGraphPreviewClipDesc& Clip, FAnimSequencePlayerRuntime& OutPlayer) const;

    void UpdateStatePreview(float DeltaTime);
    void UpdateTransitionPreview(float DeltaTime);

    bool EvaluateStatePreview(const USkeletalMesh* SkeletalMesh, TArray<FMatrix>& OutLocalPose) const;
    bool EvaluateTransitionPreview(const USkeletalMesh* SkeletalMesh, TArray<FMatrix>& OutLocalPose) const;

    bool AdvanceAndQueueSequencePlayer(FAnimSequencePlayerRuntime& Player, float DeltaTime, float NotifyWeight);

private:
    ELuaAnimGraphPreviewMode PreviewMode = ELuaAnimGraphPreviewMode::None;

    FAnimSequencePlayerRuntime StatePlayer;
    FAnimSequencePlayerRuntime FromPlayer;
    FAnimSequencePlayerRuntime ToPlayer;

    FAnimLuaTransitionRuntime Transition;

    bool bPlaying = true;

    bool bDispatchPreviewNotifies = false;
};
