#include "AnimSequencePlayerRuntime.h"

#include "Animation/AnimSequence.h"
#include "Asset/SkeletalMesh.h"
#include "Math/Utils.h"

#include <cmath>

void FAnimSequencePlayerRuntime::Reset()
{
    PreviousTime = 0.0f;
    CurrentTime = 0.0f;
}

float FAnimSequencePlayerRuntime::GetNormalizedTime() const
{
    if (!Sequence)
    {
        return 0.0f;
    }

    const float Length = Sequence->GetPlayLength();
    if (Length <= 0.0f)
    {
        return 0.0f;
    }

    return MathUtil::Clamp(CurrentTime / Length, 0.0f, 1.0f);
}

FAnimSequenceAdvanceResult FAnimSequencePlayerRuntime::Advance(float DeltaTime)
{
    FAnimSequenceAdvanceResult Result;
    Result.PreviousTime = CurrentTime;
    Result.CurrentTime = CurrentTime;

    if (!Sequence || !bPlaying)
    {
        return Result;
    }

    const float Length = Sequence->GetPlayLength();
    if (Length <= 0.0f)
    {
        PreviousTime = 0.0f;
        CurrentTime = 0.0f;
        Result.PreviousTime = 0.0f;
        Result.CurrentTime = 0.0f;
        return Result;
    }

    PreviousTime = CurrentTime;

    const float SignedDeltaTime = bReverse ? -DeltaTime : DeltaTime;
    const float RawNewTime = CurrentTime + SignedDeltaTime * PlayRate;

    Result.bAdvanced = true;
    Result.PreviousTime = PreviousTime;

    if (bLoop)
    {
        Result.bLooped = RawNewTime >= Length || RawNewTime < 0.0f;

        CurrentTime = std::fmod(RawNewTime, Length);
        if (CurrentTime < 0.0f)
        {
            CurrentTime += Length;
        }
    }
    else
    {
        Result.bHitNonLoopBoundary =
            (!bReverse && RawNewTime >= Length) ||
            (bReverse && RawNewTime <= 0.0f);

        CurrentTime = MathUtil::Clamp(RawNewTime, 0.0f, Length);
    }

    Result.CurrentTime = CurrentTime;
    return Result;
}

bool FAnimSequencePlayerRuntime::Evaluate(
    const USkeletalMesh* SkeletalMesh,
    TArray<FMatrix>& OutLocalPose) const
{
    if (!Sequence || !SkeletalMesh)
    {
        return false;
    }

    return Sequence->EvaluateLocalPose(CurrentTime, SkeletalMesh, OutLocalPose);
}
