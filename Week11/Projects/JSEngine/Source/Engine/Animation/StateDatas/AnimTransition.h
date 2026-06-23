#pragma once
#include "Object/FName.h"

struct FAnimTransition
{
    bool bActive = false;

    FName FromState;
    FName ToState;

    //전이 자체의 진행시간
    float ElapsedTime = 0.f;
    //전이 자체의 전체 시간
    float Duration = 0.f;

    void Start(FName InFromState, FName InToState, float InDuration);
    void Update(float DeltaTime);
    float GetAlpha() const;
    bool IsFinished() const;
    void Clear();
};
