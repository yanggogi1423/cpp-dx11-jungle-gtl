#pragma once

#include "Core/CoreTypes.h"

#define NOMINMAX
#include <Windows.h>

// ────────────────────────────────────────────────────────────
// FWindowsPlatformTime — QPC 기반 고정밀 타이머 (발제 스펙)
// 프로젝트 내 모든 QPC 호출을 이 클래스로 통일한다.
// ────────────────────────────────────────────────────────────
class FWindowsPlatformTime
{
public:
	static double GSecondsPerCycle;
	static bool   bInitialized;

	static void InitTiming()
	{
		if (!bInitialized)
		{
			bInitialized = true;
			double Frequency = static_cast<double>(GetFrequency());
			if (Frequency <= 0.0) Frequency = 1.0;
			GSecondsPerCycle = 1.0 / Frequency;
		}
	}

	static float GetSecondsPerCycle()
	{
		if (!bInitialized) InitTiming();
		return static_cast<float>(GSecondsPerCycle);
	}

	static uint64 GetFrequency()
	{
		LARGE_INTEGER Frequency;
		QueryPerformanceFrequency(&Frequency);
		return static_cast<uint64>(Frequency.QuadPart);
	}

	static double ToMilliseconds(uint64 CycleDiff)
	{
		return static_cast<double>(CycleDiff) * GetSecondsPerCycle() * 1000.0;
	}

	static double ToSeconds(uint64 CycleDiff)
	{
		return static_cast<double>(CycleDiff) * GetSecondsPerCycle();
	}

	static uint64 Cycles64()
	{
		LARGE_INTEGER CycleCount;
		QueryPerformanceCounter(&CycleCount);
		return static_cast<uint64>(CycleCount.QuadPart);
	}
};

typedef FWindowsPlatformTime FPlatformTime;

// ────────────────────────────────────────────────────────────
// FScopeCycleCounter — RAII 스코프 타이머 (발제 스펙)
// ────────────────────────────────────────────────────────────
struct TStatId {};

class FScopeCycleCounter
{
public:
	FScopeCycleCounter() : StartCycles(FPlatformTime::Cycles64()) {}
	explicit FScopeCycleCounter(TStatId StatId)
		: StartCycles(FPlatformTime::Cycles64()), UsedStatId(StatId) {}

	~FScopeCycleCounter() { Finish(); }

	uint64 Finish()
	{
		const uint64 EndCycles = FPlatformTime::Cycles64();
		return EndCycles - StartCycles;
	}

private:
	uint64 StartCycles;
	TStatId UsedStatId;
};
