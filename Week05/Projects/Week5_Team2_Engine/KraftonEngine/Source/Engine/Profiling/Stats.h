#pragma once

#include "Core/CoreTypes.h"
#include "Core/Singleton.h"
#include "Profiling/PlatformTime.h"

#include <cfloat>

// --- 빌드 설정 ---
// Release에서도 프로파일링 UI 및 SCOPE_STAT 활성화 (성능 대회용)
#ifndef STATS
#define STATS 1
#endif

// --- Stat Entry ---
struct FStatEntry
{
	const char* Name = nullptr;
	double MaxTime   = 0.0;
	double MinTime   = DBL_MAX;
	double LastTime  = 0.0;
	double TotalTime = 0.0;
	uint64 Count = 0;

	double GetAverageTime() const
	{
		return Count > 0 ? (TotalTime / static_cast<double>(Count)) : 0.0;
	}
};

// --- Stat Accumulator (이름별 통계) ---
struct FStatAccumulator
{
	const char* Name = nullptr;
	double MaxTime  = 0.0;
	double MinTime  = DBL_MAX;
	double LastTime = 0.0;
	double TotalTime = 0.0;
	uint64 Count = 0;

	// SCOPE_STAT_ACCUM용 누적 시간과 모드
	double FrameAccum = 0.0;  // 프레임 내 누적용
	bool bAccumMode = false;
};

// --- Stat Manager (싱글턴) ---
class FStatManager : public TSingleton<FStatManager>
{
	friend class TSingleton<FStatManager>;

public:
	void RecordTime(const char* Name, double ElapsedSeconds);
	void RecordTimeAccum(const char* Name, double ElapsedSeconds);
	void TakeSnapshot();
	void ResetStats();
	bool GetStat(const char* Name, FStatEntry& OutEntry) const;
	const TArray<FStatEntry>& GetSnapshot() const { return Snapshot; }
	LARGE_INTEGER GetFrequency() const { return Frequency; }

private:
	FStatManager();
	~FStatManager() = default;

	TMap<const char*, FStatAccumulator> Stats;
	TArray<FStatEntry> Snapshot;
	LARGE_INTEGER Frequency;
};

// --- Scoped Timer (RAII) — FPlatformTime 기반 ---
class FScopedTimer
{
public:
	FScopedTimer(const char* InName) : Name(InName), StartCycles(FPlatformTime::Cycles64()) {}

	~FScopedTimer()
	{
		const uint64 EndCycles = FPlatformTime::Cycles64();
		double Elapsed = FPlatformTime::ToSeconds(EndCycles - StartCycles);
		FStatManager::Get().RecordTime(Name, Elapsed);
	}

private:
	const char* Name;
	uint64 StartCycles;
};

// --- Scoped Timer (누적 모드) — 프레임 내 여러 번 호출 시 합산 ---
class FScopedTimerAccum
{
public:
	FScopedTimerAccum(const char* InName) : Name(InName), StartCycles(FPlatformTime::Cycles64()) {}

	~FScopedTimerAccum()
	{
		const uint64 EndCycles = FPlatformTime::Cycles64();
		double Elapsed = FPlatformTime::ToSeconds(EndCycles - StartCycles);
		FStatManager::Get().RecordTimeAccum(Name, Elapsed);
	}

private:
	const char* Name;
	uint64 StartCycles;
};

// --- SCOPE_STAT 매크로 ---
#if STATS
#define SCOPE_STAT_CONCAT2(a, b) a##b
#define SCOPE_STAT_CONCAT(a, b)  SCOPE_STAT_CONCAT2(a, b)
#define SCOPE_STAT(Name) FScopedTimer SCOPE_STAT_CONCAT(_ScopedTimer_, __COUNTER__)(Name)
#define SCOPE_STAT_ACCUM(Name) FScopedTimerAccum SCOPE_STAT_CONCAT(_ScopedTimerAccum_, __COUNTER__)(Name)
#else
#define SCOPE_STAT(Name) ((void)0)
#define SCOPE_STAT_ACCUM(Name) ((void)0)
#endif

/*
void AMyActor::HeavyUpdate()
{
    // 이 함수의 전체 실행 시간을 재고 싶을 때
    SCOPE_STAT("Actor.HeavyUpdate");

    // 특정 구간만 재고 싶을 때는 중괄호 { } 로 스코프를 만들어줍니다.
    {
        SCOPE_STAT("Actor.PhysicsMath");
        DoComplexMath(); // 이 함수 실행 시간만 따로 측정됨
    }
}
*/
