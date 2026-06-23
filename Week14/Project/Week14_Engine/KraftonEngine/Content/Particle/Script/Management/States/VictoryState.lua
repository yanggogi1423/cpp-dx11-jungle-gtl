local VictoryState = {}
VictoryState.__index = VictoryState

local SCENE_PATH = "Content/Scene/Victory.Scene"
local BOMBER_MESH = "Content/Data/Bomber/Bomber_StaticMesh.uasset"
local BOMB_MESH = "Content/Data/Bomber/Bomb_StaticMesh.uasset"
local BOMBER_IDLE_SFX = "SFX/Bomber/BomberIdle.mp3"
local BOMB_SFX = {
    "SFX/Bomber/Bomb1.mp3",
    "SFX/Bomber/Bomb2.mp3",
    "SFX/Bomber/Bomb3.mp3"
}
local BOMB_EXPLOSION_PARTICLE = "Content/Particle System/Explosion.uasset"
local RESULT_HUD = {
    name = "ResultHUD",
    path = "Content/UI/ResultHUD.rml",
    z_order = 40,
    mode = "Result",
    payload = {
        result = "Victory",
        result_radio_only = true
    }
}
local VICTORY_BGM_KEY = "VictoryBGM"
local VICTORY_BGM_PATH = "BGM/Victory.mp3"
local VICTORY_BGM_FALLBACK_PATH = "BGM/Ending.mp3"

local PLANE_SPEED = 33.75
local PLANE_MAX_FLIGHT_TIME = 30.0
local PLANE_STOP_DISTANCE = 2.0
local PLANE_BOB_HEIGHT = 2.0
local CAM1_INITIAL_FOV = 1.047198
local CAM1_ZOOM_FOV = 0.28
local CAM1_ZOOM_TIME = 0.5
local CAM1_STABILIZE_TIME = 6.0
local CAM1_FALLBACK_SECONDS = 8.0
local FADE_SECONDS = 0.34
local FADE_HOLD_SECONDS = 0.12
local BOMB_TO_CAM3_DELAY = 2.0
local CAM2_REAR_DISTANCE = 22.0
local CAM2_HEIGHT = 4.0
local BOMB_DROP_DISTANCE_FROM_ORIGIN = 140.0
local BOMB_COUNT = 30
local BOMB_DROP_DURATION = 2.0
local BOMB_SCALE = 1.0
local BOMB_GROUND_HOLD_SECONDS = 5.0
local BOMB_EXPLOSION_SCALE = 20.0
local BOMB_EXPLOSION_Z_OFFSET = 0.8
local BOMB_EXPLOSION_LIFETIME = 4.0
local GRAVITY = -15.0
local VICTORY_AUDIO_START_DELAY = 3.0
local PLANE_AUDIO_FADE_SECONDS = 3.0
local RESULT_HUD_DELAY = 2.0

local function log(message)
    if Debug and Debug.Log then
        Debug.Log("[VictoryState] " .. message)
    else
        print("[VictoryState] " .. message)
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

local function clamp01(value)
    if value == nil or value < 0.0 then
        return 0.0
    end
    if value > 1.0 then
        return 1.0
    end
    return value
end

local function smoothstep(value)
    local t = clamp01(value)
    return t * t * (3.0 - 2.0 * t)
end

local function vector_value(value, upper, lower)
    if value == nil then
        return 0.0
    end
    return value[upper] or value[lower] or 0.0
end

local function vec3(x, y, z)
    return Vec3(x or 0.0, y or 0.0, z or 0.0)
end

local function vec_add(a, b)
    return vec3(
        vector_value(a, "X", "x") + vector_value(b, "X", "x"),
        vector_value(a, "Y", "y") + vector_value(b, "Y", "y"),
        vector_value(a, "Z", "z") + vector_value(b, "Z", "z"))
end

local function vec_sub(a, b)
    return vec3(
        vector_value(a, "X", "x") - vector_value(b, "X", "x"),
        vector_value(a, "Y", "y") - vector_value(b, "Y", "y"),
        vector_value(a, "Z", "z") - vector_value(b, "Z", "z"))
end

local function vec_mul(a, scalar)
    return vec3(
        vector_value(a, "X", "x") * scalar,
        vector_value(a, "Y", "y") * scalar,
        vector_value(a, "Z", "z") * scalar)
end

local function vec_length_xy(a)
    local x = vector_value(a, "X", "x")
    local y = vector_value(a, "Y", "y")
    return math.sqrt(x * x + y * y)
end

local function vec_distance(a, b)
    local dx = vector_value(a, "X", "x") - vector_value(b, "X", "x")
    local dy = vector_value(a, "Y", "y") - vector_value(b, "Y", "y")
    local dz = vector_value(a, "Z", "z") - vector_value(b, "Z", "z")
    return math.sqrt(dx * dx + dy * dy + dz * dz)
end

local function rotation_to_target(from, target)
    local dx = vector_value(target, "X", "x") - vector_value(from, "X", "x")
    local dy = vector_value(target, "Y", "y") - vector_value(from, "Y", "y")
    local dz = vector_value(target, "Z", "z") - vector_value(from, "Z", "z")
    local distance = math.sqrt(dx * dx + dy * dy + dz * dz)
    if distance <= 0.0001 then
        return vec3(0.0, 0.0, 0.0)
    end

    local yaw = math.atan2(dy, dx) * 180.0 / math.pi
    local pitch = -math.asin(dz / distance) * 180.0 / math.pi
    return vec3(pitch, yaw, 0.0)
end

local function normalize_xy(a)
    local length = vec_length_xy(a)
    if length <= 0.0001 then
        return vec3(-1.0, 0.0, 0.0)
    end
    return vec3(vector_value(a, "X", "x") / length, vector_value(a, "Y", "y") / length, 0.0)
end

local function actor_is_valid(actor)
    return actor ~= nil and (actor.IsValid == nil or actor:IsValid())
end

local function actor_has_mesh(actor, mesh_path)
    if actor == nil or actor.GetStaticMeshComponent == nil then
        return false
    end

    local mesh = actor:GetStaticMeshComponent()
    return mesh ~= nil and mesh.GetMeshPath ~= nil and tostring(mesh:GetMeshPath()) == mesh_path
end

local function find_actor_by_tag(tag)
    if World ~= nil and World.FindFirstActorByTag ~= nil then
        local actor = World.FindFirstActorByTag(tag)
        if actor_is_valid(actor) then
            return actor
        end
    end
    return nil
end

local function get_camera(actor)
    if actor ~= nil and actor.GetCamera ~= nil then
        return actor:GetCamera()
    end
    return nil
end

local function set_camera_fov(actor, fov)
    local camera = get_camera(actor)
    if camera ~= nil and camera.SetFOV ~= nil then
        camera:SetFOV(fov)
    end
end

local function look_at(actor, target)
    if actor == nil or target == nil then
        return
    end

    local camera = get_camera(actor)
    if camera ~= nil then
        actor.Rotation = vec3(0.0, 0.0, 0.0)
        camera.Rotation = vec3(0.0, 0.0, 0.0)
        if camera.LookAt ~= nil then
            camera:LookAt(target)
        end
    else
        actor.Rotation = rotation_to_target(actor.Location, target)
    end
end

local function set_view_target(actor, blend_time)
    if actor == nil or CameraManager == nil then
        return
    end

    local camera = get_camera(actor)
    local time = blend_time or 0.0
    if CameraManager.SetViewTargetWithBlend ~= nil then
        CameraManager.SetViewTargetWithBlend(actor, time)
        return
    end
    if CameraManager.SetViewTarget ~= nil then
        CameraManager.SetViewTarget(actor)
        return
    end
    if CameraManager.SetActiveCameraWithBlend ~= nil and camera ~= nil then
        CameraManager.SetActiveCameraWithBlend(camera, time)
        return
    end
    if CameraManager.Possess ~= nil and camera ~= nil then
        CameraManager.Possess(camera)
    end
end

local function start_fade(from_alpha, to_alpha, duration, hold)
    if CameraManager ~= nil and CameraManager.StartCameraFade ~= nil then
        CameraManager.StartCameraFade(from_alpha, to_alpha, duration, hold == true)
    end
end

local function clear_camera_effects()
    if CameraManager ~= nil then
        if CameraManager.ClearDepthOfField ~= nil then
            CameraManager.ClearDepthOfField()
        end
        if CameraManager.StopAllCameraShakes ~= nil then
            CameraManager.StopAllCameraShakes(true)
        end
        if CameraManager.StopCameraFade ~= nil then
            CameraManager.StopCameraFade()
        end
    end
end

function VictoryState.new(general)
    return setmetatable({
        general = general,
        initialized = false,
        pending_scene = false,
        phase = "loading",
        phase_time = 0.0,
        elapsed = 0.0,
        plane = nil,
        plane_start = nil,
        plane_base_z = 0.0,
        plane_direction = vec3(-1.0, 0.0, 0.0),
        plane_rotation = nil,
        plane_origin_reached = false,
        plane_origin_trigger_location = nil,
        plane_sound = nil,
        plane_sound_start_volume = 1.0,
        cam1 = nil,
        cam2 = nil,
        cam3 = nil,
        cam1_base_location = nil,
        cam2_initial_offset = nil,
        cam2_switch_x = nil,
        fade_target_phase = nil,
        fade_target_actor = nil,
        fade_swapped = false,
        bomb_focus_location = nil,
        bomb_drop_anchor_location = nil,
        bomb_drop_elapsed = 0.0,
        bombs = {},
        pending_bombs = {},
        explosion_particles = {},
        bombs_spawned = false,
        result_hold_elapsed = 0.0,
        result_hud_requested = false,
        victory_audio_started = false,
        plane_audio_fading = false,
        plane_audio_fade_elapsed = 0.0
    }, VictoryState)
end

function VictoryState:GetHUD()
    return RESULT_HUD
end

function VictoryState:GetScenePath()
    return SCENE_PATH
end

function VictoryState:Enter(payload)
    payload = payload or {}
    payload.result = payload.result or "Victory"
    payload.result_radio_only = true
    self:Reset()
    log("Enter reason=" .. tostring(payload.reason) .. " target_scene=" .. SCENE_PATH)
    if is_current_scene(SCENE_PATH) then
        self.pending_scene = false
        log("Scene already current; no transition requested")
    else
        self.pending_scene = true
        log("Scene mismatch on enter; SceneManager owns transition target=" .. tostring(SCENE_PATH))
    end
end

function VictoryState:Exit()
    self:CleanupBombs()
    clear_camera_effects()
    self:Reset()
end

function VictoryState:Reset()
    self.initialized = false
    self.pending_scene = false
    self.phase = "loading"
    self.phase_time = 0.0
    self.elapsed = 0.0
    self.plane = nil
    self.plane_start = nil
    self.plane_base_z = 0.0
    self.plane_direction = vec3(-1.0, 0.0, 0.0)
    self.plane_rotation = nil
    self.plane_origin_reached = false
    self.plane_origin_trigger_location = nil
    self.plane_sound = nil
    self.plane_sound_start_volume = 1.0
    self.cam1 = nil
    self.cam2 = nil
    self.cam3 = nil
    self.cam1_base_location = nil
    self.cam2_initial_offset = nil
    self.cam2_switch_x = nil
    self.fade_target_phase = nil
    self.fade_target_actor = nil
    self.fade_swapped = false
    self.bomb_focus_location = nil
    self.bomb_drop_anchor_location = nil
    self.bomb_drop_elapsed = 0.0
    self.bombs = {}
    self.pending_bombs = {}
    self.explosion_particles = {}
    self.bombs_spawned = false
    self.result_hold_elapsed = 0.0
    self.result_hud_requested = false
    self.victory_audio_started = false
    self.plane_audio_fading = false
    self.plane_audio_fade_elapsed = 0.0
end

function VictoryState:Tick(dt)
    dt = dt or 0.0
    if not is_current_scene(SCENE_PATH) then
        return
    end

    if not self.initialized then
        self:InitializeSequence()
        return
    end

    self.phase_time = self.phase_time + dt
    self.elapsed = self.elapsed + dt

    if self.phase == "cam1" then
        self:TickPlane(dt, true)
        self:TickCam1()
        if self:ShouldSwitchToCam2() then
            self:BeginFade("cam2", self.cam2)
        end
    elseif self.phase == "fade" then
        self:TickFade()
        self:TickPlane(dt, self.fade_target_phase == "cam2" or self.fade_target_phase == "cam3")
        if self.fade_target_phase == "cam2" then
            if self.fade_swapped then
                self:TickCam2()
            end
        elseif self.fade_target_phase == "cam3" and self.fade_swapped then
            self:TickCam3()
        end
    elseif self.phase == "cam2" then
        self:TickPlane(dt, true)
        self:TickCam2()
        if not self.bombs_spawned and
            (vec_length_xy(self.plane.Location) <= BOMB_DROP_DISTANCE_FROM_ORIGIN or
                self:IsPlaneAtOrigin() or
                self.elapsed >= PLANE_MAX_FLIGHT_TIME) then
            self:QueueBombDrop()
        end
        if self.bombs_spawned then
            self.bomb_drop_elapsed = self.bomb_drop_elapsed + dt
        end
        if self.bombs_spawned and self.bomb_drop_elapsed >= BOMB_TO_CAM3_DELAY then
            self:BeginFade("cam3", self.cam3)
        end
    elseif self.phase == "cam3" then
        self:TickPlane(dt, true)
        self:TickCam3()
        self:TickVictoryAudio(dt)
        if self:IsBombSequenceComplete() then
            self.phase = "cam3_hold"
            self.result_hold_elapsed = 0.0
            set_view_target(self.cam3, 0.0)
            log("victory sequence complete; holding on Cam3")
        end
    elseif self.phase == "cam3_hold" then
        self:TickVictoryAudio(dt)
        self.result_hold_elapsed = self.result_hold_elapsed + dt
        if not self.result_hud_requested and self.result_hold_elapsed >= RESULT_HUD_DELAY then
            self:ShowResultHUD()
        end
    end

    self:TickBombs(dt)
end

function VictoryState:InitializeSequence()
    self.plane = self:FindBomber()
    self.cam1 = find_actor_by_tag("Cam1")
    self.cam2 = find_actor_by_tag("Cam2")
    self.cam3 = find_actor_by_tag("Cam3")

    if self.plane == nil or self.cam1 == nil or self.cam2 == nil or self.cam3 == nil then
        log("waiting actors plane=" .. tostring(self.plane ~= nil) ..
            " cam1=" .. tostring(self.cam1 ~= nil) ..
            " cam2=" .. tostring(self.cam2 ~= nil) ..
            " cam3=" .. tostring(self.cam3 ~= nil))
        return
    end

    self.plane_start = self.plane.Location
    self.plane_base_z = vector_value(self.plane_start, "Z", "z")
    self.plane_rotation = self.plane.Rotation
    self.plane_direction = normalize_xy(vec_sub(vec3(0.0, 0.0, self.plane_base_z), self.plane_start))
    self.cam1_base_location = self.cam1.Location
    self.cam2_initial_offset = vec_sub(self.cam2.Location, self.plane.Location)
    self.cam2_switch_x = vector_value(self.cam2.Location, "X", "x")
    self.initialized = true
    self.phase = "cam1"
    self.phase_time = 0.0
    self.elapsed = 0.0

    set_camera_fov(self.cam1, CAM1_INITIAL_FOV)
    set_camera_fov(self.cam2, 1.05)
    set_camera_fov(self.cam3, 0.85)
    set_view_target(self.cam1, 0.0)
    self:ConfigurePlaneSound()
    self:TickCam1()
    log("sequence initialized speed=" .. tostring(PLANE_SPEED) ..
        " idle_sfx=" .. BOMBER_IDLE_SFX ..
        " attenuation=120..3500")
end

function VictoryState:FindBomber()
    if World ~= nil and World.FindActorsByClass ~= nil then
        local actors = World.FindActorsByClass("AStaticMeshActor")
        if actors ~= nil then
            for _, actor in ipairs(actors) do
                if actor_has_mesh(actor, BOMBER_MESH) then
                    return actor
                end
            end
        end
    end

    if World ~= nil and World.FindActorByName ~= nil then
        local by_name = World.FindActorByName("AStaticMeshActor_10")
        if actor_is_valid(by_name) and actor_has_mesh(by_name, BOMBER_MESH) then
            return by_name
        end
        by_name = World.FindActorByName("AStaticMeshActor_7")
        if actor_is_valid(by_name) and actor_has_mesh(by_name, BOMBER_MESH) then
            return by_name
        end
    end
    return nil
end

function VictoryState:ConfigurePlaneSound()
    if self.plane == nil or self.plane.GetSoundComponent == nil then
        log("BombIdle sound component not found on bomber")
        return
    end

    local sound = self.plane:GetSoundComponent()
    if sound == nil then
        log("BombIdle sound component not found on bomber")
        return
    end
    self.plane_sound = sound

    if sound.SetSoundPath ~= nil then
        sound:SetSoundPath(BOMBER_IDLE_SFX)
    end
    if sound.SetLooping ~= nil then
        sound:SetLooping(true)
    end
    if sound.SetSpatialized ~= nil then
        sound:SetSpatialized(true)
    end
    if sound.Set3DMinMaxDistance ~= nil then
        sound:Set3DMinMaxDistance(120.0, 3500.0)
    end
    if sound.SetVolume ~= nil then
        sound:SetVolume(1.0)
    end
    if sound.Play ~= nil then
        sound:Play()
    end

    local path = BOMBER_IDLE_SFX
    if sound.GetSoundPath ~= nil then
        path = tostring(sound:GetSoundPath())
    end
    local min_distance = 120.0
    local max_distance = 3500.0
    if sound.GetMinDistance ~= nil then
        min_distance = sound:GetMinDistance()
    end
    if sound.GetMaxDistance ~= nil then
        max_distance = sound:GetMaxDistance()
    end
    if sound.GetVolume ~= nil then
        self.plane_sound_start_volume = sound:GetVolume()
    end
    log("BombIdle configured path=" .. path ..
        " spatialized=true min=" .. tostring(min_distance) ..
        " max=" .. tostring(max_distance))
end

function VictoryState:LoadVictoryBGM()
    local loaded = false
    if AudioManager ~= nil and AudioManager.Load ~= nil then
        loaded = AudioManager.Load(VICTORY_BGM_KEY, VICTORY_BGM_PATH, false) == true
        if not loaded then
            log("Victory BGM primary load failed path=" .. VICTORY_BGM_PATH ..
                "; fallback=" .. VICTORY_BGM_FALLBACK_PATH)
            loaded = AudioManager.Load(VICTORY_BGM_KEY, VICTORY_BGM_FALLBACK_PATH, false) == true
        end
    end
    return loaded
end

function VictoryState:StartVictoryAudio()
    if self.victory_audio_started then
        return
    end

    self.victory_audio_started = true
    self.plane_audio_fading = true
    self.plane_audio_fade_elapsed = 0.0
    if self.plane_sound ~= nil and self.plane_sound.GetVolume ~= nil then
        self.plane_sound_start_volume = self.plane_sound:GetVolume()
    end

    local loaded = self:LoadVictoryBGM()
    if AudioManager ~= nil and AudioManager.PlayBGM ~= nil then
        AudioManager.PlayBGM(VICTORY_BGM_KEY, 1.0)
    end

    log("victory audio started loaded=" .. tostring(loaded) ..
        " plane_fade_seconds=" .. tostring(PLANE_AUDIO_FADE_SECONDS))
end

function VictoryState:TickVictoryAudio(dt)
    if self.phase_time >= VICTORY_AUDIO_START_DELAY then
        self:StartVictoryAudio()
    end

    if not self.plane_audio_fading or self.plane_sound == nil or self.plane_sound.SetVolume == nil then
        return
    end

    self.plane_audio_fade_elapsed = self.plane_audio_fade_elapsed + (dt or 0.0)
    local alpha = clamp01(self.plane_audio_fade_elapsed / PLANE_AUDIO_FADE_SECONDS)
    local volume = (self.plane_sound_start_volume or 1.0) * (1.0 - alpha)
    self.plane_sound:SetVolume(volume)
    if alpha >= 1.0 then
        self.plane_audio_fading = false
    end
end

function VictoryState:TickPlane(dt, allow_move)
    if self.plane == nil or not allow_move then
        return
    end

    local current = self.plane.Location
    local current_xy = vec3(vector_value(current, "X", "x"), vector_value(current, "Y", "y"), self.plane_base_z)
    local to_origin = vec_sub(vec3(0.0, 0.0, self.plane_base_z), current_xy)
    local remaining = vec_length_xy(to_origin)
    local next_xy = current_xy

    if remaining <= PLANE_STOP_DISTANCE and not self.plane_origin_reached then
        self.plane_origin_reached = true
        self.plane_origin_trigger_location = vec3(0.0, 0.0, self.plane_base_z)
        log("plane reached origin; continuing fly-through")
    end

    if self.plane_origin_reached then
        next_xy = vec_add(current_xy, vec_mul(self.plane_direction, PLANE_SPEED * dt))
    elseif self.elapsed < PLANE_MAX_FLIGHT_TIME then
        local direction = normalize_xy(to_origin)
        self.plane_direction = direction
        local move_distance = math.min(remaining, PLANE_SPEED * dt)
        next_xy = vec_add(current_xy, vec_mul(direction, move_distance))
    end

    local bob = math.sin(self.elapsed * 1.7) * PLANE_BOB_HEIGHT +
        math.sin(self.elapsed * 3.4) * (PLANE_BOB_HEIGHT * 0.25)
    self.plane.Location = vec3(
        vector_value(next_xy, "X", "x"),
        vector_value(next_xy, "Y", "y"),
        self.plane_base_z + bob)
    if self.plane_rotation ~= nil then
        self.plane.Rotation = self.plane_rotation
    end
end

function VictoryState:TickCam1()
    if self.cam1 == nil or self.plane == nil then
        return
    end

    local zoom_alpha = smoothstep(self.phase_time / CAM1_ZOOM_TIME)
    local stable = smoothstep(self.phase_time / CAM1_STABILIZE_TIME)
    set_camera_fov(self.cam1, CAM1_INITIAL_FOV + (CAM1_ZOOM_FOV - CAM1_INITIAL_FOV) * zoom_alpha)

    local shake_scale = 1.4 + (1.0 - stable) * 5.4
    local shake = vec3(
        math.sin(self.elapsed * 1.35) * shake_scale,
        math.cos(self.elapsed * 0.92) * shake_scale * 0.55,
        math.sin(self.elapsed * 1.08) * shake_scale * 0.32)
    self.cam1.Location = vec_add(self.cam1_base_location, shake)
    look_at(self.cam1, self.plane.Location)

    local distance = vec_distance(self.cam1.Location, self.plane.Location)
    if CameraManager ~= nil and CameraManager.SetDepthOfField ~= nil then
        local focus_distance = distance * (0.55 + 0.45 * stable)
        local focus_range = 8.0 + 80.0 * stable
        local blur_radius = 7.0 * (1.0 - stable) + 1.0
        CameraManager.SetDepthOfField(focus_distance, focus_range, blur_radius)
    end
end

function VictoryState:TickCam2()
    if self.cam2 == nil or self.plane == nil then
        return
    end

    local offset = vec_add(vec_mul(self.plane_direction, -CAM2_REAR_DISTANCE), vec3(0.0, 0.0, CAM2_HEIGHT))

    self.cam2.Location = vec_add(self.plane.Location, offset)
    look_at(self.cam2, self.plane.Location)
end

function VictoryState:TickCam3()
    if self.cam3 == nil then
        return
    end
    local focus = self:GetBombFocus()
    if self.plane_direction ~= nil then
        local right = vec3(-vector_value(self.plane_direction, "Y", "y"), vector_value(self.plane_direction, "X", "x"), 0.0)
        local camera_location = vec_add(
            focus,
            vec_add(vec_mul(self.plane_direction, -52.0), vec_add(vec_mul(right, 18.0), vec3(0.0, 0.0, 28.0))))
        self.cam3.Location = camera_location
    end
    look_at(self.cam3, vec_add(focus, vec3(0.0, 0.0, 6.0)))
end

function VictoryState:ShouldSwitchToCam2()
    if self.plane == nil or self.cam2_switch_x == nil or self.plane_start == nil then
        return false
    end

    local start_x = vector_value(self.plane_start, "X", "x")
    local current_x = vector_value(self.plane.Location, "X", "x")
    local target_x = self.cam2_switch_x
    local crossed = (start_x - target_x) * (current_x - target_x) <= 0.0
    return crossed or self.phase_time >= CAM1_FALLBACK_SECONDS
end

function VictoryState:IsPlaneAtOrigin()
    if self.plane == nil then
        return false
    end
    return self.plane_origin_reached or vec_length_xy(self.plane.Location) <= PLANE_STOP_DISTANCE
end

function VictoryState:BeginFade(next_phase, next_actor)
    self.phase = "fade"
    self.phase_time = 0.0
    self.fade_target_phase = next_phase
    self.fade_target_actor = next_actor
    self.fade_swapped = false
    start_fade(0.0, 1.0, FADE_SECONDS, true)
end

function VictoryState:TickFade()
    if not self.fade_swapped and self.phase_time >= FADE_SECONDS + FADE_HOLD_SECONDS then
        set_view_target(self.fade_target_actor, 0.0)
        if self.fade_target_phase == "cam2" then
            if CameraManager ~= nil and CameraManager.ClearDepthOfField ~= nil then
                CameraManager.ClearDepthOfField()
            end
            self:TickCam2()
        elseif self.fade_target_phase == "cam3" then
            self:TickCam3()
        end
        start_fade(1.0, 0.0, FADE_SECONDS, false)
        self.fade_swapped = true
    end

    if self.fade_swapped and self.phase_time >= FADE_SECONDS * 2.0 + FADE_HOLD_SECONDS then
        self.phase = self.fade_target_phase or "cam3"
        self.phase_time = 0.0
        self.fade_target_phase = nil
        self.fade_target_actor = nil
        self.fade_swapped = false
    end
end

function VictoryState:QueueBombDrop()
    if self.bombs_spawned then
        return
    end

    self.bombs_spawned = true
    local anchor = (self.plane and self.plane.Location) or self.plane_origin_trigger_location or vec3(0.0, 0.0, self.plane_base_z)
    self.bomb_drop_anchor_location = anchor
    self.bomb_focus_location = vec3(
        vector_value(anchor, "X", "x"),
        vector_value(anchor, "Y", "y"),
        math.max(0.0, vector_value(anchor, "Z", "z") - 12.0))
    self.pending_bombs = {}
    for index = 1, BOMB_COUNT do
        local lane = ((index - 1) % 6) - 2.5
        local row = math.floor((index - 1) / 6)
        local delay_step = 0.0
        if BOMB_COUNT > 1 then
            delay_step = BOMB_DROP_DURATION / (BOMB_COUNT - 1)
        end
        local delay = (index - 1) * delay_step
        local side = lane * 4.2 + math.sin(index * 1.8) * 1.0
        local forward = row * -3.2 + math.cos(index * 0.9) * 2.5
        table.insert(self.pending_bombs, {
            index = index,
            delay = delay,
            side = side,
            forward = forward,
            velocity = vec3(
                vector_value(self.plane_direction, "X", "x") * 10.0 + forward * 0.25,
                vector_value(self.plane_direction, "Y", "y") * 10.0 + side * 0.15,
                -3.0 - row * 0.55),
            sfx = BOMB_SFX[((index - 1) % #BOMB_SFX) + 1]
        })
    end
    log("queued bomb drop count=" .. tostring(BOMB_COUNT) ..
        " duration=" .. tostring(BOMB_DROP_DURATION) ..
        " origin_distance=" .. tostring(vec_length_xy(anchor)) ..
        " focus=(" .. tostring(vector_value(self.bomb_focus_location, "X", "x")) ..
        "," .. tostring(vector_value(self.bomb_focus_location, "Y", "y")) ..
        "," .. tostring(vector_value(self.bomb_focus_location, "Z", "z")) .. ")")
end

function VictoryState:TickBombs(dt)
    if #self.pending_bombs > 0 then
        for index = #self.pending_bombs, 1, -1 do
            local pending = self.pending_bombs[index]
            pending.delay = pending.delay - dt
            if pending.delay <= 0.0 then
                self:SpawnBomb(pending)
                table.remove(self.pending_bombs, index)
            end
        end
    end

    for index = #self.bombs, 1, -1 do
        local bomb = self.bombs[index]
        if not actor_is_valid(bomb.actor) then
            table.remove(self.bombs, index)
        else
            if bomb.landed then
                bomb.landed_time = (bomb.landed_time or 0.0) + dt
                if bomb.landed_time >= BOMB_GROUND_HOLD_SECONDS then
                    if bomb.actor.Destroy ~= nil then
                        bomb.actor:Destroy()
                    end
                    table.remove(self.bombs, index)
                end
            else
                bomb.velocity = vec3(
                    vector_value(bomb.velocity, "X", "x"),
                    vector_value(bomb.velocity, "Y", "y"),
                    vector_value(bomb.velocity, "Z", "z") + GRAVITY * dt)
                local next_location = vec_add(bomb.actor.Location, vec_mul(bomb.velocity, dt))
                if vector_value(next_location, "Z", "z") <= 0.0 then
                    local impact_location = vec3(
                        vector_value(next_location, "X", "x"),
                        vector_value(next_location, "Y", "y"),
                        0.0)
                    bomb.actor.Location = impact_location
                    self:HideBombActor(bomb.actor)
                    bomb.velocity = vec3(0.0, 0.0, 0.0)
                    bomb.landed = true
                    bomb.landed_time = 0.0
                    if AudioManager ~= nil and AudioManager.PlaySFX3D ~= nil and bomb.sfx ~= nil then
                        AudioManager.PlaySFX3D(bomb.sfx, impact_location, 1.0, 80.0, 3000.0)
                    end
                    self:SpawnBombExplosion(impact_location)
                    log("bomb impact index=" .. tostring(bomb.index) ..
                        " destroy_delay=" .. tostring(BOMB_GROUND_HOLD_SECONDS))
                else
                    bomb.actor.Location = next_location
                end
            end
        end
    end

    self:TickExplosionParticles(dt)
end

function VictoryState:GetBombFocus()
    if #self.bombs > 0 then
        local sum = vec3(0.0, 0.0, 0.0)
        local count = 0
        for _, bomb in ipairs(self.bombs) do
            if actor_is_valid(bomb.actor) then
                sum = vec_add(sum, bomb.actor.Location)
                count = count + 1
            end
        end
        if count > 0 then
            return vec_mul(sum, 1.0 / count)
        end
    end
    if self.bomb_focus_location ~= nil then
        return self.bomb_focus_location
    end
    if self.plane ~= nil then
        return vec3(
            vector_value(self.plane.Location, "X", "x"),
            vector_value(self.plane.Location, "Y", "y"),
            math.max(0.0, vector_value(self.plane.Location, "Z", "z") - 12.0))
    end
    return vec3(0.0, 0.0, 8.0)
end

function VictoryState:IsBombSequenceComplete()
    if not self.bombs_spawned or #self.pending_bombs > 0 or #self.bombs == 0 then
        return false
    end

    for _, bomb in ipairs(self.bombs) do
        if actor_is_valid(bomb.actor) and not bomb.landed then
            return false
        end
    end
    return true
end

function VictoryState:ShowResultHUD()
    self.result_hud_requested = true
    if self.general ~= nil and self.general.Publish ~= nil then
        self.general:Publish("scene.hud_requested", {
            state = "Victory",
            hud = RESULT_HUD,
            payload = {
                result = "Victory",
                result_radio_only = false
            },
            reason = "victory_sequence_complete"
        })
        log("ResultHUD requested after Cam3 hold")
    else
        log("ResultHUD request skipped: GeneralManager.Publish unavailable")
    end
end

function VictoryState:HideBombActor(actor)
    if actor == nil or actor.GetStaticMeshComponent == nil then
        return
    end

    local mesh = actor:GetStaticMeshComponent()
    if mesh ~= nil and mesh.SetVisibility ~= nil then
        mesh:SetVisibility(false)
    end
end

function VictoryState:SpawnBombExplosion(location)
    if location == nil then
        return
    end
    if (Particle == nil or Particle.SpawnEmitterAtLocation == nil) and
        (World == nil or World.SpawnActor == nil) then
        return
    end

    local spawn_location = vec3(
        vector_value(location, "X", "x"),
        vector_value(location, "Y", "y"),
        vector_value(location, "Z", "z") + BOMB_EXPLOSION_Z_OFFSET)
    local scale = vec3(BOMB_EXPLOSION_SCALE, BOMB_EXPLOSION_SCALE, BOMB_EXPLOSION_SCALE)
    local actor = nil
    if Particle ~= nil and Particle.SpawnEmitterAtLocation ~= nil then
        actor = Particle.SpawnEmitterAtLocation(BOMB_EXPLOSION_PARTICLE, spawn_location, vec3(0.0, 0.0, 0.0), scale, true)
    elseif World.SpawnActor ~= nil then
        actor = World.SpawnActor("AParticleSystemActor", spawn_location, vec3(0.0, 0.0, 0.0), scale)
    end
    if actor == nil then
        log("explosion particle spawn failed: " .. BOMB_EXPLOSION_PARTICLE)
        return
    end

    actor.Location = spawn_location
    actor.Scale = scale

    local component = nil
    if actor.GetParticleSystemComponent ~= nil then
        component = actor:GetParticleSystemComponent()
    end
    if component == nil then
        log("explosion particle component missing: " .. BOMB_EXPLOSION_PARTICLE)
        if actor.Destroy ~= nil then
            actor:Destroy()
        end
        return
    end

    if component.SetTemplateByPath ~= nil then
        component:SetTemplateByPath(BOMB_EXPLOSION_PARTICLE)
    end
    if component.ResetParticles ~= nil then
        component:ResetParticles()
    end
    if component.Activate ~= nil then
        component:Activate()
    end

    table.insert(self.explosion_particles, {
        actor = actor,
        age = 0.0,
        lifetime = BOMB_EXPLOSION_LIFETIME
    })
end

function VictoryState:TickExplosionParticles(dt)
    for index = #(self.explosion_particles or {}), 1, -1 do
        local particle = self.explosion_particles[index]
        if not actor_is_valid(particle.actor) then
            table.remove(self.explosion_particles, index)
        else
            particle.age = (particle.age or 0.0) + dt
            if particle.age >= (particle.lifetime or BOMB_EXPLOSION_LIFETIME) then
                if particle.actor.Destroy ~= nil then
                    particle.actor:Destroy()
                end
                table.remove(self.explosion_particles, index)
            end
        end
    end
end

function VictoryState:SpawnBomb(pending)
    if World == nil or World.SpawnActor == nil then
        return
    end

    local plane_location = (self.plane and self.plane.Location) or self.bomb_focus_location or vec3(0.0, 0.0, self.plane_base_z)
    local right = vec3(-vector_value(self.plane_direction, "Y", "y"), vector_value(self.plane_direction, "X", "x"), 0.0)
    local spawn_location = vec_add(
        plane_location,
        vec_add(vec_mul(right, pending.side), vec_mul(self.plane_direction, pending.forward)))
    spawn_location = vec3(
        vector_value(spawn_location, "X", "x"),
        vector_value(spawn_location, "Y", "y"),
        vector_value(spawn_location, "Z", "z") - 3.5)

    local actor = World.SpawnActor("AStaticMeshActor", spawn_location, vec3(0.0, 0.0, 0.0), vec3(BOMB_SCALE, BOMB_SCALE, BOMB_SCALE))
    if actor == nil then
        log("bomb spawn failed")
        return
    end

    local initialized_mesh = false
    if actor.InitStaticMeshActor ~= nil then
        initialized_mesh = actor:InitStaticMeshActor(BOMB_MESH) == true
    end

    actor.Location = spawn_location
    actor.Rotation = vec3(0.0, 0.0, 0.0)
    actor.Scale = vec3(BOMB_SCALE, BOMB_SCALE, BOMB_SCALE)

    if actor.GetStaticMeshComponent ~= nil and not initialized_mesh then
        local mesh = actor:GetStaticMeshComponent()
        if mesh ~= nil and mesh.SetStaticMeshByPath ~= nil then
            initialized_mesh = mesh:SetStaticMeshByPath(BOMB_MESH) == true
        end
    end

    table.insert(self.bombs, {
        actor = actor,
        index = pending.index,
        velocity = pending.velocity,
        sfx = pending.sfx,
        landed = false,
        landed_time = 0.0
    })

    log("bomb spawned index=" .. tostring(pending.index) ..
        " active=" .. tostring(#self.bombs) ..
        " scale=" .. tostring(BOMB_SCALE) ..
        " component=" .. tostring(initialized_mesh) ..
        " plane=(" .. tostring(vector_value(plane_location, "X", "x")) ..
        "," .. tostring(vector_value(plane_location, "Y", "y")) ..
        "," .. tostring(vector_value(plane_location, "Z", "z")) ..
        ") spawn=(" .. tostring(vector_value(spawn_location, "X", "x")) ..
        "," .. tostring(vector_value(spawn_location, "Y", "y")) ..
        "," .. tostring(vector_value(spawn_location, "Z", "z")) .. ")")

end

function VictoryState:CleanupBombs()
    for _, bomb in ipairs(self.bombs or {}) do
        if actor_is_valid(bomb.actor) and bomb.actor.Destroy ~= nil then
            bomb.actor:Destroy()
        end
    end
    for _, particle in ipairs(self.explosion_particles or {}) do
        if actor_is_valid(particle.actor) and particle.actor.Destroy ~= nil then
            particle.actor:Destroy()
        end
    end
    self.bombs = {}
    self.pending_bombs = {}
    self.explosion_particles = {}
end

return VictoryState
