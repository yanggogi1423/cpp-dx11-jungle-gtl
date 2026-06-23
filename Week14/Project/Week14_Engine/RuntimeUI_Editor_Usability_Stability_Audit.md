# Runtime UI Editor Usability / Stability Audit

Purpose: keep the Runtime UI editor investigation and next patch order durable across Codex context compaction. This file is a working note for future agents and should be updated as patches land.

Date: 2026-06-06

## Current Scope

Runtime UI work currently spans three related surfaces:

- Runtime UI Layout UAsset editor: `KraftonEngine/Source/Editor/UI/Asset/RuntimeUI/RuntimeUILayoutEditorWidget.cpp`
- RML / RCSS source viewer-editor and preview: `KraftonEngine/Source/Editor/UI/EditorMainPanel.cpp`
- Runtime UI layout asset export/import/serialization: `KraftonEngine/Source/Engine/UI/RuntimeUILayoutAsset.cpp`, `.h`

The user is actively tuning `Content/UI/InGameHUD.uasset`, especially:

- `Image/Hor-Compass/Compass.png`
- `Image/Scope/Scope_3.png`

Important standing constraint from user: do not run builds unless explicitly re-authorized.

## Recent Implemented State

Runtime UI Layout UAsset now has these editor improvements:

- Preview draws image nodes using `FEditorTextureManager`.
- Image path resolution tries multiple project-root/CWD candidates.
- `Image Fit` preview supports `Stretch`, `Contain`, `Cover`.
- Missing images display a red X and `Missing image`.
- Canvas `0,0` and selected widget parent origin are drawn.
- Dragging snaps to parent horizontal/vertical center and shows guide lines.
- Details panel shows `Resolved Pos / Size`.
- Details has `Lock Aspect Ratio`, `Center H`, `Center V`, `Bottom Center`, `Fill Parent`.
- Manual `Position` edits clear percent/right/bottom anchor flags.
- Manual `Width` / `Height` edits clear width/height percent flags.
- Arrow-key nudging also clears position anchor flags.

Asset format note:

- `URuntimeUILayoutAsset::CurrentPayloadVersion` was raised to `3`.
- `FRuntimeUIWidgetNode::bLockAspectRatio` is serialized only for payload version `>= 3`.
- Existing v2 UAssets should still load; saving writes v3.

Verification so far:

- `git diff --check` passed for the touched Runtime UI files.
- No build was run.

## Batch Log

### Batch 1 - RML image preview parity and image diagnostics

Status: implemented in source, build not run.

Changes:

- RML Preview style model now parses and merges `object-fit`.
- RML Preview image drawing now applies `stretch/fill`, `contain`, and `cover`.
- RML Preview now shows a red X and `Missing image` when an `<img>` source is empty or fails to load.
- Runtime UI Layout Details now shows image diagnostics for image widgets:
  - resolved image path
  - missing / loaded / texture-load-failed state
  - loaded texture dimensions
- Runtime UI Layout Details added quick image path buttons:
  - `Use Scope_3`
  - `Use Compass`

Verification:

- `git diff --check` passed for this batch.
- Build intentionally skipped per user constraint.

### Batch 2 - UAsset editor geometry helper

Status: implemented in source, build not run.

Changes:

- Added shared editor-side helpers:
  - `ResolveRuntimeUILayoutNodeSize`
  - `ResolveRuntimeUILayoutNodePosition`
- UAsset Preview now uses the shared helpers through local wrappers.
- Details `Resolved Pos / Size` and alignment buttons now use the same helpers.
- Helpers include a depth guard so malformed parent chains are less likely to recurse forever.

Verification:

- `git diff --check` passed for this batch.
- Build intentionally skipped per user constraint.

### Batch 3 - Details numeric edit commit policy

Status: implemented in source, build not run.

Changes:

- `Position` edits now update the preview live but commit to Undo history only when the edit field deactivates.
- `Width` / `Height` edits now update the preview live but commit to Undo history only when the edit field deactivates.
- Manual position edits still clear left/top percent and right/bottom anchor flags.
- Manual size edits still clear width/height percent flags.
- Immediate actions such as buttons, checkboxes, combo changes, and other existing fields keep the existing commit behavior.
- Removed a duplicate origin / parent-origin draw path so the preview overlays are emitted once, after widgets.

Verification:

- `git diff --check` passed for this batch.
- Build intentionally skipped per user constraint.

### Batch 4 - RML import structural confirmation

Status: implemented in source, build not run.

Changes:

- `Import RML` now performs a dry-run summary when generated RML would create, remove, or reparent widgets.
- Structural imports require a second click via `Confirm Import` before applying.
- The pending confirmation is tied to the generated RML/RCSS file paths and content hash/size, so changed files require a fresh preview.
- Export, save/export, opening a new layout, and local layout edits clear pending import confirmation.
- Non-structural imports still apply immediately.

Verification:

- `git diff --check` passed for this batch.
- Build intentionally skipped per user constraint.

## Main Findings

### 1. Layout Calculation Is Duplicated

Evidence:

- UAsset Preview has local `GetNodeSize` / `GetGlobalPosition`.
- Details panel has separate `ResolveNodeSize` / `ResolveNodePosition`.
- RML Preview has another independent layout approximation inside `RenderRuntimeUIPreviewBoxPreview`.

Risk:

- A widget may look different in Details resolved values, UAsset preview, RML preview, exported RML, and runtime.
- Future fixes can accidentally patch only one surface.

Recommended patch:

- Extract a shared editor-side layout helper for UAsset widget nodes first.
- Suggested name: `RuntimeUILayoutGeometry` or `FRuntimeUILayoutResolvedGeometry`.
- Use it in:
  - UAsset preview rendering
  - hit testing
  - drag snapping
  - Details `Resolved Pos / Size`
  - alignment buttons

Keep RML preview separate for now unless scope is expanded; it parses textual RML/RCSS, not UAsset nodes.

### 2. RML Preview Image Fit Parity

Evidence:

- Export writes `object-fit` in `RuntimeUILayoutAsset.cpp`.
- Batch 1 added RML preview parsing for `object-fit`.
- Batch 1 changed RML preview image draw to fit `stretch/fill`, `contain`, and `cover`.

Risk:

- Remaining risk is runtime parity: the editor preview and actual RmlUi runtime still need visual smoke testing after a user-authorized build/relaunch.

Recommended patch:

- Batch 1 completed the editor-side RML Preview work.
- Next useful check is visual smoke testing in the running editor after rebuild/relaunch.

### 3. Commit / Undo Is Too Eager

Evidence:

- `RenderDetails` calls `CommitLayoutEdit(Layout)` when `bChanged` is true.
- ImGui numeric/text edits can fire continuously while editing.
- Drag already commits on mouse release, which is a better shape.

Risk:

- Undo stack can become noisy.
- Ctrl+Z may step through tiny intermediate numeric states instead of meaningful edits.

Recommended patch:

- For text and numeric fields, commit on:
  - `ImGui::IsItemDeactivatedAfterEdit()`
  - Enter commit where appropriate
  - button presses immediately
- Keep `MarkLayoutDirty()` for live preview updates while dragging/editing.
- Keep drag commit on mouse release.

### 4. RML Import Needs Better Safety Feedback

Evidence:

- `ImportGeneratedRmlAndRcss` reconciles by id.
- It can create, delete, and reparent widgets when generated RML has clean ids.
- Duplicate ids skip structural import.
- Idless elements are counted/policy-reported but are not fully represented as editable UAsset nodes.

Risk:

- User can import RML edits and unexpectedly create/delete/reparent widgets.
- Unsupported or idless markup can silently remain outside the UAsset model.

Recommended patch:

- Add a dry-run summary before destructive/structural import:
  - changed count
  - created widgets
  - removed widgets
  - reparented widgets
  - duplicate ids
  - idless meaningful elements
- In UI, show warning and require a second click for structural changes.
- Keep non-structural style-only imports single-click if no nodes will be created/removed/reparented.

### 5. Image Debugging UX Is Still Thin

Evidence:

- UAsset preview now resolves paths robustly, but Details only shows raw `Image Path`.
- `FEditorTextureManager` logs failures, but the UI does not show the resolved candidate path or image dimensions.

Risk:

- If an image fails, user still has to infer whether the raw path, cwd, texture loader, or alpha data is the problem.

Recommended patch:

- In Details for image nodes, show:
  - resolved path
  - exists / missing
  - texture dimensions if loaded
  - current `Image Fit`
- Add small buttons:
  - `Use Scope_3`
  - `Use Compass`
  - later: file/content picker if available

## Suggested Patch Order

1. RML Preview `object-fit` + missing-image handling. Done in Batch 1.

2. UAsset geometry helper extraction. Done in Batch 2.

3. Details image diagnostics.
   - Basic image diagnostics done in Batch 1.

4. Undo/commit policy cleanup.
   - Position / Width / Height numeric inputs done in Batch 3.
   - Remaining optional refinement: apply the same delayed-commit policy to text fields and advanced numeric fields if Undo noise is still annoying.

5. RML import dry-run summary.
   - Basic structural dry-run / confirm flow done in Batch 4.
   - Remaining optional refinement: render a dedicated multi-line import preview panel instead of putting the whole summary in `LastStatus`.

## Known Useful Commands

Read-only inspection:

```powershell
rg -n "RuntimeUILayout|RuntimeUIPreview|object-fit|ImageFit|RenderCanvasPreview|ImportGeneratedRmlAndRcss" KraftonEngine/Source/Editor KraftonEngine/Source/Engine/UI
```

Whitespace verification only:

```powershell
git diff --check -- KraftonEngine/Source/Editor/UI/Asset/RuntimeUI/RuntimeUILayoutEditorWidget.cpp KraftonEngine/Source/Editor/UI/EditorMainPanel.cpp KraftonEngine/Source/Engine/UI/RuntimeUILayoutAsset.cpp KraftonEngine/Source/Engine/UI/RuntimeUILayoutAsset.h
```

Do not run a build unless the user explicitly allows it.

## Acceptance Notes For Future Work

When a patch lands, update this file with:

- what changed
- whether `git diff --check` passed
- whether a build was intentionally skipped
- any new mismatch between UAsset Editor, RML Editor, and runtime behavior

The goal is not to make this document pretty; it is here so the next agent can continue without re-discovering the same Runtime UI editor risks.
