#pragma once
#include "Core/Containers/Array.h"
#include "Core/Containers/String.h"
#include "Math/Vector.h"
#include "Math/Vector2.h"
#include "Object/FName.h"

class UAnimTransitionCondition;

struct FAnimStateDef
{
    FName Name;
    FString AnimationPath;
    bool bLoop = true;
    float PlayRate = 1.f;
    FVector2 GraphPosition = FVector2::ZeroVector;
};

struct FAnimTransitionDef
{
    FName TransitionId;
    FName FromState;
    FName ToState;
    float BlendTime = .2f;
    bool bResetTime = true;

    TArray<UAnimTransitionCondition*> Conditions;
};
