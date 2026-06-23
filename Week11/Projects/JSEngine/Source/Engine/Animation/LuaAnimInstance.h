#pragma once

#include "Animation/AnimInstance.h"
#include "Animation/AnimSequencePlayerRuntime.h"
#include "ThirdParty/sol/sol.hpp"

enum class EAnimLuaBlendMode : uint8
{
    Linear,
    EaseInOut
};

struct FAnimLuaTransitionRequest
{
    bool bValid = false;
    FString FromState;
    FString ToState;
    float BlendTime = 0.2f;
    bool bResetTime = true;
    EAnimLuaBlendMode BlendMode = EAnimLuaBlendMode::Linear;
};

struct FAnimLuaTransitionRuntime
{
    bool bActive = false;
    FString FromState;
    FString ToState;
    float ElapsedTime = 0.0f;
    float Duration = 0.2f;
    EAnimLuaBlendMode BlendMode = EAnimLuaBlendMode::Linear;

    void Start(const FString& InFromState, const FString& InToState, float InDuration, EAnimLuaBlendMode InBlendMode);
    void Update(float DeltaTime);
    float GetRawAlpha() const;
    float GetBlendAlpha() const;
    bool IsFinished() const;
    void Clear();
};

UCLASS()
class ULuaAnimInstance : public UAnimInstance
{
public:
    GENERATED_BODY(ULuaAnimInstance, UAnimInstance)

protected:
    void UpdateAnimGraph(float DeltaTime) override;
    bool EvaluateAnimation(const USkeletalMesh* SkeletalMesh, TArray<FMatrix>& OutLocalPose) const override;

public:
    void SetLuaAnimProgramAssetPath(const FString& InAssetPath) { LuaAnimProgramAssetPath = InAssetPath; }
    const FString& GetLuaAnimProgramAssetPath() const { return LuaAnimProgramAssetPath; }

    void SetFloat(const FString& Name, float Value) { FloatParams[Name] = Value; }
    void SetBool(const FString& Name, bool Value) { BoolParams[Name] = Value; }
    void SetInt(const FString& Name, int32 Value) { IntParams[Name] = Value; }

    float GetFloat(const FString& Name, float DefaultValue = 0.0f) const;
    bool GetBool(const FString& Name, bool DefaultValue = false) const;
    int32 GetInt(const FString& Name, int32 DefaultValue = 0) const;

    FString GetLuaCurrentState() const;
    FString GetCurrentState() const { return GetLuaCurrentState(); }
    FString GetTransitionFromState() const { return Transition.FromState; }
    FString GetTransitionToState() const { return Transition.ToState; }
    bool IsInTransition() const { return Transition.bActive; }
    float GetTransitionAlpha() const { return Transition.GetBlendAlpha(); }

public:
    void InitializeAnimation(USkeletalMeshComponent* InOwningComponent) override;
    void UninitializeAnimation() override;

private:
    bool LoadLuaProgram();
    bool CreateLuaMachineInstance();
    bool BuildPlaybackRuntimeFromLuaStates();
    bool TickLuaStateMachine(float DeltaTime, FAnimLuaTransitionRequest& OutRequest);
    bool StartTransition(const FAnimLuaTransitionRequest& Request);
    void CompleteLuaTransition(const FString& ToState);
    void UpdateSequencePlayers(float DeltaTime);
    void FinishTransitionIfNeeded();
    bool AdvanceAndQueueSequencePlayer(FAnimSequencePlayerRuntime& Player, float DeltaTime, float NotifyWeight);
    sol::table BuildLuaContext(float DeltaTime) const;

    bool EvaluateCurrentStatePose(const USkeletalMesh* SkeletalMesh, TArray<FMatrix>& OutLocalPose) const;
    bool EvaluateTransitionPose(const USkeletalMesh* SkeletalMesh, TArray<FMatrix>& OutLocalPose) const;

    FAnimSequencePlayerRuntime* FindPlayer(const FString& StateName);
    const FAnimSequencePlayerRuntime* FindPlayer(const FString& StateName) const;

private:
    FString LuaAnimProgramAssetPath;

    sol::environment LuaEnv;
    sol::table LuaFactoryTable;
    sol::table LuaMachineTable;

    TMap<FString, FAnimSequencePlayerRuntime> SequencePlayers;

    TMap<FString, float> FloatParams;
    TMap<FString, bool> BoolParams;
    TMap<FString, int32> IntParams;

    FAnimLuaTransitionRuntime Transition;
    FString CachedCurrentState;
};
