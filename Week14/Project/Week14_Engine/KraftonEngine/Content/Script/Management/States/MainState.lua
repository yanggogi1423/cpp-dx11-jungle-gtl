local MainState = {}
MainState.__index = MainState

local SCENE_PATH = "Content/Scene/Main.Scene"
local CAMERA_POINT_TAG = "CameraTag"
local LOOK_DOWN_PITCH = 90.0
local MAIN_BGM_KEY = "MainBGM"
local MAIN_BGM_PATH = "BGM/Main.mp3"
local MAIN_BGM_VOLUME = 1.0
local MAIN_BGM_FADEOUT_SECONDS = 1.0
local HUD = {
    name = "MainHUD",
    path = "Content/UI/MainHUD.rml",
    z_order = 10,
    mode = "Main"
}

local function log(message)
    if Debug and Debug.Log then
        Debug.Log("[MainState] " .. message)
    else
        print("[MainState] " .. message)
    end
end

local function clamp01(value)
    if value == nil then
        return 0.0
    end
    if value < 0.0 then
        return 0.0
    end
    if value > 1.0 then
        return 1.0
    end
    return value
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

local function vector_value(value, upper, lower)
    if value == nil then
        return 0.0
    end
    return value[upper] or value[lower] or 0.0
end

local function vec_lerp(a, b, alpha)
    return Vec3(
        vector_value(a, "X", "x") + (vector_value(b, "X", "x") - vector_value(a, "X", "x")) * alpha,
        vector_value(a, "Y", "y") + (vector_value(b, "Y", "y") - vector_value(a, "Y", "y")) * alpha,
        vector_value(a, "Z", "z") + (vector_value(b, "Z", "z") - vector_value(a, "Z", "z")) * alpha)
end

local function vec_distance(a, b)
    local dx = vector_value(b, "X", "x") - vector_value(a, "X", "x")
    local dy = vector_value(b, "Y", "y") - vector_value(a, "Y", "y")
    local dz = vector_value(b, "Z", "z") - vector_value(a, "Z", "z")
    return math.sqrt(dx * dx + dy * dy + dz * dz)
end

function MainState.new(general)
    return setmetatable({
        general = general,
        camera_actor = nil,
        camera_points = {},
        recent_points = {},
        current_point = nil,
        target_point = nil,
        from_location = nil,
        travel_time = 0.0,
        travel_duration = 1.0,
        travel_speed = 0.45,
        wait_time = 0.0,
        wait_duration = 0.35,
        initialized = false,
        main_start_token = nil,
        main_bgm_loaded = false,
        main_bgm_fading = false
    }, MainState)
end

function MainState:GetHUD()
    return HUD
end

function MainState:GetScenePath(payload)
    payload = payload or {}
    return payload.target_scene or SCENE_PATH
end

function MainState:Enter(payload)
    payload = payload or {}
    local scene_path = self:GetScenePath(payload)
    log("Enter reason=" .. tostring(payload.reason) .. " target_scene=" .. tostring(scene_path))
    if scene_path ~= nil and scene_path ~= "" and is_current_scene(scene_path) then
        log("Scene already current; no transition requested")
    elseif scene_path ~= nil and scene_path ~= "" then
        log("Scene mismatch on enter; SceneManager owns transition target=" .. tostring(scene_path))
    end

    self.main_bgm_fading = false
    self.camera_actor = self:FindCameraActor()
    self.camera_points = {}
    self.recent_points = {}
    self.current_point = nil
    self.target_point = nil
    self.from_location = nil
    self.travel_time = 0.0
    self.wait_time = 0.0
    self.initialized = true

    self:CollectCameraPoints()
    log("camera tour init camera=" .. tostring(self.camera_actor ~= nil) ..
        " points=" .. tostring(#self.camera_points))

    if self.camera_actor ~= nil and CameraManager ~= nil then
        local camera = nil
        if self.camera_actor.GetCamera ~= nil then
            camera = self.camera_actor:GetCamera()
        end

        self:ApplyLookDown(camera)
        if CameraManager.PossessCamera ~= nil and camera ~= nil then
            CameraManager.PossessCamera(camera)
        end
        if CameraManager.SetViewTargetWithBlend ~= nil then
            CameraManager.SetViewTargetWithBlend(self.camera_actor, 0.0)
        end
    end

    if #self.camera_points > 0 then
        self:PickNextTarget()
    end

    self:SubscribeMainStart()
    self:PlayMainBGM()
end

function MainState:Exit()
    self:UnsubscribeMainStart()
    self:FadeOutMainBGM()
    self.camera_actor = nil
    self.camera_points = {}
    self.recent_points = {}
    self.current_point = nil
    self.target_point = nil
    self.from_location = nil
    self.travel_time = 0.0
    self.wait_time = 0.0
    self.initialized = false
end

function MainState:GetManagedAudio()
    if self.general ~= nil and self.general.managers ~= nil then
        return self.general.managers.Audio
    end
    return nil
end

function MainState:LoadMainBGM()
    if self.main_bgm_loaded then
        return true
    end

    local managed_audio = self:GetManagedAudio()
    if managed_audio ~= nil and managed_audio.Load ~= nil then
        self.main_bgm_loaded = managed_audio:Load(MAIN_BGM_KEY, MAIN_BGM_PATH, true) == true
        return self.main_bgm_loaded
    end

    if AudioManager ~= nil and AudioManager.Load ~= nil then
        self.main_bgm_loaded = AudioManager.Load(MAIN_BGM_KEY, MAIN_BGM_PATH, true) == true
        return self.main_bgm_loaded
    end

    return false
end

function MainState:PlayMainBGM()
    if not self:LoadMainBGM() then
        log("Main BGM load failed path=" .. MAIN_BGM_PATH)
        return
    end

    if self.general ~= nil and self.general.PlayBGM ~= nil then
        self.general:PlayBGM(MAIN_BGM_KEY, MAIN_BGM_VOLUME)
        return
    end

    if AudioManager ~= nil and AudioManager.PlayBGM ~= nil then
        AudioManager.PlayBGM(MAIN_BGM_KEY, MAIN_BGM_VOLUME)
    end
end

function MainState:FadeOutMainBGM()
    if self.main_bgm_fading then
        return
    end

    self.main_bgm_fading = true
    if self.general ~= nil and self.general.FadeOutBGM ~= nil then
        self.general:FadeOutBGM(MAIN_BGM_FADEOUT_SECONDS)
        return
    end

    if AudioManager ~= nil and AudioManager.FadeOutBGM ~= nil then
        AudioManager.FadeOutBGM(MAIN_BGM_FADEOUT_SECONDS)
    end
end

function MainState:SubscribeMainStart()
    if self.main_start_token ~= nil or self.general == nil or self.general.Subscribe == nil then
        return
    end

    self.main_start_token = self.general:Subscribe("main.game_start_requested", self, function(payload)
        self:FadeOutMainBGM()
    end)
end

function MainState:UnsubscribeMainStart()
    if self.main_start_token == nil or self.general == nil or self.general.Unsubscribe == nil then
        self.main_start_token = nil
        return
    end

    self.general:Unsubscribe(self.main_start_token)
    self.main_start_token = nil
end

function MainState:Tick(dt)
    if not self.initialized then
        self:Enter({})
        return
    end

    if self.camera_actor == nil then
        self.camera_actor = self:FindCameraActor()
        if self.camera_actor == nil then
            return
        end
    end

    if #self.camera_points <= 0 then
        self:CollectCameraPoints()
        if #self.camera_points <= 0 then
            return
        end
    end

    if self.target_point == nil then
        self:PickNextTarget()
        return
    end

    self.travel_time = self.travel_time + dt
    local alpha = clamp01(self.travel_time / self.travel_duration)
    self.camera_actor.Location = vec_lerp(self.from_location, self.target_point.Location, alpha)
    self:ApplyLookDown(nil)

    if alpha >= 1.0 then
        self.wait_time = self.wait_time + dt
        if self.wait_time >= self.wait_duration then
            self:PickNextTarget()
        end
    end
end

function MainState:FindCameraActor()
    if World ~= nil and World.FindFirstActorByTag ~= nil then
        local tagged = World.FindFirstActorByTag("MainCamera")
        if tagged ~= nil and tagged.GetCamera ~= nil and tagged:GetCamera() ~= nil then
            return tagged
        end

        tagged = World.FindFirstActorByTag("CameraRig")
        if tagged ~= nil and tagged.GetCamera ~= nil and tagged:GetCamera() ~= nil then
            return tagged
        end
    end

    if CameraManager ~= nil and CameraManager.GetActiveCameraOwner ~= nil then
        local actor = CameraManager.GetActiveCameraOwner()
        if actor ~= nil and actor.GetCamera ~= nil and actor:GetCamera() ~= nil then
            return actor
        end
    end

    if World ~= nil and World.FindActorsByClass ~= nil then
        local actors = World.FindActorsByClass("AActor")
        if actors ~= nil then
            for _, actor in ipairs(actors) do
                if actor ~= nil and actor.GetCamera ~= nil and actor:GetCamera() ~= nil then
                    return actor
                end
            end
        end
    end

    return nil
end

function MainState:CollectCameraPoints()
    self.camera_points = {}
    if World == nil or World.FindActorsByTag == nil then
        return
    end

    local actors = World.FindActorsByTag(CAMERA_POINT_TAG)
    if actors == nil then
        return
    end

    for _, actor in ipairs(actors) do
        if actor ~= nil then
            table.insert(self.camera_points, actor)
        end
    end
end

function MainState:ApplyLookDown(camera)
    if self.camera_actor == nil then
        return
    end

    self.camera_actor.Rotation = Vec3(0.0, 0.0, 0.0)

    local root = nil
    if self.camera_actor.GetRootComponent ~= nil then
        root = self.camera_actor:GetRootComponent()
    end
    if root ~= nil then
        root.Rotation = Vec3(0.0, 0.0, 0.0)
    end

    if camera == nil and self.camera_actor.GetCamera ~= nil then
        camera = self.camera_actor:GetCamera()
    end
    if camera ~= nil then
        camera.Rotation = Vec3(0.0, LOOK_DOWN_PITCH, 0.0)
    end
end

function MainState:IsRecentPoint(candidate)
    for _, actor in ipairs(self.recent_points) do
        if actor == candidate then
            return true
        end
    end
    return false
end

function MainState:RememberPoint(point)
    if point == nil then
        return
    end

    table.insert(self.recent_points, 1, point)
    while #self.recent_points > 3 do
        table.remove(self.recent_points)
    end
end

function MainState:ChoosePoint()
    if #self.camera_points <= 0 then
        return nil
    end

    local candidates = {}
    for _, point in ipairs(self.camera_points) do
        if point ~= self.current_point and not self:IsRecentPoint(point) then
            table.insert(candidates, point)
        end
    end

    if #candidates <= 0 then
        for _, point in ipairs(self.camera_points) do
            if point ~= self.current_point then
                table.insert(candidates, point)
            end
        end
    end

    if #candidates <= 0 then
        return self.camera_points[1]
    end

    return candidates[math.random(1, #candidates)]
end

function MainState:PickNextTarget()
    if self.camera_actor == nil then
        return
    end

    local next_target = self:ChoosePoint()
    if next_target == nil then
        return
    end

    if self.target_point ~= nil then
        self:RememberPoint(self.target_point)
        self.current_point = self.target_point
    end

    self.target_point = next_target
    self.from_location = self.camera_actor.Location
    self.travel_time = 0.0
    self.wait_time = 0.0

    local distance = vec_distance(self.from_location, next_target.Location)
    self.travel_duration = math.max(0.001, distance / self.travel_speed)
end

return MainState
