#include "AnimationStateMachine.h"

#include "StateDatas/AnimParamStore.h"
#include "StateDatas/AnimStateMachineRuntime.h"
#include "StateDatas/AnimTransitionCondition.h"

#include <algorithm>

namespace
{
void DestroyConditionTree(UAnimTransitionCondition* Condition)
{
    if (!Condition)
    {
        return;
    }

    if (Condition->GetConditionType() == EAnimConditionType::Composite)
    {
        AnimCompositeCondition* Composite = static_cast<AnimCompositeCondition*>(Condition);
        for (UAnimTransitionCondition* Child : Composite->Childrens)
        {
            DestroyConditionTree(Child);
        }
        Composite->Childrens.clear();
    }

    delete Condition;
}

void ClearTransitionConditions(TArray<FAnimTransitionDef>& Transitions)
{
    for (FAnimTransitionDef& Transition : Transitions)
    {
        for (UAnimTransitionCondition* Condition : Transition.Conditions)
        {
            DestroyConditionTree(Condition);
        }
        Transition.Conditions.clear();
    }
}

void AppendConditionNode(
    UAnimTransitionCondition* Condition,
    int32 ParentLocalIndex,
    int32 BaseIndex,
    TArray<int32>& ConditionTypes,
    TArray<int32>& ConditionParentIndices,
    TArray<FName>& ConditionParamNames,
    TArray<int32>& ConditionCompareOps,
    TArray<float>& ConditionFloatValues,
    TArray<int32>& ConditionBoolValues,
    TArray<int32>& ConditionCompositeOps)
{
    if (!Condition)
    {
        return;
    }

    const int32 LocalIndex = static_cast<int32>(ConditionTypes.size()) - BaseIndex;
    const EAnimConditionType Type = Condition->GetConditionType();

    ConditionTypes.push_back(static_cast<int32>(Type));
    ConditionParentIndices.push_back(ParentLocalIndex);
    ConditionParamNames.push_back(FName::None);
    ConditionCompareOps.push_back(static_cast<int32>(EAnimCompareOp::Equal));
    ConditionFloatValues.push_back(0.0f);
    ConditionBoolValues.push_back(0);
    ConditionCompositeOps.push_back(static_cast<int32>(EAnimConditionOp::And));

    const size_t WriteIndex = ConditionTypes.size() - 1;
    switch (Type)
    {
    case EAnimConditionType::Bool:
    {
        UAnimBoolCondition* BoolCondition = static_cast<UAnimBoolCondition*>(Condition);
        ConditionParamNames[WriteIndex] = BoolCondition->ParamName;
        ConditionBoolValues[WriteIndex] = BoolCondition->ExpectedValue ? 1 : 0;
        break;
    }

    case EAnimConditionType::FloatCompare:
    {
        UAnimFloatCompareCondition* FloatCondition = static_cast<UAnimFloatCompareCondition*>(Condition);
        ConditionParamNames[WriteIndex] = FloatCondition->ParamName;
        ConditionCompareOps[WriteIndex] = static_cast<int32>(FloatCondition->Op);
        ConditionFloatValues[WriteIndex] = FloatCondition->Threshold;
        break;
    }

    case EAnimConditionType::StateTime:
    {
        UAnimStateTimeCondition* StateTimeCondition = static_cast<UAnimStateTimeCondition*>(Condition);
        ConditionFloatValues[WriteIndex] = StateTimeCondition->NormalizedTime;
        break;
    }

    case EAnimConditionType::Composite:
    {
        AnimCompositeCondition* Composite = static_cast<AnimCompositeCondition*>(Condition);
        ConditionCompositeOps[WriteIndex] = static_cast<int32>(Composite->Op);
        for (UAnimTransitionCondition* Child : Composite->Childrens)
        {
            AppendConditionNode(
                Child,
                LocalIndex,
                BaseIndex,
                ConditionTypes,
                ConditionParentIndices,
                ConditionParamNames,
                ConditionCompareOps,
                ConditionFloatValues,
                ConditionBoolValues,
                ConditionCompositeOps);
        }
        break;
    }

    default:
        break;
    }
}
} // namespace

const FAnimStateDef* UAnimationStateMachine::FindState(FName Name) const
{
    for (const FAnimStateDef& State : States)
    {
        if (State.Name == Name)
        {
            return &State;
        }
    }

    return nullptr;
}

TArray<const FAnimTransitionDef*> UAnimationStateMachine::GetOutgoingTransitions(FName StateName) const
{
    TArray<const FAnimTransitionDef*> OutTransitions;

    for (const FAnimTransitionDef& Transition : Transitions)
    {
        if (Transition.FromState == StateName)
        {
            OutTransitions.push_back(&Transition);
        }
    }

    return OutTransitions;
}

bool UAnimationStateMachine::Validate(FString& OutError) const
{
    OutError.clear();

    if (InitialState == FName::None)
    {
        OutError = "Animation state machine has no initial state.";
        return false;
    }

    if (States.empty())
    {
        OutError = "Animation state machine has no states.";
        return false;
    }

    for (size_t StateIndex = 0; StateIndex < States.size(); ++StateIndex)
    {
        const FAnimStateDef& State = States[StateIndex];

        if (State.Name == FName::None)
        {
            OutError = "Animation state has invalid name.";
            return false;
        }

        if (State.AnimationPath.empty())
        {
            OutError = "Animation state has empty animation path: " + State.Name.ToString();
            return false;
        }

        for (size_t OtherIndex = StateIndex + 1; OtherIndex < States.size(); ++OtherIndex)
        {
            if (State.Name == States[OtherIndex].Name)
            {
                OutError = "Duplicate animation state name: " + State.Name.ToString();
                return false;
            }
        }
    }

    if (!FindState(InitialState))
    {
        OutError = "Initial state does not exist: " + InitialState.ToString();
        return false;
    }

    for (const FAnimTransitionDef& Transition : Transitions)
    {
        if (Transition.FromState == FName::None)
        {
            OutError = "Animation transition has invalid from state.";
            return false;
        }

        if (Transition.ToState == FName::None)
        {
            OutError = "Animation transition has invalid to state.";
            return false;
        }

        if (!FindState(Transition.FromState))
        {
            OutError = "Animation transition from state does not exist: " + Transition.FromState.ToString();
            return false;
        }

        if (!FindState(Transition.ToState))
        {
            OutError = "Animation transition to state does not exist: " + Transition.ToState.ToString();
            return false;
        }

        if (Transition.BlendTime < 0.0f)
        {
            OutError = "Animation transition has negative blend time: " +
                Transition.FromState.ToString() + " -> " + Transition.ToState.ToString();
            return false;
        }

        for (const UAnimTransitionCondition* Condition : Transition.Conditions)
        {
            if (!Condition)
            {
                OutError = "Animation transition has null condition: " +
                    Transition.FromState.ToString() + " -> " + Transition.ToState.ToString();
                return false;
            }
        }
    }

    return true;
}

void UAnimationStateMachine::Serialize(FArchive& Ar)
{
    UObject::Serialize(Ar);
    Ar << "InitialState" << InitialState;

    TArray<FName> StateNames;
    TArray<FString> StateAnimationPaths;
    TArray<int32> StateLoops;
    TArray<float> StatePlayRates;
    TArray<FVector2> StateGraphPositions;

    TArray<FName> TransitionIds;
    TArray<FName> TransitionFromStates;
    TArray<FName> TransitionToStates;
    TArray<float> TransitionBlendTimes;
    TArray<int32> TransitionResetTimes;
    TArray<int32> TransitionConditionNodeCounts;

    TArray<int32> ConditionTypes;
    TArray<int32> ConditionParentIndices;
    TArray<FName> ConditionParamNames;
    TArray<int32> ConditionCompareOps;
    TArray<float> ConditionFloatValues;
    TArray<int32> ConditionBoolValues;
    TArray<int32> ConditionCompositeOps;

    if (Ar.IsSaving())
    {
        StateNames.reserve(States.size());
        StateAnimationPaths.reserve(States.size());
        StateLoops.reserve(States.size());
        StatePlayRates.reserve(States.size());
        StateGraphPositions.reserve(States.size());

        for (const FAnimStateDef& State : States)
        {
            StateNames.push_back(State.Name);
            StateAnimationPaths.push_back(State.AnimationPath);
            StateLoops.push_back(State.bLoop ? 1 : 0);
            StatePlayRates.push_back(State.PlayRate);
            StateGraphPositions.push_back(State.GraphPosition);
        }

        TransitionIds.reserve(Transitions.size());
        TransitionFromStates.reserve(Transitions.size());
        TransitionToStates.reserve(Transitions.size());
        TransitionBlendTimes.reserve(Transitions.size());
        TransitionResetTimes.reserve(Transitions.size());
        TransitionConditionNodeCounts.reserve(Transitions.size());

        for (const FAnimTransitionDef& Transition : Transitions)
        {
            TransitionIds.push_back(Transition.TransitionId);
            TransitionFromStates.push_back(Transition.FromState);
            TransitionToStates.push_back(Transition.ToState);
            TransitionBlendTimes.push_back(Transition.BlendTime);
            TransitionResetTimes.push_back(Transition.bResetTime ? 1 : 0);

            const int32 BaseIndex = static_cast<int32>(ConditionTypes.size());
            for (UAnimTransitionCondition* Condition : Transition.Conditions)
            {
                AppendConditionNode(
                    Condition,
                    -1,
                    BaseIndex,
                    ConditionTypes,
                    ConditionParentIndices,
                    ConditionParamNames,
                    ConditionCompareOps,
                    ConditionFloatValues,
                    ConditionBoolValues,
                    ConditionCompositeOps);
            }
            TransitionConditionNodeCounts.push_back(static_cast<int32>(ConditionTypes.size()) - BaseIndex);
        }
    }

    Ar << "StateNames" << StateNames;
    Ar << "StateAnimationPaths" << StateAnimationPaths;
    Ar << "StateLoops" << StateLoops;
    Ar << "StatePlayRates" << StatePlayRates;
    Ar << "StateGraphPositions" << StateGraphPositions;

    Ar << "TransitionIds" << TransitionIds;
    Ar << "TransitionFromStates" << TransitionFromStates;
    Ar << "TransitionToStates" << TransitionToStates;
    Ar << "TransitionBlendTimes" << TransitionBlendTimes;
    Ar << "TransitionResetTimes" << TransitionResetTimes;
    Ar << "TransitionConditionNodeCounts" << TransitionConditionNodeCounts;

    Ar << "ConditionTypes" << ConditionTypes;
    Ar << "ConditionParentIndices" << ConditionParentIndices;
    Ar << "ConditionParamNames" << ConditionParamNames;
    Ar << "ConditionCompareOps" << ConditionCompareOps;
    Ar << "ConditionFloatValues" << ConditionFloatValues;
    Ar << "ConditionBoolValues" << ConditionBoolValues;
    Ar << "ConditionCompositeOps" << ConditionCompositeOps;

    if (Ar.IsLoading())
    {
        ClearTransitionConditions(Transitions);
        States.clear();
        Transitions.clear();

        const size_t StateCount = StateNames.size();
        States.reserve(StateCount);
        for (size_t Index = 0; Index < StateCount; ++Index)
        {
            FAnimStateDef State;
            State.Name = StateNames[Index];
            State.AnimationPath = Index < StateAnimationPaths.size() ? StateAnimationPaths[Index] : FString();
            State.bLoop = Index < StateLoops.size() ? StateLoops[Index] != 0 : true;
            State.PlayRate = Index < StatePlayRates.size() ? StatePlayRates[Index] : 1.0f;
            State.GraphPosition = Index < StateGraphPositions.size()
                ? StateGraphPositions[Index]
                : FVector2::ZeroVector;
            States.push_back(State);
        }

        const size_t TransitionCount = TransitionFromStates.size();
        Transitions.reserve(TransitionCount);
        size_t ConditionOffset = 0;
        for (size_t TransitionIndex = 0; TransitionIndex < TransitionCount; ++TransitionIndex)
        {
            FAnimTransitionDef Transition;
            Transition.TransitionId = TransitionIndex < TransitionIds.size()
                ? TransitionIds[TransitionIndex]
                : FName::None;
            Transition.FromState = TransitionFromStates[TransitionIndex];
            Transition.ToState = TransitionIndex < TransitionToStates.size()
                ? TransitionToStates[TransitionIndex]
                : FName::None;
            Transition.BlendTime = TransitionIndex < TransitionBlendTimes.size()
                ? TransitionBlendTimes[TransitionIndex]
                : 0.2f;
            Transition.bResetTime = TransitionIndex < TransitionResetTimes.size()
                ? TransitionResetTimes[TransitionIndex] != 0
                : true;

            const int32 NodeCount = TransitionIndex < TransitionConditionNodeCounts.size()
                ? std::max(0, TransitionConditionNodeCounts[TransitionIndex])
                : 0;
            TArray<UAnimTransitionCondition*> CreatedConditions;
            CreatedConditions.resize(static_cast<size_t>(NodeCount), nullptr);

            for (int32 LocalIndex = 0; LocalIndex < NodeCount; ++LocalIndex)
            {
                const size_t SourceIndex = ConditionOffset + static_cast<size_t>(LocalIndex);
                const EAnimConditionType Type = SourceIndex < ConditionTypes.size()
                    ? static_cast<EAnimConditionType>(ConditionTypes[SourceIndex])
                    : EAnimConditionType::Bool;
                UAnimTransitionCondition* Condition = CreateAnimTransitionCondition(Type);
                CreatedConditions[LocalIndex] = Condition;

                if (!Condition)
                {
                    continue;
                }

                switch (Type)
                {
                case EAnimConditionType::Bool:
                {
                    UAnimBoolCondition* BoolCondition = static_cast<UAnimBoolCondition*>(Condition);
                    BoolCondition->ParamName = SourceIndex < ConditionParamNames.size()
                        ? ConditionParamNames[SourceIndex]
                        : FName::None;
                    BoolCondition->ExpectedValue = SourceIndex < ConditionBoolValues.size()
                        ? ConditionBoolValues[SourceIndex] != 0
                        : true;
                    break;
                }

                case EAnimConditionType::FloatCompare:
                {
                    UAnimFloatCompareCondition* FloatCondition = static_cast<UAnimFloatCompareCondition*>(Condition);
                    FloatCondition->ParamName = SourceIndex < ConditionParamNames.size()
                        ? ConditionParamNames[SourceIndex]
                        : FName::None;
                    FloatCondition->Op = SourceIndex < ConditionCompareOps.size()
                        ? static_cast<EAnimCompareOp>(ConditionCompareOps[SourceIndex])
                        : EAnimCompareOp::Equal;
                    FloatCondition->Threshold = SourceIndex < ConditionFloatValues.size()
                        ? ConditionFloatValues[SourceIndex]
                        : 0.0f;
                    break;
                }

                case EAnimConditionType::StateTime:
                {
                    UAnimStateTimeCondition* StateTimeCondition = static_cast<UAnimStateTimeCondition*>(Condition);
                    StateTimeCondition->NormalizedTime = SourceIndex < ConditionFloatValues.size()
                        ? ConditionFloatValues[SourceIndex]
                        : 1.0f;
                    break;
                }

                case EAnimConditionType::Composite:
                {
                    AnimCompositeCondition* Composite = static_cast<AnimCompositeCondition*>(Condition);
                    Composite->Op = SourceIndex < ConditionCompositeOps.size()
                        ? static_cast<EAnimConditionOp>(ConditionCompositeOps[SourceIndex])
                        : EAnimConditionOp::And;
                    break;
                }

                default:
                    break;
                }
            }

            for (int32 LocalIndex = 0; LocalIndex < NodeCount; ++LocalIndex)
            {
                UAnimTransitionCondition* Condition = CreatedConditions[LocalIndex];
                if (!Condition)
                {
                    continue;
                }

                const size_t SourceIndex = ConditionOffset + static_cast<size_t>(LocalIndex);
                const int32 ParentIndex = SourceIndex < ConditionParentIndices.size()
                    ? ConditionParentIndices[SourceIndex]
                    : -1;

                if (ParentIndex < 0)
                {
                    Transition.Conditions.push_back(Condition);
                    continue;
                }

                if (ParentIndex >= NodeCount || !CreatedConditions[ParentIndex] ||
                    CreatedConditions[ParentIndex]->GetConditionType() != EAnimConditionType::Composite)
                {
                    Transition.Conditions.push_back(Condition);
                    continue;
                }

                AnimCompositeCondition* Parent = static_cast<AnimCompositeCondition*>(CreatedConditions[ParentIndex]);
                Parent->Childrens.push_back(Condition);
            }

            ConditionOffset += static_cast<size_t>(NodeCount);
            Transitions.push_back(Transition);
        }
    }
}

bool UAnimationStateMachine::InitializeRuntime(AnimStateMachineRuntime& Runtime, FString& OutError) const
{
    if (!Validate(OutError))
    {
        Runtime.Reset(FName::None);
        return false;
    }

    Runtime.Reset(InitialState);
    return true;
}

const FAnimTransitionDef* UAnimationStateMachine::FindTriggeredTransition(
    const AnimStateMachineRuntime& Runtime,
    const FAnimParamStore& Params) const
{
    const TArray<const FAnimTransitionDef*> OutgoingTransitions =
        GetOutgoingTransitions(Runtime.CurrentState);

    FAnimTransitionContext Context;
    Context.Params = &Params;
    Context.CurrentState = Runtime.CurrentState;
    Context.StateTime = Runtime.StateTime;
    Context.StateNormalizedTime = Runtime.StateNormalizedTime;
    for (const FAnimTransitionDef* Transition : OutgoingTransitions)
    {
        if (!Transition)
        {
            continue;
        }

        bool bAllConditionsPassed = true;
        for (UAnimTransitionCondition* Condition : Transition->Conditions)
        {
            if (!Condition || !Condition->Evaluate(Context))
            {
                bAllConditionsPassed = false;
                break;
            }
        }

        if (bAllConditionsPassed)
        {
            return Transition;
        }
    }

    return nullptr;
}

void UAnimationStateMachine::UpdateRuntime(
    AnimStateMachineRuntime& Runtime,
    const FAnimParamStore& Params,
    float DeltaTime) const
{
    if (Runtime.Transition.bActive)
    {
        Runtime.Transition.Update(DeltaTime);

        //Transition이 끝났으면 다음 State로 전이
        if (Runtime.Transition.IsFinished())
        {
            Runtime.CurrentState = Runtime.Transition.ToState;
            Runtime.StateTime = 0.0f;
            Runtime.Transition.Clear();
        }
        return;
    }


    Runtime.StateTime += DeltaTime;

    //조건이 전부 충족된 상태전이를 찾는다
    const FAnimTransitionDef* TriggeredTransition = FindTriggeredTransition(Runtime, Params);
    if (!TriggeredTransition)
    {
        return;
    }

    //전이 시작
    Runtime.Transition.Start(
        TriggeredTransition->FromState,
        TriggeredTransition->ToState,
        TriggeredTransition->BlendTime);


    if (Runtime.Transition.IsFinished())
    {
        Runtime.CurrentState = Runtime.Transition.ToState;
        Runtime.StateTime = 0.0f;
        Runtime.Transition.Clear();
    }
}

FAnimPlayRequest UAnimationStateMachine::Evaluate(float DeltaTime, UAnimInstance* AnimInstance)
{
    (void)DeltaTime;
    (void)AnimInstance;

    FAnimPlayRequest Request;

    const FAnimStateDef* State = FindState(InitialState);
    if (!State)
    {
        return Request;
    }

    Request.StateName = State->Name;
    Request.PlayRate = State->PlayRate;
    Request.bLoop = State->bLoop;
    Request.bValid = true;
    return Request;
}
