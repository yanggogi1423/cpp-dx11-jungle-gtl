#pragma once
#include "Particle/ParticleTypes.h"

class UParticleLODLevel;
class UParticleModule;
class UParticleModuleRequired;
class UParticleModuleSpawn;
class UParticleRendererProperties;

struct FCompiledParticleLODData
{
    int32 LODLevelIndex = 0;
    float DistanceThreshold = 0.0f;
    bool bEnabled = false;

	int32 ParticleSize = sizeof(FBaseParticle);
    int32 PayloadSize = 0;
    int32 ParticleStride = sizeof(FBaseParticle);
    int32 MaxActiveParticles = 128;

	UParticleModuleRequired* RequiredModule = nullptr;
    UParticleModuleSpawn* SpawnModule = nullptr;

	TArray<UParticleModule*> SpawnModules;
    TArray<UParticleModule*> UpdateModules;

	UParticleRendererProperties* RendererProperties = nullptr;
    EParticleEmitterRenderMode RenderMode = EParticleEmitterRenderMode::Sprite;

    UParticleLODLevel* SourceLODLevel = nullptr;
};
