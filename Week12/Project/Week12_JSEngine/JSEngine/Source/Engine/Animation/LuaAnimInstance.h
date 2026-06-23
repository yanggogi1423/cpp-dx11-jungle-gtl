#pragma once

#include "Animation/AnimationStateMachine.h"
#include "Animation/AnimInstance.h"
#include "Animation/LuaAnimGraph.h"

UCLASS()
class ULuaAnimInstance : public UAnimInstance
{
public:
	GENERATED_BODY(ULuaAnimInstance, UAnimInstance)

	void Serialize(FArchive& Ar) override;
	void Initialize(USkeletalMeshComponent* InOwnerComponent) override;
	void NativeUpdateAnimation(float DeltaTime) override;
	bool EvaluatePose(FPoseContext& OutPoseContext) override;

	void SetLuaAnimProgramAssetPath(const FString& InAssetPath);
	const FString& GetLuaAnimProgramAssetPath() const { return LuaAnimProgramAssetPath; }

	void SetFloat(const FString& Name, float Value);
	void SetBool(const FString& Name, bool Value);
	void SetInt(const FString& Name, int32 Value);

	float GetFloat(const FString& Name, float DefaultValue = 0.0f) const;
	bool GetBool(const FString& Name, bool DefaultValue = false) const;
	int32 GetInt(const FString& Name, int32 DefaultValue = 0) const;

	FString GetCurrentState() const;
	bool IsReady() const { return RuntimeMachine != nullptr; }

private:
	bool RebuildRuntimeMachine();
	FAnimTransitionCondition BuildConditionFunction(const FLuaAnimTransitionLink& Transition) const;
	bool EvaluateCondition(const FAnimCondition& Condition) const;
	FString ResolveConditionValue(const FAnimCondition& Condition) const;

private:
	UPROPERTY(DisplayName = "Lua Anim Program")
	FString LuaAnimProgramAssetPath;

	UAnimationStateMachine* RuntimeMachine = nullptr;
	FString LoadedAssetPath;

	TMap<FString, float> FloatParams;
	TMap<FString, bool> BoolParams;
	TMap<FString, int32> IntParams;
};
