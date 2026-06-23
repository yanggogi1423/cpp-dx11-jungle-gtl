# Week11 Archive Port Summary

## Scope

Date: 2026-05-27

Goal: align Week12 engine/editor behavior with the stronger Week11 asset, editor shell, AnimGraph, and Lua notify flows while explicitly excluding Blueprint.

Particle editor/runtime internals were intentionally left out of this pass because another teammate is working there.

## Completed Areas

### Asset Policy

- `.uasset` is now the first-class asset policy for major editor assets touched in this pass.
- Runtime UI layout legacy text save/load paths were removed in favor of common `.uasset` metadata plus payload.
- StaticMesh, SkeletalMesh, AnimSequence, C++ AnimGraph, and Lua AnimGraph now have metadata-backed `.uasset` routes.
- Content Browser listing, details, preview routing, drag/drop payloads, and double-click open behavior were moved toward metadata-driven `.uasset` handling.
- GamePackager follows the new runtime `.uasset` class allow-list and relevant dependencies for supported asset classes.
- Legacy `.animgraph` support was removed; C++ AnimGraph now uses `UAnimGraphAsset`, and Lua AnimGraph uses `UAnimLuaProgramAsset`.

### Binary Archive And Serialization

- Week11-style `FAssetFile` header/metadata/payload flow was applied to the supported asset classes.
- `FArchive` serialization was expanded for animation and mesh payload needs.
- `UAnimSequence` notify payloads were versioned so Lua notify fields can be saved without breaking older payloads.
- Old compatibility bridges were kept only where needed for safe migration, not as new authoring paths.

### AnimGraph

- Content Browser can create both C++ AnimGraph and Lua AnimGraph assets as `.uasset`.
- Lua AnimGraph editor uses the Week12 tab/document shell and Week12-style graph UX.
- Lua AnimGraph keeps the useful Week11 concepts: generated Lua source, transition editing, drag/drop authoring, and preview access through existing viewer routes.
- C++ AnimGraph `.animgraph` authoring was retired in favor of `.uasset`.
- Graph editor polish added pan/zoom, context menus, delete, save shortcuts, undo/redo snapshots, marquee selection, multi-selection, and multi-move where supported.

### Editor Shell UX

- Detached document windows and dock-back behavior were aligned with Week11-style expectations.
- Maximize/restore is now independent from dock-back; docking requires explicit dock control or actual dock drag/drop.
- Double-click title bar maximize follows the same maximize/restore path as the button.
- Console/content browser floating behavior was polished, including PIE visibility for Console.
- Hidden `ViewerPreview` worlds are skipped from world tick when their owning viewer is hidden or detached-minimized. Level Editor remains the always-ticking exception.

### Lua AnimNotify

- Actor-level Lua AnimNotify routing was removed.
- Responsibility is now:
  `AnimInstance -> SkeletalMeshComponent -> AnimNotify object -> optional ScriptComponent receiver`.
- Added `Lua Event Notify` and `Lua Event Notify State`.
- Lua notify payload stores event name, target policy, and target script.
- Lua receives one context table.
- Editor details expose Lua event, target policy, script picker, `Add Handler`, and `Open Script`.
- Selecting a handler script automatically uses `Named Script`, and `Add Handler` inserts a Lua stub above the script's final `return`.

## Current Lua Notify Usage

1. Add `UScriptComponent` to the actor that owns the skeletal mesh.
2. Set its script name to the Lua script that should receive animation callbacks.
3. Open an animation sequence in the viewer.
4. Add/select a notify.
5. Set `Class` to `Lua Event Notify` or `Lua Event Notify State`.
6. Set `Lua Event`, for example `Footstep` or `WeaponTrail`.
7. Pick a script from `Handler Script`.
8. Press `Add Handler` to generate the Lua function.
9. Save the animation sequence.

Generated one-shot handler:

```lua
function PlayerAnimation:AnimNotify_Footstep(context)
end
```

Generated state handler:

```lua
function PlayerAnimation:AnimNotify_WeaponTrail_Begin(context)
end

function PlayerAnimation:AnimNotify_WeaponTrail_Tick(context)
end

function PlayerAnimation:AnimNotify_WeaponTrail_End(context)
end
```

Useful context fields:

- `Name`
- `Phase`
- `StartTime`
- `Duration`
- `EndTime`
- `DeltaTime`
- `MeshComponent`
- `Owner`
- `TargetScript`
- `TargetPolicy`

## Validation

- `MSBuild Debug|x64`: warnings 0, errors 0.
- `.uasset` metadata parse was previously checked during the asset-policy pass with no parse errors.
- Legacy `.mat`, `.matinst`, `.curve`, `.particlesystem`, `.layout`, and `.animgraph` authoring routes were removed or retired for the covered systems.

## Remaining Follow-Ups

- Manual editor smoke remains recommended before commit:
  - open C++ and Lua AnimGraph assets,
  - detach/maximize/restore/dock back tabs,
  - create/move/delete graph nodes,
  - open AnimSequence and add a Lua notify handler,
  - run PIE with floating Console visible.
- Legacy `.animseq` descriptors still need a careful manual migration pass because some referenced binary caches are missing in the checkout.
- Scene/prefab serialization was not converted in this pass.
- Blueprint remains intentionally out of scope.
