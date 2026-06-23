#pragma once

#include "Particle/ParticleEmitterInstance.h"

#define PARTICLE_PTR(Owner, ActiveIndex) \
	((Owner)->GetParticle(ActiveIndex))

#define DECLARE_PARTICLE_PTR \
	FBaseParticle& Particle = *PARTICLE_PTR(Owner, ParticleIndex)

#define BEGIN_UPDATE_LOOP \
	for (int32 ParticleIndex = 0; ParticleIndex < Owner->GetActiveParticleCount(); )

#define END_UPDATE_LOOP \
	++ParticleIndex

// Accesses particle data through the emitter instance's active index mapping.
inline FBaseParticle* GetParticleDirect(FParticleEmitterInstance* Owner, int32 ActiveIndex)
{
	return Owner ? Owner->GetParticle(ActiveIndex) : nullptr;
}
