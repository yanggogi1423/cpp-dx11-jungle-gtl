#include "Particle/ParticleModuleTypeDataRibbon.h"

#include "Particle/ParticleRibbonEmitterInstance.h"

// Function : Create derived FParticleRibbonEmitterInstance for Ribbon emitter
// input : Component, EmitterIndex
// Component : owning particle system component (unused — passed via Init later)
// EmitterIndex : emitter index within the particle system (unused — passed via Init later)
// output : new FParticleRibbonEmitterInstance owned by caller
FParticleEmitterInstance* URibbonTypeData::CreateInstance(UParticleSystemComponent* Component, int32 EmitterIndex) const
{
    (void)Component;
    (void)EmitterIndex;
    return new FParticleRibbonEmitterInstance();
}
