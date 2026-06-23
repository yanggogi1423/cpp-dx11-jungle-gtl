#include "AnimSingleNodeInstance.h"

#include "Animation/AnimationAsset.h"
#include "Animation/AnimSequence.h"
#include "Asset/SkeletalMesh.h"

UAnimSingleNodeInstance::UAnimSingleNodeInstance()
{
    Player.bLoop = false;
    Player.bPlaying = false;
}

void UAnimSingleNodeInstance::SetAnimationAsset(UAnimationAsset* InAsset)
{
    UAnimSequence* NewSequence = Cast<UAnimSequence>(InAsset);

    if (Player.Sequence == NewSequence)
    {
        return;
    }

    Player.Sequence = NewSequence;
    Player.Reset();

    ActiveNotifyStates.clear();
    ResetNotifyQueue();
}

void UAnimSingleNodeInstance::PlayAnim(bool bInLooping)
{
    if (!Player.Sequence)
    {
        return;
    }

    Player.bLoop = bInLooping;
    Player.bPlaying = true;
}

void UAnimSingleNodeInstance::StopAnim()
{
    Player.bPlaying = false;
    Player.Reset();

    ActiveNotifyStates.clear();
    ResetNotifyQueue();
}

void UAnimSingleNodeInstance::PauseAnim()
{
    Player.bPlaying = false;
}

void UAnimSingleNodeInstance::SetPlaying(bool bInPlaying)
{
    Player.bPlaying = bInPlaying;
}

void UAnimSingleNodeInstance::SetLooping(bool bInLooping)
{
    Player.bLoop = bInLooping;
}

void UAnimSingleNodeInstance::SetReverse(bool bInReverse)
{
    Player.bReverse = bInReverse;
}

void UAnimSingleNodeInstance::SetPlayRate(float InPlayRate)
{
    Player.PlayRate = InPlayRate;
}

float UAnimSingleNodeInstance::GetLength() const
{
    return Player.Sequence ? Player.Sequence->GetPlayLength() : 0.0f;
}

void UAnimSingleNodeInstance::SetPosition(float NewTime, bool bFireNotifies)
{
    if (!Player.Sequence)
    {
        return;
    }

    const float Length = GetLength();
    if (Length <= 0.0f)
    {
        Player.Reset();
        return;
    }

    const float OldTime = Player.CurrentTime;
    const float ClampedTime = MathUtil::Clamp(NewTime, 0.0f, Length);

    Player.PreviousTime = OldTime;
    Player.CurrentTime = ClampedTime;

    if (bFireNotifies && bDispatchAnimNotifies)
    {
        const bool bLooped = false;

        QueueSequenceNotifies(
            Player.Sequence,
            Player.PreviousTime,
            Player.CurrentTime,
            false,
            bLooped,
            Player.bReverse,
            1.0f,
            0.0f);

        DispatchQueuedAnimEvents();
    }

    Player.PreviousTime = Player.CurrentTime;
}

void UAnimSingleNodeInstance::UpdateAnimGraph(float DeltaTime)
{
    if (!Player.Sequence)
    {
        return;
    }

    if (!Player.bPlaying)
    {
        return;
    }

    const FAnimSequenceAdvanceResult Result = Player.Advance(DeltaTime);
    if (!Result.bAdvanced)
    {
        if (GetLength() <= 0.0f)
        {
            Player.bPlaying = false;
        }
        return;
    }

    if (Result.bHitNonLoopBoundary)
    {
        Player.bPlaying = false;
    }

    if (bDispatchAnimNotifies)
    {
        QueueSequenceNotifies(
            Player.Sequence,
            Result.PreviousTime,
            Result.CurrentTime,
            Player.bLoop,
            Result.bLooped,
            Player.bReverse,
            1.0f,
            DeltaTime);
    }
}

bool UAnimSingleNodeInstance::EvaluateAnimation(
    const USkeletalMesh* SkeletalMesh,
    TArray<FMatrix>& OutLocalPose) const
{
    if (!Player.Evaluate(SkeletalMesh, OutLocalPose))
    {
        return UAnimInstance::EvaluateAnimation(SkeletalMesh, OutLocalPose);
    }

    return true;
}
