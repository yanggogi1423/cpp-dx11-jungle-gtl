#include "Particle/ParticleModuleTypeDataMesh.h"

#include "Engine/Asset/StaticMesh.h"
#include "Particle/ParticleMeshEmitterInstance.h"
#include "Particle/ParticleEmitterInstance.h"
#include "Render/Resource/Material.h"

// Function : Create derived FParticleMeshEmitterInstance for Mesh emitter
// input : Component, EmitterIndex
// Component : owning particle system component (unused — passed via Init later)
// EmitterIndex : emitter index within the particle system (unused — passed via Init later)
// output : new FParticleMeshEmitterInstance owned by caller
FParticleEmitterInstance* UMeshTypeData::CreateInstance(UParticleSystemComponent* Component, int32 EmitterIndex) const
{
    (void)Component;
    (void)EmitterIndex;
    return new FParticleMeshEmitterInstance();
}

// Function : Resolve material with override flag, section[0]'s slot index, and any-non-null fallback
// input : None
// output : OverrideMaterial when bOverrideMaterial, Section[0]'s material slot otherwise, first non-null slot last, nullptr when none
//
// Slot 0이 항상 section[0]의 material slot은 아님 (mesh asset이 slot reordering 가능).
// UStaticMeshComponent::SetStaticMesh가 `Slots[Sections[i].MaterialSlotIndex]` 패턴 사용 — 동일 방식 적용.
UMaterialInterface* UMeshTypeData::GetEffectiveMaterial() const
{
    if (bOverrideMaterial && OverrideMaterial)
    {
        return OverrideMaterial;
    }

    if (Mesh)
    {
        const TArray<FStaticMeshSection>& Sections = Mesh->GetSections();
        const TArray<FStaticMeshMaterialSlot>& Slots = Mesh->GetMaterialSlots();

        // 1순위: Section[0]의 MaterialSlotIndex가 가리키는 slot.
        if (!Sections.empty() && !Slots.empty())
        {
            const int32 SlotIdx = Sections[0].MaterialSlotIndex;
            if (SlotIdx >= 0 && SlotIdx < static_cast<int32>(Slots.size()) && Slots[SlotIdx].Material)
            {
                return Slots[SlotIdx].Material;
            }
        }

        // 2순위: 어떤 slot이든 첫 non-null material.
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
