#pragma once

#include "Core/CoreMinimal.h"
#include "Serialization/Archive.h"

#include <string>

class FLuaAnimGraphCodeGenerator;

enum class EAnimBlendMode : int32
{
    Linear,
    EaseIn,
    EaseOut,
    EaseInOut,
};

enum class EAnimConditionJoin : int32
{
    And,
    Or,
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

enum class ELuaAnimGraphNodeSide : int32
{
    Left,
    Right,
    Top,
    Bottom,
    Count,
};

enum class ELuaAnimGraphPinRole : int32
{
    Input,
    Output,
};

struct FAnimCondition
{
    FString ContextName;
    EAnimCompareOp Operator = EAnimCompareOp::Equal;
    FString Value;

    bool bUseDefaultValue = false;
    FString DefaultValue;
};

class FLuaAnimStateNode;

struct FLuaAnimResolvedPin
{
    FLuaAnimStateNode* State = nullptr;
    ELuaAnimGraphNodeSide Side = ELuaAnimGraphNodeSide::Left;
    ELuaAnimGraphPinRole Role = ELuaAnimGraphPinRole::Input;
};

class FLuaAnimStateNode
{
public:
    int32 StateId = 0;
    FString Name;
    FString AnimationPath;
    bool bLoop = true;
    float PlayRate = 1.0f;
    int32 InputPinIds[static_cast<int32>(ELuaAnimGraphNodeSide::Count)] = {};
    int32 OutputPinIds[static_cast<int32>(ELuaAnimGraphNodeSide::Count)] = {};
    float EditorPosX = 0.0f;
    float EditorPosY = 0.0f;

public:
    int32 GetStateId() const { return StateId; }
    const FString& GetName() const { return Name; }
    void SetName(const FString& InName) { Name = InName; }
    int32 GetInputPinId(ELuaAnimGraphNodeSide Side) const;
    int32 GetOutputPinId(ELuaAnimGraphNodeSide Side) const;
    float GetEditorPosX() const { return EditorPosX; }
    float GetEditorPosY() const { return EditorPosY; }
    void SetEditorPosition(float X, float Y);

    bool DrawNode(float NodeWidth);
    void Serialize(FArchive& Ar, int32 PayloadVersion = 4);
};

class FLuaAnimTransitionLink
{
public:
    int32 TransitionId = 0;
    int32 FromStateId = 0;
    int32 ToStateId = 0;
    float BlendTime = 0.15f;
    bool bResetTime = true;
    EAnimBlendMode BlendMode = EAnimBlendMode::Linear;
    EAnimConditionJoin Join = EAnimConditionJoin::And;
    TArray<FAnimCondition> Conditions;

public:
    int32 GetTransitionId() const { return TransitionId; }
    int32 GetFromStateId() const { return FromStateId; }
    int32 GetToStateId() const { return ToStateId; }
    float GetBlendTime() const { return BlendTime; }
    void SetBlendTime(float InBlendTime) { BlendTime = InBlendTime; }
    bool ShouldResetTime() const { return bResetTime; }
    void SetResetTime(bool bInResetTime) { bResetTime = bInResetTime; }
    EAnimBlendMode GetBlendMode() const { return BlendMode; }
    void SetBlendMode(EAnimBlendMode InBlendMode) { BlendMode = InBlendMode; }
    EAnimConditionJoin GetJoin() const { return Join; }
    void SetJoin(EAnimConditionJoin InJoin) { Join = InJoin; }
    TArray<FAnimCondition>& GetConditions() { return Conditions; }
    const TArray<FAnimCondition>& GetConditions() const { return Conditions; }

    void DrawLink(const FLuaAnimStateNode& FromState, const FLuaAnimStateNode& ToState, bool bSelected) const;
    void Serialize(FArchive& Ar);
};

class FLuaAnimGraph
{
public:
    FString MachineName = "Machine";
    int32 NextId = 1;
    int32 InitialStateId = 0;
    FString PreviewSkeletalMeshPath;
    TMap<int32, FLuaAnimStateNode> States;
    TMap<int32, FLuaAnimTransitionLink> Transitions;

public:
    int32 AllocId();

    FLuaAnimStateNode* FindState(int32 StateId);
    FLuaAnimStateNode& AddState(
        const FString& InStateName,
        const FString& InAnimationPath,
        float InEditorPosX,
        float InEditorPosY);
    const FLuaAnimStateNode* FindState(int32 StateId) const;
    FLuaAnimTransitionLink* FindTransition(int32 TransitionId);
    const FLuaAnimTransitionLink* FindTransition(int32 TransitionId) const;
    bool ResolvePin(int32 PinId, FLuaAnimResolvedPin& OutPin);

    FLuaAnimStateNode& AddState();
    bool DeleteState(int32 StateId);
    bool CanCreateTransition(int32 FromStateId, int32 ToStateId) const;
    FLuaAnimTransitionLink* AddTransition(int32 FromStateId, int32 ToStateId);
    bool DeleteTransition(int32 TransitionId);

    void SetInitialState(int32 StateId);
    int32 GetInitialStateId() const { return InitialStateId; }
    int32 CountTransitionsFromState(int32 StateId) const;
    FString GenerateLua() const;
    void Serialize(FArchive& Ar, int32 PayloadVersion = 4);

private:
    FString MakeUniqueStateName(const FString& BaseName) const;
    bool IsStateNameAvailable(const FString& Name, const FLuaAnimStateNode* IgnoredState = nullptr) const;
};

class FLuaAnimTransitionDetailsWidget
{
public:
    bool Draw(
        FLuaAnimTransitionLink& Transition,
        const FLuaAnimStateNode* FromState,
        const FLuaAnimStateNode* ToState);
};

FArchive& operator<<(FArchive& Ar, FAnimCondition& Condition);
FArchive& operator<<(FArchive& Ar, FLuaAnimStateNode& State);
FArchive& operator<<(FArchive& Ar, FLuaAnimTransitionLink& Transition);
FArchive& operator<<(FArchive& Ar, FLuaAnimGraph& Graph);

class FLuaAnimGraphCodeGenerator
{
public:
    FString Generate(const FLuaAnimGraph& Graph) const;

private:
    FString EmitState(const FLuaAnimGraph& Graph, const FLuaAnimStateNode& State, int32 IndentLevel) const;
    FString EmitTransition(const FLuaAnimGraph& Graph, const FLuaAnimTransitionLink& Transition, int32 IndentLevel) const;
    FString EmitConditionFunction(const FLuaAnimTransitionLink& Transition, int32 IndentLevel) const;
    FString EmitCondition(const FAnimCondition& Condition) const;

    const FLuaAnimStateNode* ResolveInitialState(const FLuaAnimGraph& Graph) const;
    FString EscapeLuaString(const FString& Value) const;
    FString ToLuaBool(bool bValue) const;
    FString ToLuaBlendMode(EAnimBlendMode Mode) const;
    FString ToLuaCompareOp(EAnimCompareOp Op) const;
    FString Indent(int32 Level) const;
    FString FormatFloat(float Value) const;
};

FLuaAnimGraph MakeDefaultLuaAnimGraph();
