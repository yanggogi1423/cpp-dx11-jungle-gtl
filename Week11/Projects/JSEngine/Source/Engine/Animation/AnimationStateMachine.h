#pragma once

#include "Animation/AnimTypes.h"
#include "Engine/Object/Object.h"
#include "StateDatas/StateMachineDefs.h"

class UAnimInstance;
struct AnimStateMachineRuntime;
struct FAnimParamStore;

UCLASS()
class UAnimationStateMachine : public UObject
{
public:
    GENERATED_BODY(UAnimationStateMachine, UObject)

    FName InitialState;

    TArray<FAnimStateDef> States;
    TArray<FAnimTransitionDef> Transitions;

    const FAnimStateDef* FindState(FName Name) const;
    TArray<const FAnimTransitionDef*> GetOutgoingTransitions(FName StateName) const;

    bool Validate(FString& OutError) const;
    void Serialize(FArchive& Ar) override;
    bool InitializeRuntime(AnimStateMachineRuntime& Runtime, FString& OutError) const;
    const FAnimTransitionDef* FindTriggeredTransition(
        const AnimStateMachineRuntime& Runtime,
        const FAnimParamStore& Params) const;
    void UpdateRuntime(
        AnimStateMachineRuntime& Runtime,
        const FAnimParamStore& Params,
        float DeltaTime) const;

    virtual FAnimPlayRequest Evaluate(float DeltaTime, UAnimInstance* AnimInstance);
};
