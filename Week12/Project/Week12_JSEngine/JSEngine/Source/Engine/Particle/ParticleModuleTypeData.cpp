#include "Particle/ParticleModuleTypeData.h"

#include "Particle/ParticleEmitterInstance.h"

// Function : Create base sprite-style emitter instance
// input : Component, EmitterIndex
// Component : owning particle system component (unused in base)
// EmitterIndex : emitter index within the particle system (unused in base)
// output : new FParticleEmitterInstance owned by caller
FParticleEmitterInstance* UParticleModuleTypeDataBase::CreateInstance(UParticleSystemComponent* Component, int32 EmitterIndex) const
{
	(void)Component;
	(void)EmitterIndex;
	return new FParticleEmitterInstance();
}

// Function : Create sprite emitter instance (same as base — Sprite path preserved)
// input : Component, EmitterIndex
// Component : owning particle system component
// EmitterIndex : emitter index within the particle system
// output : new FParticleEmitterInstance — identical to base, kept explicit for routing clarity
FParticleEmitterInstance* USpriteTypeData::CreateInstance(UParticleSystemComponent* Component, int32 EmitterIndex) const
{
	(void)Component;
	(void)EmitterIndex;
	return new FParticleEmitterInstance();
}
