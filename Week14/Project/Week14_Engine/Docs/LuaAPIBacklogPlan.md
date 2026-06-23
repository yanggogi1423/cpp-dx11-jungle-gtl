# Lua API Backlog Plan - Week14

## Purpose

Week12 exposes many game-jam-friendly Lua APIs through small API modules. Week14
uses a different architecture and currently centralizes most Lua bindings in
`FLuaScriptManager`, so this plan does not recommend a bulk port. It ranks the
reference APIs by how safely they can fit Week14 now.

## Applied In This Pass

Week14 now exposes:

- `Json.Encode(value)` and `Json.Decode(text)`.
- `Save.WriteText(path, text)`, `Save.ReadText(path)`, `Save.Exists(path)`,
  and `Save.Delete(path)`.
- `Save.WriteJson(path, value)` and `Save.ReadJson(path)`.
- `Random.SetSeed(seed)`, `Random.RandomFloat01()`,
  `Random.RandomFloat(min, max)`, `Random.RandomInt(min, max)`, and
  `Random.RandomBool(probability)`.
- `Asset.List(typeName)`, `Asset.GetPaths(typeName)`,
  `Asset.Find(typeName, nameOrPath)`, `Asset.Exists(typeName, nameOrPath)`,
  and typed path helpers for textures, meshes, materials, animations, particle
  systems, Lua scripts, Rml documents, and sounds.
- UI value/class/attribute/style/focus helpers on `UUserWidget` and global
  `UI`, including `GetElementValue`, `SetElementValue`, `SetElementClass`,
  `HasElementClass`, `GetElementAttribute`, `SetElementAttribute`,
  `SetElementStyle`, `FocusElement`, `BlurElement`, and `ClickElement`.
- Week12-style UI shortcut wrappers on `UUserWidget` and global `UI`:
  `SetImage`, `SetProgress`, `SetZOrder`, `SetTint`, `SetTextColor`,
  `SetBackgroundColor`, `SetAlpha`, `SetRounding`, `SetFontScale`,
  `SetElementTransform` / `SetTransform`, and `RemoveElement`.
- RmlUi transition/class animation wrappers on `UUserWidget` and global `UI`:
  `SetTransition`, `SetTransitionAll`, `ClearTransition`, `AnimateAlpha`,
  `AnimateTextColor`, `AnimateBackgroundColor`, `AnimateTransform`, and
  `AnimateClass`.
- `Input.ConsumeTextInput()` returning completed UTF-8 text, backed by a Win32
  `WM_CHAR` / `WM_UNICHAR` text queue.
- RmlUi keyboard and completed text forwarding through `ProcessKeyDown`,
  `ProcessKeyUp`, and `ProcessTextInput`.
- RmlUi same-frame mouse/keyboard/text consume feedback routed through
  `UUIManager` and `UGameViewportClient` before the filtered game input snapshot
  is exposed to player/Lua input.
- `Scene.Open(pathOrName)`, `Scene.Load(pathOrName)`,
  `Scene.TransitionTo(pathOrName)`, `Scene.Reload()`,
  `Scene.IsOpenPending()`, `Scene.GetCurrentPath()`, and
  `Scene.GetPendingPath()`.
- Audio handle/group/3D helpers: `Audio.PlaySFXHandle`, `Audio.PlaySFX3D`,
  `Audio.StopSound`, `Audio.StopAllSounds`, `Audio.IsSoundPlaying`,
  `Audio.SetSoundVolume`, `Audio.SetSoundPitch`, `Audio.SetSoundPosition`,
  `Audio.SetListener`, `Audio.SetBGMVolume`, `Audio.GetBGMVolume`,
  `Audio.SetSFXVolume`, `Audio.GetSFXVolume`, and `Audio.GetMasterVolume`.
- Audio playback policy helpers: `Audio.SetSFXPolicy`,
  `Audio.ClearSFXPolicy`, `Audio.ClearAllSFXPolicies`, and
  `Audio.GetActiveSoundCount`.
- `Application.QuitGame`, `Application.Exit`, `Application.GetViewportSize`,
  `Application.GetWorldType`, `Application.IsGame`, and
  `Application.IsEditor`.
- `Debug.Log`, `Debug.Warn`, `Debug.Error`, and non-throwing `Debug.Assert`.
- `Engine.Json`, `Engine.Save`, `Engine.Random`, `Engine.Asset`,
  `Engine.Scene`, `Engine.Application`, and `Engine.Debug` aliases for scripts
  that prefer a reference-style namespaced API.
- Autocomplete starter definitions in `Docs/Lua/Week14EngineAPI.def.lua` for
  the current Input/UI/Scene/Audio/ActorSequence surface.

`Save` paths are intentionally restricted to the project `Saves/` directory.
Absolute paths and `..` traversal are rejected.

## Reference Surface From Week12

Inspected reference files:

- `Runtime/Script/API/LuaJsonAPI.cpp`
- `Runtime/Script/API/LuaSaveAPI.cpp`
- `Runtime/Script/API/LuaRandomAPI.cpp`
- `Runtime/Script/API/LuaInputAPI.cpp`
- `Runtime/Script/API/LuaUIAPI.cpp`
- `Runtime/Script/API/LuaAudioAPI.cpp`
- `Runtime/Script/API/LuaWorldAPI.cpp`
- `Runtime/Script/API/LuaSceneAPI.cpp`
- `Runtime/Script/API/LuaAssetAPI.cpp`
- `Runtime/Script/API/LuaApplicationAPI.cpp`

## Current Week14 Lua API Verdict

### Good Now

- `Input` has policy-filtered key state, mouse delta, cursor
  visibility/lock/capture, and input mode helpers.
- `World` can spawn actors/pawns, spawn from prefab, find actors by name/class/
  tag, query time, and line trace.
- `UI` can create widgets, set text/value, show/hide/enable elements, manipulate
  classes/attributes/styles/focus, use game-jam shortcut wrappers, trigger
  simple RmlUi transitions, register simple action events, and poll action
  events.
- `Audio` can load/play sounds, play BGM, play looped audio, set master/BGM/SFX
  volume, play quick SFX, play 3D SFX, and control one-shot handles.
- `Json`, `Save`, and `Random` now cover the most common prototype persistence
  and data-table needs.
- `Asset` now exposes read-only asset list/path lookup backed by
  `FAssetRegistry`.
- `Scene`, `Application`, and `Debug` now cover the common Week12-style
  convenience surface.
- Actor-local component tag lookup now supports all-tags filtering through
  `actor:FindComponentByTags(...)` and `actor:FindComponentsByTags(...)`.
- Actor/component tag editing has a chip-style property-panel UI; this is editor
  polish rather than a Lua API, but it completes the practical tag workflow.
- Actor Sequence C++ runtime core now exists in Week14:
  `UActorSequence`, `UActorSequencePlayer`, `UActorSequenceComponent`, safe
  animatable scalar-channel reflection helpers, and component-owned sequence JSON
  storage. Lua bindings and persistent component GUID lookup are now present,
  but scene/prefab round-trip QA and the dedicated Actor Sequencer editor still
  need to be finished.

### Still Thin

- `Input` exposes completed text input consumption. Full IME composition window
  positioning is still not implemented.
- Lua gameplay input mirrors the `UGameViewportClient` filtered game snapshot,
  including RmlUi same-frame mouse/keyboard/text consume feedback. Remaining
  work is possessed/ejected PIE smoke testing.
- `UI` has keyboard and completed text forwarding plus Week12-style element
  value/class/attribute/style/focus helpers, shortcut wrappers, transition
  wrappers, and same-frame consume feedback. The remaining UI gap is visual
  layout/preview tooling plus manual animation/input visual QA.
- `Audio` now has handles, group volume split, listener state, `PlaySFX3D`,
  stop/query-by-handle, and opt-in SFX playback policy for max-concurrent,
  cooldown, priority, and stop-oldest behavior.
- `Scene` now exposes the important safe request-style runtime transition API.
  It does not attempt editor-time scene swapping during PIE; editor requests map
  to ending the play session.

## Proposed Additional Batches

### Batch 9 - Lua Asset Query API Applied

Added a global `Asset` table plus `Engine.Asset` alias.

Available functions:

- `Asset.List(typeName)` using `FAssetRegistry::ListByTypeName`.
- `Asset.GetPaths(typeName)`.
- `Asset.Find(typeName, nameOrPath)`.
- `Asset.Exists(typeName, nameOrPath)`.
- `Asset.GetTexturePaths()`, `GetStaticMeshPaths()`, `GetSkeletalMeshPaths()`,
  `GetMaterialPaths()`, `GetAnimationPaths()`, `GetParticleSystemPaths()`,
  `GetLuaScriptPaths()`, `GetRmlDocumentPaths()`, and `GetSoundPaths()`.

Why first:

- Low runtime risk.
- Useful for random spawn tables, menu previews, debug UI, and data-driven Lua.
- Week14 already has the registry layer; this should not require renderer or
  serialization changes.

Avoid:

- Loading arbitrary files outside `Content/`.
- Exposing editor-only import/write operations to game Lua.

### Batch 10 - UI Value/Class/Attribute/Style API Applied

Added these to both `UUserWidget` and global `UI`:

- `GetElementValue` / `SetElementValue`
- `SetElementClass`, `HasElementClass`, `GetElementClassNames`,
  `SetElementClassNames`
- `HasElementAttribute`, `GetElementAttribute`, `SetElementAttribute`,
  `RemoveElementAttribute`
- `GetElementStyle`, `SetElementStyle`, `RemoveElementStyle`
- `FocusElement`, `BlurElement`, `IsElementFocused`, `ClickElement`

Why after Asset:

- It is useful, but it should be implemented against Week14's `UUIManager` /
  `UUserWidget` ownership model rather than copied from Week12's `FRmlUiSystem`.

Remaining pair:

- Batch 4 text input if menus include actual text fields.

### Batch 24A - UI Convenience Wrappers Applied

Added these to both `UUserWidget` and global `UI`:

- `SetImage(elementId, path)`
- `SetProgress(elementId, value)`
- `SetZOrder(elementId, zOrder)`
- `SetTint(elementId, r, g, b, a?)`
- `SetTextColor(elementId, r, g, b, a?)`
- `SetBackgroundColor(elementId, r, g, b, a?)`
- `SetAlpha(elementId, alpha)`
- `SetRounding(elementId, pixels)`
- `SetFontScale(elementId, emScale)`
- `SetElementTransform(elementId, x, y, width, height)`
- `SetTransform(elementId, x, y, width, height)`
- `RemoveElement(elementId)`

Implementation note:

- These are thin wrappers over Week14's existing value/attribute/style methods.
  They do not introduce a new UI runtime or layout asset model.

Remaining UI backlog:

- Runtime UI render bridge. `RuntimeUILayoutAsset` model/export is now patched
  in Batch 26B, and viewport-mount preview exists in Batch 26C.
- Limited existing-id RML/RCSS reconcile back into `RuntimeUILayoutAsset` is
  patched in Batch 27D. Full structural import remains later.
- Runtime UI designer tooling: hierarchy/details, select/move/delete,
  dirty-state, undo, drag editing, and layout authoring now exists at first-pass
  level. Remaining work is richer controls and true in-tab Rml visual preview.
- PIE possessed/ejected smoke test for same-frame RmlUi consume feedback.
- Manual visual QA for the new UI/Input example.

Week12 rescan note:

- Treat the Runtime UI work as two batches. First port the layout asset model
  and preview-only RML document bridge so `.rml` files can be opened and action
  events inspected. Then, only if the UI owner needs it, port the heavier
  hierarchy/details/drag/export designer tooling.
- Do not bulk-copy Week12's `EditorRuntimeUIPreviewWidget` in one patch; it is a
  large authoring surface and should be split for reviewability.

Batch 26A applied:

- Content Browser opens `.rml` and `.rcss` into a singleton Runtime UI Preview
  document tab.
- The tab reloads the file, shows source, and extracts `data-action`/`action`
  plus element `id` values for quick Lua/UI wiring inspection.
- `Debug|x64` MSBuild passed after this editor-facing preview foundation.

Batch 26B applied:

- `RuntimeUILayoutAsset` now exists as a lightweight package-backed layout
  model with RML/RCSS export.
- Content Browser can create `Runtime UI Layout`; creation writes the `.uasset`
  and generated sibling `.rml/.rcss`, then opens the layout editor.
- Double-clicking the layout `.uasset` opens the layout editor.
- `Debug|x64` MSBuild passed after this layout/export bridge.

Batch 26C applied:

- Runtime UI Preview can now mount the opened `.rml` into the real viewport
  using a preview `UUserWidget` and the existing `UUIManager`/RmlUi renderer.
- The mounted preview enables mouse/keyboard/text capture and game-input
  blocking, so UI input routing can be checked against the real runtime path.
- The preview tab displays recent runtime `data-action`/`action` events from
  the mounted widget.
- `Debug|x64` MSBuild passed after this viewport-mount viewer. Remaining
  warnings were the existing PhysX/Vehicle PDB `LNK4099` warnings.

Batch 27A applied:

- Added a Runtime UI Layout editor tab with hierarchy, details, add/delete,
  numeric layout editing, color/style fields, and a simple 2D box preview.
- Save persists the package and exports generated `.rml/.rcss`.
- Open Generated RML saves/exports and then opens the generated document in the
  existing Runtime UI Preview tab.
- `Debug|x64` MSBuild passed after this minimal designer surface.

Batch 27B applied:

- The Runtime UI Layout editor canvas supports click-select and drag-to-move
  for non-root widgets.
- Drag movement updates parent-relative positions, marks the layout dirty, and
  reuses the save/export path.
- `Debug|x64` MSBuild passed after drag editing with 0 errors and 0 warnings.

Batch 27C applied:

- Runtime UI Layout editor now has local asset snapshot undo/redo using
  `FMemoryArchive`, matching the existing Material/AnimGraph/LuaBlueprint
  editor pattern.
- Toolbar buttons and shortcuts are available for undo/redo/save. Drag movement
  commits a single undo step on mouse release instead of one snapshot per frame.
- `git diff --check` and `Debug|x64` MSBuild passed for the Runtime UI Layout
  editor changes with 0 errors and 0 warnings.

Batch 27D applied:

- Runtime UI Layout editor now has `Import RML`.
- It reads generated `.rml/.rcss` files and reconciles existing layout nodes by
  `id`/sanitized CSS id.
- Imported fields are intentionally limited to low-risk authoring data: text,
  image source/fit, display name, class, action, canvas size, position/size,
  opacity, colors, borders, padding, border radius, and font size.
- `git diff --check` and `Debug|x64` MSBuild passed after the import patch.
  The build had 0 errors and 2 existing PhysX PDB `LNK4099` warnings.

Still not done:

- The editor tab still does not render RmlUi offscreen/in-tab. Batch 26C uses
  the real game viewport as the Runtime UI viewer.
- Full structural import is still missing. Batch 27D does not create/delete
  nodes or rebuild hierarchy from arbitrary hand-authored RML.
- Advanced designer parity is still missing: real Rml visual preview, richer
  style controls, dirty-state tab title refresh, and stronger manual smoke.

### Batch 24B - UI Transition Animation Wrappers Applied

Added these to both `UUserWidget` and global `UI`:

- `SetTransition(elementId, propertyName, duration, timing?, delay?)`
- `SetTransitionAll(elementId, duration, timing?, delay?)`
- `ClearTransition(elementId)`
- `AnimateAlpha(elementId, alpha, duration, timing?, delay?)`
- `AnimateTextColor(elementId, r, g, b, duration, a?, timing?, delay?)`
- `AnimateBackgroundColor(elementId, r, g, b, duration, a?, timing?, delay?)`
- `AnimateTransform(elementId, x, y, width, height, duration, timing?, delay?)`
- `AnimateClass(elementId, className, enabled, duration, timing?, delay?)`

Implementation note:

- These wrappers rely on RmlUi's built-in `transition` property support. They
  are intended for simple HUD/menu motion, fades, color pulses, and class-based
  state changes.
- This is not a full timeline/keyframe UI animation editor. Runtime UI
  layout/preview/editor work remains separate.

### Batch 24C - RmlUi Same-Frame Consume Routing Applied

Applied in the Week14 input architecture:

- `UUIManager::BeginInputFrame()` clears stale per-frame UI consume state.
- `UUIManager::PumpViewportInput(...)` lets `UGameViewportClient` pump RmlUi
  before the player/Lua game input snapshot is built.
- RmlUi mouse, keyboard, and text return values populate
  `FUIInputCaptureState` consume flags.
- `UGameViewportClient` clears consumed mouse/keyboard events from the game
  snapshot and releases raw mouse capture for UI-consumed mouse frames.
- Render-pass UI input processing is skipped if the pre-game pump already ran,
  preventing duplicate click callbacks.

Remaining caveat:

- `Debug|x64` MSBuild succeeded after the same-frame consume patch.
- `Game|x64` MSBuild succeeded after Batch 25A Lua definition/example polish.
- `Scripts/PackageGame.ps1 -RootDir . -Configuration Game -DryRun` succeeded
  after Batch 25A and wrote no files. Possessed/ejected PIE smoke is still
  deferred for speed.

### Batch 25A - Lua Definitions And Smoke Examples Applied

Added:

- `Docs/Lua/Week14EngineAPI.def.lua` now covers the new UI shortcut,
  transition, action-event, visibility, and `CameraManager` helpers.
- `KraftonEngine/Content/UI/Examples/GameJamInputSmoke.rml`.
- `KraftonEngine/Content/Script/Examples/GameJamInputUISmoke.lua`.
- `KraftonEngine/Content/Script/Examples/ActorSequenceSmoke.lua`.
- `KraftonEngine/Content/Script/Examples/CameraScopeSmoke.lua`.

Remaining caveat:

- The repo does not include a standalone `lua.exe`/`luac.exe`, so syntax and
  runtime behavior still need PIE execution. These examples are intentionally
  non-authoritative smoke scripts, not tests that run during build.
- `Game|x64` build and package dry-run passed after adding the examples;
  launch smoke remains deferred.

### Batch 11 - Input Text And RmlUi Keyboard Forwarding Applied

Added:

- `Input.ConsumeTextInput()` returning UTF-8 text.
- Platform completed-text queue using real `WM_CHAR` / `WM_UNICHAR` events.
- RmlUi `ProcessKeyDown`, `ProcessKeyUp`, and `ProcessTextInput` forwarding.
- UI capture rules that prevent gameplay scripts from consuming text when a UI
  widget owns text input.
- Same-frame RmlUi mouse/keyboard/text consume feedback now clears consumed
  game input through `UGameViewportClient`.

Remaining caveat:

- This supports committed IME text, including Korean characters delivered by
  `WM_CHAR`, but it does not reposition or render OS IME composition/candidate
  UI.

### Batch 12 - Audio Handles, Groups, And 3D SFX Applied

Available functions:

- `Audio.PlaySFXHandle(pathOrKey, volumeScale)`
- `Audio.PlaySFX3D(pathOrKey, position, volumeScale)`
- `Audio.SetListener(position, forward, up)`
- `Audio.SetBGMVolume`, `Audio.SetSFXVolume`, getters for both
- `Audio.StopSound(handle)`, `Audio.IsSoundPlaying(handle)`,
  `Audio.SetSoundPosition(handle, position)`
- `Audio.SetSFXPolicy(pathOrKey, maxConcurrent, cooldownSeconds, priority,
  stopOldest)`
- `Audio.ClearSFXPolicy(pathOrKey)`, `Audio.ClearAllSFXPolicies()`, and
  `Audio.GetActiveSoundCount(pathOrKey)`

Still later:

- Optional advanced mixing: BGM ducking, category routing presets, and
  designer-authored sound banks.

### Actor-Local Component Multi-Tag Lookup Applied

Available functions:

- `actor:FindComponentByTags("TagA", "TagB")`
- `actor:GetComponentByTags("TagA", "TagB")`
- `actor:FindComponentsByTags({ "TagA", "TagB" })`
- `actor:GetComponentsByTags({ "TagA", "TagB" })`

Semantics:

- Components must have every requested tag.
- Empty or invalid tag input returns no match instead of broadening the search.
- This intentionally stays actor-local; global component tag scans are still
  deferred.

### Batch 13 - Scene Runtime Request API Applied

Added on top of Week14's queued `UGameEngine` transition path.

Available functions:

- `Scene.Open(pathOrName)`
- `Scene.Load(pathOrName)`
- `Scene.TransitionTo(pathOrName)`
- `Scene.Reload()`
- `Scene.IsOpenPending()`
- `Scene.GetCurrentPath()`
- `Scene.GetPendingPath()`

Guardrails:

- Do not load immediately inside arbitrary Lua callbacks.
- Queue the transition and apply it outside world/component iteration.
- Keep Game build world type overrides intact.
- Missing scene files are rejected before the active world is destroyed.

### Batch 14 - Application And Debug Convenience Applied

Available functions:

- `Application.QuitGame()` as an alias to `Engine.Exit()`.
- `Application.Exit()`.
- `Application.GetViewportSize()`.
- `Application.GetWorldType()`.
- `Application.IsGame()` / `Application.IsEditor()`.
- `Debug.Log`, `Debug.Warn`, and `Debug.Error`.
- `Debug.Assert(condition, message)` logs and returns the condition without
  throwing.

Why small:

- Week14 already has `print` and `Engine.Exit()`, so this should stay as a
  convenience wrapper over the existing logger and exit path.

### Batch 15 Applied / Batch 16 Partially Applied / Batch 17 Planned - Actor Sequence Runtime, Serialization, Editor, Lua

Reference surface from Week12:

- `ActorSequenceComponent:Play()`, `Pause()`, `Stop()`.
- `ActorSequenceComponent:GetSequence()`.
- `ActorSequenceComponent:GetSequencePlayer()`.
- `ActorSequenceComponent:AddFloatTrack(desc)`.
- `ActorSequencePlayer:Play()`, `Pause()`, `Stop()`.
- `ActorSequencePlayer:SetCurrentTime(time)`.
- `ActorSequencePlayer:GetCurrentTime()`.
- `ActorSequencePlayer:IsPlaying()`.
- Dedicated editor files:
  `EditorActorSequencerWidget`, `EditorActorSequenceEditModel`,
  `EditorActorSequenceDetails`, `EditorActorSequenceTimeUtils`, and
  `EditorCurveEditorWidget`.

Required order:

- C++ runtime core is applied: `UActorSequence`, `UActorSequencePlayer`,
  `UActorSequenceComponent`, curve playback evaluation, safe reflection
  scalar-channel read/write helpers, and component-owned `SequenceDataJson`.
- Persistent component GUID binding is applied: component bindings resolve by
  GUID first and component name second.
- Lua API binding is applied for `ActorSequence`, `ActorSequencePlayer`, and
  `ActorSequenceComponent`.
- Next verify scene and prefab round-trip for embedded sequence data and
  actor-local component bindings.
- A diagnostics entry now exists for actor JSON and prefab round-trip:
  `Debug.RunActorSequenceRoundTripSelfTest()` returns `Passed`, `ChecksRun`, and
  `Message`. It still needs to be run from an actual editor/game Lua session,
  then extended to full scene save/load once the scene test harness is chosen.
- Then add Lua definitions/examples after round-trip QA is proven.
- Finally add the dedicated editor Actor Sequencer UX adapted to Week14's
  current document tabs, property panel, curve editor, and transaction patterns.
  This editor pass should include the Week12-level details: edit model,
  details-panel entry point, timeline, add-track/property popups, key and section
  editing, scrub/preview controls, embedded curve editing, source-aware dirty
  state, undo capture, invalid target handling, and UI polish.
- Treat the dedicated editor as an embedded `UActorSequenceComponent` editor,
  not an asset editor. Week12's code is the reference, but Week14's runtime data
  model uses different names (`TargetComponentGuid`, `PropertyName`,
  `Scalar/Vector3/Rotator/Vector4`), so the port must adapt the edit model
  before the UI is copied over.

Guardrails:

- Lua is now bound, but do not broaden the API further before scene/prefab
  round-trip and stale owner/component handling are verified from real saved
  data.
- Keep the first pass actor-local. No global object bindings.
- Reject invalid descriptors instead of broadening target/property lookup.
- Do not hand-write arbitrary property writes in Lua; go through the same
  `FProperty` scalar channel API used by the runtime player.
- Do not expose Lua-only sequence mutation that the dedicated editor cannot
  inspect and save. Runtime, editor, prefab, scene, and Lua must share the same
  sequence data model.
- Do not add a second independent curve-editor stack unless Week14's current
  `FFloatCurveEditorWidget` cannot cleanly support embedded actor-sequence
  curves, preview, and source-aware save/undo.

Editor completion target:

- `UActorSequenceComponent` details show clear sequence controls and an
  "open sequencer" action.
- The Actor Sequencer opens as a document/tab and can be reopened without stale
  targets.
- Property picker shows only animatable scalar/channel properties accepted by
  `FProperty::IsSequencerScalar()`.
- Tracks can target the owner actor or actor-local components, resolved by
  persistent component guid first and name second.
- Keys/sections/playback range can be added, dragged, resized, deleted, and
  saved.
- Embedded curve keys/tangents can be edited and previewed against the live
  actor without permanently leaving preview values behind.
- Scene save/load and prefab save/spawn preserve editor-authored sequence data.

Example target after implementation:

```lua
local seq = actor:GetActorSequenceComponent()
local tagged = actor:FindComponentByTag("IntroSequence")
if not seq and tagged then
    seq = tagged:AsActorSequenceComponent()
end
if seq then
    seq:Play()
end
```

Example track mutation surface now available:

```lua
local seq = actor:GetActorSequenceComponent()
if seq then
    seq:AddFloatTrack({
        target = "Owner",
        property = "PendingActorLocation",
        channel = "X",
        start = 0.0,
        duration = 1.0,
    })
end
```

## Suggested Immediate Order

1. Advanced IME composition/candidate positioning only if Korean text fields
   become central to the game.
2. Actor Sequence runtime/serialization/editor/Lua if in-world scripted motion,
   doors, lifts, cameras, cutscenes, or prefab-authored timeline events become
   central to the game.
3. Rich packaging cook/prune only if package size or stale assets become a real
   distribution problem. Current packaging already reports package size and can
   run an optional packaged-exe launch smoke test.
4. Advanced audio mixing only if BGM ducking or designer-authored sound banks
   become necessary.

## Lua Examples

```lua
local state = Save.ReadJson("player_state.json") or { coins = 0, hp = 100 }
state.coins = state.coins + 1
Save.WriteJson("player_state.json", state)

local encoded = Json.Encode({ name = "slime", hp = 30, drops = { "coin", "gem" } })
local decoded = Json.Decode(encoded)

Random.SetSeed(1234)
local spawnIndex = Random.RandomInt(1, 5)
local uiDocuments = Asset.GetRmlDocumentPaths()
local explosionPath = Asset.Find("Sound", "Explosion")
Scene.Open("Stage01")
print("pending scene:", Scene.GetPendingPath())
Audio.SetSFXPolicy(explosionPath, 4, 0.05, 0, true)
local handle = Audio.PlaySFX3D(explosionPath, FVector(0, 0, 0), 1.0)
Audio.SetSFXVolume(0.75)
Debug.Log("world:", Application.GetWorldType())
local weakPoints = enemy and enemy:FindComponentsByTags("Hitbox", "WeakPoint")

local menu = UI.CreateWidget("Content/UI/MainMenu.rml")
menu:AddToViewport()
menu:SetValue("nameInput", "Player")
menu:SetClass("startButton", "highlight", true)
UI.SetStyle("healthBar", "width", "75%")
UI.SetProgress("healthBar", 75)
UI.SetAlpha("damageFlash", 0.35)
UI.SetTextColor("scoreText", 1.0, 0.92, 0.35)
UI.SetElementTransform("toast", 32, 48, 360, 96)
UI.AnimateAlpha("damageFlash", 0.0, 0.25, "ease-out")
UI.AnimateTransform("toast", 32, 72, 360, 96, 0.18, "ease-out")
UI.Focus("nameInput", true)

local typed = Input.ConsumeTextInput()
if typed ~= "" then
    print("typed:", typed)
end
```

## Completion Gates For Future Batches

- Every new Lua table must have a short example in this file or
  `GameJamFeaturePatchPlan.md`.
- Paths exposed to Lua must be project-local unless there is a deliberate editor
  workflow reason.
- `Debug|x64` and `Game|x64` must build after each batch.
- Packaging changes must pass `Scripts/PackageGame.ps1 -DryRun` and one temp
  package smoke test that verifies `PackageManifest.json`, required
  directories, file sizes, hashes, and the packaged exe.
- Optional launch smoke changes must be exposed through `PackageRelease.bat`
  without running by default.
- Runtime APIs must fail safely: return `false` or `nil` instead of crashing when
  `GEngine`, `World`, widget, actor, component, or asset lookups fail.
