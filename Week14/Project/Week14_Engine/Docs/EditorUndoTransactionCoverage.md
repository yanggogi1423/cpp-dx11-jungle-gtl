# Editor Undo Transaction Coverage

This file is the working checklist for the unified editor Undo/Redo patch.
The rule is simple: meaningful editor mutations go through `FEditorUndoSystem` transactions.
Pure editor navigation state is excluded.

## Policy

- Use one global transaction stack: `UEditorEngine::GetUndoSystem()`.
- Do not record camera movement, viewport focus, hover state, panel scroll, or selection-only changes.
- Details panel property edits use reflection-backed before/after property bytes.
- Actor/component structure edits use serialized actor before/after state.
- Asset graph editors use their existing binary asset/editor snapshots as commands on the same global stack.
- Undo/Redo must show toast feedback through `FNotificationManager`.
- PIE recording is disabled so game runtime input/changes do not enter editor history.

## Current Coverage

| Area | Mutation | Status | Notes |
| --- | --- | --- | --- |
| Core Undo | Transaction stack, command API, undo/redo toast | Done | `CaptureSnapshot()` is now compatibility-only and does not record scene snapshots. |
| Core Undo | Lambda/binary command adapter | Done | Used by asset editors that already have stable binary snapshots. |
| Core Undo | Serialized `UObject` asset edit helper | Done | Captures before/after `UObject::Serialize` bytes and restores through the global stack with editor-specific restore hooks. |
| Level Viewport | Spawn actor from viewport menu | Done | Records actor creation. |
| Level Viewport | Delete selected actors | Done | Records actor deletion from viewport, scene tree, and details entry points. |
| Level Viewport | Duplicate selected actors | Done | Records duplicate actor creation. |
| Level Viewport | Gizmo actor transform | Done | Captures drag start/end actor transforms. |
| Level Viewport | Camera movement / selection | Excluded | Must stay out of Undo history. |
| Details | Actor/component reflected property edit | Done | Uses `FProperty` byte serialization. |
| Details | Actor rename / component rename | Done | Uses actor state transactions. |
| Details | Add/remove/reparent component | Done | Uses actor state transactions. |
| Details | ActorSequence component inline edits | Done | Uses actor state transactions. |
| Content Browser | New asset/new folder | Done | Records file-system create transactions. |
| Content Browser | Delete file/folder | Done | Records file-system delete transactions. |
| Content Browser | Rename file/folder | Done | Records file-system rename transactions. |
| Content Browser | Import/reimport static mesh, skeletal mesh, FBX scene, vector field | Mostly done | Compound operations may still be split into separate transactions. |
| Actor Sequence Editor | Track/key/range/curve edits | Done | Dedicated editor routes authoring edits through global transactions. |
| Material Editor | Graph/settings/parameter/node value edits | Done | Local stack is reduced to last-snapshot cache; Undo/Redo calls global stack. |
| Lua Blueprint Editor | Graph/variable/custom function edits | Done | Local stack is reduced to last-snapshot cache; Undo/Redo calls global stack. |
| AnimGraph Editor | Root graph, state machine, state pose, transition rule edits | Done | Local stack is reduced to last-snapshot cache; Undo/Redo calls global stack. |
| Runtime UI Layout Editor | Widget hierarchy/layout/style edits | Done | Local stack is reduced to last-snapshot cache; Undo/Redo calls global stack. |
| Camera Shake Editor | Shake property/curve path edits | Done | Uses serialized asset snapshots; Undo/Redo buttons and Ctrl+Z/Ctrl+Y route through the global stack. |
| Float Curve Editor | Key/tangent/range edits | Done | Uses serialized asset snapshots; view-only pan/zoom remains excluded. |
| Mesh Editors | Direct socket/mesh editor sub-edits | Partial | Import/reimport is covered through Content Browser; in-editor sub-edits still need audit. |
| Particle Editor | Emitter/module/key edits | Done | Render-frame serialized asset snapshots route emitter/module/curve edits through global transactions; restore rebuilds preview emitters. |
| Physics Asset Editor | Body/constraint edits | Done | Standalone and embedded editor panels use serialized snapshots; delete and viewport gizmo notifications are recorded too. |
| Project Settings | Build/game/project settings edits | Not wired | Needs settings-file transaction. |
| World Settings | GameMode/default pawn/gravity edits | Not wired | Needs world/settings state transaction. |

## Editor Mutation Inventory

Meaningful mutations found so far:

- Level: actor create/delete/duplicate/transform.
- Details: reflected property edits, actor/component rename, component add/remove/reparent, ActorSequence component edits.
- Content Browser: create/delete/rename/import/reimport content paths.
- Actor Sequence: add/remove tracks, add/delete/edit keys, playback range, duration, curve keys/tangents, interpolation/tangent modes.
- Material: graph node/link edits, graph import, material settings, parameter definitions, node literal values.
- Lua Blueprint: node/link edits, variables, inline literals/assets, custom Lua functions, debug-facing graph metadata.
- AnimGraph: node/link edits, owner class, state machine states/transitions, transition rule graph, node properties.
- Runtime UI: widget add/delete, hierarchy selection target edits, transform/layout/style/content fields, generated RML/RCSS import/export-side mutations.
- Camera Shake: scalar shake fields and curve path fields.
- Float Curve: curve keys, handles/tangents; view-only pan/zoom is excluded.
- Mesh/StaticMesh: sockets, bounds/collision/physics-related metadata, direct reimport options.
- Particle: emitter list, module params, LOD/sort/blend settings.
- Physics Asset: bodies, constraints, previews that persist to asset data.
- Settings: project/world settings that are saved to files or scenes.

## Remaining Batches

1. Wire Project/World Settings with file/world state commands.
2. Audit Mesh Editor direct sub-edits: sockets, skeleton/cloth metadata, physics asset assignment, and direct mesh metadata edits.
3. Polish transaction quality: drag coalescing, better labels, close-tab invalidation behavior, and removal of the legacy `CaptureSnapshot()` API.
4. Add focused smoke tests/manual checklist for CameraShake, FloatCurve, Particle, PhysicsAsset, and embedded Physics-in-Mesh workflows.

## Known Risks

- Asset editor commands are valid only while the relevant editor tab remains alive; a lifetime token prevents closed tabs from dereferencing dead widgets, but the command will fail instead of reopening the asset.
- Several slider/draggable editors may currently create many small transactions. Coalescing by drag start/end is a later polish pass, especially Physics viewport gizmo edits.
- Content Browser compound actions such as "create physics asset and assign to mesh" can still become more than one transaction.
- `ClearHistory(WorldHandle)` currently clears all history; world-scoped history can be refined later if multi-world editing becomes important.
