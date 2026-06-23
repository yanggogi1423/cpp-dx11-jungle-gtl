#pragma once

#include "Core/CoreMinimal.h"
#include "Core/Singleton.h"
#include "Particle/ParticleTypes.h"

#include <Windows.h>
#include <cfloat>

// --- 빌드 설정 ---
#ifndef STATS
#define STATS 0
#endif

// --- Stat Entry ---
struct FStatEntry
{
	const char* Name = nullptr;
	uint32 CallCount = 0;
	double TotalTime = 0.0;		// seconds
	double MaxTime   = 0.0;
	double MinTime   = DBL_MAX;
	double LastTime  = 0.0;

	double GetAvgTime() const { return CallCount > 0 ? TotalTime / CallCount : 0.0; }
};

struct FParticleTypeStats
{
	uint32 EmitterCount = 0;
	uint32 ActiveParticleCount = 0;
	uint32 MaxParticleCount = 0;
	uint64 MemoryBytes = 0;
};

struct FParticleStatsFrame
{
	FParticleTypeStats Sprite;
	FParticleTypeStats Mesh;
	FParticleTypeStats Ribbon;
	FParticleTypeStats Beam;

	uint32 ComponentCount = 0;

	uint32 GetTotalActiveParticleCount() const
	{
		return Sprite.ActiveParticleCount + Mesh.ActiveParticleCount + Ribbon.ActiveParticleCount + Beam.ActiveParticleCount;
	}

	uint32 GetTotalEmitterCount() const
	{
		return Sprite.EmitterCount + Mesh.EmitterCount + Ribbon.EmitterCount + Beam.EmitterCount;
	}

	uint64 GetTotalMemoryBytes() const
	{
		return Sprite.MemoryBytes + Mesh.MemoryBytes + Ribbon.MemoryBytes + Beam.MemoryBytes;
	}
};

class FParticleStats : public TSingleton<FParticleStats>
{
	friend class TSingleton<FParticleStats>;

public:
	void ResetCurrent();
	void TakeSnapshot();
	const FParticleStatsFrame& GetSnapshot() const { return Snapshot; }
	const FParticleStatsFrame& GetCurrent() const { return Current; }

	void AddComponent();
	void AddEmitter(EParticleEmitterRenderMode RenderMode, uint32 ActiveParticles, uint32 MaxParticles, uint64 MemoryBytes);

private:
	FParticleStats() = default;
	~FParticleStats() = default;

	FParticleStatsFrame Current;
	FParticleStatsFrame Snapshot;
};

// --- Stat Manager (싱글턴) ---
class FStatManager : public TSingleton<FStatManager>
{
	friend class TSingleton<FStatManager>;

public:
	void RecordTime(const char* Name, double ElapsedSeconds);
	void TakeSnapshot();
	const TArray<FStatEntry>& GetSnapshot() const { return Snapshot; }
	LARGE_INTEGER GetFrequency() const { return Frequency; }

private:
	FStatManager();
	~FStatManager() = default;

	TMap<const char*, FStatEntry> Stats;
	TArray<FStatEntry> Snapshot;
	LARGE_INTEGER Frequency;
};

// --- Scoped Timer (RAII) ---
class FScopedTimer
{
public:
	FScopedTimer(const char* InName) : Name(InName)
	{
		QueryPerformanceCounter(&StartTime);
	}

	~FScopedTimer()
	{
		LARGE_INTEGER EndTime;
		QueryPerformanceCounter(&EndTime);
		double Elapsed = static_cast<double>(EndTime.QuadPart - StartTime.QuadPart)
			/ static_cast<double>(FStatManager::Get().GetFrequency().QuadPart);
		FStatManager::Get().RecordTime(Name, Elapsed);
	}

private:
	const char* Name;
	LARGE_INTEGER StartTime;
};

// --- SCOPE_STAT 매크로 ---
#if STATS
#define SCOPE_STAT_CONCAT2(a, b) a##b
#define SCOPE_STAT_CONCAT(a, b)  SCOPE_STAT_CONCAT2(a, b)
#define SCOPE_STAT(Name) FScopedTimer SCOPE_STAT_CONCAT(_ScopedTimer_, __COUNTER__)(Name)
#else
#define SCOPE_STAT(Name) ((void)0)
#endif
