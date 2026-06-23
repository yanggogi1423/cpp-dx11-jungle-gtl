#pragma once
#include "AnimParamStore.h"
#include "Core/Containers/Array.h"
#include "Object/Object.h"

enum class EAnimConditionType : int32
{
    Bool,
    FloatCompare,
    StateTime,
    Composite
};

enum class EAnimConditionOp : uint8
{
    And,Or,Not
};

enum class EAnimCompareOp : uint8
{
    Equal,
    NotEqual,
    Less,
    LessEqual,
    Greater,
    GreaterEqual,
};

template<typename T>
struct TAnimComparator
{
    static bool Compare(const T& Lhs, EAnimCompareOp Op, const T& Rhs)
    {
        switch (Op)
        {
        case EAnimCompareOp::Equal:        return Lhs == Rhs;
        case EAnimCompareOp::NotEqual:     return Lhs != Rhs;
        case EAnimCompareOp::Less:         return Lhs <  Rhs;
        case EAnimCompareOp::LessEqual:    return Lhs <= Rhs;
        case EAnimCompareOp::Greater:      return Lhs >  Rhs;
        case EAnimCompareOp::GreaterEqual: return Lhs >= Rhs;
        default:                           return false;
        }
    }
};

template<>
bool TAnimComparator<float>::Compare(const float& Lhs, EAnimCompareOp Op, const float& Rhs);

struct FAnimTransitionContext
{
    const FAnimParamStore* Params = nullptr;
    FName CurrentState;
    float StateTime =0.f;
    float StateNormalizedTime = 0.f;
};

class UAnimTransitionCondition : public UObject
{
public:
    virtual bool Evaluate(const FAnimTransitionContext& Context) { (void)Context; return false; }
    virtual EAnimConditionType GetConditionType() const = 0;
};

class UAnimBoolCondition : public UAnimTransitionCondition
{
public:
    FName ParamName;
    bool ExpectedValue = true;

    virtual bool Evaluate(const FAnimTransitionContext& Context) override;
    virtual EAnimConditionType GetConditionType() const override { return EAnimConditionType::Bool; }
};

class UAnimFloatCompareCondition : public UAnimTransitionCondition
{
public:
    FName ParamName;
    EAnimCompareOp Op = EAnimCompareOp::Equal;
    float Threshold = 0.0f;

    virtual bool Evaluate(const FAnimTransitionContext& Context) override;
    virtual EAnimConditionType GetConditionType() const override { return EAnimConditionType::FloatCompare; }
};

class UAnimStateTimeCondition : public UAnimTransitionCondition
{
public:
    float NormalizedTime = 1.f;

    virtual bool Evaluate(const FAnimTransitionContext& Context) override;
    virtual EAnimConditionType GetConditionType() const override { return EAnimConditionType::StateTime; }
};

class AnimCompositeCondition : public UAnimTransitionCondition
{
public:
    EAnimConditionOp Op = EAnimConditionOp::And;
    TArray<UAnimTransitionCondition*> Childrens;

    virtual bool Evaluate(const FAnimTransitionContext& Context) override;
    virtual EAnimConditionType GetConditionType() const override { return EAnimConditionType::Composite; }
};

UAnimTransitionCondition* CreateAnimTransitionCondition(EAnimConditionType Type);
