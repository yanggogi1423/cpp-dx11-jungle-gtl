# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

DirectX 11 3D scene editor engine built with C++ and ImGui. Actor/Component architecture with WYSIWYG editing, raycasting object selection, multi-scene management, and JSON serialization. Includes a standalone OBJ mesh viewer mode (`ObjViewDebug` build) for previewing static meshes.

**Week 5 focus:** High-performance rendering competition — render 50,000 `StaticMeshComponent`s (50×50×20 grid) as fast as possible. Key constraint: one Draw Call per StaticMesh (no `DrawInstanced`/`DrawIndexedInstanced`). Optimization targets: Frustum Culling, Scene Graph (BVH/Octree/K-d tree), SIMD. Performance HUD must display FPS (ms), last Picking time (ms), accumulated Picking count, and accumulated Picking time (ms) in the top-left corner.

## Build Commands

```bash
# Build (x64 Debug) via MSBuild
msbuild KraftonEngine.sln /p:Configuration=Debug /p:Platform=x64

# Build (x64 Release)
msbuild KraftonEngine.sln /p:Configuration=Release /p:Platform=x64

# Build OBJ Viewer (x64) — standalone mesh preview tool
msbuild KraftonEngine.sln /p:Configuration=ObjViewDebug /p:Platform=x64

# Regenerate project files after adding/removing source files
python Scripts/GenerateProjectFiles.py

# Release packaging (copies exe + shaders + assets to ReleaseBuild/)
./ReleaseBuild.bat
```

Output: `KraftonEngine/Bin/<Configuration>/KraftonEngine.exe`

Build configurations: `Debug`, `Release`, `ObjViewDebug` (x64/x86). ObjViewDebug defines `IS_OBJ_VIEWER=1` and excludes most Editor sources, launching `UObjViewerEngine` instead of `UEditorEngine`.

Requirements: Visual Studio 2022 (v143 toolset), Windows SDK with DirectX 11. All dependencies (ImGui, SimpleJSON, DirectXTK) are included in-tree. No package manager needed.

There are no tests or linting tools configured in this project.

## Architecture

### Object System (RTTI)

Custom runtime type information using `DECLARE_CLASS` / `DEFINE_CLASS` macros that build `FTypeInfo` chains. Supports `IsA<T>()` and `Cast<T>()`. `UObjectManager` singleton handles lifecycle and auto-naming (`ClassName_N`). `FName` is a pooled, case-insensitive string identifier.

### Rendering Pipeline

Multi-pass command buffer pattern: `RenderCollector` (scene traversal → `FRenderCommand`) → `RenderBus` (per-pass queuing) → `Renderer` (GPU submission via `FPassRenderState` lookup tables).

Render pass order: Opaque → Font → SubUV → Translucent → StencilMask → Outline → Editor → Grid → DepthLess.

Adding a new render pass = one entry in the `FPassRenderState` table. Batchers (Line, Font, SubUV) own their shaders and are flushed by the Renderer.

### Editor

`UEditorEngine` extends `UEngine`. Viewport supports ray-triangle picking, stencil-based selection outline, and gizmo transform manipulation. UI is entirely ImGui-based with docking widgets (Scene hierarchy, Property editor, Viewport overlay, Console, Stats).

### OBJ Viewer

Standalone mesh preview mode (`Source/ObjViewer/`). `UObjViewerEngine` subclasses `UEngine` and is activated via `IS_OBJ_VIEWER` preprocessor flag in `EngineLoop.cpp`. Components: `ObjViewerPanel` (ImGui mesh list + viewport UI), `ObjViewerRenderPipeline` (offscreen render target), `ObjViewerViewportClient` (orbit/pan/zoom camera).

### Serialization

`.Scene` files are JSON. `FSceneSaveManager` handles read/write of actor hierarchy, components, transforms, camera state. Editor settings persist to `Settings/editor.ini`.

### Performance & Optimization (Week 5)

Competition constraints:
- **No Instanced Rendering** — `DrawInstanced` / `DrawIndexedInstanced` are forbidden; each `StaticMesh` issues exactly one Draw Call.
- **Real-time only** — pre-caching results when the camera is stationary is not allowed. Objects may move in/out of view at any time.
- Standard scene (`DefaultScene.zip`) and mesh dataset (`JungleApples.zip`) are fixed; camera info, object count, and meshes may vary between teams at judging time.
- Performance measured on Alienware m15 R5 (RTX 3070). Winner ranked by FPS score + Picking score (lower ms = better).

Key optimization techniques in scope:
- **Frustum Culling** — skip Draw Calls for meshes outside the view frustum.
- **Scene Graph / Spatial partitioning** — BVH, Octree, or K-d tree to accelerate frustum and ray-cast queries.
- **SIMD** — SSE2/AVX for math-heavy culling loops (`__m128`, aligned allocations).
- **Render State sorting** — minimize redundant state changes between Draw Calls.

Performance timing uses `FWindowsPlatformTime` (`QueryPerformanceFrequency` / `QueryPerformanceCounter`) and `FScopeCycleCounter` (RAII scope timer). The HUD overlay (top-left) must show:

```
FPS : <N> (<ms> ms)
Picking Time <ms> ms : Num Attempts <N> : Accumulated Time <ms> ms
```

## Code Conventions

- C++20 (x64), C++17 (Win32/x86)
- UTF-8 BOM for C++/H files, tab indentation (size 4)
- UTF-8 (no BOM) for HLSL shaders
- Include paths root at: `Source/Engine`, `Source`, `Source/Editor`, `Source/ObjViewer`, `ThirdParty`, `ThirdParty/ImGui`
- Headers use relative paths from these roots: `#include "Engine/Core/InputSystem.h"`
- Naming: `F` prefix for structs/data types (FVector, FName), `U` for UObject derivatives, `A` for Actors, `E` for enums
- HLSL shaders in `KraftonEngine/Shaders/` are compiled at runtime

## Key Source Layout

- `KraftonEngine/Source/Engine/` — core engine (Object, Math, Render, GameFramework, Component, Serialization, Core, Runtime)
- `KraftonEngine/Source/Editor/` — editor layer (UI widgets, viewport, selection, settings)
- `KraftonEngine/Source/ObjViewer/` — standalone mesh viewer (ObjViewerEngine, Panel, RenderPipeline, ViewportClient)
- `KraftonEngine/Shaders/` — 8 HLSL files + `Common/` subdirectory (`ConstantBuffers.hlsl`, `Functions.hlsl`, `VertexLayouts.hlsl`)
- `KraftonEngine/ThirdParty/` — ImGui and SimpleJSON (vendored)
- `KraftonEngine/Asset/` — font atlas, particle textures, default scene, MeshCache (prebuilt .bin meshes), StaticMesh
- `KraftonEngine/Data/` — mesh source files (.obj, .mtl, textures) organized by model name
