local GameState = require("Management/GameState")
local MainState = require("Management/States/MainState")
local PreInGameState = require("Management/States/PreInGameState")
local LoadingState = require("Management/States/LoadingState")
local InGameState = require("Management/States/InGameState")
local VictoryState = require("Management/States/VictoryState")
local DefeatState = require("Management/States/DefeatState")

local SceneManager = {}
SceneManager.__index = SceneManager

local DEFAULT_START_STATE = GameState.Main
local SCENE_TRANSITION_FADE_OUT_SECONDS = 0.18
local SCENE_TRANSITION_HOLD_SECONDS = 0.04
local SCENE_TRANSITION_FADE_IN_SECONDS = 0.22
local SCENE_LOAD_TIMEOUT_SECONDS = 12.0

local function log(message)
    if Debug and Debug.Log then
        Debug.Log("[SceneManager] " .. message)
    else
        print("[SceneManager] " .. message)
    end
end

local function camera_fade_out(duration)
    if CameraManager ~= nil and CameraManager.FadeOut ~= nil then
        CameraManager.FadeOut(duration or SCENE_TRANSITION_FADE_OUT_SECONDS)
    end
end

local function camera_fade_in(duration)
    if CameraManager ~= nil and CameraManager.FadeIn ~= nil then
        CameraManager.FadeIn(duration or SCENE_TRANSITION_FADE_IN_SECONDS)
    end
end

local function camera_clear_transition_fade()
    if CameraManager == nil then
        return
    end

    if CameraManager.StopCameraFade ~= nil then
        CameraManager.StopCameraFade()
        return
    end

    if CameraManager.FadeIn ~= nil then
        CameraManager.FadeIn(0.0)
    end
end

local function scene_exists(path)
    if path == nil or path == "" or Scene == nil or Scene.Exists == nil then
        return true
    end

    local ok, value = pcall(function()
        return Scene.Exists(path)
    end)
    return ok and value == true
end

local function scene_is_current(path)
    if path == nil or path == "" then
        return true
    end

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

local function scene_open_pending()
    if Scene == nil or Scene.IsOpenPending == nil then
        return false
    end

    local ok, value = pcall(function()
        return Scene.IsOpenPending()
    end)
    return ok and value == true
end

local function request_scene(path)
    if path == nil or path == "" then
        return true
    end
    if Scene == nil or Scene.TransitionTo == nil then
        log("scene request failed: Scene.TransitionTo unavailable path=" .. tostring(path))
        return false
    end

    log("Scene.TransitionTo begin path=" .. tostring(path))
    local ok = Scene.TransitionTo(path)
    log("Scene.TransitionTo result=" .. tostring(ok) .. " path=" .. tostring(path))
    return ok == true
end

local function default_huds()
    return {}
end

function SceneManager.new(general)
    local self = setmetatable({
        general = general,
        current = nil,
        pending = nil,
        transition = nil,
        guard = nil,
        start_state = DEFAULT_START_STATE,
        hud_by_state = default_huds(),
        states_by_id = {}
    }, SceneManager)

    self:RegisterState(GameState.Main, MainState.new(general))
    self:RegisterState(GameState.PreInGame, PreInGameState.new(general))
    self:RegisterState(GameState.Loading, LoadingState.new(general))
    self:RegisterState(GameState.InGame, InGameState.new(general))
    self:RegisterState(GameState.Victory, VictoryState.new(general))
    self:RegisterState(GameState.Defeat1, DefeatState.new(general, GameState.Defeat1))
    self:RegisterState(GameState.Defeat2, DefeatState.new(general, GameState.Defeat2))
    return self
end

function SceneManager:Initialize()
    self.current = self:ResolveStartState(self.start_state)

    local scene_path = ""
    if Scene ~= nil and Scene.GetCurrentPath ~= nil then
        scene_path = tostring(Scene.GetCurrentPath())
    end
    log("initialized state=" .. tostring(self.current) .. " scene=" .. scene_path)

    camera_clear_transition_fade()
    log("cleared stale transition camera fade on scene initialize")

    self:EnterState(nil, self.current, { reason = "initial" })
    self.general:Publish("scene.initialized", { state = self.current })
    self:EmitEntered(nil, self.current, { reason = "initial" })
end

function SceneManager:Shutdown()
    self.pending = nil
    self:ExitState(self.current, nil, { reason = "shutdown" })
end

function SceneManager:SetStartState(state)
    if not self:IsRegisteredState(state) then
        log("ignored unavailable start state=" .. tostring(state))
        return false
    end
    self.start_state = state
    return true
end

function SceneManager:RegisterState(state, state_object)
    if not GameState.IsValid(state) then
        print("[SceneManager] invalid registered state: " .. tostring(state))
        return false
    end

    self.states_by_id[state] = state_object
    return true
end

function SceneManager:GetStateObject(state)
    return self.states_by_id[state]
end

function SceneManager:IsRegisteredState(state)
    return GameState.IsValid(state) and self:GetStateObject(state) ~= nil
end

function SceneManager:ResolveStartState(state)
    if self:IsRegisteredState(state) then
        return state
    end

    if state ~= nil then
        log("fallback start state " .. tostring(state) .. " -> " .. tostring(DEFAULT_START_STATE))
    end
    return DEFAULT_START_STATE
end

function SceneManager:EnterState(from, state, payload)
    local state_object = self:GetStateObject(state)
    if state_object ~= nil and state_object.Enter ~= nil then
        state_object:Enter(payload or {}, from)
    end
end

function SceneManager:ExitState(state, next_state, payload)
    local state_object = self:GetStateObject(state)
    if state_object ~= nil and state_object.Exit ~= nil then
        state_object:Exit(payload or {}, next_state)
    end
end

function SceneManager:SetTransitionGuard(callback)
    self.guard = callback
end

function SceneManager:RegisterHUD(state, hud)
    if not GameState.IsValid(state) then
        print("[SceneManager] invalid HUD state: " .. tostring(state))
        return false
    end

    if hud == nil then
        self.hud_by_state[state] = false
    elseif type(hud) == "table" then
        self.hud_by_state[state] = hud
    else
        print("[SceneManager] invalid HUD payload for state: " .. tostring(state))
        return false
    end

    if self.current == state then
        self:PublishHUDForState(state, { reason = "hud_registered" })
    end
    return true
end

function SceneManager:GetState()
    return self.current
end

function SceneManager:GetHUDForState(state)
    local override = self.hud_by_state[state]
    if override ~= nil then
        if override == false then
            return nil
        end
        return override
    end

    local state_object = self:GetStateObject(state)
    if state_object ~= nil and state_object.GetHUD ~= nil then
        return state_object:GetHUD()
    end
    return nil
end

function SceneManager:GetScenePathForState(state, payload)
    local state_object = self:GetStateObject(state)
    if state_object == nil or state_object.GetScenePath == nil then
        return nil
    end

    local ok, path = pcall(function()
        return state_object:GetScenePath(payload or {})
    end)
    if not ok then
        log("GetScenePath failed for state=" .. tostring(state) .. " error=" .. tostring(path))
        return nil
    end
    return path
end

function SceneManager:RequestState(next_state, payload)
    if not self:IsRegisteredState(next_state) then
        print("[SceneManager] unavailable state: " .. tostring(next_state))
        return false
    end

    if self.transition ~= nil then
        self.pending = {
            to = next_state,
            payload = payload or {}
        }
        log("queued transition after active fade " .. tostring(self.current) .. " -> " .. tostring(next_state))
        return true
    end

    self.pending = {
        to = next_state,
        payload = payload or {}
    }
    log("queued transition " .. tostring(self.current) .. " -> " .. tostring(next_state))
    return true
end

function SceneManager:Tick(dt)
    dt = dt or 0.0

    if self.transition ~= nil then
        self:TickTransition(dt)
    end

    if self.transition == nil and self.pending ~= nil then
        local request = self.pending
        self.pending = nil
        self:BeginStateTransition(request.to, request.payload)
    end

    local state_object = self:GetStateObject(self.current)
    if state_object ~= nil and state_object.Tick ~= nil then
        state_object:Tick(dt)
    end
end

function SceneManager:BeginStateTransition(next_state, payload)
    if self.current == next_state then
        self:PublishHUDForState(next_state, payload)
        return true
    end

    if self.guard ~= nil then
        local ok, allowed = pcall(self.guard, self.current, next_state, payload)
        if not ok or allowed == false then
            self.general:Publish("scene.rejected", { from = self.current, to = next_state, payload = payload })
            return false
        end
    end

    self.transition = {
        to = next_state,
        payload = payload or {},
        phase = "fade_out",
        time = 0.0
    }
    self.general:Publish("scene.transition_started", {
        from = self.current,
        to = next_state,
        payload = payload,
        fade_out = SCENE_TRANSITION_FADE_OUT_SECONDS,
        fade_in = SCENE_TRANSITION_FADE_IN_SECONDS
    })
    camera_fade_out(SCENE_TRANSITION_FADE_OUT_SECONDS)
    log("begin fade transition " .. tostring(self.current) .. " -> " .. tostring(next_state))
    return true
end

function SceneManager:TickTransition(dt)
    local transition = self.transition
    if transition == nil then
        return
    end

    transition.time = (transition.time or 0.0) + math.max(0.0, dt or 0.0)
    if transition.phase == "fade_out" then
        if transition.time < SCENE_TRANSITION_FADE_OUT_SECONDS + SCENE_TRANSITION_HOLD_SECONDS then
            return
        end

        transition.target_scene = self:GetScenePathForState(transition.to, transition.payload)
        if transition.target_scene ~= nil and transition.target_scene ~= "" then
            if not scene_exists(transition.target_scene) then
                log("scene transition failed before state apply: missing target_scene=" .. tostring(transition.target_scene))
                self.general:Publish("scene.transition_failed", {
                    from = self.current,
                    to = transition.to,
                    payload = transition.payload,
                    target_scene = transition.target_scene,
                    reason = "missing_scene"
                })
                self.transition = nil
                camera_fade_in(SCENE_TRANSITION_FADE_IN_SECONDS)
                return
            end

            if not scene_is_current(transition.target_scene) then
                if not request_scene(transition.target_scene) then
                    log("scene transition failed before state apply: request rejected target_scene=" .. tostring(transition.target_scene))
                    self.general:Publish("scene.transition_failed", {
                        from = self.current,
                        to = transition.to,
                        payload = transition.payload,
                        target_scene = transition.target_scene,
                        reason = "request_rejected"
                    })
                    self.transition = nil
                    camera_fade_in(SCENE_TRANSITION_FADE_IN_SECONDS)
                    return
                end

                transition.phase = "waiting_scene"
                transition.time = 0.0
                self.general:Publish("scene.load_requested", {
                    from = self.current,
                    to = transition.to,
                    payload = transition.payload,
                    target_scene = transition.target_scene
                })
                return
            end
        end

        self:ApplyStateImmediate(transition.to, transition.payload)
        transition.phase = "fade_in"
        transition.time = 0.0
        camera_fade_in(SCENE_TRANSITION_FADE_IN_SECONDS)
        self.general:Publish("scene.transition_revealing", {
            to = transition.to,
            payload = transition.payload,
            fade_in = SCENE_TRANSITION_FADE_IN_SECONDS
        })
        return
    end

    if transition.phase == "waiting_scene" then
        if scene_is_current(transition.target_scene) and not scene_open_pending() then
            self:ApplyStateImmediate(transition.to, transition.payload)
            transition.phase = "fade_in"
            transition.time = 0.0
            camera_fade_in(SCENE_TRANSITION_FADE_IN_SECONDS)
            self.general:Publish("scene.transition_revealing", {
                to = transition.to,
                payload = transition.payload,
                target_scene = transition.target_scene,
                fade_in = SCENE_TRANSITION_FADE_IN_SECONDS
            })
            return
        end

        if transition.time >= SCENE_LOAD_TIMEOUT_SECONDS then
            log("scene transition timed out before state apply target_scene=" .. tostring(transition.target_scene) ..
                " current=" .. tostring(Scene ~= nil and Scene.GetCurrentPath ~= nil and Scene.GetCurrentPath() or ""))
            self.general:Publish("scene.transition_failed", {
                from = self.current,
                to = transition.to,
                payload = transition.payload,
                target_scene = transition.target_scene,
                reason = "timeout"
            })
            self.transition = nil
            camera_fade_in(SCENE_TRANSITION_FADE_IN_SECONDS)
        end
        return
    end

    if transition.phase == "fade_in" and transition.time >= SCENE_TRANSITION_FADE_IN_SECONDS then
        self.general:Publish("scene.transition_finished", {
            to = transition.to,
            payload = transition.payload
        })
        self.transition = nil
    end
end

function SceneManager:PublishHUDForState(state, payload)
    self.general:Publish("scene.hud_requested", {
        state = state,
        hud = self:GetHUDForState(state),
        payload = payload or {}
    })
end

function SceneManager:EmitEntered(from, next_state, payload)
    self.general:Publish("scene.entered", {
        from = from,
        to = next_state,
        payload = payload,
        settings = payload and payload.settings
    })
    self:PublishHUDForState(next_state, payload)
end

function SceneManager:ApplyStateImmediate(next_state, payload)
    local from = self.current
    if from == next_state then
        self:PublishHUDForState(next_state, payload)
        return true
    end

    self.general:Publish("scene.exiting", { from = from, to = next_state, payload = payload })
    self:ExitState(from, next_state, payload)

    self.current = next_state
    log("apply transition " .. tostring(from) .. " -> " .. tostring(next_state))
    self.general:Publish("scene.changed", { from = from, to = next_state, payload = payload })
    self:EnterState(from, next_state, payload)
    self:EmitEntered(from, next_state, payload)
    return true
end

function SceneManager:ApplyState(next_state, payload)
    return self:BeginStateTransition(next_state, payload)
end

return SceneManager
