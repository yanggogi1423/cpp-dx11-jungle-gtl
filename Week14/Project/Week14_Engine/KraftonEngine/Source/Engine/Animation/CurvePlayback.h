#pragma once

#include "Core/Types/CoreTypes.h"

class UFloatCurveAsset;

enum class ECurveApplyMode : uint8
{
	Absolute,
	Additive,
};

enum class ECurveTimeMappingMode : uint8
{
	Seconds,
	NormalizedTime,
};

struct FCurvePlaybackDesc
{
	UFloatCurveAsset* Curve = nullptr;
	FString CurveAssetPath;
	float StartTime = 0.0f;
	float Duration = 1.0f;
	float PlayRate = 1.0f;
	bool bLoop = false;
	ECurveApplyMode ApplyMode = ECurveApplyMode::Absolute;
	ECurveTimeMappingMode TimeMappingMode = ECurveTimeMappingMode::Seconds;
};

struct FCurvePlaybackEvalResult
{
	bool bActive = false;
	float CurveTime = 0.0f;
	float Value = 0.0f;
};

class FCurvePlaybackEvaluator
{
public:
	static FCurvePlaybackEvalResult Evaluate(const FCurvePlaybackDesc& Desc, float SequenceTime);
};
