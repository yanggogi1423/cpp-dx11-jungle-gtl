# Particle Runtime Render Contract

## Ownership

The particle runtime owns simulation state:

- `UParticleSystemComponent`
- `FParticleEmitterInstance`
- `FBaseParticle`
- spawn, kill, update, LOD selection, and event queue state
- per-emitter render snapshot buffers built from active particles

The renderer owns draw submission:

- `FPrimitiveDrawCommandBuilder`
- `FRenderCommand`
- particle render passes
- vertex factory and texture binding decisions

The renderer must not read `FBaseParticle`, `ParticleData`, or `ParticleIndices` directly.

## Frame Flow

1. `UParticleSystemComponent::TickComponent()` advances emitter instances.
2. `FParticleEmitterInstance::Tick()` spawns, updates, integrates, and kills particles.
3. `FPrimitiveDrawCommandBuilder` calls `UParticleSystemComponent::BuildInstanceData()`.
4. Each emitter converts active `FBaseParticle` entries into render snapshot data.
5. The builder writes snapshot pointers and counts into `FRenderCommand`.
6. The particle render pass consumes the command in the same frame.

## Pointer Lifetime

`FRenderCommand::ParticleInstances` points into memory owned by `FParticleEmitterInstance`.

The pointer is valid only until the next `BuildInstanceData()` call, emitter reset, component template change, or component destruction.

Renderer rules:

- read only
- do not mutate
- do not store beyond the current frame
- do not assume the pointer remains stable across frames

Runtime rules:

- rebuild snapshot data from active particles
- skip killed particles
- expose zero count and null pointer when no renderable particles exist
- keep `FRenderCommand` creation outside particle runtime classes

## Sprite Path

Sprite emitters use:

- `FRenderCommand::ParticleInstances`
- `FRenderCommand::ParticleInstanceCount`
- `FRenderCommand::ParticleTexture`
- `FRenderCommand::ParticleSubUVColumns`
- `FRenderCommand::ParticleSubUVRows`
- `EVertexFactoryType::SpriteParticle`

`FParticleEmitterInstance::BuildInstanceData()` maps active particles into `FSpriteParticleInstanceData`.

The builder publishes one particle render command per renderable emitter instance.

## Forbidden Coupling

- Do not create one component per particle.
- Do not make renderer code depend on particle spawn or kill internals.
- Do not let `UParticleSystemComponent` include or construct `FRenderCommand`.
- Do not let `FParticleEmitterInstance` know about render passes.

`UParticleSystemComponent` exposes render snapshots. `FPrimitiveDrawCommandBuilder` converts them into render commands.
