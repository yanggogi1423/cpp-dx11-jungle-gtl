#include "Animation/LuaAnimInstance.h"

#include "Animation/AnimLuaProgramAsset.h"
#include "Animation/AnimationStateMachine.h"
#include "Asset/AssetFile.h"
#include "Asset/AssetMetaData.h"
#include "Core/Logging/Log.h"
#include "Core/Paths.h"
#include "Object/Object.h"

#include <cstdlib>

namespace
{
	bool TryParseFloat(const FString& Text, float& OutValue)
	{
		char* End = nullptr;
		const float Value = std::strtof(Text.c_str(), &End);
		if (!End || End == Text.c_str() || *End != '\0')
		{
			return false;
		}
		OutValue = Value;
		return true;
	}

	bool TryParseBool(const FString& Text, bool& OutValue)
	{
		if (Text == "true" || Text == "True" || Text == "TRUE" || Text == "1")
		{
			OutValue = true;
			return true;
		}
		if (Text == "false" || Text == "False" || Text == "FALSE" || Text == "0")
		{
			OutValue = false;
			return true;
		}
		return false;
	}
}

void ULuaAnimInstance::Serialize(FArchive& Ar)
{
	UAnimInstance::Serialize(Ar);
	Ar << "LuaAnimProgramAssetPath" << LuaAnimProgramAssetPath;
}

void ULuaAnimInstance::Initialize(USkeletalMeshComponent* InOwnerComponent)
{
	UAnimInstance::Initialize(InOwnerComponent);
	RebuildRuntimeMachine();
}

void ULuaAnimInstance::NativeUpdateAnimation(float DeltaTime)
{
	if (!RuntimeMachine || LoadedAssetPath != FPaths::Normalize(LuaAnimProgramAssetPath))
	{
		RebuildRuntimeMachine();
	}

	if (RuntimeMachine)
	{
		RuntimeMachine->Update(DeltaTime);
	}
}

bool ULuaAnimInstance::EvaluatePose(FPoseContext& OutPoseContext)
{
	return RuntimeMachine ? RuntimeMachine->EvaluatePose(OutPoseContext) : false;
}

void ULuaAnimInstance::SetLuaAnimProgramAssetPath(const FString& InAssetPath)
{
	const FString NormalizedPath = FPaths::Normalize(InAssetPath);
	if (LuaAnimProgramAssetPath == NormalizedPath)
	{
		return;
	}

	LuaAnimProgramAssetPath = NormalizedPath;
	LoadedAssetPath.clear();
	RebuildRuntimeMachine();
}

void ULuaAnimInstance::SetFloat(const FString& Name, float Value)
{
	FloatParams[Name] = Value;
}

void ULuaAnimInstance::SetBool(const FString& Name, bool Value)
{
	BoolParams[Name] = Value;
}

void ULuaAnimInstance::SetInt(const FString& Name, int32 Value)
{
	IntParams[Name] = Value;
}

float ULuaAnimInstance::GetFloat(const FString& Name, float DefaultValue) const
{
	auto It = FloatParams.find(Name);
	return It != FloatParams.end() ? It->second : DefaultValue;
}

bool ULuaAnimInstance::GetBool(const FString& Name, bool DefaultValue) const
{
	auto It = BoolParams.find(Name);
	return It != BoolParams.end() ? It->second : DefaultValue;
}

int32 ULuaAnimInstance::GetInt(const FString& Name, int32 DefaultValue) const
{
	auto It = IntParams.find(Name);
	return It != IntParams.end() ? It->second : DefaultValue;
}

FString ULuaAnimInstance::GetCurrentState() const
{
	return RuntimeMachine ? RuntimeMachine->GetCurrentStateName() : FString();
}

bool ULuaAnimInstance::RebuildRuntimeMachine()
{
	RuntimeMachine = nullptr;
	LoadedAssetPath.clear();

	const FString NormalizedPath = FPaths::Normalize(LuaAnimProgramAssetPath);
	if (NormalizedPath.empty())
	{
		return false;
	}

	FAssetMetaData MetaData;
	FAnimLuaProgramAssetPayload Payload;
	const bool bLoaded = FAssetFile::Load(NormalizedPath, MetaData, [&](FArchive& Ar)
	{
		if (MetaData.ClassName != UAnimLuaProgramAsset::StaticClass()->GetName())
		{
			return false;
		}

		Payload.Serialize(Ar, MetaData.PayloadVersion);
		return true;
	});

	if (!bLoaded)
	{
		UE_LOG_ERROR("[LuaAnimInstance] Failed to load Lua AnimGraph asset: %s", NormalizedPath.c_str());
		return false;
	}

	UAnimationStateMachine* NewMachine = UObjectManager::Get().CreateObject<UAnimationStateMachine>();
	if (!NewMachine)
	{
		return false;
	}

	NewMachine->Initialize(OwnerComponent);

	TMap<int32, FString> StateIdToName;
	for (const auto& Pair : Payload.Graph.States)
	{
		const FLuaAnimStateNode& State = Pair.second;
		if (State.StateId <= 0 || State.Name.empty() || State.AnimationPath.empty())
		{
			continue;
		}

		StateIdToName[State.StateId] = State.Name;
		NewMachine->AddStateFromPathWithPlayback(State.Name, State.AnimationPath, State.PlayRate, State.bLoop, true);
	}

	auto EntryIt = StateIdToName.find(Payload.Graph.InitialStateId);
	if (EntryIt != StateIdToName.end())
	{
		NewMachine->SetEntryStateByName(EntryIt->second);
	}

	for (const auto& Pair : Payload.Graph.Transitions)
	{
		const FLuaAnimTransitionLink& Transition = Pair.second;
		auto FromIt = StateIdToName.find(Transition.FromStateId);
		auto ToIt = StateIdToName.find(Transition.ToStateId);
		if (FromIt == StateIdToName.end() || ToIt == StateIdToName.end())
		{
			continue;
		}

		NewMachine->AddTransition(
			FName(FromIt->second.c_str()),
			FName(ToIt->second.c_str()),
			Transition.BlendTime,
			BuildConditionFunction(Transition),
			0,
			false,
			Transition.bResetTime);
	}

	RuntimeMachine = NewMachine;
	LoadedAssetPath = NormalizedPath;
	return true;
}

FAnimTransitionCondition ULuaAnimInstance::BuildConditionFunction(const FLuaAnimTransitionLink& Transition) const
{
	if (Transition.Conditions.empty())
	{
		return []() { return true; };
	}

	return [this, Transition]()
	{
		if (Transition.Join == EAnimConditionJoin::Or)
		{
			for (const FAnimCondition& Condition : Transition.Conditions)
			{
				if (EvaluateCondition(Condition))
				{
					return true;
				}
			}
			return false;
		}

		for (const FAnimCondition& Condition : Transition.Conditions)
		{
			if (!EvaluateCondition(Condition))
			{
				return false;
			}
		}
		return true;
	};
}

bool ULuaAnimInstance::EvaluateCondition(const FAnimCondition& Condition) const
{
	const FString Left = ResolveConditionValue(Condition);
	const FString Right = Condition.Value;

	bool LeftBool = false;
	bool RightBool = false;
	const bool bBoolean = TryParseBool(Left, LeftBool) && TryParseBool(Right, RightBool);
	if (bBoolean)
	{
		switch (Condition.Operator)
		{
		case EAnimCompareOp::NotEqual:
			return LeftBool != RightBool;
		case EAnimCompareOp::Equal:
			return LeftBool == RightBool;
		default:
			return false;
		}
	}

	float LeftFloat = 0.0f;
	float RightFloat = 0.0f;
	const bool bNumeric = TryParseFloat(Left, LeftFloat) && TryParseFloat(Right, RightFloat);

	if (bNumeric)
	{
		switch (Condition.Operator)
		{
		case EAnimCompareOp::NotEqual:
			return LeftFloat != RightFloat;
		case EAnimCompareOp::Less:
			return LeftFloat < RightFloat;
		case EAnimCompareOp::LessEqual:
			return LeftFloat <= RightFloat;
		case EAnimCompareOp::Greater:
			return LeftFloat > RightFloat;
		case EAnimCompareOp::GreaterEqual:
			return LeftFloat >= RightFloat;
		case EAnimCompareOp::Equal:
		default:
			return LeftFloat == RightFloat;
		}
	}

	switch (Condition.Operator)
	{
	case EAnimCompareOp::NotEqual:
		return Left != Right;
	case EAnimCompareOp::Equal:
		return Left == Right;
	default:
		return false;
	}
}

FString ULuaAnimInstance::ResolveConditionValue(const FAnimCondition& Condition) const
{
	auto FloatIt = FloatParams.find(Condition.ContextName);
	if (FloatIt != FloatParams.end())
	{
		return std::to_string(FloatIt->second);
	}

	auto BoolIt = BoolParams.find(Condition.ContextName);
	if (BoolIt != BoolParams.end())
	{
		return BoolIt->second ? "true" : "false";
	}

	auto IntIt = IntParams.find(Condition.ContextName);
	if (IntIt != IntParams.end())
	{
		return std::to_string(IntIt->second);
	}

	return Condition.bUseDefaultValue ? Condition.DefaultValue : FString();
}
