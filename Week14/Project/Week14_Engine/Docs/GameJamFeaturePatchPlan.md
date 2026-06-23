# Game Jam Feature Patch Plan - Week14

## Purpose

Use Week12_JSEngine as a reference, but do not bulk-port it. Week14 has different
runtime boundaries, especially around `UGameViewportClient`, RmlUi widgets, FMOD,
reflection serialization, and project/world settings. The goal is to add small,
game-jam-friendly features on top of the current Week14 architecture and keep the
build verifiable after each batch.

## Implementation Status

- Batch 1 applied: Lua `Input` mode/cursor/capture API is bound through
  `UGameViewportClient`.
- Batch 2 applied: JSON `.prefab` save/spawn core exists via `FPrefabManager`,
  reusing `SceneSaveManager` actor/component serialization helpers.
- Batch 3 applied: Project/World default pawn prefab paths are serialized,
  exposed in settings UI, and consumed by `AGameModeBase` after existing pawn
  auto-possess fallback.
- Batch 4 partially applied: runtime UI now has Lua widget capture setters,
  element text/visible/enabled helpers, `data-action` event collection, and
  `UI.PollActionEvents()`. The important keyboard/text path is now covered by
  Batch 11; only advanced IME composition UI remains.
- Batch 5 applied: `UActorComponent` now has duplicate-safe `FName` component
  tags, scene/prefab round-trip support, Lua component tag API, and actor-local
  single-tag plus all-tags component lookup.
- Batch 6 applied: `FAudioManager::PlaySFX(pathOrKey, volumeScale)` and Lua
  `Audio.PlaySFX(pathOrKey, volumeScale)` exist.
- Batch 7 applied to a practical full-copy level: Project Settings now stores
  build/package validation options, the editor exposes a Packaging section,
  startup scene and default pawn prefab validation exist, and the build scripts
  route through `Scripts/PackageGame.ps1` for full-copy packaging, dry-run diff,
  `PackageManifest.json` generation, post-copy package smoke verification,
  package size reporting, and an optional launch smoke test. A cook/prune
  optimizer is still not implemented.
- Batch 8 applied: Lua now exposes `Json`, `Save`, and `Random` utility APIs,
  plus `Engine.Json`, `Engine.Save`, and `Engine.Random` aliases. Save paths are
  restricted to the project `Saves/` directory.
- Batch 9 applied: Lua now exposes read-only `Asset` query APIs, plus
  `Engine.Asset`, backed by Week14 `FAssetRegistry`.
- Batch 10 applied: runtime UI now has value, class, attribute, style, focus,
  blur, and synthetic click helpers on both `UUserWidget` and global `UI`.
- Batch 11 applied: Win32 completed text input is queued from `WM_CHAR` /
  `WM_UNICHAR`, Lua exposes `Input.ConsumeTextInput()`, and RmlUi receives
  keyboard plus text input through `ProcessKeyDown`, `ProcessKeyUp`, and
  `ProcessTextInput`.
- Input policy hardening applied: game scripts and `PlayerController` now read
  the policy-filtered `UGameViewportClient` `GameInputSnapshot`, invalid numeric
  virtual-key reads fail closed, invalid numeric `UInputComponent` mappings are
  ignored with a log, and focused RmlUi form controls automatically own text
  input plus block gameplay keyboard input. PIE ejected mode now stops the F8
  transition frame at the editor router and routes `PlayerController` input only
  while PIE is possessed.
- Scene API applied: Lua now exposes `Scene.Open`, `Scene.Load`,
  `Scene.TransitionTo`, `Scene.Reload`, `Scene.IsOpenPending`,
  `Scene.GetCurrentPath`, and `Scene.GetPendingPath`, plus `Engine.Scene`.
  Runtime transitions are queued through `UGameEngine` and missing files no
  longer destroy the active world first.
- Audio handle/group/3D API applied: `FAudioManager` now tracks SFX handles,
  separates BGM/SFX channel groups, exposes listener state and 3D SFX playback,
  and lets Lua stop/query/update handle-based sounds.
- Application/Debug Lua convenience applied: `Application.QuitGame`,
  `Application.Exit`, viewport/world-type queries, and `Debug.Log` /
  `Debug.Warn` / `Debug.Error` / `Debug.Assert` are available, plus
  `Engine.Application` and `Engine.Debug`.
- Actor Sequence runtime core applied: `PF_Animatable` reflection metadata,
  safe scalar channel read/write helpers, curve playback evaluation,
  `UActorSequence`, `UActorSequencePlayer`, and `UActorSequenceComponent`
  exist.
- Actor Sequence Batch 16 partially applied: actor-local component persistent
  GUIDs are saved on `UActorComponent`, Actor Sequence component bindings now
  resolve by persistent GUID first and component name second, `SequenceDataJson`
  refreshes binding caches before export, duplicate/player-owner lifecycle was
  tightened, and Lua exposes `ActorSequence`, `ActorSequencePlayer`, and
  `ActorSequenceComponent`. Actor JSON/prefab round-trip diagnostics now exist;
  dedicated editor UX, actual Lua-session self-test execution, and full scene
  save/load hostile QA remain next-batch work.
- Batch 18 implemented and Debug-built: Details now has a `Save Prefab...` action beside component add
  and remove, selected actors can be saved through a folder picker that writes
  a collision-safe actor-name `.prefab`,
  Week13 camera editor visualization mesh assets were copied, `UCameraComponent`
  now creates an editor-only camera mesh child in editor worlds, `USpringArmComponent`
  regained a no-lag immediate refresh hook for editor camera updates, and
  `ActorSequenceComponent` play-rate/start-offset editor ranges were fixed.
- Batch 19 foundation implemented and Debug-built: `FEditorUndoSystem` now owns
  scene snapshot undo/redo stacks, `UEditorEngine` exposes scene capture/restore,
  Ctrl+Z/Ctrl+Y/Ctrl+Shift+Z are routed for the level document, and snapshot
  capture is wired to level actor create/delete/duplicate, gizmo transform start,
  Details property edits, component add/remove/reparent, and actor/component
  rename. Still needs polished transaction commands/history UI and explicit
  ActorSequence edit-model integration in the sequencer batches.
- Batch 20A implemented and Debug-built: `UActorSequenceComponent` Details now
  has an inline Actor Sequence authoring panel. It can preview play/pause/stop,
  scrub preview time, edit duration/section timing, add `PF_Animatable` scalar
  tracks on the owner actor or actor-local components, add keys from the current
  preview-time value, edit/delete inline curve keys, remove tracks, clear the
  sequence, and commit edits back into `SequenceDataJson` for scene/prefab
  serialization. Remaining work is the dedicated document/tab timeline,
  embedded curve canvas polish, and hostile editor QA.
- Batch 21A implemented and Debug-built: Actor Sequence can now open from
  Details into a dedicated document tab (`FActorSequenceEditorWidget`) through
  the existing Week14 asset-editor/document-tab system. The tab provides preview
  play/pause/stop, scrub time, duration/view range controls, target-grouped
  track selection, a simple timeline canvas with section bars, key dots and
  playhead, plus selected-track key add/edit/delete and track removal. Later
  Batch 21C-21G passes added in-tab track creation, key/section drag, context
  menus, and Week12-style playback range handles; Batch 22A added first-pass
  embedded curve canvas/tangent editing. Remaining work is hostile target
  invalidation/persistence QA and source-aware curve-editor polish.
- Batch 21B implemented and Debug-built: Actor Sequence channel apply mode
  (`Absolute`/`Additive`) and time mapping (`Seconds`/`Normalized`) controls are
  now available in both the Details inline authoring panel and the dedicated
  sequencer document tab. Edits capture undo snapshots and commit back into
  `SequenceDataJson`.
- Batch 21C implemented and Debug-built: the dedicated Actor Sequencer tab now
  has an in-tab `Add Track` popup for owner/component `PF_Animatable` scalar
  channels, creates tracks through `UActorSequenceComponent::AddFloatTrack`,
  selects the newly added channel, commits into `SequenceDataJson`, and supports
  direct click/drag timeline scrubbing.
- Batch 21D implemented and Debug-built: the dedicated Actor Sequencer timeline
  now supports key hit-testing, selected-key visual feedback, key-table
  selection sync, and direct key time dragging with undo snapshot capture and
  `SequenceDataJson` commit.
- Batch 21E implemented and Debug-built: the dedicated Actor Sequencer timeline
  now supports section body selection, selected-section outlines, visible start
  and end handles, direct section start/end resizing, scrub suppression while
  selecting timeline items, undo snapshot capture, and `SequenceDataJson`
  commit.
- Batch 21F implemented and Debug-built: the dedicated Actor Sequencer timeline
  now opens a right-click context menu from the hit-tested key or section. The
  menu can add a key at the current preview time, delete the selected key,
  delete the selected track/channel, and fit the view to the selected section,
  while preserving undo snapshots and `SequenceDataJson` commits.
- Batch 21G implemented and Debug-built: Actor Sequence now has a Week12-style
  playback range model (`StartTime + Duration`) serialized in
  `SequenceDataJson`, Lua-exposed range APIs, runtime/preview player clamping
  and looping against the playback range, Details-panel playback-start editing,
  and dedicated timeline start/end range handles plus fit-to-playback-range.
- Batch 22A implemented and Debug-built: the dedicated Actor Sequencer tab now
  embeds a curve canvas for the selected channel. It can fit the view, add keys
  by double-click/context menu, select/delete keys, drag keys, drag cubic
  arrive/leave tangent handles, edit interpolation/tangent mode/time/value in a
  selected-key detail panel, and commit edits back into `SequenceDataJson` with
  undo snapshot capture. Remaining work is target-invalidation polish,
  source-aware curve dirty/save callbacks, and hostile scene/prefab round-trip
  QA.
- Batch 22B fast safety patch applied, build deferred by request: Actor Sequence
  component `EndPlay` now calls the base component lifecycle after stopping both
  runtime and preview players, editor preview play/scrub now stops/restores the
  runtime player first so both players do not write the same animated property
  channels, and `FActorSequenceDiagnostics::RunRoundTripSelfTest()` now covers
  full scene `SaveToString`/`LoadFromString` round-trip in addition to actor
  JSON and prefab round-trips. Remaining work is source-aware curve
  begin/commit undo polish and manual hostile editor QA.
- Batch 24A fast UI Lua convenience patch applied and Debug-built:
  Week12-style game-jam shortcuts now exist on both `UUserWidget` instances and
  the global `UI` table: `SetImage`, `SetProgress`, `SetZOrder`, `SetTint`,
  `SetTextColor`, `SetBackgroundColor`, `SetAlpha`, `SetRounding`,
  `SetFontScale`, `SetElementTransform`/`SetTransform`, and `RemoveElement`.
  These are thin wrappers over the existing Week14 value/attribute/style APIs,
  so the core UI architecture stays unchanged.
- Batch 24B fast UI animation convenience patch applied and Debug-built:
  RmlUi-supported transition helpers now exist on both `UUserWidget`
  and global `UI`: `SetTransition`, `SetTransitionAll`, `ClearTransition`,
  `AnimateAlpha`, `AnimateTextColor`, `AnimateBackgroundColor`,
  `AnimateTransform`, and `AnimateClass`. This covers the practical Week12-style
  class/style animation workflow without porting the full Runtime UI visual
  editor yet.
- Batch 24C fast UI/game input consume patch applied and Debug-built:
  `UGameViewportClient` now pumps RmlUi input before building the
  policy-filtered game snapshot, stores same-frame mouse/keyboard/text consume
  feedback in `FUIInputCaptureState`, clears consumed events from game input,
  and skips the later render-pass input pump to avoid duplicate UI callbacks.
- Batch 25A Lua definitions/examples fast polish applied:
  `Docs/Lua/Week14EngineAPI.def.lua` now covers the new UI shortcuts,
  transition/action helpers, `CameraManager`, and the current
  ActorSequence/Input/UI/Scene/Audio basics. Smoke examples were added under
  `KraftonEngine/Content/Script/Examples` plus
  `KraftonEngine/Content/UI/Examples/GameJamInputSmoke.rml` for UI/Input,
  Actor Sequence, and Camera scope/shake checks.

## Verified

- `Debug|x64` MSBuild succeeded after the package launch-smoke/size-report
  patch.
- `Debug|x64` MSBuild succeeded after Batch 18 camera mesh, SpringArm refresh,
  and ActorSequence Details metadata fixes. Remaining warnings are PhysX static
  library PDB `LNK4099` warnings.
- `Debug|x64` MSBuild succeeded after the `Save Prefab...` folder-picker UX
  polish. The incremental build completed successfully.
- `Debug|x64` MSBuild succeeded after Batch 19 snapshot undo foundation and
  editor operation wiring. Remaining warnings are PhysX static library PDB
  `LNK4099` warnings.
- `Debug|x64` MSBuild succeeded after Batch 20A ActorSequence Details inline
  authoring. Remaining warnings are the existing PhysX/Vehicle static library
  PDB `LNK4099` warnings.
- `Debug|x64` MSBuild succeeded after Batch 21A ActorSequence document-tab
  sequencer entry point. Remaining warnings are the existing PhysX/Vehicle
  static library PDB `LNK4099` warnings.
- `Debug|x64` MSBuild succeeded after Batch 21B apply/time-mapping UI. The
  incremental build reported 0 warnings and 0 errors.
- `Debug|x64` MSBuild succeeded after Batch 21C sequencer in-tab add-track
  popup and timeline click/drag scrubbing. Remaining warnings are the existing
  PhysX static library PDB `LNK4099` warnings.
- `Debug|x64` MSBuild succeeded after Batch 21D key selection/dragging and the
  current ScopeLens camera patch compile fix. Remaining warnings are the
  existing PhysX/Vehicle static library PDB `LNK4099` warnings.
- `Debug|x64` MSBuild succeeded after Batch 21E section resize handles and
  timeline scrub suppression. Remaining warnings are the existing PhysXVehicle
  static library PDB `LNK4099` warnings.
- `Debug|x64` MSBuild succeeded after Batch 21F timeline context menu. The
  incremental build reported 0 warnings and 0 errors.
- `Debug|x64` MSBuild succeeded after Batch 21G playback range model/handles.
  Remaining warnings are the existing PhysX/Vehicle static library PDB
  `LNK4099` warnings.
- `Debug|x64` MSBuild succeeded after Batch 22A embedded Actor Sequencer curve
  canvas/key/tangent editing. Remaining warnings are the existing PhysXVehicle
  static library PDB `LNK4099` warnings.
- Batch 22B compile/build verification is intentionally deferred for speed.
  Fast checks to run next: `git diff --check`,
  `Debug.RunActorSequenceRoundTripSelfTest()` in an editor/game Lua session,
  then `Debug|x64` MSBuild.
- `Debug|x64` MSBuild succeeded after Batch 24A/24B/24C UI wrappers,
  transition helpers, and same-frame UI/game input consume routing. Remaining
  warnings are the existing PhysX/Vehicle static library PDB `LNK4099`
  warnings; UI/transition/PIE input smoke examples are still pending.
- `Game|x64` MSBuild succeeded after Batch 25A Lua definition/example polish.
  Remaining warnings are the existing PhysX/Vehicle static library PDB
  `LNK4099` warnings.
- `Scripts/PackageGame.ps1 -RootDir . -Configuration Game -DryRun` succeeded
  after Batch 25A. It planned 488 added package files and wrote no files.
- `Game|x64` MSBuild succeeded after the package launch-smoke/size-report
  patch.
- `Scripts\python\python.exe Scripts\GenerateHeaders.py --self-test` succeeded
  after Actor Sequence reflection support.
- `Scripts\python\python.exe Scripts\GenerateHeaders.py --root KraftonEngine
  --dry-run` succeeded and detected the new Actor Sequence generated headers.
- `Debug|x64` and `Game|x64` MSBuild succeeded after the Actor Sequence runtime
  patch. Game still reports the existing PhysX/Vehicle static-library PDB
  `LNK4099` warnings.
- `Debug|x64` and `Game|x64` MSBuild succeeded after Actor Sequence persistent
  binding and Lua API wiring. Existing PhysX/Vehicle `LNK4099` PDB warnings
  remain.
- `Debug|x64` and `Game|x64` MSBuild succeeded after adding Actor Sequence
  round-trip diagnostics and the Lua debug entry point. Existing PhysX/Vehicle
  `LNK4099` PDB warnings remain.
- `Debug|x64` and `Game|x64` MSBuild succeeded after the input policy hardening
  patch. Game still reports the existing PhysX/Vehicle `LNK4099` PDB warnings.
- `Scripts/PackageGame.ps1 -Configuration Game -DryRun` succeeded.
- A real package smoke test to a temp directory succeeded and verified the
  manifest, file hashes, file sizes, required directories, and
  `Bin/KraftonEngine.exe`.
- Packaging now reports total size, top-level size breakdown, and largest files
  after a real package write.
- `Scripts/PackageGame.ps1 -LaunchSmokeTest` is available for optional packaged
  executable startup checks.
- A temp package with `-LaunchSmokeTest -LaunchSmokeTimeoutSeconds 1` succeeded;
  the packaged executable survived the timeout and was then stopped.
- The current full-copy Game package reports about `217.91 MB`, with `Content`
  at about `158.97 MB` and `Bin` at about `58.76 MB`.
- Existing warnings remain: PhysX/NvCloth PDB/link warnings. No new compile
  errors were observed.
- On this machine, MSBuild must be launched with duplicate `PATH`/`Path`
  environment variables removed, otherwise `CL.exe` fails before compilation.
- Build scripts pass `/nr:false` to MSBuild to avoid leaving reusable build
  nodes behind.

## Current Week14 Verdict

### Already Strong Enough For A Game Jam

- Lua input snapshot API plus input mode/cursor/capture controls.
- Lua `Input.ConsumeTextInput()` for completed UTF-8 text input.
- `UInputComponent` action/axis mapping and binding.
- Actor Tags in C++/Lua/Lua Blueprint/GameplayStatics.
- Component Tags for actor-local semantic lookup, including all-tags filtering.
- Actor/component tag editing uses a chip-style editor with add, remove, clear,
  and comma-paste support while preserving the existing serialized string field.
- Runtime RmlUi rendering, mouse/click/keyboard/text handling, widget capture
  flags, text/value setting, element state changes, class/attribute/style/focus
  helpers, game-jam style shortcut wrappers, transition/class animation
  wrappers, and action event polling.
- FMOD-backed audio loading/playback/BGM/loop/master volume plus quick SFX play.
- Audio handles, `Audio.PlaySFX3D`, BGM/SFX group volumes, listener state, and
  stop/query/update helpers for one-shot sounds.
- Explicit SFX playback policy helpers for max concurrent sounds, cooldown,
  priority, and stop-oldest behavior.
- JSON prefab save/spawn core and Lua `World.SpawnActorFromPrefab(path)`.
- Project/World default pawn prefab override.
- Existing `GameBuild.bat`, `ReleaseBuild.bat`, and `PackageRelease.bat`.
- Editor Project Settings Packaging section with package validation and
  one-click script launch plus dry-run launch.
- Full-copy package manifest generation for `Bin`, `Shaders`, `Content`, and
  `Settings`.
- Post-copy package smoke verification for manifest consistency, hashes, sizes,
  required directories, and executable presence.
- Optional launch smoke test for packaged executable startup checks.
- Package size report with top-level breakdown and largest packaged files.
- Lua `Json.Encode` / `Json.Decode`, `Save.WriteText` / `ReadText` /
  `WriteJson` / `ReadJson`, and deterministic `Random` helpers.
- Lua `Asset.List(typeName)`, `Asset.GetPaths(typeName)`, typed asset path
  helpers, `Asset.Find(typeName, nameOrPath)`, and `Asset.Exists(...)`.
- Lua `Scene.Open(pathOrName)`, `Scene.Reload()`, and pending/current scene
  queries for runtime map flow.
- Lua `Application` and `Debug` convenience tables for quit, viewport/world-type
  queries, logging, and non-throwing assertions.
- Actor Sequence runtime playback core plus Details-panel inline authoring now
  exists for owner-actor and actor-local component property animation. The
  dedicated sequencer tab and first-pass embedded curve canvas/key/tangent
  editing are in place, so it is usable for simple jam sequences. It still
  needs target-invalidation polish and hostile persistence QA.

### Thin But Usable

- Runtime UI action events are simple string events from `data-action` or
  `action` attributes. They are useful for menus, but not a full reactive UI
  binding system.
- Packaging is script-backed and now has dry-run, manifest support, size
  reporting, package smoke checks, and optional launch smoke. It is suitable for
  game-jam distribution, but it is still a full-copy package rather than a
  dependency-cooked/pruned package.

### Still Missing

- Advanced IME composition/candidate positioning. Completed IME text input via
  `WM_CHAR` is now queued, but composition-window management is not implemented.
- Week12-style per-event RmlUi input consumption is now applied in the Week14
  shape. `UGameViewportClient` remains the input source of truth, but RmlUi
  mouse/key/text return values are fed back into the same-frame filtered game
  snapshot. GameViewport diagnostics now cover the ejected/unpossessed player
  input leak guard; remaining work is possessed PIE smoke, full editor F8
  ejected smoke, and menu-form edge-case QA.
- Full Actor Sequencer editor UX polish. Week14 now has runtime playback,
  Details-panel inline authoring, a dedicated document tab, playback range
  handles, and first-pass embedded curve canvas/tangent editing. Remaining work
  is source-aware curve save/undo refinement and manual hostile editor QA for
  deleted/renamed/duplicated targets.
- Global world-wide component tag search. This is intentionally deferred.
- Advanced audio mixing policy such as BGM ducking, category routing presets,
  and designer-authored sound banks.
- Rich packaging optimizer: asset dependency cook, stale asset pruning rules,
  and package-size trimming rules. Size reporting and optional launch smoke are
  now available.
- Advanced Week12-style runtime helpers are now mostly covered. See
  `Docs/LuaAPIBacklogPlan.md` for the smaller remaining policy/packaging items.
- Runtime UI viewer/editor is still not ported. The lower-level runtime UI API
  plus Batch 24A/24B shortcut and transition wrappers are enough for
  hand-authored RML/Lua menus and simple animated HUDs, but not yet enough for a
  Week12-style visual UI authoring workflow.

## Actor Tags And Component Tags Decision

Actor Tags are already present and useful. They should remain actor-level
classification: enemy, player, pickup, boss, objective, damageable.

Component Tags are worth adding, but only as actor-local lookup for now. They are
best used for semantic parts inside a prefab: `Hitbox`, `Muzzle`, `WeakPoint`,
`InteractPrompt`, `CameraSocket`, `FootstepSource`.

Do not add global component tag search in the first pass. It invites per-frame
world scans and ambiguous ownership. If global lookup becomes necessary, add it
later with explicit indexing or a narrow gameplay subsystem.

## Week12 Reference Gap

Week12 has richer systems in these areas:

- UI: `FRmlUiSystem` supports element value/class/attribute/style/focus helpers,
  keyboard events, text input, and `PollActionEvents`.
- Audio: `AudioSystem` supports `PlaySFX`, `PlaySFX3D`, BGM/SFX volume groups,
  and audio handles. Week14 now covers this core surface, but not higher-level
  concurrency policy.
- Packaging: `FGameBuildSettings`, `FGamePackager`, packaging UI, startup scene
  checks, and package copy/cook logic exist. Week14 now has the UI/settings
  wrapper, validation, full-copy package script, dry-run diff, manifest
  generation, size reporting, package smoke checks, and optional launch smoke,
  but not dependency cooking/pruning.
- Tags: component tags and actor-local single-tag/all-tags lookup helpers exist.
- Input: runtime input policy work and controller-facing cursor/input-mode
  helpers exist.
- Utility API: Week12 exposes `Json`, `Save`, `Random`, `Asset`, `Scene`,
  `Application`, and debug helpers through a modular `Engine` API surface.
  Week14 now covers this core set.

Week14 now covers the most immediately useful subset: prefab spawn, default pawn
prefab, component tags, quick SFX, Lua input controls, runtime UI manipulation
and text input, script-backed packaging UX, and Lua save/json/random/asset/scene
helpers. Week14 is still behind Week12 in dependency-cooked packaging, audio
routing policy, Actor Sequence tooling, and advanced IME polish.

### Post-Batch 25A Gap Refresh

Fresh Week12/Week13 rescan after the input/UI/ActorSequence/camera smoke polish:

- Runtime UI viewer/editor is the biggest remaining productivity gap. Week12's
  `EditorRuntimeUIPreviewWidget` is a large visual authoring surface, not a
  tiny viewer. Do not bulk-port it in one blind pass. Split it into foundation,
  preview, then designer tooling.
- Actor Sequence is usable now, but Week12 still has better source-aware edit
  plumbing: dirty/save callbacks for embedded curves, undo begin/commit/cancel
  around curve gestures, reference preview/reload/cancel behavior, and hostile
  duplicated/renamed/deleted target handling.
- Camera runtime scope/DOF/shake is mostly covered. The requested Week13 camera
  work is the editor visualization mesh, not selected-camera preview rendering.
  Rescan result: Week14 has `Content/Data/EditorCamera/CameraMesh.OBJ`,
  `CameraMesh_StaticMesh.uasset`, and `EditorCamera_Blue.uasset`; patch the
  `UCameraComponent` path so it always assigns the editor-only static mesh,
  uses no collision/no shadow/hidden-in-tree, prefers the packaged `.uasset`,
  and falls back to OBJ with the Week13 `EForwardAxis::Identity` import path.
  Selected-camera preview is optional/deferred unless shot inspection becomes a
  real editor workflow requirement.
- Input/UI routing core is patched. GameViewport diagnostics cover the
  ejected/unpossessed leak guard; remaining risk is full editor PIE smoke and
  menu/form edge cases.
- Batch 25A examples exist, but they are runtime smoke scripts. They still need
  actual PIE execution because this repo does not ship a standalone `lua.exe` or
  `luac.exe` for syntax-only checks.

Recommended next split:

- Batch 26 - Runtime UI Preview Foundation: port `RuntimeUILayoutAsset` and the
  preview-only RML document bridge, then add a minimal tab that opens `.rml` and
  logs action events. High value, medium risk.
  Batch 26A applied the minimal editor-facing half: Content Browser opens
  `.rml`/`.rcss` into a single Runtime UI Preview document tab with source,
  `data-action`/`action`, and element-id inspection.
  Batch 26B adds the Week14-compatible `RuntimeUILayoutAsset` model, package
  manager, Content Browser create command, and RML/RCSS export-on-open path.
  Batch 26C adds a runtime viewport-mount viewer for the preview tab, reusing
  the real `UUIManager`/RmlUi renderer and action-event listener path.
  Batch 27A adds the first practical layout editor tab: hierarchy, details,
  add/delete, 2D box preview, save/export, and generated RML open.
  Batch 27B adds direct drag-to-move editing on the layout preview canvas.
  Batch 27C adds local asset-editor undo/redo for layout edits.
  Batch 27D adds limited existing-id RML/RCSS property/style reconciliation.
  Batch 27E adds a practical structural import step for new RML nodes with ids:
  missing widgets are created under the matching imported parent id when
  possible, otherwise under the layout root. Batch 27F adds conservative
  existing-node reparent plus visible deleted-node detection for duplicate-free
  imported RML. Batch 27G finalizes the id-less wrapper policy: id-less elements
  are transparent during import, and status text reports ignored UI attributes
  that need ids for round-trip. Batch 27H adds a practical in-tab approximate
  RML/RCSS box preview for `.rml` preview documents. Remaining Batch 26/27 work
  is the true offscreen/full-fidelity Rml render bridge and Week12
  visual-designer parity.
- Batch 27 - Runtime UI Designer Tools: hierarchy/details, select/move/delete,
  export, undo, dirty-state, and layout authoring. Batch 27A covers the
  minimal numeric-authoring subset, Batch 27B adds drag movement, and Batch 27C
  adds snapshot undo/redo. Batch 27D covers low-risk existing-id RML/RCSS
  reconcile, Batch 27E covers new-node creation from imported RML, and Batch
  27F covers duplicate-safe hierarchy reparent/delete reconcile. Batch 27G
  covers the id-less wrapper import policy, and Batch 27H covers a lightweight
  in-tab box preview for hand-authored RML. The high-risk leftover is real Rml
  preview rendering.
- Batch 28 - Actor Sequence Safety Polish: source-aware embedded-curve
  save/undo callbacks, target invalidation, duplicate/rename/delete hostile QA,
  and PIE execution of the Lua round-trip smoke.
  Batch 28A adds hostile diagnostics coverage for component rename/guid
  fallback, actor duplicate-local binding, and missing target play/stop safety.
  Batch 28B prevents external curve assets from being silently mutated by Actor
  Sequencer key edits: edit actions convert the channel to an inline curve copy,
  and the key table requires an explicit inline conversion before editing.
  Batch 28C adds direct execution routes for that self-test: editor console
  `diagnostics actorsequence` / `diag actorsequence` commands and the launch
  flag `--run-actor-sequence-self-test`, which exits 0 on pass and 2 on fail.
  Remaining Batch 28 work is manual hostile editor smoke.
- Batch 29 - Camera Editor Mesh Polish: finish the Week13 editor-camera mesh
  behavior in `UCameraComponent`, including packaged mesh preference, OBJ
  fallback with `EForwardAxis::Identity`, editor-only child repair, no collision,
  no shadow, hidden component-tree visibility, and blue editor material. The
  Week13 selected-camera preview viewport is explicitly deferred as optional
  polish, not part of the current mesh request.
  Batch 29A hardens the actual mesh path: both newly-created and repaired
  editor-camera mesh children now go through one setup path that forces visible
  editor-only rendering, assigns the mesh/material/rotation/scale, disables
  collision and shadow, refreshes bounds, and rebuilds the render state after
  setup so the viewport cannot keep an empty early proxy.
  Batch 29B adds an automated Camera Editor Mesh diagnostics path:
  `diagnostics camera mesh` / `diag camera mesh` in the editor console and
  `--run-camera-editor-mesh-self-test` / `--camera-editor-mesh-self-test` at
  launch. The self-test creates an editor world, adds a camera component,
  verifies the Week13 mesh child setup, and confirms repeated repair calls reuse
  the same editor-only mesh child instead of duplicating it.
  Batch 29C fixes the actual material payload and CineCamera parity: the camera
  mesh now uses a StaticMesh/UberLit `EditorCamera_Blue.uasset` instead of the
  previous PointLight/Billboard placeholder, `UCineCameraComponent` overrides the
  editor visualization material to `EditorCineCamera_Black.uasset`, and the
  Camera Editor Mesh self-test now verifies both Camera and CineCamera material
  paths, shader type, no-cull behavior, Week13 colors, no shadow/collision,
  rotation, scale, and duplicate-child repair.
- Batch 30 - Final Smoke Pack: PIE possessed/ejected UI input, ActorSequence
  smoke, CameraScope smoke, package dry-run, and optional launch smoke.
  Automatic smoke currently passed for the ActorSequence self-test launch flag
  and `Scripts/PackageGame.ps1` Game-configuration dry-run. Remaining Batch 30
  work is manual editor smoke for PIE possessed/ejected UI input, camera mesh
  visibility/orientation, Runtime UI Preview mount/action events, and optional
  packaged launch smoke after the team is ready to spend the Release build time.
  Batch 30A adds `diagnostics gamejam` / `diag gamejam` in the editor console
  and `--run-gamejam-self-tests` / `--gamejam-self-tests` at launch. This runs
  the current automated jam smoke pack: Actor Sequence round-trip diagnostics
  plus Camera Editor Mesh diagnostics.
  Batch 30B extends the same smoke pack with Runtime UI Layout diagnostics:
  `diagnostics runtimeui` / `diag runtimeui` and
  `--run-runtime-ui-layout-self-test` / `--runtime-ui-layout-self-test` create a
  mutated layout asset, save/load it through `FRuntimeUILayoutManager`, export
  generated RML/RCSS, inspect the generated action/style/content, and clean up
  the temporary `Saved/Diagnostics` files. Remaining Runtime UI smoke is the
  true viewport mount/action click path, because that needs an interactive RmlUi
  viewport frame.
  Batch 30C adds GameViewport input routing diagnostics for the PIE ejected
  leak guard: `diagnostics gameinput` / `diag gameinput` and
  `--run-game-viewport-input-self-test` /
  `--game-viewport-input-self-test` verify that unpossessed/ejected routing
  clears the game input snapshot, disables raw mouse capture, and does not feed
  gameplay keys/mouse into the player path. The same check is now included in
  `diagnostics gamejam` and `--run-gamejam-self-tests`. Remaining PIE work is a
  manual editor F8 smoke with an active pawn, because that confirms the full
  editor viewport loop rather than only the `UGameViewportClient` router.

## Immediate Goal - Jam-Ready Engine Patch Roadmap

이 목표는 "게임을 만들 수 있는 엔진" 상태까지 빠르게 끌어올리는 것을
우선한다. Week12/Week13 기능을 참고하되, Week14의 현재 구조에 맞춰
포팅한다. 특히 Actor Sequence는 런타임 코어만 있고 저작 UI가 없으므로
현재 팀원 입장에서는 거의 사용할 수 없는 상태다.

### P0 - 반드시 끝내야 게임 제작이 편해지는 항목

- Undo System foundation: 현재 Week14는 Material/AnimGraph/LuaBlueprint 같은
  일부 에셋 에디터에만 snapshot 기반 undo가 있고, 일반 Actor/Component
  property edit와 Actor Sequence edit를 포괄하는 공통 transaction 흐름이
  약하다. 최소한 selected actor/component serialization snapshot,
  begin/commit/cancel edit gesture, Ctrl+Z/Ctrl+Y routing, dirty state, scene
  reload safety가 필요하다.
- Actor Sequence Editor UI: `UActorSequenceComponent` details entry point,
  dedicated sequencer tab, edit model, timeline, track/property/key authoring,
  preview playback/scrub, embedded curve editing, undo/redo, scene/prefab
  persistence까지 완료해야 한다.
- Camera editor visualization mesh: Week13의 `UCameraComponent` camera mesh
  visualization을 Week14에 그대로 이식한다. 핵심은
  `CreateRenderState()`, `PreGetEditableProperties()`,
  `EnsureEditorVisualizationMesh()`, `GetEditorVisualizationMaterialPath()`,
  editor-only `UStaticMeshComponent`, no-collision/no-shadow/hidden tree 처리,
  `Content/Data/EditorCamera/CameraMesh.OBJ`, 가능하면
  `CameraMesh_StaticMesh.uasset`, 그리고 `EditorCamera_Blue.uasset` asset copy다.
- Status note after camera mesh correction: `UCameraComponent` now follows the
  Week13 editor visualization mesh path: packaged `CameraMesh_StaticMesh.uasset`
  first, OBJ fallback with `EForwardAxis::Identity`, editor-only static mesh
  child repair, blue material, no shadow, no collision, and hidden component
  tree visibility. `UCineCameraComponent` now uses its Week13 black editor mesh
  material. Batch 29A also forces a final bounds/render-state refresh after mesh
  setup, and Batch 29C replaces the placeholder Billboard material payload with
  the actual StaticMesh/UberLit camera materials. Remaining validation is manual
  editor smoke: add/select Camera and CineCamera components and confirm the mesh
  appears with the expected orientation and colors.
- Actor Sequence small fixes: `PlayRate`/`StartOffsetSeconds` metadata의
  `Max=0.0f`를 수정하고, raw `SequenceDataJson`은 details에서 직접 편집하지
  않게 숨기거나 read-only diagnostics로 제한한다.
- Actor Sequence diagnostics: Lua self-test를 실제 editor/game session에서
  실행하고, full scene save/load round-trip까지 확장한다.

### P1 - UI 담당자 생산성을 크게 올리는 항목

- Runtime UI Layout/Preview module: Week12의 Runtime UI viewer/editor는 단순
  viewer가 아니라 `RuntimeUILayoutAsset` 데이터 모델, RML/RCSS export,
  hierarchy/details, preview rendering, action event 확인까지 묶인 툴이다.
  가능하면 통째 기능 포팅을 목표로 하되, Week14의 `UUIManager` 구조에 맞춰
  preview document/widget bridge를 새로 맞춘다.
- UI Animation parity: Week12에서 실제로 분리된 keyframe UI animation
  시스템이 있는지 한 번 더 확인한다. 없다면 우선 RML/RCSS transition,
  class/style toggling, transform/opacity/background/text color helpers,
  hover/pressed/disabled state export를 Week12 수준으로 맞춘다.
- UI Lua convenience wrappers: applied in Batch 24A. Week14 now has the
  Week12-style `SetImage`, `SetProgress`, `SetTint`, `SetBackgroundColor`,
  `SetTextColor`, `SetAlpha`, `SetRounding`, `SetFontScale`,
  `SetElementTransform`, and `SetZOrder` wrappers on both `UI` and
  `UUserWidget`. Remaining work is Lua definition/example polish and visual
  UI authoring parity.
- RmlUi per-event consume router: 현재는 widget capture/block plus focused
  form control policy로 보통 메뉴는 되지만, 같은 프레임 UI click/key가
  game input으로 새는 정교한 케이스를 막으려면 Week12식
  `InputRouter`/`GameInputBridge` consume feedback을 포팅한다.

### P2 - 시간 남으면 추가

Batch 24C supersedes the RmlUi consume-router note above: same-frame RmlUi
mouse/key/text consume feedback is now routed through `UUIManager` and
`UGameViewportClient`. Remaining work is smoke/QA, not the core router.

- Input mapping/rebind asset: 팀원이 키 설정 변경 UI를 만들 계획이면 필요.
  코드/Lua binding만으로 충분하면 미룬다.
- Lua API definitions/examples: Lua를 많이 쓸수록 자동완성/예제 가치가 크다.
  최소한 이번에 새로 추가한 Scene/Input/UI/Audio/ActorSequence API 정의를
  맞춘다.
  Applied starter file: `Docs/Lua/Week14EngineAPI.def.lua` now covers the
  current Input/UI/Scene/Audio/ActorSequence autocomplete surface.
- Packaging cook/prune optimizer: 현재 full-copy package + manifest + smoke는
  충분히 쓸 수 있다. 용량이 문제가 될 때만 dependency cook/prune로 간다.
- Gamepad: 게임패드를 쓰지 않으면 이번 마감에서는 제외한다.
- Advanced audio mixer/banks/ducking: 현재 handle/group/3D/policy면 jam에는
  충분하다. 사운드 연출이 복잡해질 때만 추가한다.

### Recommended Batch Order

시간이 없으므로 작은 안정화와 큰 저작툴을 분리한다. 각 batch는 빌드 성공과
짧은 smoke test를 통과한 뒤 다음 batch로 넘어간다.

1. Batch 18 - Quick Editor Fixes And Camera Mesh
   - Add a selected-actor `Save Prefab...` button to Details, open a directory
     picker, auto-generate a collision-safe `.prefab` name, and refresh Content
     Browser after save.
   - Restore `USpringArmComponent::RefreshSpringArm(DeltaTime, bAllowLag)` so
     editor camera visualization can update against parent spring arms without
     one-frame lag.
   - Applied: `EditorCamera_Blue.uasset` is now the Week13-style StaticMesh
     UberLit blue material, not the earlier PointLight/Billboard placeholder.
     `EditorCineCamera_Black.uasset` is also present for CineCamera parity.
   - Week13 camera visualization mesh를 Week14 `UCameraComponent`에 이식.
   - `Content/Data/EditorCamera` asset과 editor camera material 복사.
   - ActorSequence `PlayRate`/`StartOffsetSeconds` metadata 수정.
   - `SequenceDataJson` raw editing 노출 정책 정리. 현재는 `Save` only라
     일반 Details에는 직접 노출되지 않으며, 전용 Actor Sequencer Details에서
     read-only diagnostics로 다루는 쪽을 유지한다.
   - Debug build 확인.

2. Batch 19 - Undo System Foundation
   - 공통 editor transaction/snapshot helper 설계.
   - actor/component property edit, component add/remove/rename, transform edit,
     ActorSequence edit가 같은 undo entry를 만들 수 있게 한다.
   - Ctrl+Z/Ctrl+Y routing과 redo invalidation 추가.
   - 기존 Material/AnimGraph/LuaBlueprint snapshot undo는 건드리지 말고,
     공통 helper와 충돌하지 않게 둔다.

   - Applied: snapshot stack, scene capture/restore, shortcut routing, and common
     level-edit capture points are in place.
   - Remaining: per-command transaction objects, visible history panel, and
     explicit ActorSequence edit-model undo entries.

3. Batch 20 - Actor Sequence Details And EditModel
   - `FEditorActorSequenceEditModel` 포팅/적응.
   - `FEditorActorSequenceDetails`를 Week14 property panel에 연결.
   - Open Sequencer, preview play/pause/stop, current time/duration,
     autoplay/loop/pause-at-end/play-rate/start-offset, clear/reset 제공.
   - animatable property filtering과 owner/component persistent-guid binding
     검증.

   - Applied in Batch 20A: the Week14 Details panel now has inline ActorSequence
     authoring for preview controls, scrub time, duration/section timing,
     owner/component `PF_Animatable` track creation, channel selection,
     current-value key insertion, inline key edit/delete, remove track, and
     clear sequence. Edits are committed into `SequenceDataJson`.
   - Remaining: split the large inline helper into a proper edit-model class if
     time allows, add apply mode and time-mapping controls, and run hostile
     scene/prefab round-trip QA from editor-authored data.

4. Batch 21 - Actor Sequencer Timeline
   - dedicated document/tab 추가.
   - toolbar, scrubber, track list, add-track/add-property, add key from current
     value, delete key/track, section/range/key drag, context menu, selection,
     zoom/view range/scroll 구현.
   - preview player만 사용하고 runtime player와 충돌하지 않게 한다.

   - Applied in Batch 21A: `FActorSequenceEditorWidget` opens as a document tab
     from `UActorSequenceComponent` Details. It renders preview controls,
     scrubber, duration/view-range controls, target-grouped track selection,
     a simple section/key timeline canvas, and selected-track key add/edit/delete
     plus track removal.
   - Remaining: stronger stale-target handling tests and hostile scene/prefab
     round-trip QA from editor-authored data.
   - Applied in Batch 21B: apply mode and time mapping controls exist in both
     the Details inline authoring panel and the dedicated sequencer tab.
   - Applied in Batch 21C: the dedicated sequencer tab now owns the add-track
     flow through an `Add Track` popup, including target/property/channel
     selection, start/length/curve asset path input, undo snapshot capture,
     selection of the newly added channel, and timeline click/drag scrubbing.
   - Applied in Batch 21D: timeline key hit-testing, selected-key feedback,
     key-table selection sync, and key time dragging are implemented.
   - Applied in Batch 21E: timeline section body selection, selected-section
     outlines, visible start/end handles, section start/end resize, and scrub
     suppression while selecting timeline items are implemented.
   - Applied in Batch 21F: timeline right-click context menu is implemented for
     hit-tested key/section selection, add-key, delete-key, delete-track, and
     fit-view-to-section commands.
   - Applied in Batch 21G: `UActorSequence` now stores playback `StartTime`
     plus `Duration`, JSON/Lua/player playback respect that range, Details
     exposes playback-start editing, and the dedicated timeline has visible
     playback range handles with resize and fit-view commands.

5. Batch 22 - Embedded Curve Editor And Actor Sequence QA
   - Week14 `FFloatCurveEditorWidget`를 embedded actor-sequence curve mode로
     확장하거나, 필요하면 Week12 `FEditorCurveEditorWidget` 역할을 포팅.
   - key/tangent/interpolation edit, source-aware dirty/save callback,
     reference preview, undo begin/commit/cancel 구현.
   - actor JSON, prefab, full scene save/load round-trip과 duplicate/rename
     hostile test 통과.

   - Applied in Batch 22A: the dedicated sequencer tab now embeds a selected
     channel curve canvas with fit-view, add/delete/select key, key dragging,
     cubic tangent-handle dragging, interpolation/tangent mode controls, and
     time/value/tangent detail editing. Edits capture undo snapshots and commit
     into `SequenceDataJson`.
   - Applied in Batch 22B: component EndPlay lifecycle and preview-vs-runtime
     player ownership were hardened, and the diagnostics self-test now includes
     full scene snapshot round-trip coverage.
  - Applied in Batch 28A: diagnostics self-test now also covers component
    rename through persistent-guid fallback, actor duplicate-local binding, and
    missing target play/stop safety.
   - Applied in Batch 28B: Actor Sequencer now treats external curve references
     as source-aware. Add/delete/drag key edit actions convert the channel to an
     inline curve copy before mutation, and the key table blocks direct external
     curve editing behind an explicit `Convert To Inline Copy` action.
   - Applied in Batch 28C: diagnostics self-test can run from the editor console
     via `diagnostics actorsequence` / `diag actorsequence`, or automatically at
     launch with `--run-actor-sequence-self-test`.
   - Remaining: manual hostile editor QA and full visual/editor smoke.

6. Batch 23 - Runtime UI Layout/Preview Module
   - `RuntimeUITypes`, `RuntimeUILayoutAsset`, RML/RCSS export 모델 포팅.
   - Week14 `UUIManager`에 preview-only document/widget bridge 추가.
   - Runtime UI preview/editor tab: open `.rml`, open layout `.uasset`, reload,
     resolution/zoom, action event log, hierarchy/details, export 확인.

   - Rescan note: split this into Batch 26 preview foundation and Batch 27
     designer tooling. Week12's runtime UI editor is too large to safely port as
     one patch under deadline pressure.

7. Batch 24 - UI Animation And UI/Input Parity
   - UI transform/opacity/color/style/class convenience API 보강.
   - RML transition/class based animation 예제와 Lua wrapper 추가.
   - Applied: RmlUi same-frame consume feedback router in the Week14
     `UGameViewportClient`/`UUIManager` shape.
   - UI click/key가 game input으로 새지 않는지 PIE possessed/ejected에서
     smoke test.

8. Batch 25 - Lua Definitions, Examples, Final Polish
   - 새 API 자동완성 정의와 짧은 Lua examples 추가.
   - Actor Sequence/UI/Input/Camera smoke scene 또는 script 추가.
   - Applied in Batch 25A: Lua API definition starter plus UI/Input,
     ActorSequence, and Camera smoke scripts. `Game|x64` build and package
     dry-run passed. Remaining: PIE execution, optional launch smoke.
   - Debug/Game build and package dry-run are done. Remaining: PIE execution of
     the smoke scripts and optional launch smoke.

### Fast-Cut If Time Runs Out

마감이 정말 빡빡하면 Batch 18-22까지만 P0로 끝낸다. 그 상태면 카메라 배치,
Actor Sequence authoring, undo/redo, prefab/scene persistence가 갖춰져서
"게임 제작용 엔진"이라고 부를 수 있다. Runtime UI editor/animation parity는
UI 담당 생산성에는 크지만, 게임 메뉴를 Lua/RML 수동 작성으로 버틸 수 있으면
P1로 미룰 수 있다.

## Batch 24A Status Note

Applied for speed before the larger Runtime UI viewer/editor work:

- `UI.SetImage(id, path)` / `widget:SetImage(id, path)`
- `UI.SetProgress(id, value)` / `widget:SetProgress(id, value)`
- `UI.SetTint(id, r, g, b, a?)`
- `UI.SetTextColor(id, r, g, b, a?)`
- `UI.SetBackgroundColor(id, r, g, b, a?)`
- `UI.SetAlpha(id, alpha)`
- `UI.SetRounding(id, px)`
- `UI.SetFontScale(id, emScale)`
- `UI.SetElementTransform(id, x, y, w, h)` / `UI.SetTransform(...)`
- `UI.SetZOrder(id, z)`
- `UI.RemoveElement(id)`

## Batch 24B Status Note

Applied for simple RmlUi transition/class animation:

- `UI.SetTransition(id, property, duration, timing?, delay?)`
- `UI.SetTransitionAll(id, duration, timing?, delay?)`
- `UI.ClearTransition(id)`
- `UI.AnimateAlpha(id, alpha, duration, timing?, delay?)`
- `UI.AnimateTextColor(id, r, g, b, duration, a?, timing?, delay?)`
- `UI.AnimateBackgroundColor(id, r, g, b, duration, a?, timing?, delay?)`
- `UI.AnimateTransform(id, x, y, w, h, duration, timing?, delay?)`
- `UI.AnimateClass(id, className, enabled, duration, timing?, delay?)`

The same methods are also available on `UUserWidget` instances.

Remaining UI work after this patch:

- Runtime UI render bridge. `RuntimeUILayoutAsset` model/export is now patched
  in Batch 26B and viewport-mount preview exists in Batch 26C.
- Limited existing-id RML/RCSS reconcile is patched in Batch 27D. Full
  structural import/reconcile back into `RuntimeUILayoutAsset` remains later.
- Runtime UI Designer Tools: hierarchy/details, select/move/delete, dirty-state,
  undo, drag editing, and layout authoring now exist at a practical first-pass
  level. Remaining designer work is richer controls and true Rml visual preview.
- UI animation/input examples are present; manual PIE visual QA is still needed.
- PIE possessed UI/game input smoke and full editor F8 ejected smoke for the
  Batch 24C/30C consume router.
- Optional launch smoke remains deferred for speed.

## Batch 24C Status Note

Applied for UI/game input separation:

- `UUIManager::BeginInputFrame()` resets per-frame RmlUi consume state.
- `UUIManager::PumpViewportInput(...)` can pump RmlUi input before render.
- RmlUi `ProcessMouse*`, `ProcessKey*`, and `ProcessTextInput` return values now
  populate same-frame consume flags.
- `UGameViewportClient::ProcessInput()` calls the UI pump before creating the
  game snapshot and clears consumed mouse/keyboard events.
- Render-time UI input processing is skipped when the pre-game pump already ran,
  preventing duplicate click/action callbacks.

Verification:

- `Debug|x64` MSBuild succeeded after Batch 24C. Remaining warnings are the
  existing PhysX/Vehicle static library PDB `LNK4099` warnings.
- `Game|x64` MSBuild succeeded after Batch 25A.
- Pending: PIE possessed click/key smoke.
- Automated GameViewport input diagnostics now confirm the ejected/unpossessed
  router clears gameplay snapshots and releases mouse capture before player
  input can run.
- Pending: manual PIE ejected smoke confirming the full editor F8 loop keeps
  player input blocked while UI still receives render-time input.

## Batch 26A Status Note

Applied for Runtime UI preview foundation:

- Content Browser recognizes `.rml` and `.rcss` as Runtime UI documents.
- Double-click opens a singleton Runtime UI Preview document tab instead of
  falling through to the OS shell.
- The preview tab reloads the file, shows source, lists `data-action`/`action`
  values, and lists element `id` values so UI/Lua wiring can be checked quickly.

Batch 26B applied:

- Added `URuntimeUILayoutAsset` as a lightweight Week14 layout model with a
  default canvas/panel/title/button tree.
- Added `FRuntimeUILayoutManager` and `EAssetPackageType::RuntimeUILayout`, so
  layout `.uasset` files can save/load through the existing asset package path.
- Added Content Browser `Create > Runtime UI Layout`; it creates the `.uasset`,
  exports sibling `.rml/.rcss`, refreshes the browser, and opens the layout
  asset in the Runtime UI Layout editor.
- Double-clicking a Runtime UI Layout `.uasset` opens the layout editor.

Batch 26C applied:

- Runtime UI Preview now has `Mount In Viewport` / `Unmount Viewport` controls.
- The mount path creates a preview `UUserWidget`, turns on mouse/keyboard/text
  capture plus game-input blocking flags, and adds it to the real viewport with
  the existing `UUIManager`/RmlUi render path.
- The preview tab polls the mounted widget's `data-action`/`action` events and
  shows recent runtime events beside the static source/action/id inspection.
- `Reload` remounts the widget when it is already mounted, so generated RML can
  be iterated without restarting the editor.

Batch 27A applied:

- Added `FRuntimeUILayoutEditorWidget` as a document-tab asset editor.
- The editor provides a hierarchy panel, add Panel/Text/Image/Button commands,
  delete-selected, a simple 2D box preview, and a details panel for id/name,
  position, size, text, image path, class, action, colors, border, padding,
  opacity, visibility, and image fit.
- `Save` persists the `.uasset` and exports generated `.rml/.rcss`.
- `Open Generated RML` saves/exports and opens the generated RML in the existing
  Runtime UI Preview tab.
- Content Browser now opens Runtime UI Layout `.uasset` files into the editor
  instead of using the preview tab as the primary surface.

Batch 27B applied:

- The Runtime UI Layout editor canvas now supports direct click-select and
  drag-to-move for non-root widgets.
- Drag movement updates the selected widget's parent-relative `Position`, marks
  the layout dirty, and uses the same save/export path as numeric edits.

Batch 27C applied:

- Added local snapshot undo/redo to the Runtime UI Layout editor using the same
  `FMemoryArchive` pattern as Material/AnimGraph/LuaBlueprint editors.
- Toolbar now exposes `Undo`/`Redo`; shortcuts cover `Ctrl+Z`, `Ctrl+Shift+Z`,
  `Ctrl+Y`, and `Ctrl+S`.
- Add/delete/details edits commit undo snapshots immediately, while drag edits
  keep the canvas responsive and commit one undo step on mouse release.

Batch 27D applied:

- Added `Import RML` to the Runtime UI Layout editor toolbar.
- The import path reads generated `.rml/.rcss` files and reconciles existing
  layout nodes by `id`/sanitized CSS id.
- This intentionally imports only low-risk authoring values: display name,
  class, action, text, image source, image fit, canvas size, position/size,
  opacity, colors, border width/radius, padding, and font size.
- The importer does not create new nodes or rebuild hierarchy from arbitrary
  hand-authored RML yet; that remains a later full structural reconcile pass.

Batch 27E applied:

- `Import RML` now records imported RML id order and nearest parent id while it
  parses tags.
- Missing RML ids are created as new `RuntimeUILayoutAsset` widgets using the
  parsed widget type, RML attributes, text, and matching RCSS style block.
- New imported widgets are attached under the matching imported parent id when
  that parent already exists or was created earlier in the import; otherwise
  they fall back to the layout root.
- This is intentionally not a destructive full reconcile yet: it does not delete
  removed RML nodes, reparent existing widgets, or infer hierarchy for duplicate
  or id-less hand-authored nodes.

Batch 27F applied:

- `Import RML` now detects duplicate ids and skips structural sync when the
  imported hierarchy would be ambiguous. The status message reports the first
  duplicate ids so the author can fix the generated RML quickly.
- Duplicate-free imports now reparent existing widgets to match the imported
  parent id relationship, while guarding against self-parent and descendant
  cycles.
- Duplicate-free imports now remove visible non-root widgets whose id/sanitized
  id disappeared from the generated RML. Id-less nodes, hidden nodes, and the
  root canvas are left untouched to avoid destroying hand-authored editor state.

Batch 27G applied:

- Id-less RML elements are now an explicit transparent-wrapper policy during
  import. They can influence the nearest imported parent stack only as wrappers;
  they are not materialized into `RuntimeUILayoutAsset` nodes.
- The import status reports how many id-less elements were ignored and how many
  had UI-relevant attributes such as class/style/action/source. Those elements
  need ids if the author wants them to round-trip into the layout asset.

Batch 27H applied:

- Runtime UI Preview documents now have a `Preview` tab beside `Source`.
- The preview tab parses practical RML/RCSS selectors (`#id`, `.class`, and tag
  rules), element ids, text/input/button/action attributes, and draws an
  approximate in-tab box preview with colors, borders, padding, basic margins,
  action labels, and runtime-event highlight feedback.
- This is intentionally not the full RmlUi renderer. `Mount In Viewport` remains
  the exact RmlUi path, and true in-tab/offscreen Rml rendering remains a
  separate high-risk bridge.

Remaining Runtime UI work:

- True in-tab/offscreen Rml render bridge inside the editor document surface.
  The preview tab now gives a useful approximate box view, while exact Rml
  rendering still happens through the game viewport mount path.
- Full Week12 designer parity: advanced transaction grouping, dirty-state tab
  label refresh, richer style controls, real runtime Rml visual fidelity, and
  stronger designer smoke tests.

Verification:

- `Debug|x64` MSBuild succeeded after Batch 26A. Remaining warnings are the
  existing PhysX/Vehicle static library PDB `LNK4099` warnings.
- `Debug|x64` MSBuild succeeded after Batch 26B. Remaining warnings are the
  existing PhysX/Vehicle static library PDB `LNK4099` warnings.
- `Debug|x64` MSBuild succeeded after Batch 26C. Remaining warnings are the
  existing PhysX/Vehicle static library PDB `LNK4099` warnings.
- `Debug|x64` MSBuild succeeded after Batch 27A. Remaining warnings are the
  existing PhysX/Vehicle static library PDB `LNK4099` warnings.
- `Debug|x64` MSBuild succeeded after Batch 27B with 0 errors and 0 warnings.
- `git diff --check` passed for Batch 27C Runtime UI Layout editor changes.
- `Debug|x64` MSBuild succeeded after Batch 27C with 0 errors and 0 warnings.
- `git diff --check` passed for Batch 27D Runtime UI Layout import changes.
- `Debug|x64` MSBuild succeeded after Batch 27D with 0 errors and 2 existing
  PhysX static library PDB `LNK4099` warnings.
- `Debug|x64` MSBuild succeeded after Batch 27E Runtime UI structural
  new-node import with 0 errors and 2 existing PhysXExtensions PDB `LNK4099`
  warnings.
- `Debug|x64` MSBuild succeeded after Batch 27F Runtime UI structural
  reparent/delete reconcile with 0 errors and 2 existing PhysXExtensions PDB
  `LNK4099` warnings.
- `Debug|x64` MSBuild succeeded after Batch 27G Runtime UI id-less wrapper
  policy with 0 errors and 2 existing PhysXExtensions PDB `LNK4099` warnings.
- `git diff --check` passed for Batch 27H Runtime UI Preview box preview.
- `Debug|x64` MSBuild succeeded after Batch 27H Runtime UI Preview box preview
  with 0 warnings and 0 errors after the follow-up wrapper-stack fix.
- `Debug|x64` MSBuild succeeded after the Camera Editor Mesh correction with 0
  warnings and 0 errors.
- `Debug|x64` MSBuild succeeded after Batch 29A Camera Editor Mesh render-state
  hardening with 0 warnings and 0 errors.
- `Debug|x64` MSBuild succeeded after Batch 29B Camera Editor Mesh diagnostics
  with 0 errors and 42 existing PhysX/Vehicle PDB `LNK4099` warnings.
- `KraftonEngine/Bin/Debug/KraftonEngine.exe --run-camera-editor-mesh-self-test`
  returned exit code 0.
- `Debug|x64` MSBuild succeeded after Batch 29C Camera/CineCamera editor mesh
  material parity with 0 warnings and 0 errors.
- `KraftonEngine/Bin/Debug/KraftonEngine.exe --run-camera-editor-mesh-self-test`
  returned exit code 0 after verifying Camera and CineCamera mesh materials.
- `Debug|x64` MSBuild succeeded after Batch 30A integrated GameJam diagnostics
  with 0 warnings and 0 errors.
- `KraftonEngine/Bin/Debug/KraftonEngine.exe --run-gamejam-self-tests` returned
  exit code 0.
- `Debug|x64` MSBuild succeeded after Batch 30B Runtime UI Layout diagnostics
  with 0 errors and 42 existing PhysX/Vehicle PDB `LNK4099` warnings.
- `KraftonEngine/Bin/Debug/KraftonEngine.exe --run-runtime-ui-layout-self-test`
  returned exit code 0.
- `KraftonEngine/Bin/Debug/KraftonEngine.exe --run-gamejam-self-tests` returned
  exit code 0 after the Runtime UI Layout diagnostics were added to the pack.
- `KraftonEngine/Bin/Debug/KraftonEngine.exe --run-game-viewport-input-self-test`
  returned exit code 0 after verifying the possessed/ejected GameViewport input
  routing guard.
- `KraftonEngine/Bin/Debug/KraftonEngine.exe --run-gamejam-self-tests` returned
  exit code 0 after the GameViewport input diagnostics were added to the pack.
- `Debug|x64` MSBuild succeeded after Batch 28A ActorSequence hostile
  diagnostics coverage with 0 warnings and 0 errors.
- `Game|x64` MSBuild succeeded after Batch 28A with 0 errors and existing
  PhysX/Vehicle static-library PDB `LNK4099` warnings.
- `Debug|x64` MSBuild succeeded after Batch 28B ActorSequencer external-curve
  inline conversion guard with 0 warnings and 0 errors.
- `Debug|x64` MSBuild succeeded after Batch 28C ActorSequence diagnostics
  command/launch routes with 0 warnings and 0 errors.
- `KraftonEngine.exe --run-actor-sequence-self-test` executed through
  `Start-Process -Wait` and returned exit code 0.
- `KraftonEngine/Bin/Debug/KraftonEngine.exe --run-actor-sequence-self-test`
  returned exit code 0 after the latest Camera Mesh and Runtime UI Preview
  patches.
- `Scripts/PackageGame.ps1 -Configuration Game -DryRun` succeeded and reported
  the package plan without writing files.
- Manual editor smoke is still pending: double-click
  `Content/UI/Examples/GameJamInputSmoke.rml` and confirm the Runtime UI Preview
  tab lists `pulse`/`close` action events, mount it in the viewport, click the
  buttons, and confirm runtime events appear; also create `Runtime UI Layout`
  from Content Browser, drag/edit a widget, save/export, and confirm the
  generated RML opens.

## Smoke And Commit Handoff

Do not forget these currently untracked files when committing the jam-ready
patch:

- `KraftonEngine/Content/Data/EditorCamera/CameraMesh.OBJ`
- `KraftonEngine/Content/Data/EditorCamera/CameraMesh_StaticMesh.uasset`
- `KraftonEngine/Content/Material/Editor/EditorCamera_Blue.uasset`
- `KraftonEngine/Content/Material/Editor/EditorCineCamera_Black.uasset`
- `KraftonEngine/Source/Engine/Diagnostics/CameraEditorMeshDiagnostics.*`
- `KraftonEngine/Source/Engine/Diagnostics/GameViewportInputDiagnostics.*`
- `KraftonEngine/Source/Engine/Diagnostics/RuntimeUILayoutDiagnostics.*`

User smoke gate before marking the larger patch fully done:

- Add/select Camera and CineCamera components and confirm the editor mesh is
  visible, oriented correctly, and uses the blue/black materials.
- Enter PIE possessed mode and confirm gameplay input still works.
- Press F8 to eject from PIE and confirm player input no longer reaches the
  pawn/controller while editor camera/UI interaction still works.
- Open `Content/UI/Examples/GameJamInputSmoke.rml`, mount it through Runtime UI
  Preview, click the action buttons, and confirm runtime events appear.
- Create a `Runtime UI Layout` asset, move/edit a widget, save/export, and open
  the generated RML.

## Actor Sequence Next Batch Candidate

Reference files inspected from `Week12_JSEngine`:

- `Source/Engine/Animation/ActorSequence.h/.cpp`
- `Source/Engine/Animation/CurvePlayback.h`
- `Source/Engine/Animation/TimelinePlayer.h/.cpp`
- `Source/Engine/Component/ActorSequenceComponent.h/.cpp`
- `Source/Editor/UI/EditorActorSequencerWidget.*`
- `Source/Editor/UI/EditorActorSequenceEditModel.*`
- `Source/Editor/UI/EditorActorSequenceDetails.*`
- `Source/Editor/UI/EditorActorSequenceTimeUtils.h`
- `Source/Editor/UI/EditorCurveEditorWidget.*`
- Lua definitions for `ActorSequenceComponent`, `ActorSequence`, and
  `ActorSequencePlayer`.

### What Week12 Actually Has

- `UActorSequenceComponent` is a spawnable actor component that owns an embedded
  `UActorSequence` plus runtime and editor-preview `UActorSequencePlayer`
  objects.
- `UActorSequence` stores bindings to the owning actor or its components,
  tracks, sections, channels, curve asset paths, and inline curve key data.
- `UActorSequencePlayer` resolves bindings by persistent component guid first,
  then component name, caches base property values, evaluates curve playback,
  and writes animatable scalar property channels.
- Playback supports autoplay, looping, play rate, pause-at-end, start offset,
  play/pause/stop, preview play/pause/stop, and scrubbing by time.
- Lua exposes `ActorSequenceComponent:Play/Pause/Stop`,
  `GetSequence`, `GetSequencePlayer`, `AddFloatTrack`, and player
  `Play/Pause/Stop/SetCurrentTime/GetCurrentTime/IsPlaying`.
- Editor support is not trivial: the property panel embeds sequence details,
  Actor Sequencer opens as a document/tab, curve editor can edit embedded
  sequence curves, and timeline UI supports property track/key editing.
- The dedicated editor is split into several responsibilities:
  `FEditorActorSequencerWidget` owns the document/tab UI and timeline
  interactions, `FEditorActorSequenceEditModel` owns target/property/key/undo
  operations, `FEditorActorSequenceDetails` owns component-level details, and
  `FEditorCurveEditorWidget` provides embedded-curve editing plus reference
  preview support.
- `FEditorActorSequencerWidget` is featureful: open/reset target, toolbar,
  add-track popup, add-property popup, pinned components, selected tracks,
  selected keys, playback range dragging, section start/end dragging, key
  dragging, context menus, timeline scrolling, and scrub/play preview controls.
- `FEditorActorSequenceEditModel` is the important safety layer. It validates
  that the sequence component is live, resolves owner/component bindings,
  collects only animatable scalar properties, maps property type to channel
  names, creates tracks/curves, adds keys from current property values, moves
  and deletes keys, deletes tracks, changes apply/time-mapping modes, resizes
  sections/playback range, and captures sequence undo state.
- `FEditorActorSequenceDetails` wraps the details-panel workflow: begin edit
  undo, mark edited, commit edit undo, and render sequence component controls.
- Week12's `FEditorCurveEditorWidget` has extra Actor Sequence behaviors that
  Week14 still does not fully cover. Batch 22A covers embedded rendering plus
  key/tangent/interpolation editing inside the dedicated sequencer, but
  actor-sequence source labels, reference preview targets, sequence-reference
  detection, source-aware dirty/save callbacks, and curve undo
  begin/commit/cancel polish remain.
- Reference inspection confirms that this is a dedicated component-sequence
  editor, not a standalone asset editor. It should open from an
  `UActorSequenceComponent`, keep editing the embedded `UActorSequence`, and
  use the selected actor as its preview/transaction source.
- Week12 and Week14 data names do not match one-to-one. Week12 editor code uses
  names such as `TargetObjectGuid`, `PropertyPath`, `EActorSequenceTrackType::Vec3`,
  `Color`, and `Transform`; Week14 currently uses `TargetComponentGuid`,
  `PropertyName`, and `Scalar/Vector3/Rotator/Vector4`. Porting must include a
  deliberate compatibility pass instead of direct copy/paste.

### Week14 Fit Check

- Week14 already has persistent component guids, prefab/scene component
  serialization, property panels, reflection property metadata, animation
  timelines, Lua property get/set helpers, an asset-editor document manager,
  and a `FFloatCurveEditorWidget` registered for curve assets.
- Current Week14 now has a dedicated `FActorSequenceEditorWidget` plus property
  panel controls for opening, preview play/pause/stop, scrubbing, and add-key
  workflows. This means the next Actor Sequence work is polish/hostile QA, not
  basic editor existence.
- Current Week14 Actor Sequence editor already captures many scene undo
  snapshots for add/delete/move/resize/key/curve operations. Remaining undo
  risk is edge-case transaction grouping and recovery from target
  rename/delete/duplicate, not a total absence of undo hooks.
- Week14 now has `FProperty::IsSequencerScalar`, `ReadScalarChannelValue`, and
  `WriteScalarChannelValue`. Future editor/Lua work should go through these
  helpers rather than introducing ad-hoc property writes.
- Week14's prefab system must serialize the embedded sequence object through
  the component's existing reflection/archive path, not through a second custom
  prefab format.
- Do not bulk-copy Week12 editor UI blindly. Port the data/runtime layer first,
  then adapt the editor UI to Week14's current panel/tab/property architecture.
  However, the dedicated Actor Sequencer editor is not optional if Actor
  Sequence is expected to be designer-usable during the jam.
- Prefer extending Week14's existing `FFloatCurveEditorWidget` for embedded
  actor-sequence curve editing if the extension stays clean. Only introduce a
  separate Week12-style `FEditorCurveEditorWidget` if the existing asset editor
  cannot support embedded sequence curves, reference preview, and source-aware
  undo without becoming tangled.
- Fresh rescan risk list: multi-select/bulk shift/copy-paste shortcuts are not
  obvious, external `.jseq`/JSON import-export UI is still weak, and binding is
  still fragile when actor/component names change despite persistent component
  guid fallback. Batch 28 should prioritize rename/delete/duplicate hostile
  tests and then only port Week12 interaction conveniences that are proven
  missing.

### Batch 15 - Actor Sequence Runtime Core Applied

Applied:

- Added curve playback descriptors/evaluator.
- Extended `FProperty` with a safe animatable scalar channel API:
  `IsSequencerScalar`, `ReadScalarChannelValue`, and
  `WriteScalarChannelValue`.
- Added `PF_Animatable` reflection support and `Animatable` metadata parsing in
  `Scripts/GenerateHeaders.py`.
- Marked actor transform/visibility and scene-component relative
  location/scale/rotation edit fields as animatable.
- Added `UActorSequence`, `UActorSequencePlayer`, and
  `UActorSequenceComponent`.
- Added owner-actor and actor-local component bindings. The first runtime pass
  started name-based; Batch 16 now upgrades component bindings to persistent
  GUID first, component name second.
- Added inline float-curve JSON export/import on the component's
  `SequenceDataJson` property so reflection scene/prefab serialization has a
  single component-owned data source.
- Added runtime and preview players, auto-play, loop, play-rate, start-offset,
  pause-at-end, current-time, play/pause/stop, base-value restore on explicit
  stop, and stale-target invalidation guards.
- Added the new files to `KraftonEngine.vcxproj` and filters.

Original task checklist:

- Add curve playback descriptors/evaluator if Week14 lacks an equivalent
  reusable layer.
- Extend `FProperty` with a safe animatable scalar channel API:
  `IsSequencerScalar`, `ReadScalarChannelValue`, and
  `WriteScalarChannelValue`.
- Add an explicit `Animatable` metadata/flag path or metadata fallback for
  properties that should be sequence-editable.
- Add `UActorSequence`, `UActorSequencePlayer`, and
  `UActorSequenceComponent`.
- Support owner-actor and actor-local component bindings only. Keep world/global
  bindings out of the first pass.
- Preserve base values on stop and when resolved targets disappear.
- Ensure runtime playback ticks through the normal component tick path and
  editor preview tick stays separate from game playback.

Validation status:

- Done: `Debug|x64` and `Game|x64` build.
- Done: header generator self-test and dry-run.
- Still needs editor/runtime QA: add an `ActorSequenceComponent` to an actor and
  play/pause/stop without crashes.
- Still needs editor/runtime QA: animate at least one float property on an
  actor-local component.
- Still needs editor/runtime QA: stop restores the cached base value.
- Still needs editor/runtime QA: destroying/removing a target component
  invalidates the track safely instead of dereferencing stale objects.

### Batch 16 - Actor Sequence Persistent Binding And Lua Partially Applied

Applied:

- Added a saved `PersistentGuid` field to `UActorComponent`, generated on
  save/load for older or newly created components.
- Added `TargetComponentGuid` to Actor Sequence component bindings.
- Runtime resolution now looks up actor-local component bindings by persistent
  GUID first and component name second.
- Sequence export refreshes owner/component binding caches before writing
  `SequenceDataJson`, so legacy name-only bindings can migrate when the target
  component is present.
- `UActorSequenceComponent` now refreshes player owner state on load, duplicate,
  runtime play, and preview play/scrub paths to avoid stale/null-owner players.
- Lua now exposes:
  `ActorSequence:GetDuration/SetDuration/Clear/ExportToJsonString/ImportFromJsonString`,
  `ActorSequencePlayer:Play/Pause/Stop/SetCurrentTime/GetCurrentTime/IsPlaying/IsPaused`,
  and `ActorSequenceComponent:Play/Pause/Stop/GetSequence/GetSequencePlayer/AddFloatTrack`.
- Lua `ActorComponent:GetPersistentGuid()`,
  `Object:AsActorSequenceComponent()`, and
  `Actor:GetActorSequenceComponent()` are available.
- `ActorSequenceComponent:AddFloatTrack(desc)` accepts compact script keys such
  as `target`, `property`, `channel`, `start`, `duration`, and `curve`, plus
  C++-style key names. `target` may be `"Owner"`, a component name, or a
  component persistent GUID.
- Guarded `LuaScriptManager.cpp` against the Win32 `GetCurrentTime` macro so
  `ActorSequencePlayer:GetCurrentTime()` binds to the engine method rather than
  `GetTickCount`.
- Added `FActorSequenceDiagnostics::RunRoundTripSelfTest()` and Lua
  `Debug.RunActorSequenceRoundTripSelfTest()`. The self-test creates a source
  actor, adds a component-bound `Location.X` sequence with inline curve keys,
  serializes actor JSON, spawns from serialized actor, saves/loads a full scene,
  saves/spawns a prefab, validates persistent component GUID binding, applies
  playback, verifies stop/base-value restore, and now covers component rename
  fallback, actor duplicate-local binding, and missing target play/stop safety.

Remaining tasks:

- Run the new Lua diagnostics entry in an actual editor/game session and record
  returned `Passed`, `ChecksRun`, and `Message`.
- Keep expanding diagnostics only for proven editor failure cases; actor JSON,
  full scene, prefab, rename, duplicate, and missing-target coverage now exist.
- Verify loop/start offset/pause-at-end flags from a real saved scene or prefab
  authored through the future editor UI.
- Lua definitions/examples exist; keep them in sync as diagnostics result fields
  or debug commands change.

Validation:

- `local r = Debug.RunActorSequenceRoundTripSelfTest()` returns
  `r.Passed == true`.
- Save a scene containing an actor sequence, reload it, and play the sequence.
- Save that actor as a prefab, spawn the prefab, and play the sequence.
- Lua can trigger a sequence on a spawned prefab.
- Invalid Lua descriptors return `false`/`nil` and log a useful warning instead
  of crashing.

### Proposed Batch 17 - Actor Sequencer Editor UX And Polish

Status note after Batch 20A: the Details-panel entry point and a practical inline
authoring path now exist in Week14, so this candidate should be treated as the
remaining full sequencer/document-tab polish list rather than a from-zero task.
Status note after Batch 21A: the dedicated document tab also exists. Treat the
remaining items below as timeline interaction, embedded curve editing, and
hostile QA/persistence polish.
Status note after Batch 21B: apply/time-mapping UI is no longer missing.
Status note after Batch 21C: in-tab Add Track and click/drag scrubbing are no
longer missing.
Status note after Batch 21D: key selection, selected-key feedback, and key time
dragging are no longer missing.
Status note after Batch 21E: section start/end handles and selected-section
feedback are no longer missing. Playback range handles are still separate work.
Status note after Batch 21F: timeline context menus are no longer missing.
Status note after Batch 21G: playback range handles are no longer missing.
Status note after Batch 22A: embedded curve canvas, key dragging, tangent
dragging, and interpolation/tangent mode editing are no longer missing. Treat
remaining curve work as source-aware save/undo polish and hostile QA.

Tasks:

- Add a Week14-native dedicated Actor Sequencer editor that opens from
  `UActorSequenceComponent` details and can live as a document/tab, matching the
  current editor document workflow.
- Port/adapt the Week12 editor model as a first-class layer, not as UI glue:
  `IsSequenceComponentLive`, `GetLiveOwner`, `CollectAnimatableScalarProperties`,
  binding resolution, component labels, property-to-track/channel mapping,
  add-track, add-key-from-current-value, move/delete key, delete track,
  section/range resize, apply mode, time mapping mode, curve creation, and undo
  notification. Key dragging has a first-pass direct implementation; a dedicated
  edit model is still useful for gesture-level polish.
- Add a details-panel section for `UActorSequenceComponent`: open sequencer,
  runtime play/pause/stop, preview play/pause/stop, current time, duration,
  playback range, auto play, pause at end, loop, play rate, start offset, and
  clear/reset sequence actions.
- Port the dedicated sequencer UI interactions: toolbar play/pause/stop,
  scrubber, current time display, duration/range display, add-track popup
  polish, owner actor row, actor-local component rows, pinned
  components, timeline ruler, sections, keys, selected track/key state, section
  start/end drag polish, playback range drag, key-drag polish, context-menu
  polish, add key at current time, zoom/view range, and vertical track scrolling.
- Add property picker filtering that shows only `PF_Animatable` properties
  accepted by `FProperty::IsSequencerScalar()`. Avoid presenting read-only,
  transient, object-reference, string, array, or unsupported struct properties.
- Add channel picker support for scalar/vector-like properties: `Value`,
  `X/Y/Z`, `Pitch/Yaw/Roll`, and `R/G/B/A` where the runtime scalar channel API
  supports them.
- Integrate curve editing for embedded sequence curves. Batch 22A already
  covers embedded canvas rendering, key list/table editing, add/delete key,
  key/tangent dragging, interpolation/tangent mode editing, and fit-to-keys in
  the dedicated sequencer. Remaining Week12 parity work is source labels,
  reference preview, source-aware dirty/save callback, reload/cancel behavior,
  and cleaner gesture-level undo begin/commit.
- Connect curve edits back to the sequence component so scene/prefab dirty
  state and undo state are captured once per edit gesture, not every frame.
- Add editor preview isolation: preview playback/scrubbing must use the
  component's preview player and must not start runtime playback or permanently
  leave animated values on the actor when the editor is closed.
- Add close/target-invalid handling: if the selected actor/component is deleted,
  hidden by scene reload, or the component loses its owner, the editor clears
  its target and stops preview without crashing.
- Reuse Week14 undo/property-change paths where they already exist. If the
  transaction path cannot safely serialize sequence data yet, gate undo behind
  a minimal actor-state capture like Week12 and document the limitation.
- Add small UI polish: stable panel widths, no text clipping, disabled states
  for invalid selections, tooltips for apply/time-mapping modes, visible
  selected-section feedback, and clear empty states for "no sequence",
  "no animatable properties", and "no keys".

Dedicated editor port checklist:

- `FEditorActorSequenceEditModel` first:
  adapt the Week12 safety layer to Week14's current runtime names. It must use
  `TargetComponentGuid`, `PropertyName`, `EActorSequenceTrackType::Scalar`,
  `Vector3`, `Rotator`, and `Vector4`, and it must call
  `FProperty::ReadScalarChannelValue/WriteScalarChannelValue` for validation.
- `FEditorActorSequenceDetails` second:
  embed component controls in the existing Week14 property panel, with undo
  begin/commit around autoplay, loop, pause-at-end, play rate, start offset,
  duration, current time, clear sequence, preview, and open-sequencer changes.
- `FEditorActorSequencerWidget` third:
  port the timeline UI after the model compiles. Keep Week12 interactions:
  toolbar icons, add-track popup polish, owner/component target rows, pinned
  components, ruler, playback range handles, sections, keys, selection,
  drag/drop edits, context menus, preview play/pause/stop, and scroll.
- Curve editor integration fourth:
  Batch 22A keeps curve editing inside `FActorSequenceEditorWidget` instead of
  introducing a parallel Week12 `FEditorCurveEditorWidget`. Remaining source
  context fields are component pointer, binding/track/section/channel handle,
  label, preview target, dirty callback, undo begin/commit/cancel, and
  restore-on-close behavior.
- Document/tab integration:
  use Week14 `FEditorDocumentTabManager` style behavior so the sequencer can be
  reopened, focused, closed, and invalidated without duplicate windows or stale
  actor/component pointers.
- Transaction policy:
  reuse Week14 undo if it can serialize the owning actor plus embedded
  `SequenceDataJson`. If not, port Week12's actor-state snapshot fallback and
  make the UI honest about any edit type that cannot undo yet.
- Preview policy:
  preview and scrub must use `GetPreviewSequencePlayer()`/`SetPreviewTime()` and
  must stop/restore on close, target deletion, scene reload, and component
  removal.
- Polishing target:
  no clipped labels in track rows, disabled buttons for invalid selections,
  clear empty states, deterministic row heights, visible selected-section states,
  helpful tooltips for Absolute/Additive/Multiply and Seconds/Normalized
  mapping, and stable behavior when the edited actor is duplicated or prefab
  spawned.

Validation:

- Existing property panel still edits ordinary components.
- Actor Sequencer opens from `UActorSequenceComponent` details as a document/tab
  and can be reopened without duplicating stale editor instances.
- Add an animatable float track on the owner actor and on an actor-local
  component.
- Add vector/rotator/color channel tracks where supported by the scalar channel
  API.
- Add key, drag key, delete key, resize section, resize playback range, change
  apply mode, change time-mapping mode, scrub, preview play, pause, and stop.
- Curve editor opens for an embedded sequence curve; add/delete key, drag key,
  edit tangents/interpolation, save/apply, reload/cancel where supported, and
  return to the sequencer without losing selection.
- Preview stop/editor close restores cached base values.
- Scene/prefab save after editor sequence edits persists exactly the edited
  data, including inline curve keys and component persistent-guid bindings.
- Undo/redo either works for sequence edits or is explicitly disabled for the
  first pass with no false UI affordance.
- Deleting the target component while the sequencer is open clears the editor
  target safely.
- Detached/docked editor window behavior remains unchanged.

Suggested split if Batch 17 is too large:

- Batch 17A: Details-panel entry point plus `FEditorActorSequenceEditModel`
  port/adaptation, with no complex timeline drawing yet.
- Batch 17B: Dedicated sequencer document/tab polish, key workflow, playback
  controls, selection, context-menu polish, and playback-range drag.
- Batch 17C: Embedded curve editor integration, reference preview, source-aware
  dirty/save/undo, and curve key/tangent polish.
- Batch 17D: UX polish and hostile editor QA: invalid target handling, empty
  states, disabled controls, docking/reopen behavior, undo/redo verification,
  scene/prefab round-trip from editor-authored data, and build verification.

### Component Multi-Tag Lookup Applied

Added:

- `AActor::FindComponentByTags(...)` and `AActor::FindComponentsByTags(...)`.
- Lua `actor:FindComponentByTags(...)` / `actor:GetComponentByTags(...)`.
- Lua `actor:FindComponentsByTags(...)` / `actor:GetComponentsByTags(...)`.
- Lua calls accept either `actor:FindComponentsByTags("Hitbox", "WeakPoint")`
  or `actor:FindComponentsByTags({ "Hitbox", "WeakPoint" })`.

Policy:

- Multi-tag lookup means all requested tags must be present on the component.
- Lookup remains actor-local. Do not add global component tag scans without an
  explicit indexed gameplay use case.

### Tag-List Editor Polish Applied

Added:

- Actor and component `Tags` properties render as a chip-style list in the
  property panel.
- Existing tags can be removed by clicking their chip.
- New tags can be added one at a time or pasted as a comma-separated list.
- `Clear` removes the whole tag list.

Implementation note:

- This intentionally keeps the existing `PendingTagsString` storage path, so
  scene/prefab serialization and `PostEditProperty` tag synchronization stay
  unchanged.

## Remaining Batches

### Batch 4 Remainder - UI Keyboard/Text Applied

Added:

- Platform completed-text input queue using `WM_CHAR` / `WM_UNICHAR`.
- `InputSystem::ConsumeTextInput()` for RmlUi and
  `InputSystem::ConsumeScriptTextInput()` for Lua.
- Lua `Input.ConsumeTextInput()` returning UTF-8 text.
- RmlUi `ProcessKeyDown`, `ProcessKeyUp`, and `ProcessTextInput` forwarding.
- Lua helpers for value/class/attribute/style/focus.

Still avoid:

- Fake Korean/IME support by composing keycodes manually.
- A second input router separate from `UGameViewportClient`.

### Batch 7 - Packaging Pipeline Applied

Added:

- `Scripts/PackageGame.ps1` as the shared full-copy packager.
- `GameBuild.bat`, `ReleaseBuild.bat`, and `PackageRelease.bat` now call the
  shared packager after building.
- Build scripts pass `/nr:false` to MSBuild to avoid persistent node reuse.
- Dry-run diff mode reports added/updated/unchanged/deleted package files.
- `PackageManifest.json` records relative path, source, size, and SHA-256.
- `Play.bat` and `BuildInfo.txt` are generated by the shared packager.
- Editor Packaging section can launch package dry-run.
- Non-dry-run packages run a smoke verification over the manifest, required
  directories, file sizes, hashes, and packaged executable.
- Non-dry-run packages print total package size, top-level breakdown, and the
  ten largest packaged files.
- `-LaunchSmokeTest` starts the packaged executable and fails if it exits early
  with a non-zero code. Surviving the timeout is considered a startup pass.
- Project Settings can pass `--launch-smoke` and `--launch-smoke-timeout` to
  `PackageRelease.bat`.

Still later:

- Asset dependency cook/prune rules.
- Package-size trimming.
- A C++ `FGamePackager` only if script-backed packaging becomes limiting.

### Audio Handle/Group/3D Applied

Added:

- Handle-returning one-shot playback with `Audio.PlaySFXHandle(...)`.
- 3D one-shot playback with `Audio.PlaySFX3D(...)`.
- `Audio.SetListener(position, forward, up)`.
- BGM/SFX group volume setters and getters.
- Handle control: `StopSound`, `StopAllSounds`, `IsSoundPlaying`,
  `SetSoundVolume`, `SetSoundPitch`, and `SetSoundPosition`.

### Audio Playback Policy Applied

Added:

- `Audio.SetSFXPolicy(pathOrKey, maxConcurrent, cooldownSeconds, priority,
  stopOldest)`.
- `Audio.ClearSFXPolicy(pathOrKey)` and `Audio.ClearAllSFXPolicies()`.
- `Audio.GetActiveSoundCount(pathOrKey)`.
- Policy is opt-in per sound key/path. Sounds without a policy keep the old
  fire-and-forget behavior.

Policy behavior:

- `maxConcurrent <= 0` means unlimited.
- `cooldownSeconds` is checked against the last successful playback of that
  sound.
- When the concurrent cap is full, `stopOldest=true` replaces the oldest active
  sound whose priority is less than or equal to the new request.

Still avoid:

- Pretending this is a full mixer authoring layer. BGM ducking, bus routing
  presets, and designer-authored sound banks should stay separate.

### Application/Debug Convenience Applied

Added:

- `Application.QuitGame()` / `Application.Exit()`.
- `Application.GetViewportSize()`, `Application.GetWorldType()`,
  `Application.IsGame()`, and `Application.IsEditor()`.
- `Debug.Log(...)`, `Debug.Warn(...)`, `Debug.Error(...)`, and
  `Debug.Assert(condition, message)`.

Still avoid:

- A separate logging subsystem just for Lua. These wrappers intentionally route
  into the existing engine logger.

### Batch 8/9 Remainder - Lua API Backlog

See `Docs/LuaAPIBacklogPlan.md` for the detailed Week12 comparison and proposed
batch order. Short version:

- Add `Scene` API only after Week14 has a safe runtime scene-transition request
- `Scene` API is now applied on top of Week14's queued runtime transition path.
- Richer `UI` APIs and `Input.ConsumeTextInput()` are now mostly applied. Batch
  24A added the Week12-style shortcut wrappers; remaining work is the visual UI
  viewer/editor, animation parity, and Lua definitions/examples.
- Deeper `Audio` handle/group/listener APIs are now represented in Week14.

## Recommended Next Work

1. Build the game prototype using the newly available prefab, default pawn,
   component tag, UI action, and SFX APIs.
2. If menus or text fields become central, implement the Batch 4 text input path.
3. Use `PackageRelease.bat <VersionName> --dry-run` before final packaging to
   inspect package changes without writing files.
4. Use `PackageRelease.bat <VersionName> --launch-smoke --launch-smoke-timeout 5`
   when you want the final package to prove the exe survives startup.
5. Use `Save.WriteJson` / `Save.ReadJson` for prototype settings, progress, and
   high-score data.
6. Use `Asset.GetRmlDocumentPaths()` / `Asset.GetSoundPaths()` for data-driven
   menus and random content selection.
7. Use `Scene.Open("Stage01")` / `Scene.Reload()` for level flow instead of
   destroying or loading worlds directly from gameplay callbacks.
8. Use `Audio.PlaySFX3D(...)` for spatial gameplay cues, and keep
   `Audio.PlaySFX(...)` for fire-and-forget UI/arcade sounds.
9. Use `Debug.Assert(...)` in prototype Lua scripts for bad data checks that
   should be visible but should not crash the whole run.
10. Keep global component tag search deferred until there is an indexed gameplay
   system that actually needs it.

## Quick Lua Examples

```lua
Input.SetInputModeGameAndUI()
Input.SetCursorVisible(true)

local controller = World.GetFirstPlayerController()
if controller then
    controller:SetInputModeGameAndUI()
end

local enemy = World.SpawnActorFromPrefab("Content/Prefab/Enemy.prefab")
local hitbox = enemy and enemy:FindComponentByTag("Hitbox")
local weakHitboxes = enemy and enemy:FindComponentsByTags("Hitbox", "WeakPoint")

Audio.PlaySFX("Explosion.wav", 0.8)
Audio.SetSFXPolicy("Explosion.wav", 4, 0.05, 0, true)
local sfx = Audio.PlaySFX3D("Explosion.wav", enemy:GetActorLocation(), 1.0)
Audio.SetSFXVolume(0.75)

Save.WriteJson("player_state.json", { hp = 100, coins = 3 })
local state = Save.ReadJson("player_state.json")
local roll = Random.RandomInt(1, 6)
local menuDocs = Asset.GetRmlDocumentPaths()
Scene.Open("Stage01")
Debug.Log("world:", Application.GetWorldType())

local menu = UI.CreateWidget("Content/UI/MainMenu.rml")
menu:AddToViewport()
menu:SetWantsMouse(true)
menu:SetBlocksGameInput(true)
menu:SetActionEvent("startButton", "StartGame")
menu:SetValue("nameInput", "Player")
menu:SetClass("startButton", "highlight", true)
UI.SetStyle("healthBar", "width", "75%")
UI.SetProgress("healthBar", 75)
UI.SetAlpha("damageFlash", 0.35)
UI.SetTextColor("scoreText", 1.0, 0.92, 0.35)
UI.SetElementTransform("toast", 32, 48, 360, 96)
UI.AnimateAlpha("damageFlash", 0.0, 0.25, "ease-out")
UI.AnimateTransform("toast", 32, 72, 360, 96, 0.18, "ease-out")

for _, eventName in ipairs(UI.PollActionEvents()) do
    if eventName == "StartGame" then
        menu:RemoveFromParent()
        Input.SetInputModeGameOnly()
    end
end
```

## Guardrails

- Do not bulk-port Week12.
- Do not replace Week14 serialization, material, asset package, or renderer
  formats as part of this feature patch.
- Keep input source of truth in `UGameViewportClient`/`InputSystem`.
- Keep prefab format as JSON `.prefab` for now.
- Keep component tag lookup actor-local until a real indexed global use case
  appears.
