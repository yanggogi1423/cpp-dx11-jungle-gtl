#include "Particle/ParticleModule.h"

#include <algorithm>

#include "Core/Random/EngineRandom.h"
#include "Serialization/Archive.h"

namespace
{
	bool IsNonZeroVector(const FVector& Value)
	{
		return Value.X != 0.0f || Value.Y != 0.0f || Value.Z != 0.0f;
	}

	void SerializeFloatCurve(FArchive& Ar, const FString& Key, FFloatCurve& Curve)
	{
		TArray<float> Times;
		TArray<float> Values;
		TArray<int32> InterpModes;
		TArray<int32> TangentModes;
		TArray<float> ArriveTangents;
		TArray<float> LeaveTangents;

		if (Ar.IsSaving())
		{
			Times.reserve(Curve.Keys.size());
			Values.reserve(Curve.Keys.size());
			InterpModes.reserve(Curve.Keys.size());
			TangentModes.reserve(Curve.Keys.size());
			ArriveTangents.reserve(Curve.Keys.size());
			LeaveTangents.reserve(Curve.Keys.size());

			for (const FCurveKey& CurveKey : Curve.Keys)
			{
				Times.push_back(CurveKey.Time);
				Values.push_back(CurveKey.Value);
				InterpModes.push_back(static_cast<int32>(CurveKey.InterpMode));
				TangentModes.push_back(static_cast<int32>(CurveKey.TangentMode));
				ArriveTangents.push_back(CurveKey.ArriveTangent);
				LeaveTangents.push_back(CurveKey.LeaveTangent);
			}
		}

		Ar.BeginObject(Key);
		Ar << "Times" << Times;
		Ar << "Values" << Values;
		Ar << "InterpModes" << InterpModes;
		Ar << "TangentModes" << TangentModes;
		Ar << "ArriveTangents" << ArriveTangents;
		Ar << "LeaveTangents" << LeaveTangents;
		Ar.EndObject();

		if (Ar.IsLoading())
		{
			Curve.Keys.clear();
			Curve.Keys.reserve(Times.size());
			for (int32 Index = 0; Index < static_cast<int32>(Times.size()); ++Index)
			{
				FCurveKey CurveKey;
				CurveKey.Time = Times[Index];
				CurveKey.Value = Index < static_cast<int32>(Values.size()) ? Values[Index] : 0.0f;
				CurveKey.InterpMode = Index < static_cast<int32>(InterpModes.size())
					? static_cast<ECurveInterpMode>(InterpModes[Index])
					: ECurveInterpMode::Cubic;
				CurveKey.TangentMode = Index < static_cast<int32>(TangentModes.size())
					? static_cast<ECurveTangentMode>(TangentModes[Index])
					: ECurveTangentMode::Auto;
				CurveKey.ArriveTangent = Index < static_cast<int32>(ArriveTangents.size()) ? ArriveTangents[Index] : 0.0f;
				CurveKey.LeaveTangent = Index < static_cast<int32>(LeaveTangents.size()) ? LeaveTangents[Index] : 0.0f;
				Curve.Keys.push_back(CurveKey);
			}
			Curve.SortKeys();
		}
	}

	float RandomRange(float Min, float Max)
	{
		return FEngineRandom::Get().RandomFloat(std::min(Min, Max), std::max(Min, Max));
	}

	FVector RandomRangeVector(const FVector& Min, const FVector& Max)
	{
		return FVector(
			RandomRange(Min.X, Max.X),
			RandomRange(Min.Y, Max.Y),
			RandomRange(Min.Z, Max.Z));
	}
}

void UParticleModule::PostDuplicate(UObject* Original)
{
	UObject::PostDuplicate(Original);

	const UParticleModule* SourceModule = Cast<UParticleModule>(Original);
	DistributionRuntimeData = SourceModule
		? SourceModule->DistributionRuntimeData
		: TMap<FString, FParticleDistributionRuntimeData>{};
}

void UParticleModule::Serialize(FArchive& Ar)
{
	UObject::Serialize(Ar);

	TArray<FString> PropertyNames;
	TArray<int32> Kinds;
	TArray<int32> VectorFlags;
	TArray<float> StoredMaxFloats;
	TArray<FVector> StoredMaxVectors;
	TArray<FString> CurveChannelNames;

	if (Ar.IsSaving())
	{
		PropertyNames.reserve(DistributionRuntimeData.size());
		Kinds.reserve(DistributionRuntimeData.size());
		VectorFlags.reserve(DistributionRuntimeData.size());
		StoredMaxFloats.reserve(DistributionRuntimeData.size());
		StoredMaxVectors.reserve(DistributionRuntimeData.size());

		for (const auto& Pair : DistributionRuntimeData)
		{
			PropertyNames.push_back(Pair.first);
			Kinds.push_back(Pair.second.Kind);
			VectorFlags.push_back(Pair.second.bVector ? 1 : 0);
			StoredMaxFloats.push_back(Pair.second.StoredMaxFloat);
			StoredMaxVectors.push_back(Pair.second.StoredMaxVector);
		}
	}

	Ar << "DistributionProperties" << PropertyNames;
	Ar << "DistributionKinds" << Kinds;
	Ar << "DistributionVectorFlags" << VectorFlags;
	Ar << "DistributionStoredMaxFloats" << StoredMaxFloats;
	Ar << "DistributionStoredMaxVectors" << StoredMaxVectors;

	if (Ar.IsLoading())
	{
		DistributionRuntimeData.clear();
		for (int32 Index = 0; Index < static_cast<int32>(PropertyNames.size()); ++Index)
		{
			FParticleDistributionRuntimeData Data;
			Data.Kind = Index < static_cast<int32>(Kinds.size()) ? Kinds[Index] : 0;
			Data.bVector = Index < static_cast<int32>(VectorFlags.size()) ? VectorFlags[Index] != 0 : false;
			Data.StoredMaxFloat = Index < static_cast<int32>(StoredMaxFloats.size()) ? StoredMaxFloats[Index] : 0.0f;
			Data.StoredMaxVector = Index < static_cast<int32>(StoredMaxVectors.size()) ? StoredMaxVectors[Index] : FVector::ZeroVector;
			DistributionRuntimeData[PropertyNames[Index]] = Data;
		}
	}

	for (const FString& PropertyName : PropertyNames)
	{
		FParticleDistributionRuntimeData& Data = DistributionRuntimeData[PropertyName];
		TArray<FString> Channels;
		if (Ar.IsSaving())
		{
			Channels.reserve(Data.Curves.size());
			for (const auto& CurvePair : Data.Curves)
			{
				Channels.push_back(CurvePair.first);
			}
		}

		Ar.BeginObject(FString("DistributionCurve_") + PropertyName);
		Ar << "Channels" << Channels;
		for (const FString& ChannelName : Channels)
		{
			SerializeFloatCurve(Ar, ChannelName, Data.Curves[ChannelName]);
		}
		Ar.EndObject();
	}
}

void UParticleModule::SetDistributionRuntimeData(const FString& PropertyName, const FParticleDistributionRuntimeData& Data)
{
	if (PropertyName.empty())
	{
		return;
	}
	DistributionRuntimeData[PropertyName] = Data;
}

const FParticleDistributionRuntimeData* UParticleModule::FindDistributionRuntimeData(const FString& PropertyName) const
{
	auto It = DistributionRuntimeData.find(PropertyName);
	return It != DistributionRuntimeData.end() ? &It->second : nullptr;
}

float UParticleModule::EvaluateFloatDistribution(const char* PropertyName, float ConstantValue, float UniformMaxValue, float Time) const
{
	const FParticleDistributionRuntimeData* Data = FindDistributionRuntimeData(PropertyName ? PropertyName : "");
	if (!Data)
	{
		return ConstantValue;
	}

	const int32 Kind = std::clamp(Data->Kind, 0, 3);
	if (Kind == static_cast<int32>(EParticleDistributionRuntimeKind::FloatConstantCurve))
	{
		auto CurveIt = Data->Curves.find("Value");
		return CurveIt != Data->Curves.end() ? CurveIt->second.Evaluate(Time) : ConstantValue;
	}
	if (Kind == static_cast<int32>(EParticleDistributionRuntimeKind::FloatUniform))
	{
		const float MaxValue = Data->StoredMaxFloat != 0.0f ? Data->StoredMaxFloat : UniformMaxValue;
		return RandomRange(ConstantValue, MaxValue);
	}
	if (Kind == static_cast<int32>(EParticleDistributionRuntimeKind::FloatUniformCurve))
	{
		const FFloatCurve* MinCurve = nullptr;
		const FFloatCurve* MaxCurve = nullptr;
		if (auto It = Data->Curves.find("Value"); It != Data->Curves.end())
		{
			MinCurve = &It->second;
		}
		if (auto It = Data->Curves.find("MaxValue"); It != Data->Curves.end())
		{
			MaxCurve = &It->second;
		}
		const float MinValue = MinCurve ? MinCurve->Evaluate(Time) : ConstantValue;
		const float MaxValue = MaxCurve ? MaxCurve->Evaluate(Time) : (Data->StoredMaxFloat != 0.0f ? Data->StoredMaxFloat : UniformMaxValue);
		return RandomRange(MinValue, MaxValue);
	}
	return ConstantValue;
}

FVector UParticleModule::EvaluateVectorDistribution(const char* PropertyName, const FVector& ConstantValue, const FVector& UniformMaxValue, float Time) const
{
	const FParticleDistributionRuntimeData* Data = FindDistributionRuntimeData(PropertyName ? PropertyName : "");
	if (!Data)
	{
		return ConstantValue;
	}

	const int32 Kind = std::clamp(Data->Kind, 0, 3);
	auto EvalChannel = [&](const char* ChannelName, float Fallback) -> float
	{
		auto It = Data->Curves.find(ChannelName);
		return It != Data->Curves.end() ? It->second.Evaluate(Time) : Fallback;
	};
	if (Kind == static_cast<int32>(EParticleDistributionRuntimeKind::FloatConstantCurve))
	{
		return FVector(
			EvalChannel("X", ConstantValue.X),
			EvalChannel("Y", ConstantValue.Y),
			EvalChannel("Z", ConstantValue.Z));
	}
	if (Kind == static_cast<int32>(EParticleDistributionRuntimeKind::FloatUniform))
	{
		const FVector MaxValue = IsNonZeroVector(Data->StoredMaxVector) ? Data->StoredMaxVector : UniformMaxValue;
		return RandomRangeVector(ConstantValue, MaxValue);
	}
	if (Kind == static_cast<int32>(EParticleDistributionRuntimeKind::FloatUniformCurve))
	{
		const FVector MinValue(
			EvalChannel("X", ConstantValue.X),
			EvalChannel("Y", ConstantValue.Y),
			EvalChannel("Z", ConstantValue.Z));
		const FVector MaxValue(
			EvalChannel("MaxX", IsNonZeroVector(Data->StoredMaxVector) ? Data->StoredMaxVector.X : UniformMaxValue.X),
			EvalChannel("MaxY", IsNonZeroVector(Data->StoredMaxVector) ? Data->StoredMaxVector.Y : UniformMaxValue.Y),
			EvalChannel("MaxZ", IsNonZeroVector(Data->StoredMaxVector) ? Data->StoredMaxVector.Z : UniformMaxValue.Z));
		return RandomRangeVector(MinValue, MaxValue);
	}
	return ConstantValue;
}
