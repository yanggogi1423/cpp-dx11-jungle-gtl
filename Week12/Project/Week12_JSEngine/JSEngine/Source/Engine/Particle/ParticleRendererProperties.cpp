#include "Particle/ParticleRendererProperties.h"

#include "Particle/ParticleBeamEmitterInstance.h"
#include "Particle/ParticleEmitterInstance.h"
#include "Particle/ParticleMeshEmitterInstance.h"
#include "Particle/ParticleRibbonEmitterInstance.h"

FParticleEmitterInstance* UParticleRendererProperties::CreateInstance(UParticleSystemComponent* Component, int32 EmitterIndex) const
{
    (void)Component;
    (void)EmitterIndex;
    return new FParticleEmitterInstance();
}

FParticleEmitterInstance* UParticleSpriteRendererProperties::CreateInstance(UParticleSystemComponent* Component, int32 EmitterIndex) const
{
    (void)Component;
    (void)EmitterIndex;
    return new FParticleEmitterInstance();
}

FParticleEmitterInstance* UParticleMeshRendererProperties::CreateInstance(UParticleSystemComponent* Component, int32 EmitterIndex) const
{
    (void)Component;
    (void)EmitterIndex;
    return new FParticleMeshEmitterInstance();
}

UMaterialInterface* UParticleMeshRendererProperties::GetEffectiveMaterial() const
{
    if (bOverrideMaterial && OverrideMaterial)
    {
        return OverrideMaterial;
    }

    if (Mesh)
    {
        const TArray<FStaticMeshSection>& Sections = Mesh->GetSections();
        const TArray<FStaticMeshMaterialSlot>& Slots = Mesh->GetMaterialSlots();

        if (!Sections.empty() && !Slots.empty())
        {
            const int32 SlotIdx = Sections[0].MaterialSlotIndex;
            if (SlotIdx >= 0 && SlotIdx < static_cast<int32>(Slots.size()) && Slots[SlotIdx].Material)
            {
                return Slots[SlotIdx].Material;
            }
        }

        for (const FStaticMeshMaterialSlot& Slot : Slots)
        {
            if (Slot.Material)
            {
                return Slot.Material;
            }
        }
    }

    return nullptr;
}

FParticleEmitterInstance* UParticleRibbonRendererProperties::CreateInstance(UParticleSystemComponent* Component, int32 EmitterIndex) const
{
    (void)Component;
    (void)EmitterIndex;
    return new FParticleRibbonEmitterInstance();
}

FParticleEmitterInstance* UParticleBeamRendererProperties::CreateInstance(UParticleSystemComponent* Component, int32 EmitterIndex) const
{
    (void)Component;
    (void)EmitterIndex;
    return new FParticleBeamEmitterInstance();
}
