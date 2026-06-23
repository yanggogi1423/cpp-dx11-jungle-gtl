#pragma once

#include <cmath>

namespace FMath
{
	constexpr float Pi = 3.14159265358979323846f;
	constexpr float DegToRad = Pi / 180.0f;
	constexpr float RadToDeg = 180.0f / Pi;
	constexpr float Epsilon = 1e-4f;

	// TODO:
	// 일단 기존 유틸 함수들과의 위치를 통일하고자 float 전용 스칼라 유틸만 이 파일에 둡니다.
	// 나중 가서 늘어나면 템플릿 기반 유틸 또는 스칼라/벡터 분리 헤더로 정리하는 편이 좋을 지도요.
	inline float Clamp(float Val, float Lo, float Hi)
	{
		if (Val >= Hi) return Hi;
		if (Val <= Lo) return Lo;
		return Val;
	}

	inline float Lerp(float A, float B, float Alpha)
	{
		return A + (B - A) * Alpha;
	}

	inline float Exp(float Val)
	{
		return expf(Val);
	}

	inline float Saturate(float Val)
	{
		return Clamp(Val, 0.0f, 1.0f);
	}

	inline float Remap(float Value, float InMin, float InMax, float OutMin, float OutMax)
	{
		if (InMin == InMax) return OutMin;

		const float Alpha = (Value - InMin) / (InMax - InMin);
		return Lerp(OutMin, OutMax, Alpha);
	}
}

// 기존 매크로 호환 — 이행 완료 후 제거
#define M_PI FMath::Pi
#define DEG_TO_RAD FMath::DegToRad
#define RAD_TO_DEG FMath::RadToDeg
#define EPSILON FMath::Epsilon

// 기존 전역 Clamp 호환
inline float Clamp(float val, float lo, float hi) { return FMath::Clamp(val, lo, hi); }
inline float Lerp(float a, float b, float alpha) { return FMath::Lerp(a, b, alpha); }
inline float Exp(float val) { return FMath::Exp(val); }
inline float Saturate(float val) { return FMath::Saturate(val); }
inline float Remap(float value, float inMin, float inMax, float outMin, float outMax) { return FMath::Remap(value, inMin, inMax, outMin, outMax); }
