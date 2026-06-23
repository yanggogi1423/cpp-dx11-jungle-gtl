#include "CurvePlayback.h"

#include "FloatCurve/FloatCurveAsset.h"
#include "Math/FloatCurve.h"

#include <algorithm>
#include <cmath>

FCurvePlaybackEvalResult FCurvePlaybackEvaluator::Evaluate(const FCurvePlaybackDesc& Desc, float SequenceTime)
{
	FCurvePlaybackEvalResult Result;
	if (!Desc.Curve)
	{
		return Result;
	}

	const float SafePlayRate = std::fabs(Desc.PlayRate) > 0.0001f ? Desc.PlayRate : 1.0f;
	const float SafeDuration = std::max(0.0f, Desc.Duration);
	float LocalTime = (SequenceTime - Desc.StartTime) * SafePlayRate;

	if (LocalTime < 0.0f)
	{
		return Result;
	}

	if (SafeDuration > 0.0001f)
	{
		if (LocalTime > SafeDuration)
		{
			if (!Desc.bLoop)
			{
				return Result;
			}
			LocalTime = std::fmod(LocalTime, SafeDuration);
			if (LocalTime < 0.0f)
			{
				LocalTime += SafeDuration;
			}
		}
	}
	else if (LocalTime > 0.0001f && !Desc.bLoop)
	{
		return Result;
	}

	Result.bActive = true;
	Result.CurveTime = Desc.TimeMappingMode == ECurveTimeMappingMode::NormalizedTime
		? (SafeDuration > 0.0001f ? LocalTime / SafeDuration : 0.0f)
		: LocalTime;
	Result.Value = Desc.Curve->GetCurve().Evaluate(Result.CurveTime);
	return Result;
}
