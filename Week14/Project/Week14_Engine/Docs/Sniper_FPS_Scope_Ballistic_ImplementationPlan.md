# Sniper FPS / Scope / Ballistic Shooting Implementation Plan

## Context

This document defines the implementation plan for the sniper player system used by **Last Overwatch**.

The target scope is the owner's assigned gameplay slice:

- FPS camera look
- scope zoom and scoped sensitivity
- ballistic projectile firing
- aim sway
- hold breath
- recoil
- ammunition data separation
- hit result generation and external event delivery

This plan intentionally stops before score, phase progression, frontline gauge, enemy death, ragdoll presentation, and result flow. Those systems should remain outside this implementation and be connected later through Lua or other gameplay scripts.

The engine currently provides the following useful base systems:

- `APawn` possession and `SetupInputComponent()` flow
- `UInputComponent` runtime input mapping and binding
- `UCameraComponent` camera FOV control
- `UWorld::PhysicsSweep()` and `UWorld::PhysicsRaycast()`
- multicast delegates in `Core/Delegate.h`
- Lua integration through `ULuaScriptComponent` and `LuaScriptManager`

The current engine coordinate system matches Unreal Engine conventions:

- `+X`: forward
- `+Y`: right
- `+Z`: up

The engine camera FOV appears to use **radians**, not degrees. All scope tuning must respect that.

## Current Constraint

The input system is currently being modified by another teammate.

Because of that, implementation should **not** begin with final input bindings yet. The sniper system plan must therefore separate:

- work that depends on finalized input behavior
- work that can be built independently before input integration

Until the teammate input changes are merged or stabilized:

- do not lock in final action names
- do not hardcode temporary input mappings into long-term gameplay code unless they are explicitly marked as provisional
- prioritize architecture and implementation order that allows input hookup to be added late with minimal rework

## Goals

- Implement a reusable sniper player gameplay slice on top of existing engine architecture.
- Keep gameplay rules outside this system.
- Reuse existing `Pawn`, `Camera`, `Physics`, `Delegate`, and Lua systems where possible.
- Build the system in small vertical slices that remain testable.
- Keep the implementation compatible with both Editor Build and Standalone Build.

## No Goals

- No enemy AI work
- No score calculation
- No frontline gauge logic
- No phase logic
- No radio subtitle logic
- No air support sequence
- No enemy death handling inside the sniper firing system
- No ragdoll/slow motion implementation inside the bullet system
- No large input framework redesign
- No renderer refactor
- No generalized weapon framework unless later required

## Recommended Runtime Ownership

### Primary gameplay actor

Create a dedicated sniper pawn:

- `ASniperPawn : public APawn`

Reason:

- `APawn` already matches player possession, control rotation, and input processing flow.
- The sniper role is mostly stationary and does not require a full character controller for MVP.
- We can keep the player slice small and explicit.

### Primary components

Recommended component composition:

- `UCameraComponent`
  - player view
  - scope FOV interpolation
- `USniperWeaponComponent`
  - fire cooldown
  - ammo selection
  - recoil data
  - fire request entry point
- `UBallisticBulletManagerComponent`
  - owns and updates active bullets
  - applies gravity
  - performs sweep/raycast collision
  - emits hit event

Reason for a bullet manager component instead of spawning an actor per bullet:

- lower implementation cost
- easier debug trajectory control
- clearer ownership for an overwatch sniper weapon
- better fit for the spec, which defines `BallisticBullet` as data rather than a full actor

## Component Usage Plan

This system should explicitly reuse existing engine component types where possible.

| Component / System | Existing purpose | Planned use in sniper system | MVP or polish |
|---|---|---|---|
| `APawn` | possessed gameplay actor base | sniper player actor base | MVP |
| `UInputComponent` | runtime input mapping and binding | final look / fire / scope / hold breath / ammo switch bindings after teammate input changes settle | MVP |
| `UCameraComponent` | gameplay camera and FOV control | FPS view and scoped zoom | MVP |
| `UWorld` physics query helpers | sweep and raycast wrappers | bullet segment collision | MVP |
| `TMulticastDelegate` | event delivery | `OnSniperHit` event broadcast | MVP |
| `ULuaScriptComponent` / Lua bindings | gameplay scripting | later score/frontline/friendly fire reactions outside this system | MVP integration |
| `UActionComponent` | local hit stop / slomo / knockback utilities | optional future important-kill presentation hook | Polish |
| particle/audio components | presentation | muzzle flash, impact, rifle report, drone warning | Polish |

## System Boundary

### C++ ownership

The following should live in C++:

- sniper pawn camera state
- scope FOV state
- scoped sensitivity multiplier
- aim sway math
- hold breath state
- recoil state
- ammo ballistic data
- bullet simulation
- bullet collision query
- `SniperHitInfo`
- hit event dispatch
- minimal Lua exposure for hit data if needed

### Lua ownership

The following should remain outside this implementation and later be driven from Lua:

- score gain/loss
- ally/enemy identification consequences
- friendly fire penalties
- frontline gauge changes
- phase progression
- win/lose logic
- restart
- radio and event subtitles
- result screen flow

## Proposed Data Structures

The following data structures match the current spec and are still recommended:

- `FSniperInputState`
- `FScopeState`
- `FAimSwayState`
- `FRecoilState`
- `FAmmoBallisticData`
- `FBallisticBullet`
- `FSniperHitInfo`

Implementation note:

- `FSniperInputState` should remain even if the new input system later changes how raw input arrives.
- This keeps the sniper logic insulated from future input API churn.

## Input Dependency Strategy

Because input is in flux, the plan should treat input hookup as a late integration phase.

### Before input system changes settle

Safe work:

- define sniper runtime data
- define sniper pawn class skeleton
- define weapon component interfaces
- define bullet manager data flow
- define hit event structures
- implement ballistic simulation that can be triggered from test code or temporary debug calls
- prepare Lua-facing hit payload structure

Unsafe work to avoid for now:

- final action names
- final key/gamepad mappings
- assumptions about mouse delta ownership
- assumptions about trigger/button edge behavior
- assumptions about gamepad axis naming

### After input system changes settle

Hook in:

- look axis binding
- scope hold binding
- fire pressed binding
- hold breath binding
- ammo switch binding
- optional restart/confirm support if the same pawn is expected to handle it

## Phase Plan

## Phase 0: Pre-Implementation Alignment

### Goal

Freeze the system boundary and dependency assumptions before writing gameplay code.

### Tasks

1. Confirm the final input integration wait state.
2. Confirm whether `ASniperPawn` should be a brand-new class or derived from an existing project-specific pawn later.
3. Confirm whether the sniper camera should live directly on the pawn root or under a helper scene component.
4. Confirm the desired collision channel or object-type query policy for bullets.
5. Confirm whether `SniperHitInfo` must be exposed directly to Lua in the first implementation pass.

### Deliverable

- this planning document

## Phase 1: Core Class Skeleton

### Goal

Create the sniper system ownership structure without depending on finalized input bindings.

### Tasks

1. Add `ASniperPawn`.
2. Add a camera component to the pawn.
3. Add `USniperWeaponComponent`.
4. Add `UBallisticBulletManagerComponent`.
5. Define the core state structs:
   - `FScopeState`
   - `FAimSwayState`
   - `FRecoilState`
   - `FAmmoBallisticData`
   - `FBallisticBullet`
   - `FSniperHitInfo`
6. Define update responsibilities between pawn, weapon, and bullet manager.
7. Ensure the pawn can exist in scene/standalone without assuming editor-only behavior.

### Validation

- project builds
- pawn can be spawned and possessed
- camera exists and can be possessed by the current player camera flow

### Notes

- No final input implementation in this phase.

## Phase 2: Camera and Scope Runtime

### Goal

Implement scope state and look-state math independent of the final external input system.

### Tasks

1. Add aim yaw/pitch state to the pawn.
2. Add pitch clamp.
3. Add scope FOV state:
   - normal FOV
   - scoped FOV
   - target FOV
   - current FOV
   - blend speed
4. Add scoped sensitivity multiplier state.
5. Implement FOV interpolation through `UCameraComponent::SetFOV()`.
6. Keep all FOV values in radians.
7. Add temporary non-final hooks so the scope state can be toggled for testing if needed.

### Validation

- scoped and unscoped FOV transition works
- pitch stays clamped
- camera direction remains stable

### Notes

- Final mouse and gamepad bindings still wait for teammate input work.

## Phase 3: Weapon State and Ballistic Data

### Goal

Implement weapon-side fire rules and ammunition definitions.

### Tasks

1. Add current ammo type state.
2. Define normal and anti-material ammo data.
3. Add fire cooldown tracking.
4. Add a weapon fire request entry point that can be triggered without final player input bindings.
5. Add recoil application entry point.
6. Add helper to resolve final aim direction from current control rotation plus sway and recoil.

### Validation

- weapon can accept a fire request
- cooldown prevents invalid rapid fire
- ammo data can be switched and queried

## Phase 4: Bullet Simulation MVP

### Goal

Implement projectile bullets with gravity and lifetime.

### Tasks

1. Add active bullet storage in the bullet manager.
2. Spawn bullets from weapon requests.
3. Store:
   - current position
   - previous position
   - velocity
   - lifetime
   - radius
   - damage
   - ammo type
   - scoped-shot flag
   - armor damage flag
4. Apply world gravity every tick.
5. Integrate bullet position every tick.
6. Expire bullets on lifetime end.
7. Add debug draw for bullet trajectories and current bullet count.

### Validation

- bullets visibly drop over distance
- bullets are removed on timeout
- multiple bullets can coexist without actor spawning overhead

## Phase 5: Collision and Hit Event

### Goal

Implement reliable segment collision and external hit notification.

### Tasks

1. Use `previous position -> current position` segment testing for each bullet.
2. Prefer sphere sweep using `UWorld::PhysicsSweep()`.
3. Fallback to `UWorld::PhysicsRaycast()` if sweep behavior is unavailable or insufficient for a given case.
4. Build `FSniperHitInfo` from the hit result.
5. Add `OnSniperHit` multicast delegate on the weapon component or bullet manager component.
6. Mark bullets dead on confirmed blocking hit.
7. Keep ally/enemy interpretation outside this layer.
8. If headshot detection is not ready, emit `IsHeadshot = false`.

### Validation

- fast bullets do not tunnel through targets in ordinary test cases
- hit actor and hit location are reported correctly
- external systems can subscribe to the hit event without modifying bullet code

## Phase 6: Aim Sway, Hold Breath, and Recoil

### Goal

Add the core sniper feel systems after ballistic firing is stable.

### Tasks

1. Add periodic sway calculation.
2. Differentiate scoped and unscoped sway amounts.
3. Add hold breath gauge, consume, and recover logic.
4. Reduce sway while holding breath.
5. Add recoil pitch and recoil yaw kick.
6. Add recoil recovery over time.
7. Ensure final bullet direction matches visible camera aim as closely as possible.

### Validation

- scoped view has stronger sway than unscoped view
- hold breath reduces sway while gauge remains
- recoil is applied on each shot and recovers smoothly

## Phase 7: Input Integration

### Goal

Hook the stabilized teammate input system into the already-built sniper runtime.

### Tasks

1. Add final axis and action mappings.
2. Bind mouse and gamepad look.
3. Bind fire, scope, hold breath, and ammo switch.
4. Confirm whether reload is real, a reset action, or omitted for MVP.
5. Verify keyboard/mouse and gamepad parity.
6. Remove any temporary debug toggles used before input finalization.

### Validation

- keyboard/mouse support works
- gamepad support works
- no duplicate behavior exists between provisional and final input paths

## Phase 8: Lua Connection

### Goal

Expose only the minimum needed for Lua gameplay rules.

### Tasks

1. Decide whether Lua consumes:
   - the full `FSniperHitInfo`
   - a reduced callback payload
   - a relay component/event wrapper
2. Expose the chosen hit data to Lua.
3. Keep score and frontline logic in Lua-side systems.
4. Provide a simple sample hookup path for:
   - enemy damage
   - friendly fire penalty
   - special target reaction

### Validation

- Lua can react to a sniper hit without C++ score logic
- the sniper layer stays reusable and rule-agnostic

## Recommended Build Order

The recommended practical build order is:

1. `ASniperPawn` skeleton
2. camera and scope runtime
3. weapon component and ammo data
4. bullet manager and ballistic update
5. collision and hit event
6. sway / hold breath / recoil
7. final input integration
8. Lua exposure

This order minimizes risk because:

- camera and control math must be stable before fire direction is trusted
- bullet simulation should exist before presentation tuning
- input should be attached late while teammate changes are in progress
- Lua should connect to an already-stable hit event boundary

## Primary Risks

### Risk 1: FOV unit mismatch

The engine camera uses radians. The spec document uses degree-like example values.

Mitigation:

- store and tune FOV in radians
- document conversion clearly in code comments where values are edited

### Risk 2: Input integration churn

Another teammate is updating the input system.

Mitigation:

- isolate sniper logic behind `FSniperInputState`
- bind final inputs late
- avoid spreading raw input assumptions through weapon/bullet code

### Risk 3: Bullet collision mismatch with world content

Sweep/raycast behavior may vary by object type and collision setup in scenes.

Mitigation:

- start with simple blocking collision assumptions
- log hit component and actor names
- keep debug visualizations on while tuning

### Risk 4: Camera aim and bullet direction divergence

If muzzle position and camera direction are not aligned, near-range misses can feel unfair.

Mitigation:

- use camera-centered firing for MVP or place muzzle near camera
- postpone advanced muzzle-to-crosshair reconciliation until core behavior is stable

### Risk 5: Lua boundary creep

Gameplay rules may accidentally leak into the sniper system.

Mitigation:

- keep `FSniperHitInfo` as the handoff boundary
- reject score/frontline logic inside sniper firing code

## Debug and Tuning Requirements

The sniper system should include development-only debug support for:

- current scoped state
- current FOV
- current ammo type
- hold breath gauge
- active bullet count
- bullet trajectory lines
- hit location marker
- final aim direction line

This is important because gameplay feel tuning will be much faster than repeatedly building UI first.

## Editor and Standalone Considerations

The implementation must avoid editor-only assumptions.

Checklist:

- no direct dependency on editor viewport-only behavior
- no asset lookup that only works in editor paths
- no reliance on editor-only temporary state to drive gameplay
- test possession and camera activation in ordinary runtime flow
- ensure Lua script paths and any future data assets resolve in standalone

## Test Checklist

### Core system

- pawn can be placed or spawned and possessed
- camera becomes the player view
- scope zoom changes FOV smoothly
- aim pitch is clamped

### Ballistics

- pressing fire after final input integration spawns a projectile
- projectile visibly drops over range
- projectile expires correctly
- projectile collision uses previous-to-current segment testing

### Weapon feel

- scoped sensitivity is reduced
- sway is visible
- hold breath reduces sway
- recoil kicks and recovers
- ammo switching changes ballistic behavior

### Integration

- hit event reaches external listeners
- no score logic exists in the sniper subsystem
- Lua can consume hit results after the Lua binding phase

### Platform path

- works in Editor Build
- works in Standalone Build

## Suggested Commit Boundaries

Recommended commit grouping once implementation starts:

1. `Sniper pawn skeleton + camera state`
2. `Weapon component + ammo ballistic data`
3. `Ballistic bullet manager + gravity/lifetime`
4. `Bullet collision + sniper hit event`
5. `Aim sway + hold breath + recoil`
6. `Input integration after teammate changes`
7. `Lua hit exposure`

## Immediate Next Step

Do **not** implement final input hookup yet.

The next recommended action after this document is:

1. wait for the teammate input-system update direction to settle
2. begin Phase 1 skeleton work only after confirming there will not be a conflicting pawn/input ownership change

If input-system merge timing is uncertain, the safest early implementation work is:

- class/struct definitions
- bullet manager implementation
- hit event plumbing
- debug visualization support

These can all proceed with minimal dependency on final input behavior.
