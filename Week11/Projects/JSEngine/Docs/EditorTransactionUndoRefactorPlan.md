# Editor Transaction Undo Refactor Plan

This document is a working checklist for replacing the legacy world-snapshot undo system with a transaction/command-based undo system.

Use this file while implementing the refactor. Do not treat a phase as complete until every checklist item in that phase is handled or explicitly deferred with a reason.

## 0. Goal

The editor undo system must behave like a normal editor:

- Every user-visible edit is undoable and redoable.
- World edits, ActorSequence edits, Curve edits, Material edits, Settings edits, and Asset edits use one consistent transaction model.
- Undo does not depend on restoring the whole world snapshot.
- Delete operations are fully reversible.
- Drag operations produce one undo step, not one step per frame.
- Multi-object edits produce one transaction.
- Commands store stable references, not raw pointers.

## 0.1 Current Legacy Undo Behavior

Current undo is world-snapshot based.

Current runtime shape:

- `FEditorUndoSystem::CaptureSnapshot()` serializes the active editor world through `UEditorEngine::CaptureSceneSnapshot()`.
- `Undo()` captures the current world snapshot, pops a previous snapshot, and restores it through `UEditorEngine::RestoreSceneSnapshot()`.
- `Redo()` captures the current world snapshot, pops a redo snapshot, and restores it through `UEditorEngine::RestoreSceneSnapshot()`.
- Histories are stored per active `WorldHandle`.
- `MaxUndoHistory` is count based and currently limited to 50 entries.
- Restore unregisters/reloads a world-level scene snapshot, then rebuilds spatial index and selection/world bindings.

Important current files:

- `Source/Editor/Undo/EditorUndoSystem.h`
- `Source/Editor/Undo/EditorUndoSystem.cpp`
- `Source/Editor/EditorEngine.h`
- `Source/Editor/EditorEngine.cpp`
- `Source/Editor/Command/EditorCommandSystem.cpp`
- `Source/Editor/UI/EditorMainPanelDebug.cpp`
- `Source/Editor/UI/EditorConsoleWidget.cpp`
- `Source/Editor/UI/EditorToolbarWidget.cpp`

Current limitations:

- Only active world state is captured.
- Asset document edits are not naturally represented.
- ActorSequence, Curve, Material, Mesh socket, Packaging, Project Settings, and World Settings need either accidental world serialization coverage or have no undo coverage.
- Restore swaps world state, which can invalidate raw pointers owned by details panels, selection, previewers, sequencer, and editor widgets.
- Small property edits store full world snapshots.
- Some editor asset edits mutate data directly without any `CaptureSnapshot()` call.

Hard replacement policy:

- Do not keep a compatibility layer for snapshot undo.
- Intermediate commits may temporarily have no working undo.
- The final state must remove normal editor undo dependency on `CaptureSceneSnapshot()` and `RestoreSceneSnapshot()`.
- Scene serialization remains only for save/load, copy/paste/prefab/delete backup helpers if useful, not for normal undo.

## 0.2 Current Snapshot Call Inventory

Every active `CaptureSnapshot()` call below must be removed or replaced by a transaction command.

World and viewport edit paths:

- `Source/Editor/EditorEngine.cpp:731` - Delete Actors.
- `Source/Editor/Viewport/EditorViewportClient.cpp:1057` - Transform Actors.
- `Source/Editor/Viewport/EditorViewportClient.cpp:1539` - Duplicate Actors.

Placement paths:

- `Source/Editor/UI/EditorControlWidget.cpp:119` - Place Actor.
- `Source/Editor/UI/EditorMainPanelPlacement.cpp:154` - Place Static Mesh.
- `Source/Editor/UI/EditorMainPanelPlacement.cpp:214` - Place Skeletal Mesh.
- `Source/Editor/UI/EditorMainPanelPlacement.cpp:273` - Place Prefab.
- `Source/Editor/UI/EditorMainPanelPlacement.cpp:313` - Place Prefab.

ActorSequence paths:

- `Source/Editor/UI/EditorActorSequenceDetails.cpp:51` - Edit Actor Sequence.
- `Source/Editor/UI/EditorActorSequenceEditModel.cpp:920` - Edit Actor Sequence.

Component and property paths:

- `Source/Editor/UI/EditorPropertyWidget.cpp:824` - Add Component.
- `Source/Editor/UI/EditorPropertyWidget.cpp:1020` - Attach Component.
- `Source/Editor/UI/EditorPropertyWidget.cpp:1033` - Set Updated Component.
- `Source/Editor/UI/EditorPropertyWidget.cpp:1107` - Delete Component.
- `Source/Editor/UI/EditorPropertyWidget.cpp:1175` - Edit Billboard.
- `Source/Editor/UI/EditorPropertyWidget.cpp:1201` - Edit Light.
- `Source/Editor/UI/EditorPropertyWidget.cpp:1217` - Edit Light.
- `Source/Editor/UI/EditorPropertyWidget.cpp:1271` - Remove Actor Tag.
- `Source/Editor/UI/EditorPropertyWidget.cpp:1307` - Add Actor Tag.
- `Source/Editor/UI/EditorPropertyWidget.cpp:1356` - Remove Component Tag.
- `Source/Editor/UI/EditorPropertyWidget.cpp:1387` - Add Component Tag.
- `Source/Editor/UI/EditorPropertyWidget.cpp:1600` - Edit Component Reference.
- `Source/Editor/UI/EditorPropertyWidget.cpp:2052` - Edit Material Slot.
- `Source/Editor/UI/EditorPropertyWidget.cpp:2080` - Edit Material Slot.
- `Source/Editor/UI/EditorPropertyWidget.cpp:2172` - Edit Property.
- `Source/Editor/UI/EditorPropertyWidget.cpp:2185` - Edit Property.
- `Source/Editor/UI/EditorPropertyWidget.cpp:2239` - Edit Property.
- `Source/Editor/UI/EditorPropertyWidget.cpp:2247` - Edit Property.
- `Source/Editor/UI/EditorPropertyWidget.cpp:2395` - Call Function.
- `Source/Editor/UI/EditorPropertyWidget.cpp:2438` - Edit Property.
- `Source/Editor/UI/EditorPropertyWidget.cpp:2453` - Edit Property.
- `Source/Editor/UI/EditorPropertyWidget.cpp:2468` - Edit Property.
- `Source/Editor/UI/EditorPropertyWidget.cpp:2483` - Edit Property.
- `Source/Editor/UI/EditorPropertyWidget.cpp:2498` - Edit Property.
- `Source/Editor/UI/EditorPropertyWidget.cpp:2513` - Edit Property.
- `Source/Editor/UI/EditorPropertyWidget.cpp:2531` - Edit Property.
- `Source/Editor/UI/EditorPropertyWidget.cpp:2548` - Edit Property.
- `Source/Editor/UI/EditorPropertyWidget.cpp:2563` - Edit Property.
- `Source/Editor/UI/EditorPropertyWidget.cpp:2725` - Edit Property.
- `Source/Editor/UI/EditorPropertyWidget.cpp:2752` - Edit Property.
- `Source/Editor/UI/EditorPropertyWidget.cpp:2795` - Edit Property.
- `Source/Editor/UI/EditorPropertyWidget.cpp:2818` - Edit Property.
- `Source/Editor/UI/EditorPropertyWidget.cpp:2862` - Edit Property.
- `Source/Editor/UI/EditorPropertyWidget.cpp:2884` - Edit Property.
- `Source/Editor/UI/EditorPropertyWidget.cpp:2946` - Edit Property.
- `Source/Editor/UI/EditorPropertyWidget.cpp:2968` - Edit Property.
- `Source/Editor/UI/EditorPropertyWidget.cpp:3024` - Edit Material.
- `Source/Editor/UI/EditorPropertyWidget.cpp:3048` - Edit Material.
- `Source/Editor/UI/EditorPropertyWidget.cpp:3210` - Edit Bone Pose.
- `Source/Editor/UI/EditorPropertyWidget.cpp:3241` - Reset Bone Pose.
- `Source/Editor/UI/EditorPropertyWidget.cpp:3251` - Reset Bone Pose.
- `Source/Editor/UI/EditorPropertyWidget.cpp:3353` - Rename.

Scene/outliner paths:

- `Source/Editor/UI/EditorSceneWidget.cpp:610` - Rename Actor.

## 0.3 Direct Mutation Surfaces Found During Audit

These edit surfaces must be covered even if they do not currently call `CaptureSnapshot()`.

Curve editor:

- `Source/Editor/UI/EditorCurveEditorWidget.cpp`
- Adds keys through `AddKey()` and `AddKeyAt()`.
- Removes keys through `RemoveSelectedKey()` and `RemoveKeyAtIndex()`.
- Moves key time/value directly during canvas drag.
- Edits tangents directly during handle drag.
- Edits interpolation and tangent values directly in the key table.
- Saves/reloads curve assets.

ActorSequence editor:

- `Source/Editor/UI/EditorActorSequenceEditModel.cpp`
- `Source/Editor/UI/EditorActorSequenceDetails.cpp`
- `Source/Editor/UI/EditorActorSequencerWidget.cpp`
- Adds bindings/tracks/sections/channels.
- Adds/removes/moves keys.
- Edits section timing.
- Edits sequence playback timing.
- Edits channel apply mode and time mapping.
- Edits component play rate.

Material and material slot editing:

- `Source/Editor/UI/EditorMaterialWidget.cpp`
- `Source/Editor/UI/EditorPropertyWidget.cpp`
- Creates material instances.
- Saves material instances.
- Applies material slot changes through primitive components.
- Edits material-related object references.

Skeletal mesh viewer and socket editing:

- `Source/Editor/UI/EditorViewerWindowWidget.cpp`
- `Source/Editor/Viewer/EditorViewer.cpp`
- Adds sockets.
- Deletes sockets.
- Renames sockets.
- Edits socket transform/properties.
- Adds/removes preview meshes.
- Saves skeletal mesh asset edits.

Packaging and project settings:

- `Source/Editor/UI/EditorMainPanelPackaging.cpp`
- `Source/Editor/UI/EditorMainPanelPackagingHelpers.cpp`
- `Source/Editor/UI/EditorMainPanelProjectSettings.cpp`
- `Source/Editor/UI/EditorMainPanelGameModeSettings.cpp`
- `Source/Editor/Settings/ProjectSettings.cpp`
- Edits startup scene, included scenes, output directory, build configuration, clean output, run after packaging, icon, splash, game name, game mode classes, and default pawn settings.
- Writes settings through `FProjectSettings::SaveToFile()`.

World settings:

- `Source/Editor/UI/EditorMainPanelGameModeSettings.cpp`
- Edits world-level game mode/default pawn/player controller settings.
- Saves into scene/world state.

Content browser and asset operations:

- `Source/Editor/UI/EditorContentBrowserWidget.cpp`
- `Source/Editor/Asset/EditorAssetService.cpp`
- Currently mostly browses/loads/opens assets, but any rename/move/delete/import/reimport added here must use file transaction commands.

Preview/runtime-only surfaces excluded from undo:

- PIE start/stop/eject/possess.
- Runtime UI preview play/input events.
- Viewport camera movement.
- Viewer preview mesh components if they are transient and not saved to the asset.
- Editor tab docking, panel visibility, popup state, scroll state, hover state.

## 0.4 Hard Replacement Batch Plan

The refactor should be executed as batches. Each batch must build before moving to the next one. Since intermediate undo compatibility is not required, the first batches may remove legacy undo behavior before all editor edit routes are reconnected.

### Batch 0 - Audit Lock And Compile Baseline

Goal:

- Freeze the edit surface list and ensure the branch starts from a known compiling state.

Checklist:

- [ ] Confirm no unreviewed local changes belong to undo refactor.
- [ ] Run `rg "CaptureSnapshot\\(" JSEngine/Source/Editor` and compare against section `0.2`.
- [ ] Run edit-surface searches for `SpawnActor`, `DestroyActor`, `AddComponent`, `RemoveComponent`, `Keys.push_back`, `Keys.erase`, `Sockets.push_back`, `Sockets.erase`, `BuildSettings`, and `SaveToFile`.
- [ ] Build Debug x64.

### Batch 1 - Replace Undo Core API With Transaction Skeleton

Goal:

- Remove world snapshot history and introduce transaction stacks.

Work:

- [x] Replace `FUndoSnapshotEntry` and `FWorldUndoHistory`.
- [x] Add `IEditorUndoCommand`, `FEditorTransaction`, `FEditorUndoContext`, and `FScopedEditorTransaction`.
- [x] Implement transaction-only `Undo()`, `Redo()`, `CanUndo()`, `CanRedo()`, `ClearHistory()`, stats, labels, and memory budgeting.
- [x] Keep old `CaptureSnapshot()` only as a deprecated no-op migration marker.
- [x] Update `EditorCommandSystem` compatibility through transaction history accessors.
- [x] Update debug/console history UI to transaction entries.

Batch completion:

- [x] `FEditorUndoSystem` no longer calls `CaptureSceneSnapshot()` or `RestoreSceneSnapshot()`.
- [x] `EditorCommandSystem` compiles against transaction undo.
- [x] History UI compiles against transaction history.
- [x] Build passes.

Notes:

- `CaptureSnapshot()` still exists only so existing edit paths compile during the migration. It does not capture or push history.
- Remaining `CaptureSnapshot()` call sites are the Batch 3-9 replacement inventory.

### Batch 2 - Stable Reference And Serialization Utilities

Goal:

- Give commands a safe way to resolve targets.

Work:

- [x] Add world object refs.
- [x] Add asset refs.
- [x] Add ActorSequence refs.
- [x] Add actor persistent guid serialization.
- [x] Add actor/component resolver by world handle + guid.
- Add property path/value serialization helpers.
- [x] Add actor/component serialize helpers for delete/restore.
- Add resolver diagnostics.

Batch completion:

- [x] Actor by guid can resolve.
- [x] Component by guid can resolve.
- [ ] Asset by path can resolve.
- [ ] Property path can read/write supported reflected values.
- [x] Delete backup helpers can serialize/restore actor/component data.
- [x] Build passes.

Notes:

- `AActor` now owns a serialized `PersistentGuid`, matching the existing `UActorComponent` persistent guid model.
- `FEditorObjectRef` resolves editor-world objects through `WorldHandle + ActorGuid + ComponentGuid`.
- `FEditorUndoObjectResolver` currently covers actors and components. Asset, ActorSequence sub-object, property-path, and resolver diagnostics remain for later batches.
- `FActorSerialization` now exposes component JSON backup/restore helpers used by component lifecycle undo commands.

### Batch 3 - World Actor Lifecycle Commands

Goal:

- Restore level editor basics before broad property work.

Work:

- [x] `FDeleteActorCommand` first pass for `UEditorEngine::DeleteActors`.
- [x] `FCreateActorCommand` first pass for actor placement and duplicate-created actors.
- [x] `FDuplicateActorCommand` covered by `FCreateActorCommand` for the newly duplicated actor set.
- [x] `FRenameActorCommand` covered by `FRenameObjectCommand` for Actor and Component names.
- [x] `FSetActorTransformCommand` first pass for actor gizmo drag transforms.
- `FSetActorAttachmentCommand`
- [x] `FSetActorTagsCommand` covered by generic `FSetObjectTagsCommand`.
- [x] Replace `EditorEngine::DeleteActors`.
- [x] Replace viewport duplicate.
- [x] Replace placement.
- [x] Replace scene outliner rename/delete routes.

Batch completion:

- [x] Actor create/delete/duplicate/rename/transform are transaction based.
- [x] Multi-actor delete and multi-actor transform are one transaction.
- [x] Spatial index and selection update after undo/redo for actor create/delete/transform routes.
- [x] Build passes.

Notes:

- Actor delete is now transaction-backed through `FDeleteActorsCommand`.
- Delete Undo uses actor JSON backup data and restores the same actor persistent guid.
- Delete Redo resolves the restored actor by `WorldHandle + ActorGuid` and destroys it again.
- Actor placement and viewport duplicate are now transaction-backed through `FCreateActorsCommand`.
- Placement/create Undo destroys the created actor set by guid.
- Placement/create Redo recreates the same actor data from JSON with preserved persistent guids.
- Actor gizmo transform now records drag start/end as one transaction instead of per-frame entries.
- Details panel and scene outliner rename now use `FRenameObjectCommand`.
- Component rename is covered by the same object rename command, but broader component lifecycle/property commands remain Batch 4.
- Actor and component tags are now handled by `FSetObjectTagsCommand`.
- Actor attachment still needs command conversion if/when editor exposes an actor attachment edit route.

### Batch 4 - Component Lifecycle And References

Goal:

- Cover component creation/removal/attachment/reference edits.

Work:

- [x] `FAddComponentCommand`
- [x] `FDeleteComponentCommand`
- [x] `FRenameComponentCommand` covered by `FRenameObjectCommand`.
- [x] `FSetComponentAttachmentCommand`
- [x] `FSetComponentReferenceCommand` for `UMovementComponent::UpdatedComponent`.
- [x] `FSetMaterialSlotCommand`
- [x] Replace details add/delete component paths.
- [x] Replace attach component path.
- [x] Replace updated component path.
- [x] Replace material slot path.

Batch completion:

- [x] Component add/delete/rename/attach/reference/material slot edits are transaction based for the current Details UI routes.
- [x] Component guids restore correctly for add/delete.
- [ ] Component order restore correctly.
- [x] Build passes.

Notes:

- Component add/delete uses component JSON backup data instead of world snapshots.
- Component restore preserves UUID and persistent guid through serialization.
- MovementComponent `UpdatedComponent` is included in component JSON backup data and has a direct command for drag/drop retargeting.
- SceneComponent drag/drop attachment has a direct before/after attachment command.
- Material slot changes now use `FSetMaterialSlotCommand`.
- `Updated Component` changes from both drag/drop and Details combo now use `FSetMovementUpdatedComponentCommand`.
- Generic reflected object reference properties beyond `Updated Component` still belong to Batch 5 if more are introduced.

### Batch 5 - Generic Property Widget Conversion

Goal:

- Convert the high-volume property editing paths.

Work:

- [x] Generic object-state property command for Actor/Component JSON-backed edits.
- [x] Array add/remove/set routes covered through before/after object state.
- [x] Convert primitive reflected property widgets.
- [x] Convert struct/vector/color widgets.
- [x] Convert enum/object/asset reference widgets.
- [x] Convert function-call edits that mutate state through before/after object state.
- [x] Convert billboard texture, light range/angle, tags, material refs, and material slot UI routes.
- [x] Add skeletal bone pose command for runtime pose data not covered by component serialization.
- [ ] Improve all remaining numeric drag routes to strictly one transaction per drag.

Batch completion:

- [x] All `CaptureSnapshot("Edit Property")` property paths are gone.
- [ ] Drag values record one transaction across every Details route.
- [x] Text edits record on commit/focus loss for converted reflected property routes.
- [x] Build passes.

Notes:

- `FEditorObjectState` now captures Actor/Component JSON before/after and re-applies it through `FSetObjectStateCommand` / `FSetObjectStatesCommand`.
- Reflected primitive, vector, color, enum, array, asset-reference, tag, material-reference, billboard, light, and call-in-editor property paths no longer call `CaptureSnapshot()`.
- Skeletal bone pose uses `FEditorSkeletalBonePoseState` and `FSetSkeletalBonePoseCommand` because `USkeletalMeshComponent::CurrentLocalPose` is not serialized as a normal component property.
- Some immediate Details drags currently record per edit tick instead of a single begin/end transaction. This is functionally undoable but still needs UX batching polish.

### Batch 6 - Gizmo Drag Batching

Goal:

- Make viewport/gizmo editing behave like a normal editor.

Work:

- Capture before transforms on drag start.
- Apply live transforms without recording.
- Capture after transforms on drag end.
- Push one transaction.
- Cover translate/rotate/scale and multi selection.

Batch completion:

- [x] `Transform Actors` snapshot path is gone.
- [x] One viewport gizmo drag is one transaction.
- [x] Build passes.

Notes:

- `FEditorViewportClient` captures selected actor transforms at gizmo drag start and records `FSetActorTransformsCommand` once on drag end.

### Batch 7 - ActorSequence Transaction Commands

Goal:

- Fully cover sequencer edits outside world snapshot.

Work:

- [x] SequenceComponent before/after object-state command for sequence data.
- [x] Sequence duration/playback edits covered through object state.
- [x] Track/section/channel/key add/remove/move edits covered through object state.
- [x] Channel apply/time mapping edits covered through object state.
- [x] Key drag and section/playback-range drag use capture-before + notify-after flow.
- [x] Remove `FEditorActorSequenceEditModel::CaptureUndo()` snapshot path.
- [x] Convert details and sequencer widget edit routes.
- [ ] Split into smaller semantic command classes later if diff size or memory usage becomes a problem.

Batch completion:

- [x] ActorSequence has no snapshot undo dependency.
- [x] Track/key/delete/timing/channel edits undo/redo.
- [x] Build passes.

Notes:

- `FEditorActorSequenceEditModel::CaptureSequenceUndo()` now stores the `UActorSequenceComponent` before-state instead of calling world snapshot undo.
- `NotifySequenceEdited()` captures the after-state and records one object-state transaction for sequencer edit-model routes.
- `FEditorActorSequenceDetails` records direct before/after object-state transactions for AutoPlay, Looping, PlayRate, PauseAtEnd, and StartOffset edits.
- Drag batching is correct for sequencer timeline routes that already separate capture and notify. Details numeric drags still need polish to avoid per-frame transactions.

### Batch 8 - Curve Editor Transaction Commands

Goal:

- Cover curve asset and embedded curve edits.

Work:

- [x] Add curve asset before/after state command.
- [x] Asset curve add/remove/edit key commands.
- [x] ActorSequence embedded curve edits covered through owning `UActorSequenceComponent` object state.
- [x] Tangent/interp edits covered for canvas/table routes.
- [x] Key/tangent canvas drag uses begin/end batching.
- [x] Dirty state integration preserved.
- [ ] AnimSequence embedded curve undo needs a stable owner reference instead of only `SaveCallback`.

Batch completion:

- [x] Curve editor direct key mutations are transaction based for asset curves and ActorSequence embedded curves.
- [x] Dragging a key/tangent records one transaction on the canvas.
- [x] Build passes.

Notes:

- `FEditorCurveAssetState` stores `AssetPath + FFloatCurve` and applies via `FSetCurveAssetStateCommand`.
- `FEditorCurveEditorWidget` records asset curve edits by path and ActorSequence embedded curve edits by capturing the owning sequence component before/after.
- Key table numeric drags are undoable, but may still create more transactions than ideal during continuous dragging.
- AnimSequence embedded curve editing remains explicitly deferred until the editor exposes a stable AnimSequence asset/object reference to the undo system.

### Batch 9 - Material And Mesh Asset Editors

Goal:

- Cover asset document edits that currently bypass world undo.

Work:

- [x] Material parameter/reference/instance commands.
- [x] Material slot commands not already covered in component batch.
- [x] Skeletal mesh socket add/delete/rename/property commands.
- [x] Mesh socket preview policy: saved socket preview data is undoable, transient preview components are not.
- [ ] Improve Material numeric drag routes to strictly one transaction per drag.
- [ ] Improve Skeletal Mesh socket transform drag routes to strictly one transaction per drag.

Batch completion:

- [x] Material editor edits undo/redo for material instance parameters.
- [x] Skeletal mesh socket edits undo/redo.
- [x] Build passes.

Notes:

- `FEditorMaterialState` stores `MaterialPath + Params` and applies through `FSetMaterialStateCommand`.
- Material instance parameter edits now update both runtime memory and serialized material instance data during Undo/Redo.
- `FEditorSkeletalMeshSocketState` stores `SkeletalMeshPath + Sockets[]` and applies through `FSetSkeletalMeshSocketStateCommand`.
- Socket add/delete/rename/bone retarget/relative transform edits are transaction-backed.
- Socket preview meshes remain transient and are intentionally excluded.
- Material creation/file creation itself remains a Batch 10 file-transaction concern; assigning a created instance to a component slot is already covered by material-slot undo.

### Batch 10 - Settings, Packaging, And File Transactions

Goal:

- Cover non-world editor settings and file operations.

Work:

- [x] Project settings command.
- [x] World settings command.
- [x] Packaging settings command.
- [x] Asset rename/delete/import/create commands for currently exposed Content Browser routes.
- [x] Folder commands.
- [x] File snapshot restore policy for asset delete.
- [x] Material creation/file creation command.
- [ ] Asset move/duplicate commands when exposed by UI.

Batch completion:

- [x] Project/world/packaging setting edits are transaction based.
- [x] File delete is recoverable for project-local Content Browser deletes.
- [x] Packaging execution is not undoable.
- [x] Build passes.

Notes:

- `FEditorProjectSettingsState` stores `Settings/Project.ini` state as `BuildSettings + LastScenePath + SkinningMode`.
- Project Game Mode defaults, Project Rendering skinning mode, Packaging modal `Package` settings save, and console `skinning cpu/gpu` are transaction-backed.
- `FEditorWorldGameModeSettingsState` stores the focused world handle and world-level GameMode override fields.
- World Settings `Save World` now records before/after state and marks the scene dirty on Undo/Redo.
- Opening the Packaging modal may still refresh `LastScenePath` as editor convenience state; the user-visible Packaging settings change is recorded when `Package` is pressed.
- Packaging execution, copied build output, generated binaries, and asynchronous MSBuild/package side effects are intentionally not undoable.
- `FEditorFileSystemState` stores project-local files/directories as path plus bytes and is used by create/delete file commands.
- Content Browser create folder/text/lua/material/curve/scene, delete, rename, and FBX import routes are transaction-backed.
- Material Editor `Create Instance` records the created `.matinst` file and records the component material slot assignment when invoked from a slot.
- Details `Create Script` records the created Lua file and the ScriptComponent state assignment.
- Current file delete restore is snapshot-based, not trash-based. This is acceptable for editor undo, but does not provide a user-visible recycle bin.
- Move/duplicate are deferred because no exposed Content Browser move/duplicate UI was found in the current implementation.

### Batch 11 - Legacy Removal And Enforcement

Goal:

- Make transaction usage mandatory.

Work:

- [x] Delete or hard-disable `CaptureSnapshot()`.
- [x] Remove snapshot undo structs and per-world snapshot histories.
- [x] Remove undo dependency on `CaptureSceneSnapshot()` and `RestoreSceneSnapshot()`.
- [x] Add helper APIs for common editor mutations.
- [x] Add debug/assert guard for editor mutation without transaction where practical.
- [x] Run final `rg` sweeps.

Batch completion:

- [x] `rg "CaptureSnapshot\\(" JSEngine/Source/Editor` returns no active edit path usage.
- [x] `rg "RestoreSceneSnapshot" JSEngine/Source/Editor/Undo` returns no undo-system usage.
- [x] All items in sections `0.2` and `0.3` are covered or explicitly excluded.
- [x] Full build passes.

Notes:

- `FEditorUndoSystem::CaptureSnapshot()` has been removed from the public API.
- `UEditorEngine::CaptureSceneSnapshot()` and `RestoreSceneSnapshot()` still exist as scene serialization/load helpers, but the undo system no longer calls them.
- Runtime/stat/camera/preview snapshot names found by broad search are unrelated to editor undo.
- `FEditorUndoSystem` exposes mutation-tracking guard state through transaction revision and pending-capture flags.
- `FEditorSceneService::MarkDirty()` now warns when a scene first becomes dirty without a recent editor undo transaction/capture.
- The guard is intentionally practical detection, not a full setter-level write barrier. Asset-only mutations and transient preview state still rely on the transaction helper APIs and explicit exclusions above.

## 1. Non-Negotiable Rules

- No editor state mutation without a transaction.
- No undo command stores a raw object pointer as its persistent target.
- Every command stores enough before/after data to undo and redo without recomputing.
- Every delete command stores enough data to restore the deleted object.
- Undo executes commands in reverse order.
- Redo executes commands in forward order.
- New transactions clear the redo stack.
- During undo/redo application, no new transaction may be recorded.
- PIE start/stop, viewport navigation, tab switching, hover, scroll, and preview playback state are not undoable edits.
- If an edit path cannot create a transaction yet, mark it as a known gap before merging.

## 2. Core Types To Implement

### IEditorUndoCommand

- `GetLabel()`
- `Undo(FEditorUndoContext&)`
- `Redo(FEditorUndoContext&)`
- `GetMemoryUsage()`
- Optional: `GetAffectedDocuments()`

### FEditorTransaction

- Label
- Command list
- Affected document list
- Timestamp or serial
- Memory usage
- Undo by reverse command order
- Redo by forward command order

### FEditorUndoContext

- `UEditorEngine*`
- Object resolver
- Asset resolver
- Notification service
- Selection manager lookup
- Dirty-state service or equivalent hooks

### FEditorUndoSystem

- `BeginTransaction(Label)`
- `AddCommand(Command)`
- `EndTransaction()`
- `CancelTransaction()`
- `Undo()`
- `Redo()`
- `CanUndo()`
- `CanRedo()`
- `ClearHistory()`
- `IsApplyingUndoRedo()`
- Transaction count and memory-budget trimming

### FScopedEditorTransaction

- Begins a transaction on construction.
- Ends transaction on successful scope exit.
- Cancels transaction if no command was added or if explicitly cancelled.

## 3. Stable Reference Types

### World Object Reference

Required fields:

- World handle
- Actor persistent guid
- Component persistent guid, optional
- Subobject path, optional
- Property path, optional

Used for:

- Actor
- Component
- Component property
- Actor-owned sequence component
- Actor-owned material slot overrides

### Asset Reference

Required fields:

- Asset path
- Asset type
- Object guid, if present

Used for:

- Curve asset
- Material asset
- Material instance
- Runtime UI asset
- Imported asset metadata

### ActorSequence Reference

Required fields:

- Owner component ref or asset ref
- Sequence guid
- Binding guid
- Track guid
- Section guid
- Channel name or channel guid
- Key handle or key guid

Used for:

- Binding edits
- Track edits
- Section edits
- Channel edits
- Key edits

## 4. Phase 1: Replace Undo Core

Status checklist:

- [x] Add `IEditorUndoCommand`.
- [x] Add `FEditorTransaction`.
- [x] Add `FEditorUndoContext`.
- [x] Add `FScopedEditorTransaction`.
- [x] Replace `FEditorUndoSystem` internals with transaction stacks.
- [x] Remove active-world-only history assumptions.
- [x] Replace `FUndoSnapshotEntry` history data in toolbar/history UI.
- [x] Update `FEditorCommandSystem::CanExecute(Undo/Redo)`.
- [x] Update `FEditorCommandSystem::Execute(Undo/Redo)`.
- [x] Add applying guard to prevent nested recording during undo/redo.
- [x] Add transaction memory accounting.
- [x] Add transaction-count limit.
- [x] Add redo clear on new transaction.
- [x] Add notification on undo/redo failure.
- [x] Build.

Completion criteria:

- Undo/Redo buttons operate from transaction stack.
- Empty transactions are discarded.
- No snapshot restore is used by the new undo path.

## 5. Phase 2: Object And Property Resolve Layer

Status checklist:

- [x] Implement world object resolver by world handle + actor guid.
- [x] Implement component resolver by actor guid + component guid.
- [ ] Implement asset resolver by asset path.
- [ ] Implement ActorSequence resolver.
- [ ] Implement property path resolver.
- [ ] Implement property value serialize/deserialize helpers.
- [ ] Support primitive property values.
- [ ] Support struct property values.
- [ ] Support object/asset reference values.
- [ ] Support array element paths or add dedicated array commands.
- [ ] Add failure diagnostics for unresolved target.
- [x] Build.

Completion criteria:

- A command can find its target after ordinary editor interactions.
- Missing target errors are visible and do not corrupt stacks.

## 6. Phase 3: Generic Property Undo

Commands:

- `FSetPropertyCommand`
- `FArrayInsertCommand`
- `FArrayRemoveCommand`
- `FArrayMoveCommand`
- `FArraySetElementCommand`

Status checklist:

- [ ] Bool property undo/redo.
- [ ] Int property undo/redo.
- [ ] Float property undo/redo.
- [ ] String property undo/redo.
- [ ] Name property undo/redo.
- [ ] Enum property undo/redo.
- [ ] Vector property undo/redo.
- [ ] Rotator property undo/redo.
- [ ] Color property undo/redo.
- [ ] Asset reference undo/redo.
- [ ] Actor reference undo/redo.
- [ ] Component reference undo/redo.
- [ ] Nested struct property undo/redo.
- [ ] Array add undo/redo.
- [ ] Array remove undo/redo.
- [ ] Array reorder undo/redo.
- [ ] Text edit records on commit/focus loss, not per character unless explicitly desired.
- [ ] Slider/drag records one transaction at drag end.
- [ ] Replace common `CaptureSnapshot("Edit Property")` call sites.
- [ ] Build.

Completion criteria:

- Details/property widget edits are covered by commands.
- Dragging a numeric property creates exactly one transaction.

## 7. Phase 4: Level Actor Commands

Commands:

- `FCreateActorCommand`
- `FDeleteActorCommand`
- `FDuplicateActorCommand`
- `FRenameActorCommand`
- `FSetActorTransformCommand`
- `FSetActorAttachmentCommand`
- `FSetActorTagsCommand`
- `FSetActorVisibilityCommand`, if supported
- `FSetActorActiveCommand`, if supported

Status checklist:

- [ ] Actor creation undo/redo.
- [x] Actor deletion undo/redo.
- [x] Multi-actor deletion undo/redo.
- [ ] Actor duplicate undo/redo.
- [ ] Actor rename undo/redo.
- [ ] Actor transform undo/redo.
- [ ] Multi-actor transform undo/redo.
- [ ] Actor attach undo/redo.
- [ ] Actor detach undo/redo.
- [ ] Actor tag add/remove undo/redo.
- [ ] Actor order/index restore, if ordering matters.
- [ ] Spatial index rebuild/sync after undo/redo.
- [ ] Selection restore after create/delete/duplicate.
- [ ] Replace placement undo paths.
- [ ] Replace scene outliner rename/delete paths.
- [ ] Replace viewport delete/duplicate shortcuts.
- [ ] Build.

Delete requirements:

- [ ] Serialized actor data is stored before deletion.
- [ ] Actor guid is preserved on restore.
- [ ] Component guids are preserved on restore.
- [ ] Attachment is restored.
- [ ] Tags are restored.
- [ ] Material slots are restored.
- [ ] Script components are restored.
- [ ] ActorSequence components are restored.

Completion criteria:

- Actor lifecycle operations behave normally through repeated undo/redo.

## 8. Phase 5: Gizmo And Viewport Transform

Status checklist:

- [ ] Capture selected actor transforms at drag start.
- [ ] Apply live transforms during drag without recording per-frame commands.
- [ ] Capture final transforms at drag end.
- [ ] Create one transaction for move.
- [ ] Create one transaction for rotate.
- [ ] Create one transaction for scale.
- [ ] Support multi-selection.
- [ ] Support snapping.
- [ ] Ignore no-op drags.
- [ ] Ensure undo does not re-enter gizmo drag state.
- [ ] Build.

Completion criteria:

- One drag equals one undo step.
- Multi-actor transform restores all actors exactly.

## 9. Phase 6: Component Commands

Commands:

- `FAddComponentCommand`
- `FDeleteComponentCommand`
- `FRenameComponentCommand`
- `FSetComponentPropertyCommand`
- `FSetComponentAttachmentCommand`
- `FSetComponentReferenceCommand`
- `FSetMaterialSlotCommand`

Status checklist:

- [ ] Component add undo/redo.
- [ ] Component delete undo/redo.
- [ ] Component rename undo/redo.
- [ ] Component transform undo/redo.
- [ ] Component property undo/redo.
- [ ] Component attach/detach undo/redo.
- [ ] UpdatedComponent/reference property undo/redo.
- [ ] Material slot undo/redo.
- [ ] Component array order restore.
- [ ] Component persistent guid restore.
- [ ] Owner actor references update after restore.
- [ ] Replace details panel add/delete component paths.
- [ ] Replace attach component paths.
- [ ] Build.

Delete requirements:

- [ ] Serialized component data is stored before deletion.
- [ ] Component class is restored.
- [ ] Component name is restored.
- [ ] Component guid is restored.
- [ ] Component properties are restored.
- [ ] Component attachment is restored.
- [ ] Owner actor component order is restored.

Completion criteria:

- Component lifecycle operations survive repeated undo/redo.

## 10. Phase 7: ActorSequence Commands

Commands:

- `FActorSequenceAddBindingCommand`
- `FActorSequenceRemoveBindingCommand`
- `FActorSequenceAddTrackCommand`
- `FActorSequenceRemoveTrackCommand`
- `FActorSequenceSetTrackPropertyCommand`
- `FActorSequenceAddSectionCommand`
- `FActorSequenceRemoveSectionCommand`
- `FActorSequenceSetSectionTimingCommand`
- `FActorSequenceAddChannelCommand`
- `FActorSequenceRemoveChannelCommand`
- `FActorSequenceSetChannelPlaybackCommand`
- `FActorSequenceAddKeyCommand`
- `FActorSequenceRemoveKeyCommand`
- `FActorSequenceEditKeyCommand`
- `FActorSequenceEditTangentCommand`

Status checklist:

- [ ] Sequence creation undo/redo.
- [ ] Binding add/remove undo/redo.
- [ ] Binding target change undo/redo.
- [ ] Track add/remove undo/redo.
- [ ] Track rename undo/redo.
- [ ] Target property path change undo/redo.
- [ ] Section add/remove undo/redo.
- [ ] Section start time undo/redo.
- [ ] Section duration undo/redo.
- [ ] Section play rate undo/redo.
- [ ] Section loop flag undo/redo.
- [ ] Channel add/remove undo/redo.
- [ ] Channel curve change undo/redo.
- [ ] Channel apply mode undo/redo.
- [ ] Channel time mapping mode undo/redo.
- [ ] Key add/remove undo/redo.
- [ ] Key time/value edit undo/redo.
- [ ] Key tangent edit undo/redo.
- [ ] Key interpolation edit undo/redo.
- [ ] Dragging keys creates one transaction.
- [ ] Preview play/stop is not recorded.
- [ ] Preview time scrub policy is explicitly decided.
- [ ] Build.

Delete requirements:

- [ ] Removed binding data is fully stored.
- [ ] Removed track data is fully stored.
- [ ] Removed section data is fully stored.
- [ ] Removed channel data is fully stored.
- [ ] Guids are preserved on restore.

Completion criteria:

- ActorSequence editor edits are fully undoable without touching world snapshot.

## 11. Phase 8: Curve Editor Commands

Commands:

- `FCurveAddKeyCommand`
- `FCurveRemoveKeyCommand`
- `FCurveEditKeyCommand`
- `FCurveEditTangentCommand`
- `FCurveSetInterpModeCommand`

Status checklist:

- [ ] Key add undo/redo.
- [ ] Key remove undo/redo.
- [ ] Key time edit undo/redo.
- [ ] Key value edit undo/redo.
- [ ] Tangent edit undo/redo.
- [ ] Interpolation mode undo/redo.
- [ ] Multi-key edit undo/redo, if supported.
- [ ] Key drag creates one transaction.
- [ ] Asset dirty state updates.
- [ ] Build.

Completion criteria:

- Curve editor can edit, undo, redo, and preserve dirty state correctly.

## 12. Phase 9: Material Commands

Commands:

- `FMaterialSetParameterCommand`
- `FMaterialSetTextureCommand`
- `FMaterialSetParentCommand`
- `FMaterialSetFlagCommand`
- `FMaterialSlotChangeCommand`
- `FMaterialCreateCommand`
- `FMaterialDeleteCommand`

Status checklist:

- [ ] Scalar parameter undo/redo.
- [ ] Vector/color parameter undo/redo.
- [ ] Texture parameter undo/redo.
- [ ] Boolean/switch parameter undo/redo.
- [ ] Parent material undo/redo.
- [ ] Blend/shading/two-sided flags undo/redo, if supported.
- [ ] Material instance override undo/redo.
- [ ] Static mesh material slot undo/redo.
- [ ] Skeletal mesh material slot undo/redo.
- [ ] Material asset dirty state updates.
- [ ] Build.

Completion criteria:

- Material editor edits are undoable before save and after repeated redo.

## 13. Phase 10: Settings And Packaging Commands

Commands:

- `FSetProjectSettingsStateCommand`
- `FSetWorldGameModeSettingsCommand`

Status checklist:

- [x] Project startup scene undo/redo when saved through Packaging.
- [x] Project GameMode/default pawn/player controller undo/redo.
- [x] Render skinning setting undo/redo.
- [ ] Input setting undo/redo, if editable.
- [ ] Audio setting undo/redo, if editable.
- [x] World GameMode setting undo/redo.
- [x] World default pawn setting undo/redo.
- [x] Packaging output directory undo/redo.
- [x] Packaging build configuration undo/redo.
- [ ] Packaging target platform undo/redo.
- [x] Packaging include/copy rule undo/redo.
- [x] Packaging execution itself is not recorded.
- [x] Build.

Completion criteria:

- Project Settings, World Settings, Packaging setting saves, and console skinning edits behave like document edits.
- Remaining unchecked items are only for settings categories that are not currently exposed as editable UI in this engine.

## 14. Phase 11: Content Browser And Asset File Commands

Commands:

- `FAssetRenameCommand`
- `FAssetMoveCommand`
- `FAssetDuplicateCommand`
- `FAssetDeleteCommand`
- `FFolderCreateCommand`
- `FFolderDeleteCommand`
- `FFolderRenameCommand`
- `FAssetImportCommand`
- `FAssetReimportCommand`

Status checklist:

- [ ] Choose trash/soft-delete policy.
- [ ] Asset rename undo/redo.
- [ ] Asset move undo/redo.
- [ ] Asset duplicate undo/redo.
- [ ] Asset delete undo/redo.
- [ ] Folder create undo/redo.
- [ ] Folder delete undo/redo.
- [ ] Folder rename undo/redo.
- [ ] Import undo/redo policy.
- [ ] Reimport undo/redo policy.
- [ ] Resource cache refresh after file undo/redo.
- [ ] Content browser refresh after file undo/redo.
- [ ] Build.

Recommended delete policy:

- Move files into `.EditorTrash/Transaction_<id>/...`.
- Undo restores original path.
- Redo moves back to trash.

Completion criteria:

- File operations are reversible without data loss.

## 15. Phase 12: Legacy Removal Sweep

Status checklist:

- [x] Remove or hard-deprecate `CaptureSnapshot`.
- [x] Remove snapshot-based undo entry structs.
- [x] Remove active-world-only undo history.
- [x] Remove undo dependency on `CaptureSceneSnapshot`.
- [x] Remove undo dependency on `RestoreSceneSnapshot`.
- [x] Keep scene save/load serialization for save/load and import/file helpers only.
- [x] `rg "CaptureSnapshot"` returns no active edit-path usage.
- [x] `rg "RestoreSceneSnapshot"` returns no undo-system usage.
- [x] All audited editor edit paths use transaction helpers or are explicitly excluded.
- [x] Build.

Completion criteria:

- Undo system no longer serializes/restores the whole world for normal editor undo.

## 16. Regression Test Matrix

### Level Editor

- [x] Create actor undo/redo.
- [x] Delete actor undo/redo.
- [x] Delete multiple actors undo/redo.
- [x] Duplicate actor undo/redo.
- [x] Rename actor undo/redo.
- [x] Move actor undo/redo.
- [x] Rotate actor undo/redo.
- [x] Scale actor undo/redo.
- [x] Multi-actor transform undo/redo.
- [ ] Attach/detach undo/redo.
- [ ] Save scene after undo.
- [ ] Save scene after redo.

### Component

- [ ] Add component undo/redo.
- [ ] Delete component undo/redo.
- [ ] Rename component undo/redo.
- [ ] Component property undo/redo.
- [ ] Component reference undo/redo.
- [ ] Material slot undo/redo.

### Property Widget

- [ ] Bool.
- [ ] Int.
- [ ] Float.
- [ ] String.
- [ ] Name.
- [ ] Enum.
- [ ] Vector.
- [ ] Color.
- [ ] Asset reference.
- [ ] Actor reference.
- [ ] Component reference.
- [ ] Array add/remove/reorder.
- [ ] Nested struct.

### ActorSequence

- [ ] Add/remove binding.
- [ ] Add/remove track.
- [ ] Add/remove section.
- [ ] Add/remove channel.
- [ ] Add/remove key.
- [ ] Move key.
- [ ] Edit key value.
- [ ] Edit tangent.
- [ ] Edit playback desc.
- [ ] Change target property.

### Curve

- [ ] Add/remove key.
- [ ] Move key.
- [ ] Edit value.
- [ ] Edit tangent.
- [ ] Edit interpolation.

### Material

- [ ] Scalar parameter.
- [ ] Vector parameter.
- [ ] Texture parameter.
- [ ] Material slot.
- [ ] Instance override.

### Settings

- [ ] Project setting.
- [ ] World setting.
- [ ] Packaging setting.

### Asset Files

- [ ] Rename asset.
- [ ] Move asset.
- [ ] Duplicate asset.
- [ ] Delete asset.
- [ ] Restore deleted asset.
- [ ] Rename folder.
- [ ] Delete folder.

## 17. Completion Definition

The refactor is complete only when:

- [ ] All old snapshot edit paths are gone.
- [ ] Every user-visible editor edit route creates a transaction.
- [ ] Every delete operation can be undone and redone.
- [ ] Every drag operation records exactly one transaction.
- [ ] Every multi-object operation records exactly one transaction.
- [ ] Undo/Redo works for world, sequence, curve, material, settings, and asset edits.
- [ ] Dirty states are updated by undo/redo.
- [ ] Undo history UI shows transaction labels.
- [ ] Build passes.
- [ ] Manual regression checklist is complete.
