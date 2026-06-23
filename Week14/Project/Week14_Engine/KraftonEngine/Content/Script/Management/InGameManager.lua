local GameState = require("Management/GameState")

local InGameManager = {}
InGameManager.__index = InGameManager

local ENEMY_KILL_SCORE = 100
local FRIENDLY_KILL_PENALTY = 500
local HEADSHOT_BONUS = 50
local PENETRATION_BONUS = 25
local KILLCAM_TRIGGER_BONUS = 200
local DEFAULT_HIT_SCORE = 5

local PAUSE_CAMERA_NAME = "PauseMenu_Camera"
local PAUSE_RIFLE_NAME = "PauseMenu_Rifle"
local PAUSE_CLOTH_NAME = "PauseMenu_ClothBackdrop"
local PAUSE_CLOTH_NAME_ALIASES = {
    "PauseMenu_ClothBackdrop",
    "PauseMenu_ClothBackUp",
    "PauseMenu_ClothBackup"
}
local PAUSE_CAMERA_TAG = "PauseCamera"
local PAUSE_RIFLE_TAG = "PauseRifle"
local PAUSE_CLOTH_TAG = "PauseCloth"
local PAUSE_FADE_TIME = 0.080
local PAUSE_BLEND_TIME = 0.120
local PAUSE_TOGGLE_KEY_CODE = 81
local DEFAULT_MATCH_DURATION = 420.0
local ALLY_DEFEAT_CHECK_INTERVAL = 0.25
local PAUSE_CLOTH_BASE_WIND_X = 3.2
local PAUSE_CLOTH_GUST_X = 1.15
local PAUSE_CLOTH_SWAY_Y = 0.28
local PAUSE_CLOTH_SWAY_Z = 0.18
local DEBUG_TIME_TO_THREE_MIN_DIALOGUE = 181.0
local DEBUG_TIME_TO_ONE_MIN_DIALOGUE = 61.0
local RESULT_TRANSITION_FADE_OUT_SECONDS = 3.0
local RESULT_TRANSITION_HOLD_SECONDS = 0.05
local RESULT_TRANSITION_FADE_IN_SECONDS = 0.30

local function log(message)
    if Debug and Debug.Log then
        Debug.Log("[InGameManager] " .. message)
    else
        print("[InGameManager] " .. message)
    end
end

local function is_world_paused()
    if Engine ~= nil and Engine.IsPaused ~= nil then
        return Engine.IsPaused() == true
    end
    return false
end

local function set_world_paused(paused)
    if Engine == nil then
        return false
    end
    if paused == true then
        if Engine.PauseGame ~= nil then
            Engine.PauseGame()
            return true
        end
    elseif Engine.ResumeGame ~= nil then
        Engine.ResumeGame()
        return true
    end
    return false
end

local function is_valid_actor(actor)
    if actor == nil then
        return false
    end
    if actor.IsValid ~= nil then
        return actor:IsValid() == true
    end
    return true
end

local function actor_has_tag(actor, tag)
    return is_valid_actor(actor) and tag ~= nil and actor.HasTag ~= nil and actor:HasTag(tag) == true
end

local function actor_name_matches(actor, name)
    if not is_valid_actor(actor) or name == nil then
        return false
    end
    if actor.GetName == nil then
        return false
    end
    local actual = actor:GetName()
    if type(name) == "table" then
        for _, candidate in ipairs(name) do
            if actual == candidate or string.find(actual, candidate, 1, true) == 1 then
                return true
            end
        end
        return false
    end
    return actual == name or string.find(actual, name, 1, true) == 1
end

local function actor_has_camera(actor)
    return is_valid_actor(actor) and actor.GetCamera ~= nil and actor:GetCamera() ~= nil
end

local function actor_has_sequence(actor)
    return is_valid_actor(actor) and actor.GetActorSequenceComponent ~= nil and actor:GetActorSequenceComponent() ~= nil
end

local function actor_has_skeletal_mesh(actor)
    return is_valid_actor(actor) and actor.GetSkeletalMeshComponent ~= nil and actor:GetSkeletalMeshComponent() ~= nil
end

local function find_pause_actor(name, tag, predicate)
    predicate = predicate or is_valid_actor

    if World ~= nil and World.FindActorByName ~= nil then
        if type(name) == "table" then
            for _, candidate in ipairs(name) do
                local actor = World.FindActorByName(candidate)
                if predicate(actor) then
                    return actor
                end
            end
        else
            local actor = World.FindActorByName(name)
            if predicate(actor) then
                return actor
            end
        end
    end

    if World ~= nil and World.FindFirstActorByTag ~= nil then
        local actor = World.FindFirstActorByTag(tag)
        if predicate(actor) then
            return actor
        end
    end

    if World ~= nil and World.FindActorsByTag ~= nil then
        local actors = World.FindActorsByTag(tag)
        if actors ~= nil then
            for _, actor in ipairs(actors) do
                if predicate(actor) then
                    return actor
                end
            end
        end
    end

    if World ~= nil and World.FindActorsByClass ~= nil then
        local actors = World.FindActorsByClass("AActor")
        if actors ~= nil then
            for _, actor in ipairs(actors) do
                if (actor_name_matches(actor, name) or actor_has_tag(actor, tag)) and predicate(actor) then
                    return actor
                end
            end
        end
    end

    return nil
end

local function set_pause_cloth_world_wind(wind_x, wind_y, wind_z)
    if Engine ~= nil and Engine.SetClothWorldWindVelocityXYZ ~= nil then
        Engine.SetClothWorldWindVelocityXYZ(wind_x, wind_y, wind_z)
    end
end

local function clear_pause_cloth_world_wind()
    if Engine ~= nil and Engine.ClearClothWorldWindVelocity ~= nil then
        Engine.ClearClothWorldWindVelocity()
    end
end

local function set_time_dilation(value)
    if Time ~= nil and Time.SetTimeDilation ~= nil then
        Time.SetTimeDilation(value)
    end
end

local function clear_result_transition_effects()
    if CameraManager ~= nil then
        if CameraManager.StopCameraFade ~= nil then
            CameraManager.StopCameraFade()
        end
        if CameraManager.ClearShockWaves ~= nil then
            CameraManager.ClearShockWaves()
        end
    end
    if SniperKillCam ~= nil then
        if SniperKillCam.EnableShockWave ~= nil then
            SniperKillCam.EnableShockWave(false)
        end
        if SniperKillCam.ClearPendingBullets ~= nil then
            SniperKillCam.ClearPendingBullets()
        end
    end
    set_time_dilation(1.0)
    set_world_paused(false)
end

local function get_hit_region_score(hit)
    if hit == nil then
        return DEFAULT_HIT_SCORE
    end

    if SniperHitRegion ~= nil then
        if hit.HitRegion == SniperHitRegion.Head then
            return 50
        end
        if hit.HitRegion == SniperHitRegion.Torso then
            return 20
        end
        if hit.HitRegion == SniperHitRegion.Arm or hit.HitRegion == SniperHitRegion.Leg then
            return 10
        end
    end

    local regionName = hit.HitRegionName or ""
    if regionName == "Head" then
        return 50
    end
    if regionName == "Torso" then
        return 20
    end
    if regionName == "Arm" or regionName == "Leg" then
        return 10
    end
    return DEFAULT_HIT_SCORE
end

local function get_hit_body_name(hit)
    if hit == nil then
        return ""
    end
    if hit.HitBodyName ~= nil and hit.HitBodyName ~= "" then
        return hit.HitBodyName
    end
    if hit.HitBoneNameString ~= nil then
        return hit.HitBoneNameString
    end
    return ""
end

local function get_hit_region_name(hit)
    if hit == nil then
        return "Unknown"
    end
    if hit.HitRegionName ~= nil and hit.HitRegionName ~= "" then
        return hit.HitRegionName
    end
    return "Unknown"
end

local function get_hit_region_display_name(hit)
    if hit == nil then
        return "UNKNOWN"
    end
    if hit.HitRegionDisplayName ~= nil and hit.HitRegionDisplayName ~= "" then
        return hit.HitRegionDisplayName
    end
    return string.upper(get_hit_region_name(hit))
end

local function normalize_team_tag(value)
    if value == nil then
        return ""
    end
    local text = string.lower(tostring(value))
    text = string.gsub(text, "^%s+", "")
    text = string.gsub(text, "%s+$", "")
    return text
end

local FRIENDLY_TEAM_TAGS = {
    ally = true,
    friendly = true,
    player = true,
    bravo = true
}

local ALLY_SURVIVAL_TEAM_TAGS = {
    ally = true,
    friendly = true,
    bravo = true
}

local ENEMY_TEAM_TAGS = {
    enemy = true,
    hostile = true,
    opfor = true
}

local function read_object_team_tag(object)
    if object == nil then
        return nil
    end
    if object.GetTeamTag ~= nil then
        local ok, value = pcall(function()
            return object:GetTeamTag()
        end)
        if ok and value ~= nil and value ~= "" then
            return value
        end
    end
    if object.GetCombatCoverAgentComponent ~= nil then
        local ok, agent = pcall(function()
            return object:GetCombatCoverAgentComponent()
        end)
        if ok and agent ~= nil and agent.GetTeamTag ~= nil then
            local tag_ok, value = pcall(function()
                return agent:GetTeamTag()
            end)
            if tag_ok and value ~= nil and value ~= "" then
                return value
            end
        end
    end
    return nil
end

local function object_has_team_tag(object, tag_map)
    if object == nil then
        return false
    end

    if object.GetTags ~= nil then
        local ok, tags = pcall(function()
            return object:GetTags()
        end)
        if ok and type(tags) == "table" then
            for _, tag in pairs(tags) do
                if tag_map[normalize_team_tag(tag)] == true then
                    return true
                end
            end
        end
    end

    if object.HasTag ~= nil then
        for tag, _ in pairs(tag_map) do
            local ok, has_tag = pcall(function()
                return object:HasTag(tag)
            end)
            if ok and has_tag == true then
                return true
            end
        end
    end

    local team_tag = normalize_team_tag(read_object_team_tag(object))
    return team_tag ~= "" and tag_map[team_tag] == true
end

local function read_agent_bool(agent, method_name, fallback)
    if agent == nil or method_name == nil or agent[method_name] == nil then
        return fallback
    end

    local ok, value = pcall(function()
        return agent[method_name](agent)
    end)
    if not ok or value == nil then
        return fallback
    end
    return value == true
end

local function read_agent_number(agent, method_name)
    if agent == nil or method_name == nil or agent[method_name] == nil then
        return nil
    end

    local ok, value = pcall(function()
        return agent[method_name](agent)
    end)
    if not ok then
        return nil
    end
    return tonumber(value)
end

local function is_ally_survival_agent(agent)
    return object_has_team_tag(agent, ALLY_SURVIVAL_TEAM_TAGS)
end

local function is_agent_alive(agent)
    local alive = read_agent_bool(agent, "IsAlive", nil)
    if alive ~= nil then
        return alive
    end

    local dead = read_agent_bool(agent, "IsDead", nil)
    if dead ~= nil then
        return not dead
    end

    local health = read_agent_number(agent, "GetHealth")
    if health ~= nil then
        return health > 0.0
    end

    local current_hp = read_agent_number(agent, "GetCurrentHP")
    if current_hp ~= nil then
        return current_hp > 0.0
    end

    return true
end

local function get_payload_hit(payload)
    if payload == nil then
        return nil
    end
    if payload.hit ~= nil then
        return payload.hit
    end
    if type(payload.payload) == "table" then
        return payload.payload.hit
    end
    return nil
end

local function get_payload_target(payload, hit)
    if payload == nil then
        return nil
    end
    if payload.target ~= nil then
        return payload.target
    end
    if type(payload.payload) == "table" and payload.payload.target ~= nil then
        return payload.payload.target
    end
    hit = hit or get_payload_hit(payload)
    if hit ~= nil and hit.HitActor ~= nil then
        return hit.HitActor
    end
    return nil
end

local function resolve_friendly_payload(payload)
    if payload == nil then
        return false
    end

    local hit = get_payload_hit(payload)
    local target = get_payload_target(payload, hit)
    if object_has_team_tag(target, ENEMY_TEAM_TAGS) then
        return false
    end
    if object_has_team_tag(target, FRIENDLY_TEAM_TAGS) then
        return true
    end

    if hit ~= nil and hit.HitActor ~= nil and hit.HitActor ~= target then
        if object_has_team_tag(hit.HitActor, ENEMY_TEAM_TAGS) then
            return false
        end
        if object_has_team_tag(hit.HitActor, FRIENDLY_TEAM_TAGS) then
            return true
        end
    end

    if type(payload.payload) == "table" and payload.payload.friendly == true then
        return true
    end
    if hit ~= nil and hit.bFriendlyTarget == true then
        return true
    end
    return payload.friendly == true
end

local function normalize_sniper_payload_friendly(payload)
    local is_friendly = resolve_friendly_payload(payload)
    if payload ~= nil then
        payload.friendly = is_friendly
        if type(payload.payload) == "table" then
            payload.payload.friendly = is_friendly
        end
    end
    local hit = get_payload_hit(payload)
    if hit ~= nil then
        pcall(function()
            hit.bFriendlyTarget = is_friendly
        end)
    end
    return is_friendly
end

local function calculate_hit_score_delta(hit, isFriendly)
    if hit == nil then
        return 0
    end

    local rawScore = tonumber(hit.HitScoreValue)
    local score = 0
    if rawScore ~= nil and rawScore > 0 then
        score = math.floor(rawScore + 0.5)
    else
        local multiplier = tonumber(hit.HitScoreMultiplier or hit.RegionDamageMultiplier) or 1.0
        score = math.floor(get_hit_region_score(hit) * math.max(0.0, multiplier) + 0.5)
    end

    if isFriendly then
        return -score
    end
    return score
end

function InGameManager.new(general)
    return setmetatable({
        general = general,
        running = false,
        timer = 0.0,
        match_duration = DEFAULT_MATCH_DURATION,
        phase = "Idle",
        wave = 0,
        settings = {},
        last_timer_second = -1,
        result_requested = false,
        pending_result = nil,
        sniper_kills = 0,
        friendly_fire_kills = 0,
        ally_total_count = 0,
        ally_alive_count = 0,
        ally_defeat_check_time = 0.0,
        paused = false,
        pause_transition = nil,
        pause_transition_time = 0.0,
        pause_previous_view_target = nil,
        pause_previous_world_paused = false,
        pause_world_pause_captured = false,
        pause_camera = nil,
        pause_rifle = nil,
        pause_rifle_sequence = nil,
        pause_cloth = nil,
        pause_cloth_component = nil,
        pause_cloth_time = 0.0,
        pause_cloth_active = false,
        pause_cloth_warned = false,
        pause_camera_warned = false,
        pending_kill_score_payloads = {}
    }, InGameManager)
end

function InGameManager:Initialize()
    self.general:Subscribe("scene.entered", self, function(payload)
        if payload ~= nil and payload.to == "InGame" then
            self:Start(payload.settings)
        end
    end)

    self.general:Subscribe("scene.exiting", self, function(payload)
        if payload ~= nil and payload.from == "InGame" then
            local reason = payload.reason
            if reason == nil and type(payload.payload) == "table" then
                reason = payload.payload.reason
            end
            self:Stop(reason)
        end
    end)

    self.general:Subscribe("sniper.target_damaged", self, function(payload)
        if not self.running then
            self:EnsureRunningForCurrentState("sniper_target_damaged")
        end
        if not self.running then
            return
        end

        local hit = get_payload_hit(payload)
        local isFriendly = normalize_sniper_payload_friendly(payload)
        local scoreDelta = calculate_hit_score_delta(hit, isFriendly)
        if scoreDelta ~= 0 then
            self.general:AddScore(scoreDelta)
            if hit ~= nil then
                pcall(function()
                    hit.HitScoreValue = math.abs(scoreDelta)
                end)
            end
        end

        self.general:Publish("ingame.sniper_damaged", {
            timer = self.timer,
            wave = self.wave,
            phase = self.phase,
            payload = payload,
            score_delta = scoreDelta,
            total_score = self.general:GetScore(),
            hit_body_name = get_hit_body_name(hit),
            hit_region_name = get_hit_region_name(hit),
            hit_region_display_name = get_hit_region_display_name(hit)
        })

        self.general:Publish("ingame.sniper_hit_scored", {
            timer = self.timer,
            wave = self.wave,
            phase = self.phase,
            hit = hit,
            killed = payload ~= nil and payload.killed == true,
            target = payload ~= nil and payload.target or nil,
            shooter = payload ~= nil and payload.shooter or nil,
            friendly = isFriendly,
            payload = payload,
            score_delta = scoreDelta,
            total_score = self.general:GetScore(),
            hit_body_name = get_hit_body_name(hit),
            hit_region_name = get_hit_region_name(hit),
            hit_region_display_name = get_hit_region_display_name(hit)
        })
    end)

    self.general:Subscribe("sniper.target_killed", self, function(payload)
        if not self.running then
            self:EnsureRunningForCurrentState("sniper_target_killed")
        end
        if not self.running then
            return
        end

        local hit = get_payload_hit(payload)
        local isFriendly = normalize_sniper_payload_friendly(payload)
        local scoreDelta = 0

        if isFriendly then
            self.friendly_fire_kills = self.friendly_fire_kills + 1
            scoreDelta = -FRIENDLY_KILL_PENALTY
        else
            self.sniper_kills = self.sniper_kills + 1
            scoreDelta = ENEMY_KILL_SCORE

            if hit ~= nil and hit.bIsHeadshot == true then
                scoreDelta = scoreDelta + HEADSHOT_BONUS
            end
            if hit ~= nil and hit.HitOutcome == SniperHitOutcome.Penetrated then
                scoreDelta = scoreDelta + PENETRATION_BONUS
            end
        end

        self.general:AddScore(scoreDelta)
        local scorePayload = {
            timer = self.timer,
            wave = self.wave,
            phase = self.phase,
            hit = hit,
            killed = true,
            target = payload ~= nil and payload.target or nil,
            shooter = payload ~= nil and payload.shooter or nil,
            friendly = isFriendly,
            payload = payload,
            score_delta = scoreDelta,
            total_score = self.general:GetScore(),
            sniper_kills = self.sniper_kills,
            friendly_fire_kills = self.friendly_fire_kills,
            hit_body_name = get_hit_body_name(hit),
            hit_region_name = get_hit_region_name(hit),
            hit_region_display_name = get_hit_region_display_name(hit)
        }

        local bulletId = hit ~= nil and tonumber(hit.BulletId) or nil
        if bulletId ~= nil and bulletId ~= 0 and not isFriendly then
            self.pending_kill_score_payloads[bulletId] = scorePayload
        end

        self.general:Publish("ingame.sniper_killed", scorePayload)
        if isFriendly then
            self:CheckAllyDefeatCondition("friendly_killed", true)
        end
    end)

    self.general:Subscribe("sniper.killcam_triggered", self, function(payload)
        local bulletId = payload ~= nil and tonumber(payload.bullet_id) or nil
        if bulletId == nil or bulletId == 0 then
            return
        end

        local scorePayload = self.pending_kill_score_payloads ~= nil and self.pending_kill_score_payloads[bulletId] or nil
        if scorePayload == nil then
            return
        end

        self.pending_kill_score_payloads[bulletId] = nil
        scorePayload.killcam_bonus = KILLCAM_TRIGGER_BONUS
        scorePayload.score_delta = (tonumber(scorePayload.score_delta) or 0) + KILLCAM_TRIGGER_BONUS
        if scorePayload.payload ~= nil then
            scorePayload.payload.killcam_bonus = KILLCAM_TRIGGER_BONUS
            scorePayload.payload.score_delta = scorePayload.score_delta
        end
        self.general:AddScore(KILLCAM_TRIGGER_BONUS)
        scorePayload.total_score = self.general:GetScore()
        scorePayload.bullet_id = bulletId
        self.general:Publish("ingame.sniper_killcam_hit", scorePayload)
    end)

    self.general:Subscribe("ingame.pause_resume_requested", self, function()
        self:BeginResume()
    end)

    self.general:Subscribe("ingame.pause_toggle_requested", self, function(payload)
        self:TogglePause(payload and payload.reason or "ui")
    end)

    self.general:Subscribe("ingame.pause_main_requested", self, function(payload)
        self:GoToMain(payload and payload.reason or "pause_menu")
    end)

    self.general:Subscribe("cutscene.stopped", self, function()
        if self.pending_result ~= nil then
            self:TryCompletePendingResult("cutscene_stopped")
        end
    end)

    self.general:Subscribe("general.initialized", self, function()
        self:EnsureRunningForCurrentState("general_initialized")
    end)
end

function InGameManager:Shutdown()
    self.general:UnsubscribeOwner(self)
end

function InGameManager:Start(settings)
    self.running = true
    self.timer = 0.0
    self.match_duration = self:ResolveMatchDuration(settings)
    self.phase = "Wave"
    self.wave = 1
    self.settings = settings or self.settings or {}
    self.last_timer_second = -1
    self.result_requested = false
    self.pending_result = nil
    self.sniper_kills = 0
    self.friendly_fire_kills = 0
    self.ally_total_count = 0
    self.ally_alive_count = 0
    self.ally_defeat_check_time = 0.0
    self.general:SetScore(0)
    self.paused = false
    self.pause_transition = nil
    self.pause_transition_time = 0.0
    self.pause_previous_view_target = nil
    self.pause_previous_world_paused = false
    self.pause_world_pause_captured = false
    self.pause_camera = nil
    self.pause_rifle = nil
    self.pause_rifle_sequence = nil
    self.pause_cloth = nil
    self.pause_cloth_component = nil
    self.pause_cloth_time = 0.0
    self.pause_cloth_active = false
    self.pause_cloth_warned = false
    self.pause_camera_warned = false
    self.pending_kill_score_payloads = {}
    self.general:Publish("ingame.started", self:GetSnapshot())
    self.general:Publish("ingame.timer", self:GetSnapshot())
    self.general:Publish("ingame.pause_changed", { paused = false, reason = "start" })
end

function InGameManager:Stop(reason)
    if not self.running then
        return
    end

    local snapshot = self:GetSnapshot()
    snapshot.reason = reason
    self:EndPausePresentation(false)
    self:RestorePauseWorld(true)
    self.running = false
    self.phase = "Idle"
    self.pending_result = nil
    self.general:Publish("ingame.stopped", snapshot)
end

function InGameManager:Tick(dt)
    dt = dt or 0.0
    self:EnsureRunningForCurrentState("tick")
    if not self.running then
        if self:IsCurrentStateInGame() and self:IsPauseKeyPressed() then
            log("pause key detected while manager was not running; recovering InGame runtime")
            self:Start(self.settings)
            self:TogglePause("key_recovered")
        end
        return
    end

    self:PollDebugCheatInput()
    self:PollPauseInput()
    self:TickPauseTransition(dt)
    if self.pending_result ~= nil then
        self:TryCompletePendingResult("tick")
        return
    end
    if self.paused or self.pause_transition ~= nil then
        self:UpdatePauseClothWind(dt)
        return
    end

    self.timer = self.timer + dt
    if self:GetRemainingTime() <= 0.0 then
        self.timer = self.match_duration
        self.general:Publish("ingame.timer", self:GetSnapshot())
        self:RequestVictory("air_support_arrived")
        return
    end

    if self:CheckAllyDefeatCondition("tick", false, dt) then
        return
    end

    local second = math.ceil(self:GetRemainingTime())
    if second ~= self.last_timer_second then
        self.last_timer_second = second
        self.general:Publish("ingame.timer", self:GetSnapshot())
    end
end

function InGameManager:ResolveMatchDuration(settings)
    local duration = nil
    if type(settings) == "table" then
        duration = settings.match_duration_seconds or settings.air_support_duration_seconds or settings.duration_seconds
    end
    duration = tonumber(duration) or self.match_duration or DEFAULT_MATCH_DURATION
    if duration <= 0.0 then
        duration = DEFAULT_MATCH_DURATION
    end
    return duration
end

function InGameManager:SetMatchDuration(seconds)
    if self.running then
        return false
    end
    seconds = tonumber(seconds) or DEFAULT_MATCH_DURATION
    self.match_duration = math.max(1.0, seconds)
    return true
end

function InGameManager:GetRemainingTime()
    return math.max(0.0, (self.match_duration or DEFAULT_MATCH_DURATION) - (self.timer or 0.0))
end

function InGameManager:GetAllySurvivalSnapshot()
    local snapshot = {
        available = false,
        total = 0,
        alive = 0
    }

    if Combat == nil or Combat.GetAgents == nil then
        return snapshot
    end

    local ok, agents = pcall(function()
        return Combat.GetAgents()
    end)
    if not ok or agents == nil then
        return snapshot
    end

    snapshot.available = true
    for _, agent in ipairs(agents) do
        if is_ally_survival_agent(agent) then
            snapshot.total = snapshot.total + 1
            if is_agent_alive(agent) then
                snapshot.alive = snapshot.alive + 1
            end
        end
    end

    return snapshot
end

function InGameManager:CheckAllyDefeatCondition(reason, force, dt)
    if self.result_requested == true then
        return false
    end

    if force ~= true then
        self.ally_defeat_check_time = (self.ally_defeat_check_time or 0.0) + math.max(0.0, tonumber(dt) or 0.0)
        if self.ally_defeat_check_time < ALLY_DEFEAT_CHECK_INTERVAL then
            return false
        end
    end
    self.ally_defeat_check_time = 0.0

    local snapshot = self:GetAllySurvivalSnapshot()
    if snapshot.available ~= true or snapshot.total <= 0 then
        return false
    end

    self.ally_total_count = snapshot.total
    self.ally_alive_count = snapshot.alive
    if snapshot.alive > 0 then
        return false
    end

    self.general:Publish("ingame.allies_eliminated", {
        timer = self.timer,
        elapsed_time = self.timer,
        remaining_time = self:GetRemainingTime(),
        total_allies = snapshot.total,
        alive_allies = snapshot.alive,
        reason = reason or "all_allies_eliminated"
    })
    self:RequestDefeat("all_allies_eliminated")
    return true
end

function InGameManager:SetRemainingTime(seconds, reason)
    if not self.running then
        return false
    end

    seconds = tonumber(seconds)
    if seconds == nil then
        return false
    end

    local duration = math.max(1.0, tonumber(self.match_duration) or DEFAULT_MATCH_DURATION)
    local remaining = math.max(0.0, seconds)
    if remaining > duration then
        duration = remaining
        self.match_duration = duration
    end

    self.timer = math.max(0.0, duration - remaining)
    self.last_timer_second = -1
    local snapshot = self:GetSnapshot()
    snapshot.reason = reason or "set_remaining_time"
    self.general:Publish("ingame.timer", snapshot)
    self.general:Publish("ingame.debug_time_warp", snapshot)
    log("debug time warp remaining=" .. tostring(math.ceil(remaining)) .. " reason=" .. tostring(snapshot.reason))
    return true
end

function InGameManager:RequestVictory(reason)
    return self:RequestResult(GameState.Victory, reason or "victory_requested")
end

function InGameManager:RequestDefeat(reason, state)
    return self:RequestResult(state or GameState.Defeat1, reason or "defeat_requested")
end

function InGameManager:RequestResult(state, reason)
    if self.result_requested then
        return false
    end

    if state ~= GameState.Victory and state ~= GameState.Defeat1 and state ~= GameState.Defeat2 then
        state = GameState.Defeat1
    end

    self.result_requested = true
    self.pending_result = {
        state = state,
        reason = reason or "result_requested"
    }
    log("result queued state=" .. tostring(state) .. " reason=" .. tostring(reason))
    return self:TryCompletePendingResult("request")
end

function InGameManager:IsResultTransitionBlocked()
    local cutscene = self.general ~= nil and self.general.managers ~= nil and self.general.managers.CutScene or nil
    if cutscene ~= nil and cutscene.current ~= nil then
        return true, "cutscene"
    end

    if SniperKillCam ~= nil and SniperKillCam.IsPlaying ~= nil then
        local ok, playing = pcall(function()
            return SniperKillCam.IsPlaying()
        end)
        if ok and playing == true then
            return true, "sniper_killcam"
        end
    end

    return false, nil
end

function InGameManager:TryCompletePendingResult(source)
    local pending = self.pending_result
    if pending == nil then
        return false
    end

    local blocked, block_reason = self:IsResultTransitionBlocked()
    if blocked then
        if pending.logged_block_reason ~= block_reason then
            pending.logged_block_reason = block_reason
            log("result transition waiting for " .. tostring(block_reason) ..
                " state=" .. tostring(pending.state) ..
                " source=" .. tostring(source))
        end
        return true
    end

    self.pending_result = nil
    clear_result_transition_effects()
    if self.general ~= nil and self.general.Publish ~= nil then
        self.general:Publish("ingame.result_transition_started", {
            state = pending.state,
            reason = pending.reason,
            fade_out = RESULT_TRANSITION_FADE_OUT_SECONDS
        })
    end
    if pending.state == GameState.Victory then
        return self:CompleteVictory(pending.reason)
    end
    return self:CompleteDefeat(pending.reason, pending.state)
end

function InGameManager:CompleteVictory(reason)
    self.result_requested = true
    local snapshot = self:GetSnapshot()
    snapshot.result = "Victory"
    snapshot.reason = reason or "victory"
    self.general:Publish("ingame.victory_requested", snapshot)
    self.general:Publish("ingame.completed", snapshot)

    if self.general ~= nil and self.general.SetTempRun ~= nil then
        self.general:SetTempRun("Victory", self.general.GetScore ~= nil and self.general:GetScore() or 0)
    end

    self:EndPausePresentation(false)
    self:RestorePauseWorld(true)
    self.running = false
    self.phase = "Result"
    local scene_manager = self.general ~= nil and self.general.managers ~= nil and self.general.managers.Scene or nil
    if scene_manager ~= nil and scene_manager.IsRegisteredState ~= nil and
        scene_manager:IsRegisteredState(GameState.Victory) and
        self.general.RequestState ~= nil then
        return self.general:RequestState(GameState.Victory, {
            reason = snapshot.reason,
            result = "Victory",
            elapsed_time = snapshot.elapsed_time,
            transition_fade_out_seconds = RESULT_TRANSITION_FADE_OUT_SECONDS,
            transition_hold_seconds = RESULT_TRANSITION_HOLD_SECONDS,
            transition_fade_in_seconds = RESULT_TRANSITION_FADE_IN_SECONDS,
            transition_hud_fade = true
        })
    end

    log("victory completed without state transition; Victory state is not registered yet")
    return true
end

function InGameManager:CompleteDefeat(reason, state)
    state = state or GameState.Defeat1
    if state ~= GameState.Defeat1 and state ~= GameState.Defeat2 then
        state = GameState.Defeat1
    end

    self.result_requested = true
    local snapshot = self:GetSnapshot()
    snapshot.result = "Defeat"
    snapshot.reason = reason or "defeat"
    snapshot.defeat_state = state
    self.general:Publish("ingame.defeat_requested", snapshot)
    self.general:Publish("ingame.completed", snapshot)

    if self.general ~= nil and self.general.SetTempRun ~= nil then
        self.general:SetTempRun("Defeat", self.general.GetScore ~= nil and self.general:GetScore() or 0)
    end

    self:EndPausePresentation(false)
    self:RestorePauseWorld(true)
    self.running = false
    self.phase = "Result"
    local scene_manager = self.general ~= nil and self.general.managers ~= nil and self.general.managers.Scene or nil
    if scene_manager ~= nil and scene_manager.IsRegisteredState ~= nil and
        scene_manager:IsRegisteredState(state) and
        self.general.RequestState ~= nil then
        return self.general:RequestState(state, {
            reason = snapshot.reason,
            result = "Defeat",
            elapsed_time = snapshot.elapsed_time,
            transition_fade_out_seconds = RESULT_TRANSITION_FADE_OUT_SECONDS,
            transition_hold_seconds = RESULT_TRANSITION_HOLD_SECONDS,
            transition_fade_in_seconds = RESULT_TRANSITION_FADE_IN_SECONDS,
            transition_hud_fade = true
        })
    end

    log("defeat completed without state transition; Defeat state is not registered yet")
    return true
end

function InGameManager:EnsureRunningForCurrentState(reason)
    if self.running or not self:IsCurrentStateInGame() then
        return
    end
    if self.result_requested == true or self.pending_result ~= nil or self.phase == "Result" then
        return
    end

    log("starting InGame runtime from current state reason=" .. tostring(reason))
    self:Start(self.settings)
end

function InGameManager:PollDebugCheatInput()
    if Input == nil or Input.GetKeyDown == nil then
        return
    end

    if Input.GetKeyDown("F1") == true then
        self:SetRemainingTime(DEBUG_TIME_TO_THREE_MIN_DIALOGUE, "debug_f1_three_minute_dialogue")
    elseif Input.GetKeyDown("F2") == true then
        self:SetRemainingTime(DEBUG_TIME_TO_ONE_MIN_DIALOGUE, "debug_f2_one_minute_dialogue")
    end
end

function InGameManager:PollPauseInput()
    if self:IsPauseKeyPressed() then
        self:TogglePause("key")
    end
end

function InGameManager:IsPauseKeyPressed()
    if Input == nil then
        return false
    end

    if Input.WasPausePressed ~= nil then
        return Input.WasPausePressed() == true
    end

    if Input.GetKeyDown == nil then
        return false
    end

    if Input.GetKeyDown("Q") then
        return true
    end
    return Input.GetKeyDown(PAUSE_TOGGLE_KEY_CODE)
end

function InGameManager:IsCurrentStateInGame()
    if self.general == nil or self.general.GetState == nil then
        return false
    end
    return self.general:GetState() == GameState.InGame
end

function InGameManager:FindPauseCamera()
    if is_valid_actor(self.pause_camera) and actor_has_camera(self.pause_camera) then
        return self.pause_camera
    end
    self.pause_camera = find_pause_actor(PAUSE_CAMERA_NAME, PAUSE_CAMERA_TAG, actor_has_camera)
    return self.pause_camera
end

function InGameManager:FindPauseRifle()
    if is_valid_actor(self.pause_rifle) then
        return self.pause_rifle
    end
    self.pause_rifle = find_pause_actor(PAUSE_RIFLE_NAME, PAUSE_RIFLE_TAG, actor_has_sequence)
    if self.pause_rifle == nil then
        self.pause_rifle = find_pause_actor(PAUSE_RIFLE_NAME, PAUSE_RIFLE_TAG, is_valid_actor)
    end
    return self.pause_rifle
end

function InGameManager:GetPauseRifleSequence()
    if self.pause_rifle_sequence ~= nil then
        return self.pause_rifle_sequence
    end
    local rifle = self:FindPauseRifle()
    if rifle ~= nil and rifle.GetActorSequenceComponent ~= nil then
        self.pause_rifle_sequence = rifle:GetActorSequenceComponent()
    end
    return self.pause_rifle_sequence
end

function InGameManager:FindPauseCloth()
    if is_valid_actor(self.pause_cloth) then
        return self.pause_cloth
    end
    self.pause_cloth = find_pause_actor(PAUSE_CLOTH_NAME_ALIASES, PAUSE_CLOTH_TAG, actor_has_skeletal_mesh)
    if self.pause_cloth == nil then
        self.pause_cloth = find_pause_actor(PAUSE_CLOTH_NAME_ALIASES, PAUSE_CLOTH_TAG, is_valid_actor)
    end
    return self.pause_cloth
end

function InGameManager:GetPauseClothComponent()
    if self.pause_cloth_component ~= nil then
        return self.pause_cloth_component
    end

    local cloth = self:FindPauseCloth()
    if cloth == nil then
        return nil
    end

    if cloth.GetSkeletalMeshComponent ~= nil then
        self.pause_cloth_component = cloth:GetSkeletalMeshComponent()
    end
    if self.pause_cloth_component == nil and cloth.GetRootComponent ~= nil then
        local root = cloth:GetRootComponent()
        if root ~= nil and root.SetClothPreviewWindOverride ~= nil then
            self.pause_cloth_component = root
        end
    end
    return self.pause_cloth_component
end

function InGameManager:SetPauseClothWindEnabled(enabled)
    local comp = self:GetPauseClothComponent()
    if comp == nil then
        if enabled ~= true then
            clear_pause_cloth_world_wind()
        end
        if enabled and not self.pause_cloth_warned then
            log("pause cloth missing: " .. PAUSE_CLOTH_NAME)
            self.pause_cloth_warned = true
        end
        self.pause_cloth_active = false
        return
    end

    self.pause_cloth_active = enabled == true
    if self.pause_cloth_active and comp.Activate ~= nil then
        comp:Activate()
    end
    if comp.SetTickWhenPaused ~= nil then
        comp:SetTickWhenPaused(self.pause_cloth_active)
    end

    if self.pause_cloth_active then
        self.pause_cloth_time = 0.0
        if comp.ResetClothSimulation ~= nil then
            comp:ResetClothSimulation()
        end
        if comp.SetClothPreviewWindOverride ~= nil then
            comp:SetClothPreviewWindOverride(true, PAUSE_CLOTH_BASE_WIND_X, 0.0, 0.0)
        end
        set_pause_cloth_world_wind(PAUSE_CLOTH_BASE_WIND_X, 0.0, 0.0)
        log("pause cloth wind enabled: x=" .. tostring(PAUSE_CLOTH_BASE_WIND_X))
    elseif comp.ClearClothWindOverride ~= nil then
        comp:ClearClothWindOverride()
        clear_pause_cloth_world_wind()
    elseif comp.SetClothPreviewWindOverride ~= nil then
        comp:SetClothPreviewWindOverride(false, 0.0, 0.0, 0.0)
        clear_pause_cloth_world_wind()
    end
end

function InGameManager:UpdatePauseClothWind(dt)
    if not self.pause_cloth_active then
        return
    end

    local comp = self:GetPauseClothComponent()
    if comp == nil or comp.SetClothPreviewWindOverride == nil then
        self.pause_cloth_active = false
        return
    end

    self.pause_cloth_time = (self.pause_cloth_time or 0.0) + (dt or 0.0)
    local t = self.pause_cloth_time
    local gust = math.sin(t * 1.7) * PAUSE_CLOTH_GUST_X + math.sin(t * 4.3) * (PAUSE_CLOTH_GUST_X * 0.35)
    local wind_x = PAUSE_CLOTH_BASE_WIND_X + gust
    local wind_y = math.sin(t * 2.6) * PAUSE_CLOTH_SWAY_Y
    local wind_z = math.sin(t * 3.8) * PAUSE_CLOTH_SWAY_Z
    comp:SetClothPreviewWindOverride(true, wind_x, wind_y, wind_z)
    set_pause_cloth_world_wind(wind_x, wind_y, wind_z)
end

function InGameManager:CaptureAndPauseWorld()
    if self.pause_world_pause_captured then
        return
    end

    self.pause_previous_world_paused = is_world_paused()
    self.pause_world_pause_captured = true
    set_world_paused(true)
end

function InGameManager:RestorePauseWorld(force_resume)
    if force_resume == true then
        set_world_paused(false)
    elseif self.pause_world_pause_captured and self.pause_previous_world_paused ~= true then
        set_world_paused(false)
    end

    self.pause_previous_world_paused = false
    self.pause_world_pause_captured = false
end

function InGameManager:TogglePause(reason)
    if self.pause_transition ~= nil then
        return
    end

    if self.paused then
        self:BeginResume(reason or "toggle")
    else
        self:BeginPause(reason or "toggle")
    end
end

function InGameManager:BeginPause(reason)
    if self.paused or self.pause_transition ~= nil then
        return
    end

    local camera = self:FindPauseCamera()
    if camera == nil then
        if not self.pause_camera_warned then
            log("pause camera missing: " .. PAUSE_CAMERA_NAME .. " tag=" .. PAUSE_CAMERA_TAG)
            self.pause_camera_warned = true
        end
        return
    end

    if CameraManager ~= nil and CameraManager.GetActiveCameraOwner ~= nil then
        self.pause_previous_view_target = CameraManager.GetActiveCameraOwner()
    elseif CameraManager ~= nil and CameraManager.GetPossessedCameraOwner ~= nil then
        self.pause_previous_view_target = CameraManager.GetPossessedCameraOwner()
    else
        self.pause_previous_view_target = nil
    end

    self.paused = true
    self.pause_transition = "enter_fade_out"
    self.pause_transition_time = 0.0
    self:CaptureAndPauseWorld()
    self:SetPauseClothWindEnabled(true)
    if CameraManager ~= nil and CameraManager.FadeOut ~= nil then
        CameraManager.FadeOut(PAUSE_FADE_TIME)
    end
    self.general:Publish("ingame.pause_changed", { paused = true, reason = reason })
    log("pause begin reason=" .. tostring(reason))
end

function InGameManager:BeginResume(reason)
    if (not self.paused) or self.pause_transition ~= nil then
        return
    end

    self.pause_transition = "exit_fade_out"
    self.pause_transition_time = 0.0
    if CameraManager ~= nil and CameraManager.FadeOut ~= nil then
        CameraManager.FadeOut(PAUSE_FADE_TIME)
    end
    log("resume begin reason=" .. tostring(reason))
end

function InGameManager:TickPauseTransition(dt)
    if self.pause_transition == nil then
        return
    end

    self.pause_transition_time = self.pause_transition_time + (dt or 0.0)
    if self.pause_transition == "enter_fade_out" and self.pause_transition_time >= PAUSE_FADE_TIME then
        local camera = self:FindPauseCamera()
        if camera ~= nil and CameraManager ~= nil and CameraManager.SetViewTargetWithBlend ~= nil then
            CameraManager.SetViewTargetWithBlend(camera, PAUSE_BLEND_TIME)
        end
        local seq = self:GetPauseRifleSequence()
        if seq ~= nil and seq.Play ~= nil then
            if seq.SetTickWhenPaused ~= nil then
                seq:SetTickWhenPaused(true)
            end
            seq:Play()
            log("pause rifle sequence play: " .. PAUSE_RIFLE_NAME)
        else
            log("pause rifle sequence missing: " .. PAUSE_RIFLE_NAME)
        end
        if CameraManager ~= nil and CameraManager.FadeIn ~= nil then
            CameraManager.FadeIn(PAUSE_FADE_TIME)
        end
        self.pause_transition = "enter_fade_in"
        self.pause_transition_time = 0.0
        return
    end

    if self.pause_transition == "enter_fade_in" and self.pause_transition_time >= PAUSE_FADE_TIME then
        self.pause_transition = nil
        self.pause_transition_time = 0.0
        return
    end

    if self.pause_transition == "exit_fade_out" and self.pause_transition_time >= PAUSE_FADE_TIME then
        self:EndPausePresentation(true)
        self:RestorePauseWorld(false)
        if CameraManager ~= nil and CameraManager.FadeIn ~= nil then
            CameraManager.FadeIn(PAUSE_FADE_TIME)
        end
        self.paused = false
        self.pause_transition = "exit_fade_in"
        self.pause_transition_time = 0.0
        self.general:Publish("ingame.pause_changed", { paused = false, reason = "resume" })
        return
    end

    if self.pause_transition == "exit_fade_in" and self.pause_transition_time >= PAUSE_FADE_TIME then
        self.pause_transition = nil
        self.pause_transition_time = 0.0
    end
end

function InGameManager:EndPausePresentation(restore_camera)
    local seq = self.pause_rifle_sequence
    if seq == nil and self.pause_rifle ~= nil then
        seq = self:GetPauseRifleSequence()
    end
    if seq ~= nil and seq.Stop ~= nil then
        seq:Stop()
        if seq.SetTickWhenPaused ~= nil then
            seq:SetTickWhenPaused(false)
        end
    end

    if restore_camera and self.pause_previous_view_target ~= nil and
        CameraManager ~= nil and CameraManager.SetViewTargetWithBlend ~= nil then
        CameraManager.SetViewTargetWithBlend(self.pause_previous_view_target, PAUSE_BLEND_TIME)
    end

    self:SetPauseClothWindEnabled(false)
    self.pause_transition = nil
    self.pause_transition_time = 0.0
    self.pause_rifle_sequence = nil
end

function InGameManager:GoToMain(reason)
    self:EndPausePresentation(false)
    self:RestorePauseWorld(true)
    self.paused = false
    self.general:Publish("ingame.pause_changed", { paused = false, reason = reason or "go_main" })
    if self.general ~= nil and self.general.RequestState ~= nil then
        self.general:RequestState(GameState.Main, { reason = reason or "pause_menu" })
    end
end

function InGameManager:SetPhase(phase)
    if self.phase == phase then
        return
    end

    local from = self.phase
    self.phase = phase
    self.general:Publish("ingame.phase_changed", {
        from = from,
        to = phase,
        wave = self.wave,
        timer = self.timer
    })
end

function InGameManager:SetWave(wave)
    wave = math.max(0, math.floor(tonumber(wave) or 0))
    if self.wave == wave then
        return
    end

    local from = self.wave
    self.wave = wave
    self.general:Publish("ingame.wave_changed", {
        from = from,
        to = wave,
        phase = self.phase,
        timer = self.timer
    })
end

function InGameManager:NextWave()
    self:SetWave((self.wave or 0) + 1)
end

function InGameManager:GetSnapshot()
    return {
        running = self.running,
        timer = self.timer,
        elapsed_time = self.timer,
        remaining_time = self:GetRemainingTime(),
        match_duration = self.match_duration,
        phase = self.phase,
        wave = self.wave,
        settings = self.settings,
        sniper_kills = self.sniper_kills,
        friendly_fire_kills = self.friendly_fire_kills,
        score = self.general:GetScore(),
        paused = self.paused,
        result_requested = self.result_requested
    }
end

return InGameManager
