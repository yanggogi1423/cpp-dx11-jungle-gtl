#include "Core/Logging/Stats.h"

#include <algorithm>

namespace
{
	FParticleTypeStats& SelectParticleTypeStats(FParticleStatsFrame& Frame, EParticleEmitterRenderMode RenderMode)
	{
		switch (RenderMode)
		{
		case EParticleEmitterRenderMode::Mesh:
			return Frame.Mesh;
		case EParticleEmitterRenderMode::Ribbon:
			return Frame.Ribbon;
		case EParticleEmitterRenderMode::Beam:
			return Frame.Beam;
		case EParticleEmitterRenderMode::Sprite:
		default:
			return Frame.Sprite;
		}
	}
}

FStatManager::FStatManager()
{
	QueryPerformanceFrequency(&Frequency);
}

void FStatManager::RecordTime(const char* Name, double ElapsedSeconds)
{
	auto it = Stats.find(Name);
	if (it == Stats.end())
	{
		FStatEntry Entry;
		Entry.Name = Name;
		Entry.CallCount = 1;
		Entry.TotalTime = ElapsedSeconds;
		Entry.MaxTime = ElapsedSeconds;
		Entry.MinTime = ElapsedSeconds;
		Entry.LastTime = ElapsedSeconds;
		Stats[Name] = Entry;
		return;
	}

	FStatEntry& Entry = it->second;
	Entry.CallCount++;
	Entry.TotalTime += ElapsedSeconds;
	Entry.MaxTime = (std::max)(Entry.MaxTime, ElapsedSeconds);
	Entry.MinTime = (std::min)(Entry.MinTime, ElapsedSeconds);
	Entry.LastTime = ElapsedSeconds;
}

void FStatManager::TakeSnapshot()
{
	Snapshot.clear();
	Snapshot.reserve(Stats.size());

	for (auto& [Key, Entry] : Stats)
	{
		Snapshot.push_back(Entry);

		// Reset for next frame
		Entry.CallCount = 0;
		Entry.TotalTime = 0.0;
		Entry.MaxTime = 0.0;
		Entry.MinTime = DBL_MAX;
		Entry.LastTime = 0.0;
	}
}

void FParticleStats::ResetCurrent()
{
	Current = FParticleStatsFrame();
}

void FParticleStats::TakeSnapshot()
{
	Snapshot = Current;
}

void FParticleStats::AddComponent()
{
	Current.ComponentCount++;
}

void FParticleStats::AddEmitter(EParticleEmitterRenderMode RenderMode, uint32 ActiveParticles, uint32 MaxParticles, uint64 MemoryBytes)
{
	FParticleTypeStats& TypeStats = SelectParticleTypeStats(Current, RenderMode);
	TypeStats.EmitterCount++;
	TypeStats.ActiveParticleCount += ActiveParticles;
	TypeStats.MaxParticleCount += MaxParticles;
	TypeStats.MemoryBytes += MemoryBytes;
}
