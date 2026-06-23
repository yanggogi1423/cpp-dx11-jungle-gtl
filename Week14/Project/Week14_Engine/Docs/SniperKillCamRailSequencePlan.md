# Sniper KillCam Rail Sequence Plan

## Goal

Sniper Elite-style kill cam needs a reusable cinematic language that works for targets at very different distances. The camera cannot be authored only in world space, because every shot has a different muzzle position, hit position, direction, travel distance, and impact context.

The target architecture is:

- C++ owns the dynamic bullet rail and camera application.
- Actor Sequence owns editable cinematic parameters, not fixed world camera positions.
- Lua owns game logic, cutscene triggering, state flow, skip behavior, and high-level profile selection.
- The editor must let designers place and tune the Director, Bullet prefab, camera offsets, FOV, DOF, and sequence curves without rebuilding.

## Current State

Current shot-to-killcam flow:

1. `USniperWeaponComponent` fires through `UBallisticBulletManagerComponent`.
2. `UBallisticBulletManagerComponent::SpawnBullet` assigns a `BulletId` and records a spawn snapshot through `ASniperKillCamDirector::NotifyBulletSpawned`.
3. `UBallisticBulletManagerComponent::HandleBulletHit` builds hit data and calls `ASniperKillCamDirector::NotifyBulletHit`.
4. The Director stores the hit snapshot and pushes the `BulletId` to a pending queue.
5. `CutSceneManager.lua` polls `SniperKillCam.ConsumePendingBulletId`.
6. Lua plays `sniper_killcam` and calls `SniperKillCam.Start(bullet_id, duration, camera_mode)`.
7. `ASniperKillCamDirector` finds a scene-placed Director if one exists, otherwise auto-spawns one.
8. Director switches `PlayerCameraManager` view target to itself.
9. Director ticks bullet playback from spawn to hit and updates camera/Bullet visual every frame.

Relevant files:

- `KraftonEngine/Source/Engine/GameFramework/Actor/SniperKillCamDirector.h`
- `KraftonEngine/Source/Engine/GameFramework/Actor/SniperKillCamDirector.cpp`
- `KraftonEngine/Source/Engine/Component/Gameplay/BallisticBulletManagerComponent.cpp`
- `KraftonEngine/Content/Script/Management/CutSceneManager.lua`

Already useful:

- `Sniper KillCam Director` can be placed from the editor.
- Placed Director values are editable through Details.
- `CinematicBulletPrefabPath` can spawn a designer-authored Bullet prefab.
- Existing fallback StaticMesh bullet visual still exists for quick tests.
- Lua can start/stop the killcam through the `SniperKillCam` global.

Current limitation:

- Actor Sequence still should not key the final camera world transform directly, because Director Tick owns the shot-relative conversion.
- Screen effects such as shockwave, vignette, letterbox, and impact post-process are still future work.
- Multi-profile asset reuse is not implemented yet; use one placed Director plus Lua profile selection for now.

Already opened for authoring:

- Camera, look target, and Bullet visual each have independent rail alpha override/scale/offset/ease/power controls.
- Rail alpha can extrapolate before muzzle and after impact with clamp min/max.
- Linear camera offsets and orbit radius can scale by shot distance.
- Orbit pivot can be shifted independently from the camera rail position.
- Bullet visual can be offset, independently time-shifted, scaled per axis, and rotated without rebuilding.
- Lua can set/get any exposed rail rig scalar by reflected property name.

## Design Principle

Do not make Actor Sequence animate the final world camera transform directly.

Instead, use Actor Sequence to animate a small set of rail-relative parameters. The Director then converts those parameters into world-space camera transforms using the current shot's dynamic rail.

Rail basis:

```text
RailStart = bullet spawn/muzzle position
RailEnd = hit position
RailDirection = normalize(RailEnd - RailStart)
RailSide = normalize(cross(WorldUp, RailDirection))
RailUp = WorldUp, with future option for impact normal based up correction
RailAlpha = normalized bullet playback progress, 0..1
RailPosition = lerp(RailStart, RailEnd, RailAlpha)
```

Final camera:

```text
CameraPosition =
    RailPosition
  + RailDirection * ForwardOffset
  + RailSide      * SideOffset
  + RailUp        * UpOffset

LookTarget =
    RailPosition
  + RailDirection * LookAhead
  + RailUp        * LookUpOffset
```

Actor Sequence should drive `ForwardOffset`, `SideOffset`, `UpOffset`, `LookAhead`, `FOV`, `Roll`, DOF values, lag values, bullet scale, and optional impact emphasis. The Director should own final conversion and camera manager state.

## Proposed Runtime Architecture

### ASniperKillCamDirector

Responsibilities:

- Receive and resolve bullet spawn/hit snapshots.
- Build a normalized rail per shot.
- Own the active killcam state.
- Switch/restore `PlayerCameraManager`.
- Spawn/destroy the cinematic Bullet prefab.
- Scrub the rail sequence each frame.
- Read rail rig parameters and apply final camera/Bullet transform.
- Expose simple Lua entry points through existing `SniperKillCam.Start/Stop/IsPlaying`.

Expected new/refined properties:

- `bAutoStartFromHit`
- `DefaultProfileId`
- `CameraDriveMode`
- `DistanceScalingMode`
- `MinDuration`
- `MaxDuration`
- `ReferenceDistance`
- `CinematicBulletPrefabPath`
- `CinematicBulletScale`
- `CinematicBulletRotationOffset`
- `bUseActorSequenceRig`
- `RailSequenceComponent`
- `RailRigComponent`

### UKillCamRailRigComponent

New component used as the editable/animatable parameter target.

It should not directly move the final camera. It only stores authored cinematic parameters.

Suggested animatable properties:

- `ForwardOffset`
- `SideOffset`
- `UpOffset`
- `LookAhead`
- `LookSideOffset`
- `LookUpOffset`
- `CameraRailAlphaOverride`
- `CameraRailAlphaScale`
- `CameraRailAlphaOffset`
- `CameraRailAlphaEase`
- `CameraRailAlphaPower`
- `LookRailAlphaOverride`
- `LookRailAlphaScale`
- `LookRailAlphaOffset`
- `LookRailAlphaEase`
- `LookRailAlphaPower`
- `RailAlphaClampMin`
- `RailAlphaClampMax`
- `Roll`
- `FOV`
- `CameraLagSpeed`
- `OrbitBlend`
- `OrbitYaw`
- `OrbitPitch`
- `OrbitRadius`
- `OrbitPivotForwardOffset`
- `OrbitPivotSideOffset`
- `OrbitPivotUpOffset`
- `DOFFocusRange`
- `DOFBlurRadius`
- `BulletForwardOffset`
- `BulletSideOffset`
- `BulletUpOffset`
- `BulletScaleMultiplier`
- `BulletScaleXMultiplier`
- `BulletScaleYMultiplier`
- `BulletScaleZMultiplier`
- `BulletPitchOffset`
- `BulletYawOffset`
- `BulletRollOffset`
- `BulletRailAlphaOverride`
- `BulletRailAlphaScale`
- `BulletRailAlphaOffset`
- `BulletRailAlphaEase`
- `BulletRailAlphaPower`
- `ImpactSlowAmount`
- `LetterboxAmount`
- `VignetteAmount`

Metadata:

- `Edit`
- `Save`
- `Animatable`
- clear categories such as `KillCam|Rail`, `KillCam|Camera`, `KillCam|DOF`, `KillCam|Bullet`, `KillCam|Screen`

Reason:

- Actor Sequence already edits reflected properties.
- This keeps the system inside the current engine rule: reflected properties, editor details, transaction undo, prefab/scene persistence, and Actor Sequence binding.
- Designers can tune the rig in the Actor Sequencer without changing C++.

### Actor Sequence Component

Attach an `UActorSequenceComponent` to the Director or to a child helper actor.

Preferred ownership:

- Director owns `UKillCamRailRigComponent`.
- Director also owns or references an `UActorSequenceComponent`.
- Actor Sequence targets the RailRigComponent by component binding.

Runtime behavior:

- Do not call normal `Play()` for the main rail evaluation.
- Director scrubs sequence manually:

```text
SequenceTime = RailAlpha * SequenceDuration
SequencePlayer.SetCurrentTime(SequenceTime)
```

This makes the cinematic follow normalized rail distance/progress instead of only wall-clock time.

Optional future modes:

- `TimeDriven`: sequence time is elapsed seconds.
- `RailDistanceDriven`: sequence time is normalized rail alpha.
- `Hybrid`: rail alpha drives camera offsets, elapsed time drives UI/fade/audio.

## Editor Workflow

Recommended designer workflow:

1. Place `Sniper KillCam Director` in the level.
2. Add/confirm `KillCamRailRigComponent`.
3. Add/confirm `ActorSequenceComponent`.
4. Assign `CinematicBulletPrefabPath`.
5. Open Actor Sequencer from the Director's ActorSequenceComponent.
6. Add tracks for RailRigComponent properties:
   - `ForwardOffset`
   - `SideOffset`
   - `UpOffset`
   - `LookAhead`
   - `LookSideOffset`
   - `FOV`
   - `Roll`
   - `OrbitBlend`
   - `OrbitYaw`
   - `OrbitPitch`
   - `OrbitRadius`
   - `DOFBlurRadius`
   - `BulletScaleMultiplier`
7. Test in PIE by shooting a target.
8. Iterate values in Details/Sequencer without rebuilding.

Important editor rule:

- The sequence should author rail-relative values, not world transform keys.
- World transform keys are allowed only for decorative spawned actors, not the main killcam camera.

Useful authoring patterns:

- Bullet centered side pass: keep `LookSideOffset` near `0`, animate `OrbitBlend` to `1`, then animate `OrbitYaw` from side view toward `0`.
- Bullet enters frame: start `LookSideOffset` or `LookUpOffset` away from `0`, then animate it back to `0` while the bullet travels.
- Camera waits near muzzle: set `CameraRailAlphaOverride` near `0` for the early section, then animate it back into the shot path or set it to `-1` through a cut/preset when procedural rail follow should resume.
- Cinematic bullet drift/slow read: animate `BulletRailAlphaOverride` separately from camera/look rail alpha, or leave it at `-1` to follow the real playback alpha.

## Lua Fit

Lua should continue to own game flow:

- whether killcam should start
- which profile/camera mode to use
- whether this shot is eligible
- skip input
- HUD prompt
- cutscene state transitions

Current Lua entry remains:

```lua
SniperKillCam.Start(bullet_id, duration, camera_mode)
SniperKillCam.Stop()
SniperKillCam.IsPlaying()
SniperKillCam.ConsumePendingBulletId()
SniperKillCam.ClearPendingBullets()
SniperKillCam.SetRigScalar("OrbitYaw", 90.0)
SniperKillCam.GetRigScalar("OrbitYaw", 0.0)
```

Planned Lua additions:

```lua
SniperKillCam.StartWithProfile(bullet_id, profile_id, options)
SniperKillCam.SetDefaultProfile(profile_id)
SniperKillCam.SetAutoStartEnabled(enabled)
```

`CutSceneManager.lua` should remain the orchestration point:

- poll pending hit
- choose profile
- start killcam
- publish skip prompt
- stop on Space
- restore normal game state

Lua should not calculate camera transforms every frame. That belongs in C++ for stability and frame-time safety.

## Profile Strategy

Initial implementation should avoid a large new asset system. Use Director fields and Actor Sequence first.

Phase 1 profile shape:

- one placed Director per scene
- one embedded ActorSequenceComponent
- one RailRigComponent
- `camera_mode` selects procedural preset or sequence mode

Phase 2 profile shape:

- multiple profile entries on Director:
  - `Default`
  - `LongShot`
  - `CloseShot`
  - `SidePass`
  - `ImpactFocus`
- Lua selects by distance/weapon/ammo/hit type.

Phase 3 profile shape:

- dedicated `UKillCamProfileAsset` if the game needs reusable profiles across scenes.

Do not add a profile asset first unless repeated scene reuse becomes painful. For the current schedule, placed Director + Actor Sequence is faster and easier to debug.

## Distance Normalization

The Director should calculate rail duration and playback speed from distance:

```text
Distance = length(Hit - Start)
DistanceFactor = clamp(Distance / ReferenceDistance, MinFactor, MaxFactor)
Duration = clamp(BaseDuration * DistanceFactor, MinDuration, MaxDuration)
RailAlpha = Elapsed / Duration
```

But authored offsets should remain stable:

- close targets should not push the camera through the target
- long targets should not make camera offsets feel tiny
- optional offset scaling should be clamped separately from duration scaling

Suggested properties:

- `ReferenceDistance`
- `MinDuration`
- `MaxDuration`
- `MinOffsetScale`
- `MaxOffsetScale`
- `bScaleOffsetsByDistance`

## Batch Plan

### Batch 1 - Rail Rig Component

Files:

- `Source/Engine/Component/Gameplay/KillCamRailRigComponent.h`
- `Source/Engine/Component/Gameplay/KillCamRailRigComponent.cpp`
- project file and filters

Work:

- Add reflected component with animatable rail/camera/DOF/bullet parameters.
- Add defaults that match current procedural camera as closely as possible.
- Ensure scene/prefab save works through existing component serialization.
- Regenerate headers.

Done when:

- Component can be added to Director.
- Properties show in Details.
- Actor Sequence can add tracks for scalar/vector/rotator properties.

### Batch 2 - Director Rail Evaluator

Files:

- `SniperKillCamDirector.h/.cpp`

Work:

- Build explicit rail state from spawn/hit snapshots.
- Add `CameraDriveMode`.
- Read `UKillCamRailRigComponent`.
- Convert rail-relative rig parameters to final camera transform.
- Preserve existing procedural fallback.
- Keep Bullet prefab transform driven from rail.

Done when:

- Placed Director can drive camera with rig values.
- Changing rig values in Details changes runtime killcam without C++ edits.
- Existing Lua trigger still works.

### Batch 3 - Actor Sequence Scrub Integration

Files:

- `SniperKillCamDirector.h/.cpp`
- possibly `ActorSequenceComponent.h/.cpp` only if a small public helper is missing

Work:

- Add optional sequence component lookup.
- On killcam start, initialize sequence player.
- Every tick, set sequence time from rail alpha.
- Read updated rig values after sequence evaluation.
- Avoid normal Play mode fighting manual scrub.

Done when:

- Actor Sequencer keys on RailRigComponent change killcam camera during PIE.
- Sequence behaves consistently for close and far targets.
- Director still works if no ActorSequenceComponent exists.

### Batch 4 - Editor Polish

Files:

- `FLevelViewportLayout.*`
- `EditorPropertyWidget.*`
- `ActorSequenceEditorWidget.*` if presets are worth adding

Work:

- Ensure `Sniper KillCam Director` placement is obvious.
- Add friendly categories/display names.
- Optionally add "Add KillCam Rail Rig" helper in Details if missing.
- Optionally add Actor Sequencer presets:
  - Side Pass
  - Tail Follow
  - Impact Focus
  - Bullet Scale Punch

Done when:

- Designer can place Director, add/open sequence, key rail properties, and test in PIE without hunting through code.

### Batch 5 - Lua Profile Selection

Files:

- `CutSceneManager.lua`
- `LuaScriptManager.ReflectionCore.cpp`
- optional Lua definitions/examples

Work:

- Add profile/camera mode selection hook.
- Allow Lua to pass options:
  - profile id
  - duration override
  - camera mode
  - force/skip eligibility
- Keep existing `SniperKillCam.Start` compatible.

Done when:

- `CutSceneManager` can choose a killcam mode based on distance or shot context.
- Existing game flow and skip prompt remain intact.

### Batch 6 - Smoke And Tuning Pack

Work:

- Add one test scene setup or documented smoke steps.
- Test close/medium/long shot.
- Test skip.
- Test no placed Director fallback.
- Test placed Director with prefab bullet.
- Test Actor Sequence scrub.

Done when:

- Same authored rail sequence gives recognizable cinematic shape across multiple target distances.

## Risks

- Actor Sequence may not currently expose the exact target component/property conveniently enough. If so, improve editor preset/search rather than changing ActorSequence core first.
- Manual sequence scrub may call property application every frame. Keep RailRig properties small and cheap.
- If Actor Sequence restores base values on stop, ensure Director stop order does not leave camera/Bullet in stale state.
- If Bullet prefab contains collision or gameplay scripts, it may accidentally interact with the world. The prefab should be decorative only, or Director should disable collision/visibility on spawned primitives.
- Multiple Directors in one scene can be ambiguous. Initial rule: first valid Director wins. Future polish: active/default Director flag.

## Recommended Direction

Do not subclass `UActorSequence` for the first implementation.

Use:

```text
ASniperKillCamDirector
  + UKillCamRailRigComponent
  + UActorSequenceComponent
  + optional Bullet prefab
```

This fits the current engine better:

- reflected properties
- Details editing
- Actor Sequence editor
- undo/redo
- scene and prefab persistence
- Lua orchestration
- minimal new engine surface

Subclassing or creating a new sequence asset should only happen later if multiple systems beyond killcam need distance-normalized rail sequencing.

## Immediate Next Implementation Target

Start with Batch 1 and Batch 2 together:

1. Add `UKillCamRailRigComponent`.
2. Make Director consume it when present.
3. Keep existing procedural defaults as fallback.
4. Verify Details editing affects runtime camera.

Then do Batch 3:

1. Attach ActorSequenceComponent to Director.
2. Scrub sequence by rail alpha.
3. Key RailRigComponent properties in Actor Sequencer.

## ShockWave Plan

### Goal

Add a reusable screen-space shockwave/distortion effect that can stay anchored to a world-space source. For killcam, the source is the cinematic bullet position. Later, the same effect must also work for explosions or impacts.

Important rule:

- Do not make this a Bullet-only shader.
- Implement it as a shared `WorldAnchoredShockWave` camera/post-process effect.
- Bullet killcam only feeds one moving source into that shared system.

### Visual Behavior

For sniper bullet killcam:

- ShockWave center follows the bullet world position every frame.
- C++ projects the bullet world position to screen UV using the current camera view/projection.
- The post-process shader samples SceneColor around that UV and applies radial distortion.
- Radius and strength are driven by rail progress:
  - early alpha: larger, sharper, stronger wave
  - later alpha: smaller, calmer, more stable wave
- Optional directional stretch can align the distortion with bullet travel direction after projecting velocity to screen.

For explosions:

- Source position is a fixed world origin.
- Lifetime, radius curve, and strength curve are different, but shader path is the same.

### Data Model

Use a small fixed-size shockwave array first.

Suggested runtime state:

```text
FWorldAnchoredShockWave
  bEnabled
  WorldPosition
  WorldDirection
  Age
  Duration
  Radius
  Width
  Strength
  Falloff
  DirectionalStretch
  ScreenUV
  ScreenDirection
```

Suggested camera manager API:

```text
AddWorldShockWave(WorldPosition, Direction, Duration, Radius, Strength)
UpdateWorldShockWave(Handle, WorldPosition, Direction, Radius, Strength)
ClearWorldShockWave(Handle)
ClearAllWorldShockWaves()
```

Suggested Lua API:

```lua
Camera.AddShockWave(x, y, z, duration, radius, strength)
Camera.ClearShockWaves()
```

KillCam Director should use the C++ API directly for the bullet-driven moving source.

### Render Integration

Add after main scene render and before UI.

Preferred order:

```text
SceneColor
 -> DOF / ScopeLens / Bloom as existing pipeline allows
 -> WorldAnchoredShockWave distortion
 -> Vignette / Letterbox / Fade
 -> UI
```

If current pass ordering makes this expensive, place it in the existing post-process chain near ScopeLens/DOF and keep the source SRV/target RTV ping-pong safe.

Implementation note:

- `PostProcess` commands are not strictly ordered by the small user sort key because shader hash participates in the command sort key.
- Use a dedicated `WorldAnchoredShockWave` render pass after `PostProcess` and before `FXAA` so the source SceneColor copy and execution order are deterministic.
- This means the current implementation distorts the post-processed scene result. If we later need shockwave before vignette/letterbox/fade, split those camera overlays into a later pass instead of relying on same-pass command order.

Shader input:

```text
SceneColor SRV
PerFrame viewport size
ShockWave count
ShockWave screen center/direction/radius/width/strength/falloff/stretch
```

Shader behavior:

```text
delta = uv - center
dist = length(delta)
ring = 1 - saturate(abs(dist - radius) / width)
wave = pow(ring, falloff) * strength
offset = normalize(delta) * wave
color = SceneColor.Sample(uv - offset)
```

Directional stretch should be optional and data-driven, not hardcoded for bullets.

### KillCam Connection

`ASniperKillCamDirector` already has:

- current bullet snapshot
- rail alpha
- bullet world position
- bullet velocity/direction

Add rig parameters:

- `bEnableShockWave`
- `ShockWaveRadius`
- `ShockWaveStartRadiusBoost`
- `ShockWaveWidth`
- `ShockWaveStrength`
- `ShockWaveStartStrengthBoost`
- `ShockWaveFalloff`
- `ShockWaveDirectionalStretch`

Per tick:

```text
Radius = ShockWaveRadius + ShockWaveStartRadiusBoost * exp(-RailAlpha * Decay)
Strength = ShockWaveStrength + ShockWaveStartStrengthBoost * exp(-RailAlpha * Decay)
UpdateWorldShockWave(Handle, BulletPosition, BulletDirection, Radius, Strength)
```

On stop:

```text
ClearWorldShockWave(Handle)
```

### Batch Plan

Batch 1:

- Add plan and inspect current post-process pass wiring.
- Decide exact insertion point.

Batch 2:

- Add camera manager shockwave state/API.
- Add frame context shockwave payload.

Batch 3:

- Add `WorldAnchoredShockWave.hlsl`.
- Add render pass/ping-pong integration.

Batch 4:

- Add KillCam rig properties and Director update.
- Use bullet snapshot position/direction as moving shockwave source.

Batch 5:

- Add Lua helper for explosion/static usage.
- Smoke with killcam and one Lua-spawned static shockwave.
