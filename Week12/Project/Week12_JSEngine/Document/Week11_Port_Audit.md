# Week11 Engine Port Audit

Source project: `C:\Users\jungle\Desktop\YG\Week11\Projects`

Target project: `C:\Users\jungle\Desktop\YG\Week12\Project\Week12_JSEngine`

Date: 2026-05-26

## Summary

Week11 and Week12 still share a close engine lineage, but their centers of gravity differ.

- Week11 has stronger editor workflow features: transaction-style undo, runtime UI designer work, blueprint editor/runtime, asset/file undo, skeletal socket undo, and some richer detail-panel editing.
- Week12 has newer architecture in several core areas: generated reflection pipeline, `TObjectPtr`/`TSoftObjectPtr`, particle/Cascade work, newer animation graph/runtime, newer collision branch work, and a cleaner multi-actor transform proxy design.
- Avoid full-file replacement for most editor modules. The safe path is selective porting by feature.

The best first port is the Week11 transaction-based undo system, but it should be adapted to Week12's newer reflection, serialization, particles, and project-file generation.

## User Candidates

### 1. Undo System

Recommendation: port, but as a staged replacement.

Current Week12 undo is snapshot based:

- `FEditorUndoSystem::CaptureSnapshot`
- whole scene snapshots per world
- max history 50
- simple and robust, but heavy and coarse

Week11 undo is transaction/command based:

- `FEditorTransaction`
- `IEditorUndoCommand`
- scoped transactions
- actor transform commands
- object property state commands
- actor/component create/delete
- attachment changes
- movement updated component changes
- material slot changes
- material asset state
- curve asset state
- skeletal bone pose
- skeletal mesh socket edits
- project settings
- filesystem create/delete/rename
- memory accounting, max entries 200, max memory 128 MB

Risk:

- High integration surface.
- Week11 depends on old property/reflection types in places.
- Week12 has newer `PostEditProperty`, generated reflection, `ObjectPtr`, particle assets, and updated animation assets.
- `EditorPropertyWidget`, asset browser, material editor, scene widget, viewport transform, and project settings all need call-site updates.

Suggested approach:

- Batch 1: Add Week11 command framework beside current snapshot undo, without deleting snapshot undo.
- Batch 2: Port actor transform, rename, create/delete actor/component, material slot, attachment commands.
- Batch 3: Convert detail panel property edits to object-state undo.
- Batch 4: Add asset/file/project settings undo after basic editor workflows are stable.
- Batch 5: Remove legacy snapshot-only pathways once command undo covers normal workflows.

### 2. Gizmo Internal Calculation And Rendering

Recommendation: do not wholesale port. Compare and cherry-pick.

Findings:

- Week12 `TransformProxy.h` is structurally better for multi-actor transforms. `FActorTransformProxy` stores all selected actors and applies transform deltas to secondary actors.
- Week12 `GizmoComponent` has better rotation drag math than Week11: it stores an initial rotation transform, computes angle on the drag plane, and quantizes angle from the interaction start.
- Week11 `GizmoComponent` has older direct multi-actor rotate/scale logic inside the gizmo component itself.

Port candidates:

- Build regression tests/manual checklist from Week11 behavior: multi-select translate/rotate/scale around pivot, snap behavior, component transform, bone/socket gizmo.
- If Week12 multi-select scale feels wrong, compare Week11's axis-offset pivot scaling and port only that math into `FActorTransformProxy`.
- Keep Week12's initial-transform rotation model unless a specific bug shows up.

Avoid:

- Replacing Week12 `GizmoComponent` wholesale.
- Moving multi-actor transform logic back from `FActorTransformProxy` into `UGizmoComponent`.

### 3. Editor Detail Panel Design

Recommendation: selective design/UX port, not full replacement.

Findings:

- Week11 `EditorPropertyWidget` is heavily tied to old reflection/property descriptors and the large Week11 undo system.
- Week12 `EditorPropertyWidget` already understands newer reflection handles, `TObjectPtr`, `TSoftObjectPtr`, particle system properties, debug details, and current animation graph state.
- Week11 has richer specialized editors for some asset/property flows and transaction-aware edit state capture.

Port candidates:

- Visual polish helpers and layout conventions if they look better in-app.
- Transaction-aware property edit begin/commit model from Week11, after command undo exists.
- Specialized asset pickers or material preview details if Week12 lacks equivalent behavior.

Avoid:

- Replacing the entire `EditorPropertyWidget`.
- Bringing back old `FPropertyDescriptor` as a primary path.

## Additional High-Value Candidates

### Runtime UI Designer

Recommendation: high value, medium-to-high cost.

Week11 has:

- `URuntimeUILayoutAsset`
- large `EditorRuntimeUIPreviewWidget` with designer functionality
- layout asset model
- widget tree/editing/export path
- document plan: `RUNTIME_UI_DESIGNER_PLAN.md`

Week12 currently has only a much smaller runtime UI previewer and no `RuntimeUILayoutAsset`.

Value:

- Strong productivity gain for UI work.
- Lets designers/editors stop hand-editing `.rml/.rcss`.
- Can reuse existing RmlUi backend.

Risk:

- Large file: Week11 `EditorRuntimeUIPreviewWidget.cpp` is about 145 KB.
- Needs project-file updates, generated reflection adaptation, asset service hooks, and tab/toolbar integration.

Suggested batch:

- Port `RuntimeUILayoutAsset` first.
- Add read/write/export without editor canvas manipulation.
- Then bring designer UI in slices: hierarchy, details, canvas move/resize, export/preview.

### Blueprint Prototype

Recommendation: inspect further before porting.

Week11 has:

- `Engine/Blueprint/BlueprintAsset`
- `BlueprintGraph`
- `BlueprintGraphExecutor`
- `BlueprintFunctionSignature`
- `EditorBlueprintWidget`
- `BlueprintComponent`

Value:

- Potentially huge if you want visual scripting or event graphs.

Risk:

- Likely depends on older reflection function metadata.
- Week12's Lua/reflection bridge and UFUNCTION generation changed.
- Could compete with current Lua workflow.

Suggested batch:

- First port only data model serialization into a sandbox branch.
- Then check executor compatibility with Week12 `UFunction`.
- Only after that wire editor UI and component lifecycle.

### Binary Serialization

Recommendation: small, useful, low-to-medium risk.

Week11 has:

- `WindowsBinReader`
- `WindowsBinWriter`

Value:

- Useful for compact assets or faster runtime loads.
- Small file set and isolated API.

Risk:

- Must verify Week12 `FArchive` interface compatibility.
- Needs explicit tests with strings, names, vectors, matrices, arrays.

This is a good warm-up port before larger editor systems.

### Animation Viewer / Socket Editing Undo

Recommendation: selectively port missing editor workflow pieces.

Week11 has stronger socket edit undo support via `FEditorSkeletalMeshSocketState`.

Week12 has newer animation graph assets and particle editor work, so old animation files are not all directly valuable.

Good candidates:

- Skeletal mesh socket undo
- Bone/socket transform edit transaction model
- Viewer-side undo capture/record patterns

Avoid:

- Bringing old animation runtime classes wholesale. Week12 has newer animation graph/runtime naming and asset layout.

### Architecture Cleanup From Current Week12

Current architecture check reports:

- `Engine/Runtime/Engine.cpp` includes `Editor/Selection/SelectionManager.h`
- `Source\Editor\Selection\SelectionManager.cpp` is not excluded from GameClient builds

Recommendation:

- Fix this before or during undo porting.
- Undo port will otherwise deepen the Engine -> Editor coupling.

## Recommended Batch Order

### Batch 0: Safety Baseline

- Run `Scripts\CheckArchitecture.ps1`.
- Generate a short manual editor smoke-test checklist.
- Build current editor/game client once.
- Decide whether to create a port branch.

### Batch 1: Architecture Cleanup

- Remove direct engine dependency on `FSelectionManager` where possible.
- Ensure all editor-only source is excluded from GameClient builds.
- This reduces later merge pain.

### Batch 2: Binary Serialization Warm-Up

- Port `WindowsBinReader/Writer`.
- Add compile-only verification and a tiny serialization smoke test if practical.

### Batch 3: Undo Core

- Introduce transaction command undo framework.
- Preserve current snapshot undo as fallback during transition.
- Port only actor transform and rename first.

### Batch 4: Editor Workflow Undo

- Actor/component create/delete.
- Component attachment.
- Material slot.
- Object property state.
- Update viewport, scene widget, property widget call sites.

### Batch 5: Detail Panel UX

- Port only visual/layout improvements that survive Week12 reflection.
- Convert property edits to transaction-aware begin/commit.
- Keep Week12 particle/reflection support intact.

### Batch 6: Gizmo Regression And Selective Math

- Test multi-selection translate/rotate/scale.
- If needed, port Week11 pivot/axis scaling math into Week12 `FActorTransformProxy`.
- Keep Week12 rotation-plane model unless a regression is confirmed.

### Batch 7: Runtime UI Designer MVP

- Port layout asset model.
- Add export to RML/RCSS.
- Integrate preview.
- Then add canvas editing.

### Batch 8: Blueprint Prototype

- Only after undo and UI are stable.
- Treat as separate feature branch.

## Initial Recommendation

Start with:

1. Architecture cleanup.
2. Transaction undo core.
3. Detail panel edit capture integration.
4. Gizmo regression fixes only if tests reveal a problem.

Runtime UI Designer is probably the best "big feature" after undo. Blueprint is tempting, but it is more likely to become a rabbit hole because it touches reflection, function invocation, editor graph UI, serialization, and runtime component lifecycle all at once.
