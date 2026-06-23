local EffectManager = {}
EffectManager.__index = EffectManager

local BLOOD_HIT_PARTICLE_PATH = "Content/Particle System/BloodHit.uasset"
local BLOOD_HIT_POOL_SIZE = 5
local BLOOD_HIT_NORMAL_COUNT = 3
local BLOOD_HIT_PENETRATION_HEADSHOT_COUNT = 5
local BLOOD_HIT_KILLCAM_FORCE_DELAY = 2.5
local BLOOD_HIT_BURST_DELAYS = { 0.0, 0.06, 0.12, 0.18, 0.24 }
local BLOOD_HIT_ROTATION_MODES = { "normal", "inverse_normal", "default", "normal", "inverse_normal" }
local BLOOD_HIT_SCALE = Vec3(3.0, 3.0, 3.0)
local BLOOD_HIT_LIFETIME = 8.0
local HIDDEN_LOCATION = Vec3(0.0, 0.0, -100000.0)

local function log(message)
    if Debug and Debug.Log then
        Debug.Log("[EffectManager] " .. message)
    else
        print("[EffectManager] " .. message)
    end
end

local function is_valid_object(value)
    if value == nil then
        return false
    end
    if value.IsValid ~= nil then
        return value:IsValid()
    end
    return true
end

local function get_hit_info(payload)
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

local function get_hit_location(hit)
    if hit == nil then
        return nil
    end
    return hit.HitLocation or hit.WorldHitLocation
end

local function get_hit_normal(hit)
    if hit == nil then
        return nil
    end
    return hit.HitNormal or hit.WorldHitNormal or hit.Normal
end

local function get_bullet_id(hit)
    if hit == nil then
        return nil
    end
    local bullet_id = tonumber(hit.BulletId)
    if bullet_id == nil or bullet_id == 0 then
        return nil
    end
    return bullet_id
end

local function get_payload_bullet_id(payload)
    if payload == nil then
        return nil
    end
    local bullet_id = tonumber(payload.bullet_id) or tonumber(payload.BulletId)
    if bullet_id ~= nil and bullet_id ~= 0 then
        return bullet_id
    end
    return get_bullet_id(get_hit_info(payload))
end

local function get_effect_delta_time(dt)
    dt = tonumber(dt) or 0.0
    if dt > 0.0 then
        return dt
    end
    if Time ~= nil and Time.RawDeltaTime ~= nil then
        return tonumber(Time.RawDeltaTime()) or 0.0
    end
    return dt
end

local function is_penetrating_headshot(hit)
    if hit == nil or hit.bIsHeadshot ~= true then
        return false
    end
    if SniperHitOutcome ~= nil and hit.HitOutcome == SniperHitOutcome.Penetrated then
        return true
    end
    return tonumber(hit.HitOutcome) == 3
end

local function atan2_degrees(y, x)
    if math.atan2 ~= nil then
        return math.deg(math.atan2(y, x))
    end

    if x > 0.0 then
        return math.deg(math.atan(y / x))
    end
    if x < 0.0 and y >= 0.0 then
        return math.deg(math.atan(y / x)) + 180.0
    end
    if x < 0.0 and y < 0.0 then
        return math.deg(math.atan(y / x)) - 180.0
    end
    if y > 0.0 then
        return 90.0
    end
    if y < 0.0 then
        return -90.0
    end
    return 0.0
end

local function direction_to_rotation(direction)
    if direction == nil or direction:Length() <= 0.000001 then
        return Vec3(0.0, 0.0, 0.0)
    end

    local normal = direction:Normalized()
    local horizontal = math.sqrt(normal.X * normal.X + normal.Y * normal.Y)
    local pitch = atan2_degrees(-normal.Z, horizontal)
    local yaw = atan2_degrees(normal.Y, normal.X)

    return Vec3(0.0, pitch, yaw)
end

local function get_blood_hit_rotation(index, hit_normal)
    local mode = BLOOD_HIT_ROTATION_MODES[index] or "default"
    if mode == "normal" then
        return direction_to_rotation(hit_normal)
    end
    if mode == "inverse_normal" and hit_normal ~= nil then
        return direction_to_rotation(hit_normal * -1.0)
    end
    return Vec3(0.0, 0.0, 0.0)
end

function EffectManager.new(general)
    return setmetatable({
        general = general,
        blood_hit_effects = {},
        pending_killcam_hits = {},
        forced_killcam_blood_hits = {},
        recent_hit_payloads = {},
        played_blood_hits = {},
        played_killcam_blood_hits = {},
        active_killcam_bullet_id = nil
    }, EffectManager)
end

function EffectManager:Initialize()
    self:EnsureBloodHitEffects()

    self.general:Subscribe("ingame.sniper_hit_scored", self, function(payload)
        self:HandleBloodHit(payload)
    end)

    self.general:Subscribe("ingame.sniper_killed", self, function(payload)
        self:HandleBloodHit(payload)
    end)

    self.general:Subscribe("sniper.killcam_triggered", self, function(payload)
        self:HandleKillCamTriggered(payload)
    end)

    self.general:Subscribe("ingame.sniper_killcam_hit", self, function(payload)
        self:HandleKillCamHit(payload)
    end)

    self.general:Subscribe("sniper.killcam_impact", self, function(payload)
        self:PlayPendingKillCamBlood(payload)
    end)

    self.general:Subscribe("scene.exiting", self, function()
        self:Clear()
    end)
end

function EffectManager:Shutdown()
    self:Clear()
    self.general:UnsubscribeOwner(self)
end

function EffectManager:EnsureBloodHitEffects()
    if #self.blood_hit_effects == BLOOD_HIT_POOL_SIZE then
        local all_valid = true
        for _, effect in ipairs(self.blood_hit_effects) do
            if effect.component == nil or not is_valid_object(effect.actor) then
                all_valid = false
                break
            end
        end
        if all_valid then
            return true
        end
    end

    self:Clear()

    if Particle == nil or Particle.SpawnEmitterAtLocation == nil then
        log("Particle.SpawnEmitterAtLocation is not available")
        return false
    end

    for index = 1, BLOOD_HIT_POOL_SIZE do
        local actor = Particle.SpawnEmitterAtLocation(
            BLOOD_HIT_PARTICLE_PATH,
            HIDDEN_LOCATION,
            Vec3(0.0, 0.0, 0.0),
            BLOOD_HIT_SCALE,
            false,
            true)
        if actor == nil then
            log("failed to pre-spawn blood hit particle: " .. BLOOD_HIT_PARTICLE_PATH)
            self:Clear()
            return false
        end

        local component = nil
        if actor.GetParticleSystemComponent ~= nil then
            component = actor:GetParticleSystemComponent()
        end
        if component == nil then
            log("pre-spawned blood hit particle has no component: " .. BLOOD_HIT_PARTICLE_PATH)
            if actor.Destroy ~= nil then
                actor:Destroy()
            end
            self:Clear()
            return false
        end

        component:Deactivate()
        component:ResetParticles()
        if component.SetTickWhenPaused ~= nil then
            component:SetTickWhenPaused(true)
        end

        if actor ~= nil then
            actor.Location = HIDDEN_LOCATION
            actor.Rotation = Vec3(0.0, 0.0, 0.0)
            actor.Scale = BLOOD_HIT_SCALE
        end

        self.blood_hit_effects[index] = {
            actor = actor,
            component = component,
            delay = 0.0,
            remaining = 0.0,
            pending_location = nil,
            pending_rotation = nil
        }
    end

    return true
end

function EffectManager:ActivateBloodHitEffect(effect, location, rotation)
    if effect == nil or effect.actor == nil or effect.component == nil then
        return
    end

    effect.actor.Location = location
    effect.actor.Rotation = rotation or Vec3(0.0, 0.0, 0.0)
    effect.actor.Scale = BLOOD_HIT_SCALE
    effect.component:Deactivate()
    effect.component:ResetParticles()
    effect.component:Activate(true)

    effect.delay = 0.0
    effect.remaining = BLOOD_HIT_LIFETIME
    effect.pending_location = nil
    effect.pending_rotation = nil
end

function EffectManager:StopBloodHitEffect(effect)
    if effect == nil then
        return
    end
    if effect.component ~= nil then
        effect.component:Deactivate()
        effect.component:ResetParticles()
    end
    if is_valid_object(effect.actor) then
        effect.actor.Location = HIDDEN_LOCATION
    end
    effect.delay = 0.0
    effect.remaining = 0.0
    effect.pending_location = nil
    effect.pending_rotation = nil
end

function EffectManager:StopAllBloodHitEffects()
    for _, effect in ipairs(self.blood_hit_effects) do
        self:StopBloodHitEffect(effect)
    end
end

function EffectManager:GetBloodHitEffectCount(hit)
    if is_penetrating_headshot(hit) then
        return BLOOD_HIT_PENETRATION_HEADSHOT_COUNT
    end
    return BLOOD_HIT_NORMAL_COUNT
end

function EffectManager:CacheKillCamBloodHit(bullet_id, payload)
    if bullet_id == nil or bullet_id == 0 or payload == nil then
        return nil
    end

    local cached = {
        payload = payload,
        count = BLOOD_HIT_PENETRATION_HEADSHOT_COUNT
    }
    self.pending_killcam_hits[bullet_id] = cached
    self.recent_hit_payloads[bullet_id] = payload
    self.forced_killcam_blood_hits[bullet_id] = {
        payload = payload,
        count = cached.count,
        delay = BLOOD_HIT_KILLCAM_FORCE_DELAY
    }
    return cached
end

function EffectManager:HandleBloodHit(payload)
    local hit = get_hit_info(payload)
    local bullet_id = get_payload_bullet_id(payload)

    if bullet_id ~= nil then
        self.recent_hit_payloads[bullet_id] = payload
    end

    if is_penetrating_headshot(hit) then
        if bullet_id ~= nil then
            self.pending_killcam_hits[bullet_id] = {
                payload = payload,
                count = self:GetBloodHitEffectCount(hit)
            }
            self:StopAllBloodHitEffects()
            return nil
        end
    end

    if bullet_id ~= nil then
        if self.played_blood_hits[bullet_id] == true then
            return nil
        end
        self.played_blood_hits[bullet_id] = true
    end

    return self:PlayBloodHit(payload, BLOOD_HIT_NORMAL_COUNT)
end

function EffectManager:HandleKillCamTriggered(payload)
    local bullet_id = get_payload_bullet_id(payload)
    if bullet_id == nil or bullet_id == 0 then
        self:StopAllBloodHitEffects()
        return nil
    end

    self.active_killcam_bullet_id = bullet_id
    local hit_payload = self.recent_hit_payloads[bullet_id]
    if hit_payload == nil and get_hit_location(get_hit_info(payload)) ~= nil then
        hit_payload = payload
    end
    if hit_payload ~= nil then
        self:CacheKillCamBloodHit(bullet_id, hit_payload)
        self.played_blood_hits[bullet_id] = true
    end

    self:StopAllBloodHitEffects()
    return nil
end

function EffectManager:HandleKillCamHit(payload)
    local bullet_id = get_payload_bullet_id(payload) or self.active_killcam_bullet_id
    if bullet_id == nil or bullet_id == 0 then
        return nil
    end

    self:CacheKillCamBloodHit(bullet_id, payload)
    self.played_blood_hits[bullet_id] = true
    self:StopAllBloodHitEffects()
    return nil
end

function EffectManager:PlayKillCamBloodForBullet(bullet_id, payload)
    if bullet_id == nil or bullet_id == 0 then
        return nil
    end
    if self.played_killcam_blood_hits[bullet_id] == true then
        return nil
    end

    local pending = self.pending_killcam_hits[bullet_id]
    if pending == nil and get_hit_location(get_hit_info(payload)) ~= nil then
        pending = {
            payload = payload,
            count = BLOOD_HIT_PENETRATION_HEADSHOT_COUNT
        }
    end
    if pending == nil and self.recent_hit_payloads[bullet_id] ~= nil then
        local hit_payload = self.recent_hit_payloads[bullet_id]
        pending = {
            payload = hit_payload,
            count = BLOOD_HIT_PENETRATION_HEADSHOT_COUNT
        }
    end
    if pending == nil then
        log("killcam impact has no cached hit payload for bullet_id=" .. tostring(bullet_id))
        return nil
    end

    self.pending_killcam_hits[bullet_id] = nil
    self.forced_killcam_blood_hits[bullet_id] = nil
    self.played_blood_hits[bullet_id] = true
    self.played_killcam_blood_hits[bullet_id] = true
    return self:PlayBloodHit(pending.payload, pending.count)
end

function EffectManager:PlayPendingKillCamBlood(payload)
    local bullet_id = get_payload_bullet_id(payload) or self.active_killcam_bullet_id
    if payload ~= nil and payload.direct_blood_spawned == true then
        if bullet_id ~= nil and bullet_id ~= 0 then
            self.pending_killcam_hits[bullet_id] = nil
            self.forced_killcam_blood_hits[bullet_id] = nil
            self.played_blood_hits[bullet_id] = true
            self.played_killcam_blood_hits[bullet_id] = true
        end
        return nil
    end
    return self:PlayKillCamBloodForBullet(bullet_id, payload)
end

function EffectManager:PlayBloodHit(payload, effect_count)
    local hit = get_hit_info(payload)
    local location = get_hit_location(hit)
    if location == nil then
        return nil
    end

    if not self:EnsureBloodHitEffects() then
        return nil
    end

    effect_count = math.floor(tonumber(effect_count) or BLOOD_HIT_NORMAL_COUNT)
    if effect_count < 1 then
        effect_count = 1
    end
    if effect_count > BLOOD_HIT_POOL_SIZE then
        effect_count = BLOOD_HIT_POOL_SIZE
    end

    local hit_normal = get_hit_normal(hit)
    for index, effect in ipairs(self.blood_hit_effects) do
        if index > effect_count then
            self:StopBloodHitEffect(effect)
        else
            local rotation = get_blood_hit_rotation(index, hit_normal)

            effect.component:Deactivate()
            effect.component:ResetParticles()
            effect.remaining = 0.0
            effect.pending_location = location
            effect.pending_rotation = rotation
            effect.delay = BLOOD_HIT_BURST_DELAYS[index] or 0.0

            if effect.delay <= 0.0 then
                self:ActivateBloodHitEffect(effect, location, rotation)
            elseif is_valid_object(effect.actor) then
                effect.actor.Location = HIDDEN_LOCATION
            end
        end
    end

    return self.blood_hit_effects[1] and self.blood_hit_effects[1].actor or nil
end

function EffectManager:Tick(dt)
    dt = get_effect_delta_time(dt)

    for bullet_id, forced in pairs(self.forced_killcam_blood_hits) do
        forced.delay = (tonumber(forced.delay) or 0.0) - dt
        if forced.delay <= 0.0 then
            self:PlayKillCamBloodForBullet(bullet_id, forced.payload)
        end
    end

    for _, effect in ipairs(self.blood_hit_effects) do
        if effect.delay > 0.0 then
            effect.delay = effect.delay - dt
            if effect.delay <= 0.0 and effect.pending_location ~= nil then
                self:ActivateBloodHitEffect(effect, effect.pending_location, effect.pending_rotation)
            end
        elseif effect.remaining > 0.0 then
            effect.remaining = effect.remaining - dt
            if effect.remaining <= 0.0 and effect.component ~= nil then
                effect.component:Deactivate()
                effect.component:ResetParticles()
                if is_valid_object(effect.actor) then
                    effect.actor.Location = HIDDEN_LOCATION
                end
            end
        end
    end
end

function EffectManager:Clear()
    for _, effect in ipairs(self.blood_hit_effects) do
        if effect.component ~= nil then
            effect.component:Deactivate()
            effect.component:ResetParticles()
        end
        if is_valid_object(effect.actor) then
            effect.actor:Destroy()
        end
    end
    self.blood_hit_effects = {}
    self.pending_killcam_hits = {}
    self.forced_killcam_blood_hits = {}
    self.recent_hit_payloads = {}
    self.played_blood_hits = {}
    self.played_killcam_blood_hits = {}
    self.active_killcam_bullet_id = nil
end

return EffectManager
