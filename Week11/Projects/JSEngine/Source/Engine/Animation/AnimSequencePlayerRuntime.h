#pragma once

#include "Core/Containers/Array.h"
#include "Core/Containers/String.h"
#include "Math/Matrix.h"

class UAnimSequence;
class USkeletalMesh;

struct FAnimSequenceAdvanceResult
{
    bool bAdvanced = false;
    bool bLooped = false;
    bool bHitNonLoopBoundary = false;
    float PreviousTime = 0.0f;
    float CurrentTime = 0.0f;
};

struct FAnimSequencePlayerRuntime
{
    FString DebugName;
    FString AnimationPath;
    UAnimSequence* Sequence = nullptr;

    float PreviousTime = 0.0f;
    float CurrentTime = 0.0f;
    float PlayRate = 1.0f;

    bool bLoop = true;
    bool bReverse = false;
    bool bPlaying = true;

    void Reset();
    float GetNormalizedTime() const;
    FAnimSequenceAdvanceResult Advance(float DeltaTime);
    bool Evaluate(const USkeletalMesh* SkeletalMesh, TArray<FMatrix>& OutLocalPose) const;
};
