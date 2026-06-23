#pragma once

#include "Render/Common/ViewTypes.h"

class FMeshBufferManager;
class FRenderBus;
class UParticleLODLevel;
class UParticleModuleLight;
class UParticleSystemComponent;
class UPrimitiveComponent;

class FPrimitiveDrawCommandBuilder
{
public:
    bool CollectPrimitive(UPrimitiveComponent* Primitive, const FShowFlags& ShowFlags, EViewMode ViewMode,
                          FRenderBus& RenderBus, FMeshBufferManager& MeshBufferManager) const;

private:
    static constexpr int32 MaxRenderBusLightCount = 1024;

    static const UParticleModuleLight* FindParticleLightModule(const UParticleLODLevel* LODLevel);
    static void CollectParticleLights(UParticleSystemComponent* ParticleSystemComponent, FRenderBus& RenderBus);
};
