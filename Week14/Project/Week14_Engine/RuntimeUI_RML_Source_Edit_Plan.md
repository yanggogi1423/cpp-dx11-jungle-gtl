# Runtime UI RML/RCSS Source Edit Plan

## Goal

Allow the engine editor to edit raw RML and linked RCSS directly from the Runtime UI Preview tab, while keeping Runtime UI Layout assets as the preferred structured authoring path.

## Current State

- Runtime UI Preview can reload and mount a `.rml` document into the level viewport.
- The Preview tab can draw a lightweight approximation of the RML, including linked RCSS and images.
- The Source tab is read-only.
- Runtime UI Layout assets can export RML/RCSS and can import supported generated edits back into the asset.

## Ownership Model

- `URuntimeUILayoutAsset` remains the structured source of truth for layout-editor authored UI.
- Raw `.rml` and `.rcss` editing is an advanced source-edit path for AI-assisted patches and quick manual tuning.
- If generated source and layout asset disagree, users should explicitly choose import-from-source or regenerate-from-layout in the Runtime UI Layout editor.

## Patch Scope

### Phase 1: Runtime UI Preview Source Editor

- Add editable RML source buffer.
- Detect and load the first linked RCSS file from the RML document.
- Add editable RCSS source buffer.
- Add Save, Reload, and Save + Remount controls.
- Track dirty state for RML and RCSS buffers.
- Save to disk, refresh Preview data, and force viewport remount when requested.

### Phase 2: Layout Asset Sync Improvements

- Extend `FRuntimeUIWidgetNode` only for properties needed by current HUD work.
- Prefer support for percent sizing/positioning, right/bottom anchors, and simple flex alignment.
- Improve import/export status messaging.

Status:

- Added Runtime UI payload version 2.
- Added percent position/size fields, right/bottom anchors, and simple flex container fields.
- Export writes percent/anchor/flex fields to RCSS.
- Import reads percent/anchor/flex fields from generated RCSS.
- Layout Details exposes the new fields under Advanced Layout.
- Layout preview approximates percent and right/bottom anchors.

## Verification

- Do not run a full build unless explicitly allowed.
- Run `git diff --check` on touched files.
- Inspect source-level changes for C++ compile risks.
