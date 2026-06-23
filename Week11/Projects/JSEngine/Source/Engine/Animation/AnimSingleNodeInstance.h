#pragma once

#include "Animation/AnimInstance.h"
#include "Animation/AnimSequencePlayerRuntime.h"

class UAnimationAsset;
class UAnimSequence;

UCLASS()
class UAnimSingleNodeInstance : public UAnimInstance
{
public:
    GENERATED_BODY(UAnimSingleNodeInstance, UAnimInstance)

public:
    UAnimSingleNodeInstance();

    void SetAnimationAsset(UAnimationAsset* InAsset);
    UAnimSequence* GetAnimationAsset() const { return Player.Sequence; }

    void SetNotifyDispatchEnabled(bool bInEnabled) { bDispatchAnimNotifies = bInEnabled; }
    bool IsNotifyDispatchEnabled() const { return bDispatchAnimNotifies; }

    void PlayAnim(bool bInLooping);
    void StopAnim();
    void PauseAnim();

    void SetPlaying(bool bInPlaying);
    bool IsPlaying() const { return Player.bPlaying; }

    void SetLooping(bool bInLooping);
    bool IsLooping() const { return Player.bLoop; }

    void SetReverse(bool bInReverse);
    bool IsReverse() const { return Player.bReverse; }

    void SetPlayRate(float InPlayRate);
    float GetPlayRate() const { return Player.PlayRate; }

    void SetPosition(float NewTime, bool bFireNotifies = false);
    float GetCurrentAnimTime() const { return Player.CurrentTime; }

    float GetLength() const;

protected:
    void UpdateAnimGraph(float DeltaTime) override;
    bool EvaluateAnimation(const USkeletalMesh* SkeletalMesh, TArray<FMatrix>& OutLocalPose) const override;

private:
    FAnimSequencePlayerRuntime Player;
    bool bDispatchAnimNotifies = true;
};
