# Particle Event Dispatch Policy

## Purpose

Particle collision and particle event dispatch must stay separate.

Collision modules answer one question:

- did a particle hit something this frame?

Event dispatch answers a different question:

- who should be notified about the queued event?

Keeping those two jobs separate prevents physics code from knowing about gameplay code, sound, damage, editor tools, or external listeners.

## Ownership

Asset data owns event policy:

- `UParticleModuleCollision`
- `UParticleModuleEventGenerator`
- module enabled flags and editable collision/event settings

Runtime data owns event state:

- `UParticleSystemComponent`
- `FParticleEmitterInstance`
- `FParticleEventCollideData`
- `PendingCollisionEvents`

External gameplay code owns event reactions:

- delegate bindings on `UParticleSystemComponent::OnParticleCollide`
- optional `AParticleEventManager` runtime listener
- damage, sound, script calls, spawning, analytics, or editor notifications

## Official Event Surface

`UParticleSystemComponent::OnParticleCollide` is the official event API.

This follows the Unreal-style component event model:

- the placed component owns the simulation instance
- the component broadcasts runtime events
- gameplay code binds to that component
- assets do not store listener actors or event manager references

`AParticleEventManager` is not the source of truth. It is an optional runtime bridge that can bind to a component and re-broadcast the same event through its own delegate.

## Frame Flow

1. `UParticleSystemComponent::TickComponent()` ticks emitter instances.
2. `FParticleEmitterInstance::Tick()` runs update modules from the current compiled LOD.
3. `UParticleModuleCollision::Update()` performs trace or sweep tests.
4. On hit, collision response is applied to the particle.
5. If event generation is enabled, collision creates `FParticleEventCollideData`.
6. Collision queues the event through `FParticleEmitterInstance::QueueCollisionEvent()`.
7. The emitter forwards the event to `UParticleSystemComponent::QueueCollisionEvent()`.
8. `UParticleModuleEventGenerator::Update()` runs after other update modules.
9. EventGenerator calls `DispatchQueuedParticleEvents()`.
10. `UParticleSystemComponent` broadcasts every queued event through `OnParticleCollide`.
11. `PendingCollisionEvents` is cleared.

## Module Order Rule

`UParticleModuleEventGenerator` must execute after event-producing update modules.

`UParticleLODLevel::CacheModuleLists()` enforces this by moving EventGenerator modules to the end of `UpdateModules`.

This guarantees that collision can queue events before EventGenerator drains the queue in the same frame.

## Runtime Bridge

`AParticleEventManager` is a runtime listener.

Use it when external code wants a central object to observe particle events:

```cpp
AParticleEventManager* Manager = World->SpawnActor<AParticleEventManager>();
Manager->BindToParticleSystemComponent(ParticleSystemComponent);

Manager->OnParticleCollide.Add(
    [](const FParticleEventCollideData& Event)
    {
        // gameplay reaction
    });
```

The binding is deliberately runtime-only:

- `AParticleEventManager` stores `BoundComponent` only in memory
- `BoundComponent` is not a `UPROPERTY`
- asset serialization never writes the dispatcher binding
- changing a particle asset cannot create hidden gameplay dependencies

## Serialization Rule

Particle assets serialize only editable source data:

- `UParticleSystem`
- `UParticleEmitter`
- `UParticleLODLevel`
- `UParticleModule`
- `UParticleRendererProperties`

Particle assets must not serialize runtime event state:

- `UParticleSystemComponent`
- `FParticleEmitterInstance`
- `FBaseParticle`
- `FCompiledParticleLODData`
- `FParticleEventCollideData`
- `PendingCollisionEvents`
- bound delegate handles
- `AParticleEventManager` bindings

`FCompiledParticleLODData` is rebuilt by `CacheEmitterModuleInfo()`.
Event queues are rebuilt naturally by simulation.
Delegate bindings are installed by runtime code.

## Forbidden Coupling

Do not do these:

- Do not call gameplay logic from `UParticleModuleCollision`.
- Do not make collision modules play sound, apply damage, spawn actors, or run scripts.
- Do not store `AActor*` listener references in `UParticleSystem` assets.
- Do not let particle assets own `AParticleEventManager`.
- Do not make `UParticleSystemComponent` automatically scan the world for a dispatcher.
- Do not persist `PendingCollisionEvents` or delegate binding ids.

The collision module may only detect, respond, and queue.
The EventGenerator may only drain and broadcast.
External listeners decide what the event means.

## Why This Structure Works

The system follows single ownership and one-way dependency flow:

```text
Asset modules
  -> runtime emitter instance
  -> particle system component queue
  -> component delegate
  -> optional runtime listener
  -> gameplay response
```

Dependencies do not flow backward.

Gameplay code can depend on particle events.
Particle simulation must not depend on gameplay code.

This keeps particle assets reusable. The same explosion asset can be placed in several worlds or actors, and each runtime component can bind different listeners without mutating the asset.

## Test Coverage

`Run Particle Event Dispatch Smoke Test` verifies the dispatch contract:

- an event can be queued on `UParticleSystemComponent`
- queued events are not broadcast before dispatch
- dispatch broadcasts through the component delegate
- an `AParticleEventManager` bound at runtime receives the same event
- both component and manager queues are cleared after dispatch

`Run Particle Serialization Smoke Test` also rejects runtime particle state tokens in saved `.particlesystem` assets.
