local InGameState = {}
InGameState.__index = InGameState

local DEFAULT_SCENE_PATH = "Content/Scene/InGame.Scene"
local HUD = {
    name = "InGameHUD",
    path = "Content/UI/InGameHUD.rml",
    z_order = 10,
    mode = "InGame"
}

local function log(message)
    if Debug and Debug.Log then
        Debug.Log("[InGameState] " .. message)
    else
        print("[InGameState] " .. message)
    end
end

local function is_current_scene(path)
    if Scene ~= nil and Scene.IsCurrent ~= nil then
        local ok, value = pcall(function()
            return Scene.IsCurrent(path)
        end)
        if ok then
            return value == true
        end
    end

    if Scene == nil or Scene.GetCurrentPath == nil then
        return false
    end
    local current = string.lower(string.gsub(tostring(Scene.GetCurrentPath()), "\\", "/"))
    local target = string.lower(string.gsub(tostring(path), "\\", "/"))
    return current == target or string.sub(current, -string.len(target)) == target
end

local function transition_to_scene(path)
    if Scene == nil or path == nil or path == "" then
        log("transition skipped: Scene API or path unavailable")
        return false
    end
    if Scene.TransitionTo ~= nil then
        log("Scene.TransitionTo begin path=" .. tostring(path))
        local ok = Scene.TransitionTo(path)
        log("Scene.TransitionTo result=" .. tostring(ok) .. " path=" .. tostring(path))
        return ok
    end
    if Scene.Open ~= nil then
        log("Scene.Open fallback begin path=" .. tostring(path))
        Scene.Open(path)
        log("Scene.Open fallback completed path=" .. tostring(path))
        return true
    end
    log("transition skipped: no Scene.TransitionTo/Open function")
    return false
end

function InGameState.new(general)
    return setmetatable({
        general = general
    }, InGameState)
end

function InGameState:GetHUD()
    return HUD
end

function InGameState:GetScenePath(payload)
    payload = payload or {}
    return payload.target_scene or DEFAULT_SCENE_PATH
end

function InGameState:Enter(payload)
    payload = payload or {}
    local scene_path = self:GetScenePath(payload)
    log("Enter reason=" .. tostring(payload.reason) .. " target_scene=" .. tostring(scene_path))
    if scene_path ~= nil and scene_path ~= "" and is_current_scene(scene_path) then
        log("Scene already current; no transition requested")
    else
        log("Scene mismatch on enter; SceneManager owns transition target=" .. tostring(scene_path))
    end
end

function InGameState:Exit()
end

function InGameState:Tick()
end

return InGameState
