#pragma once

#include "Animation/AnimInstance.h"
#include "Animation/AnimSequencePlayerRuntime.h"
#include "Animation/StateDatas/AnimParamStore.h"
#include "Animation/StateDatas/AnimStateMachineRuntime.h"
#include "Core/Containers/Map.h"

class UAnimationStateMachine;
struct FAnimTransitionDef;

UCLASS()
class UAnimStateMachineInstance : public UAnimInstance
{
public:
    GENERATED_BODY(UAnimStateMachineInstance, UAnimInstance)

public:
    void SetStateMachine(UAnimationStateMachine* InStateMachine, const FString& InAssetPath = FString());
    UAnimationStateMachine* GetStateMachine() const { return StateMachine; }
    const FString& GetStateMachinePath() const { return StateMachinePath; }

    void SetFloat(FName Name, float Value) { Parameters.SetFloat(Name, Value); }
    void SetBool(FName Name, bool Value) { Parameters.SetBool(Name, Value); }
    void SetInt(FName Name, int32 Value) { Parameters.SetInt(Name, Value); }

    FName GetCurrentState() const { return Runtime.CurrentState; }
    bool IsInTransition() const { return Runtime.Transition.bActive; }
    FName GetTransitionFromState() const { return Runtime.Transition.FromState; }
    FName GetTransitionToState() const { return Runtime.Transition.ToState; }
    int32 GetLoadedPlayerCount() const { return static_cast<int32>(SequencePlayers.size()); }

    void Serialize(FArchive& Ar) override;

protected:
    void NativeInitializeAnimation() override;
    void NativeUninitializeAnimation() override;
    void UpdateAnimGraph(float DeltaTime) override;
    bool EvaluateAnimation(const USkeletalMesh* SkeletalMesh, TArray<FMatrix>& OutLocalPose) const override;

private:
    bool InitializeStateMachineRuntime();
    //Loads AnimSequence
    bool RebuildSequencePlayers();
    bool AdvanceAndQueueSequencePlayer(FAnimSequencePlayerRuntime& Player, float DeltaTime, float NotifyWeight);

    //--------------Evaluate
    bool EvaluateCurrentStatePose(const USkeletalMesh* SkeletalMesh, TArray<FMatrix>& OutLocalPose) const;
    bool EvaluateTransitionPose(const USkeletalMesh* SkeletalMesh, TArray<FMatrix>& OutLocalPose) const;
private://---------------Helper
    FAnimSequencePlayerRuntime* FindPlayer(FName StateName);
    const FAnimSequencePlayerRuntime* FindPlayer(FName StateName) const;
    const FAnimTransitionDef* FindTransitionDef(FName FromState, FName ToState) const;

private:
    UPROPERTY(EditAnywhere, Category = "Animation", DisplayName = "Animation State Machine")
    UAnimationStateMachine* StateMachine = nullptr;
    FString StateMachinePath;

    AnimStateMachineRuntime Runtime;
    FAnimParamStore Parameters;
    TMap<FName, FAnimSequencePlayerRuntime, FName::Hash> SequencePlayers;
};
