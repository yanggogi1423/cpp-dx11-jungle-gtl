#include "Particle/ParticleModuleTypeDataBeam.h"
#include "Particle/ParticleEmitterInstance.h"
#include "Particle/ParticleBeamEmitterInstance.h"

// Function : Create derived FParticleBeamEmitterInstance for Beam emitter
// input : Component, EmitterIndex
// Component : owning particle system component (unused — passed via Init later)
// EmitterIndex : emitter index within the particle system (unused — passed via Init later)
// output : new FParticleBeamEmitterInstance owned by caller
FParticleEmitterInstance* UBeamTypeData::CreateInstance(UParticleSystemComponent* Component, int32 EmitterIndex) const
{
    (void)Component;
    (void)EmitterIndex;
    return new FParticleBeamEmitterInstance();
}
