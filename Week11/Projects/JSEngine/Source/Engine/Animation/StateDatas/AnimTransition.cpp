#include "Pch.h"
#include "AnimTransition.h"

void FAnimTransition::Start(FName InFromState, FName InToState, float InDuration)
{
    bActive = true;
    FromState = InFromState;
    ToState = InToState;
    ElapsedTime = 0.f;
    Duration = InDuration;
}

void FAnimTransition::Update(float DeltaTime)
{
    if (bActive)
    {
        ElapsedTime+=DeltaTime;
    }
}

float FAnimTransition::GetAlpha() const
{
    if (!bActive||Duration<=0.f)
    {
        return 1.f;
    }
    return MathUtil::Clamp(ElapsedTime/Duration,0.f,1.f);
}

bool FAnimTransition::IsFinished() const
{
    return bActive && ElapsedTime>=Duration;
}

void FAnimTransition::Clear()
{
    bActive = false;
    FromState = FName::None;
    ToState = FName::None;
    ElapsedTime = .0f;
    Duration = .0f;
}