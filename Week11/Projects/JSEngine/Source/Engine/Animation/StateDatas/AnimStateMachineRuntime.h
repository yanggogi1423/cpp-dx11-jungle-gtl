#pragma once
#include "AnimTransition.h"

struct AnimStateMachineRuntime
{
    FName CurrentState;
    float StateTime = 0.f;
    float StateNormalizedTime = 0.f;

    FAnimTransition Transition;

    void Reset(FName InitialState);
    bool IsInTransition() const;
};
