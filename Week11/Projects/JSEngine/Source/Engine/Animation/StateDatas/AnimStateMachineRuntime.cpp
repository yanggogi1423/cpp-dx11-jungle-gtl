#include "Pch.h"
#include "AnimStateMachineRuntime.h"

void AnimStateMachineRuntime::Reset(FName InitialState)
{
    CurrentState = InitialState;
    StateTime = .0f;
    StateNormalizedTime = .0f;
    Transition.Clear();
}

bool AnimStateMachineRuntime::IsInTransition() const
{
    return Transition.bActive;
}
