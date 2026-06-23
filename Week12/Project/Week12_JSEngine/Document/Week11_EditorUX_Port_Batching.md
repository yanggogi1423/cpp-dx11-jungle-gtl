# Week11 Editor UX Port Batching

## Goal

Port the Week11 editor UX pieces into the current Week12 branch without losing current Week12-only Lua/uasset work.

Requested scope:
- Bring Week11 Anim Sequencer / Animation Sequence editor features and design over as-is.
- Preserve and merge current Week12 Lua-related animation notify/script helper logic.
- Bring Week11-style settings separation, persistence, and UI coverage.
- Add title-bar menus per editor tab/window with actions mapped to each editor context.

Current branch note:
- Working branch at plan time: `feature/scene`.
- Existing dirty files before this batch must be treated as user work:
  - `JSEngine/Asset/Animation/Ahri_Skeleton_Skeleton_Skeleton_Ahri_skin14_run_homeguard.anm.uasset`
  - `JSEngine/Settings/Editor.ini`
  - `JSEngine/imgui.ini`
- Do not touch Particle System code in this batch unless the user explicitly reopens that area.

## Source Findings

Week11 has these relevant editor animation files:
- `Source/Editor/UI/EditorActorSequencerWidget.*`
- `Source/Editor/UI/EditorActorSequenceEditModel.*`
- `Source/Editor/UI/EditorActorSequenceDetails.*`
- `Source/Editor/UI/EditorActorSequenceTimeUtils.h`
- `Source/Editor/UI/EditorAnimationSequenceViewerWidget.*`
- `Source/Editor/UI/AnimSequenceViewerContextBuilder.*`

Current Week12 has:
- `EditorActorSequencerWidget.*`, but cpp differs from Week11 substantially.
- `EditorActorSequenceEditModel.*`, `EditorActorSequenceDetails.*`, `EditorActorSequenceTimeUtils.h`.
- No standalone `EditorAnimationSequenceViewerWidget.*`.
- Current Lua Anim Notify helper logic lives in `EditorViewerWindowWidget.cpp`; it should be preserved and exposed through the Week11-style animation sequence editor UX.

Settings comparison:
- `EditorSettings.h` is almost identical, except Week11 has `LightCullMode`.
- `ProjectSettings.*` differs slightly and needs semantic merge, not blind replacement.
- Current `Editor.ini` is dirty, so code should support new settings without overwriting user-local config.

Tab/window menu comparison:
- Week11 already has richer app/window menu and detached document chrome patterns.
- Current Week12 already has detached/dockable window machinery, so the port should extend that instead of replacing it.

## Batch Plan

### Batch 0 - Baseline Guard

Status: Completed

Tasks:
- Record dirty files and avoid overwriting user-local config/assets.
- Use source-level diffs only; do not copy `Editor.ini` or `imgui.ini` from Week11.
- Keep current Week12 Lua/uasset policy as canonical.

Validation:
- `git status --short` before and after each batch.
- No unrelated asset/config churn.

### Batch 1 - Animation Sequence Viewer Foundation

Status: In Progress

Tasks:
- Add the Week11-compatible animation data bridge first:
  - `FAnimNotifyTrack` / notify id support.
  - AnimSequence curve track payload support.
  - PayloadVersion 4 save path for `.anm.uasset`.
  - v3 flat notify assets are promoted to a default `Notifies` track when loaded.
- Add Week11 `EditorAnimationSequenceViewerWidget.*`.
- Add Week11 `AnimSequenceViewerContextBuilder.*`.
- Wire into current `EditorMainPanel`, `EditorMainPanelWidgetSet`, `EditorMainPanelWidgetSetup`, content browser open path, and project files.
- Merge current Week12 Lua Notify helper behavior from `EditorViewerWindowWidget.cpp` into the Week11-style animation sequence editor, instead of losing it.
- Keep `.uasset` animation sequence loading as the only asset policy.

Validation:
- `Debug|x64` builds after the animation data bridge.
- AnimSequence `.uasset` double-click opens the new editor.
- Preview mesh/sequence loads.
- Notify creation/editing works.
- Lua handler add/select flow still works.

### Batch 2 - Actor Sequencer Week11 UX Parity

Status: In Progress

Tasks:
- Compare current `EditorActorSequencerWidget.cpp` against Week11.
- Bring Week11 visual layout and interaction behavior over:
  - Toolbar styling.
  - Timeline/ruler/key drawing.
  - Add track/property popup behavior.
  - Context menus.
  - Dragging playback range, sections, and keys.
- Preserve current Week12 edit model, undo integration, uasset serialization assumptions, and any Lua-specific additions if found.

Validation:
- Existing ActorSequence component opens.
- Add track/property/key works.
- Drag key/section/playback range works.
- Undo/redo still records sequence edits.

### Batch 3 - Settings Separation and Persistence

Status: In Progress

Tasks:
- Port Week11 settings separation semantics:
  - Editor settings remain editor-local.
  - Project settings own project/game/build startup state.
  - Render/build settings are not mixed into arbitrary UI state.
- Merge `LightCullMode` and any missing Week11 fields into current `FEditorSettings`.
- Merge `ProjectSettings` behavior carefully with current packaging/build settings.
- Ensure UI panels save/load through their owning settings class.
- Do not replace local `Settings/Editor.ini`; add backwards-compatible load defaults in code.

Validation:
- Editor launch loads settings without resetting current local config.
- Editing editor settings persists to `Editor.ini`.
- Editing project/build settings persists to project settings.
- Packaging reads project settings consistently.

### Batch 4 - Per-Tab Title Bar Menus

Status: In Progress

Tasks:
- Define a common title/menu rendering helper for docked and detached document windows.
- Add context menus per tab/window:
  - Level viewport: view mode, light cull mode, camera speed, show flags, layout.
  - Content Browser: new asset/script/Lua Anim Graph, refresh, import/open folder where available.
  - Console: clear, copy, filters if supported.
  - Animation Sequence Viewer: save/reload, notify tools, preview controls.
  - Actor Sequencer: add track/key, playback controls, snap/view range.
  - C++ AnimGraph / Lua AnimGraph: save, compile/generate Lua, preview controls.
  - Material/UI/Particle editors: save/reload/open asset-specific actions without touching particle internals.
- Detached windows must expose the same actions as docked tabs.

Validation:
- Docked and detached windows show the same relevant menu actions.
- Menu actions operate on the active document/window only.
- Maximize/minimize/dock behavior remains unchanged.

### Batch 5 - Verification and Cleanup

Status: Pending

Tasks:
- Build `Debug|x64`.
- Run text checks for conflict markers and accidental legacy animation asset references.
- Smoke-test opening:
  - Animation Sequence editor.
  - Actor Sequencer.
  - Lua AnimGraph.
  - Content Browser.
  - Settings panels.
  - Detached/docked windows.
- Remove temporary migration/scaffold artifacts if any were introduced.

Validation commands:
- `rg -n "^(<<<<<<<|=======|>>>>>>>)" JSEngine/Source JSEngine/Settings`
- `rg -n "\.(animseq|animgraph|matinst|particlesystem)" JSEngine/Source JSEngine/Settings JSEngine/Asset/Scene`
- `MSBuild JSEngine.sln /p:Configuration=Debug /p:Platform=x64 /m`

## Progress Log

- 2026-05-27: Initial scope captured. Week11 source locations identified. Current dirty user files noted.
- 2026-05-27: Batch 0 completed. Confirmed current Week12 has animation sequence viewer logic folded into `EditorViewerWindowWidget`, while Week11 has a standalone `EditorAnimationSequenceViewerWidget` plus `AnimSequenceViewerContextBuilder`. Batch 1 will port the standalone Week11 UX and merge current Lua notify helper behavior into it.
- 2026-05-27: Batch 1 foundation started. Added Week11-style Notify Track / Curve Track data bridge to current AnimSequence `.uasset` serialization without removing the current flat Notify API or Lua notify fields. `Debug|x64` build passed with 0 errors and 0 warnings.
- 2026-05-27: Batch 1 UI bridge added. Current AnimSequence timeline now renders notify rows by track, right-click add targets the clicked track, and `Add Notify Track` is available from the timeline context menu. Lua notify detail editing remains on the existing Week12 path. `Debug|x64` build passed with 0 errors and 0 warnings.
- 2026-05-27: Batch 3 low-risk settings split started. Added an `Editor Settings` window alongside existing Project/World settings, routed it through the main Window menu, and kept persistence on the existing `FEditorSettings::SaveToFile` path instead of replacing local `Editor.ini`. `Debug|x64` build passed with 0 errors and 0 warnings.
- 2026-05-27: Batch 4 first title menu fix added. AnimSequence viewer title/menu actions now expose `Save Animation` instead of the mesh-only save wording and call the AnimSequence `.uasset` save path. `Debug|x64` build passed with 0 errors and 0 warnings.
- 2026-05-27: Batch 2 visual parity pass started. Current Actor Sequencer already has Week11-level key/context/drag/pan interactions; restored the Week11 icon toolbar buttons for add-key/play/pause/stop. `Debug|x64` build passed with 0 errors and 0 warnings.
- 2026-05-27: Batch 3 settings parity tightened. Added `LightCullMode` to `FEditorSettings`, persisted Light Culling / Shadow Filter / Decal / Fog show flags, initialized viewport states from saved editor settings, and made viewport menus update the saved settings state. `Debug|x64` build passed with 0 errors and 0 warnings.
- 2026-05-27: Batch 4 detached/docked menu parity improved. Active document menus now expose Settings shortcuts, and detached animation sequence viewer menus use `Save Animation` with the animation `.uasset` save path instead of mesh-only actions.
- 2026-05-27: Batch 4 detached utility menu pass. Added Settings shortcuts to detached Content Browser and Console windows, and added save/settings menus to detached C++/Lua AnimGraph windows. Particle editor internals were intentionally left untouched for the parallel owner. `Debug|x64` build passed with 0 errors and 0 warnings.
- 2026-05-27: Particle editor was reopened for this batch. Shared Particle View/Particle title menus between docked and detached states, added explicit detached `Dock Back`, Settings shortcuts, and wired Restart Simulation / LOD menu actions to the existing Particle editor functions. First rebuild hit `LNK1168` because a running `JSEngine.exe` held the output; after stopping the process, `Debug|x64` passed with 0 errors and 0 warnings.
