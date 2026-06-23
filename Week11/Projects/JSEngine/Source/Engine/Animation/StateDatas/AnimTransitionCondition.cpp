#include "Pch.h"
#include "AnimTransitionCondition.h"

#include <cmath>

template<>
bool TAnimComparator<float>::Compare(const float& Lhs, EAnimCompareOp Op, const float& Rhs)
{
    constexpr float Epsilon = 1.e-4f;

    switch (Op)
    {
    case EAnimCompareOp::Equal:
        return std::fabs(Lhs - Rhs) <= Epsilon;

    case EAnimCompareOp::NotEqual:
        return std::fabs(Lhs - Rhs) > Epsilon;

    case EAnimCompareOp::Less:
        return Lhs < Rhs;

    case EAnimCompareOp::LessEqual:
        return Lhs < Rhs || std::fabs(Lhs - Rhs) <= Epsilon;

    case EAnimCompareOp::Greater:
        return Lhs > Rhs;

    case EAnimCompareOp::GreaterEqual:
        return Lhs > Rhs || std::fabs(Lhs - Rhs) <= Epsilon;

    default:
        return false;
    }
}

bool UAnimBoolCondition::Evaluate(const FAnimTransitionContext& Context)
{
    if (!Context.Params)
    {
        return false;
    }

    bool Value = false;
    if (!Context.Params->TryGetBool(ParamName, Value))
    {
        return false;
    }

    return Value == ExpectedValue;
}

bool UAnimFloatCompareCondition::Evaluate(const FAnimTransitionContext& Context)
{
    if (!Context.Params)
    {
        return false;
    }

    float Value = 0.0f;
    if (!Context.Params->TryGetFloat(ParamName, Value))
    {
        return false;
    }

    return TAnimComparator<float>::Compare(Value, Op, Threshold);
}

bool UAnimStateTimeCondition::Evaluate(const FAnimTransitionContext& Context)
{
    return Context.StateNormalizedTime >= NormalizedTime;
}

bool AnimCompositeCondition::Evaluate(const FAnimTransitionContext& Context)
{
    switch (Op)
    {
    case EAnimConditionOp::And:
        for (UAnimTransitionCondition* Child : Childrens)
        {
            if (!Child || !Child->Evaluate(Context))
            {
                return false;
            }
        }
        return true;

    case EAnimConditionOp::Or:
        for (UAnimTransitionCondition* Child : Childrens)
        {
            if (Child && Child->Evaluate(Context))
            {
                return true;
            }
        }
        return false;

    case EAnimConditionOp::Not:
        return Childrens.empty() || !Childrens[0] || !Childrens[0]->Evaluate(Context);

    default:
        return false;
    }
}

UAnimTransitionCondition* CreateAnimTransitionCondition(EAnimConditionType Type)
{
    switch (Type)
    {
    case EAnimConditionType::Bool:
        return new UAnimBoolCondition();

    case EAnimConditionType::FloatCompare:
        return new UAnimFloatCompareCondition();

    case EAnimConditionType::StateTime:
        return new UAnimStateTimeCondition();

    case EAnimConditionType::Composite:
        return new AnimCompositeCondition();

    default:
        return nullptr;
    }
}
