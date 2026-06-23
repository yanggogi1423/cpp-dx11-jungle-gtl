#pragma once

#include "Animation/AnimSequenceBase.h"

class UAnimSequence;

enum class EAnimInterruptMode : uint8
{
    Normal,
    ForceFade,
    ForceImmediate,
    Queue
};

enum class EAnimSameAnimPolicy : uint8
{
    Ignore,
    Restart,
    Continue
};

struct FAnimPlayRequest
{
    FName StateName = FName::None;
    UAnimSequence* Sequence = nullptr;
    uint32 Priority = 0;
    float BlendInTime = 0.15f;
    float BlendOutTime = 0.15f;
    float PlayRate = 1.0f;
    bool bLoop = false;
    bool bReverse = false;
    EAnimInterruptMode InterruptMode = EAnimInterruptMode::Normal;
    EAnimSameAnimPolicy SameAnimPolicy = EAnimSameAnimPolicy::Ignore;
    bool bValid = false;
};
