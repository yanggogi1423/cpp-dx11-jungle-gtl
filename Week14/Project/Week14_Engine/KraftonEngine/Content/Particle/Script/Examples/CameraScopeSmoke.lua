-- Attach this script to a pawn/player actor.
-- Expected: Right mouse toggles scope zoom, F5 starts a small wave shake.

local scoped = false

local scope_radius = 0.42
local scope_outer_blur = 4.0
local scope_zoom_fov = 0.30
local scope_feather = 0.08
local scope_edge_blur = 1.5
local scope_intensity = 1.0
local scope_look_sensitivity_scale = 0.275
local scope_blend_time = 0.08

local function log(message)
    if Debug and Debug.Log then
        Debug.Log("[CameraScopeSmoke] " .. message)
    else
        print("[CameraScopeSmoke] " .. message)
    end
end

local function set_scope_enabled(enabled)
    scoped = enabled
    if CameraManager and CameraManager.SetScopeZoomEnabled then
        CameraManager.SetScopeZoomEnabled(scoped)
    end
    log("scope=" .. tostring(scoped))
end

function BeginPlay()
    log("BeginPlay " .. obj.UUID)

    if Input and Input.SetInputModeGameOnly then
        Input.SetInputModeGameOnly()
    end
    if Input and Input.SetMouseCaptured then
        Input.SetMouseCaptured(true)
    end

    if CameraManager and CameraManager.SetScopeLensProfile then
        CameraManager.SetScopeLensProfile(
            scope_radius,
            scope_outer_blur,
            scope_zoom_fov,
            scope_feather,
            scope_edge_blur,
            scope_intensity,
            scope_look_sensitivity_scale,
            scope_blend_time
        )
        CameraManager.SetScopeZoomEnabled(false)
    end
end

function EndPlay()
    if CameraManager and CameraManager.ClearScopeLens then
        CameraManager.ClearScopeLens()
    end
    if Input and Input.ReleaseMouseCapture then
        Input.ReleaseMouseCapture()
    end
end

function Tick(dt)
    if Input.GetKeyDown("RightMouseButton") then
        set_scope_enabled(not scoped)
    end

    if Input.GetKeyDown("F5") and CameraManager and CameraManager.StartWaveShake then
        CameraManager.StartWaveShake(0.35)
        log("wave shake")
    end
end
