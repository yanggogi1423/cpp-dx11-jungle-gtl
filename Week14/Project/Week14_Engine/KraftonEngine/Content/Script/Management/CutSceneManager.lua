local CutSceneManager = {}
CutSceneManager.__index = CutSceneManager

local SNIPER_KILLCAM_TRAVEL_DURATION = 5.0
local SNIPER_KILLCAM_POST_IMPACT_SECONDS = 2.0
local SNIPER_KILLCAM_DURATION = SNIPER_KILLCAM_TRAVEL_DURATION + SNIPER_KILLCAM_POST_IMPACT_SECONDS
local SNIPER_KILLCAM_PRE_IMPACT_FORWARD_DISTANCE = 0.18
local SNIPER_KILLCAM_POST_IMPACT_FORWARD_DISTANCE = 4.0
local SNIPER_KILLCAM_IMPACT_BULLET_SCALE = 0.05
local SNIPER_KILLCAM_TRAVEL_FOV_BOOST = 0.22
local SNIPER_KILLCAM_SLOWDOWN_SFX = "SFX/Sniper/SlowDown.mp3"
local SNIPER_KILLCAM_SLOWDOWN_SFX_DELAY = 0.3
local SNIPER_KILLCAM_BULLET_CAM_SFX = "SFX/Sniper/BulletCam1.mp3"
local SNIPER_KILLCAM_BULLET_CAM_SFX_DELAY = 1.0
local SNIPER_KILLCAM_DAMAGED_SFX = "SFX/Sniper/Damaged1.mp3"
local SNIPER_KILLCAM_IMPACT_SFX_VOLUME = 1.0
local SNIPER_KILLCAM_FORCED_BLOOD_DELAY = 4.65
local SNIPER_KILLCAM_BLOOD_PARTICLE_PATH = "Content/Particle System/BloodHit.uasset"
local SNIPER_KILLCAM_BLOOD_SCALE = Vec3(3.0, 3.0, 3.0)
local SNIPER_KILLCAM_BLOOD_LIFETIME = 8.0
local SNIPER_KILLCAM_BLOOD_BURST_DELAYS = { 0.0, 0.06, 0.12, 0.18, 0.24 }
local SNIPER_KILLCAM_FLASH_OUT_ALPHA = 0.30
local SNIPER_KILLCAM_FLASH_IN_ALPHA = 0.306
local SNIPER_KILLCAM_FLASH_OUT_DURATION = 0.0
local SNIPER_KILLCAM_FLASH_IN_DURATION = 0.0

local function clamp01(value)
    value = tonumber(value) or 0.0
    if value < 0.0 then
        return 0.0
    end
    if value > 1.0 then
        return 1.0
    end
    return value
end

local function lerp(a, b, alpha)
    return a + (b - a) * alpha
end

local function smoothstep(alpha)
    alpha = clamp01(alpha)
    return alpha * alpha * (3.0 - 2.0 * alpha)
end

local function resolve_sniper_killcam_hit_payload(bullet_id)
    if SniperKillCam == nil or SniperKillCam.GetHitSnapshot == nil then
        return nil
    end

    local snapshot = SniperKillCam.GetHitSnapshot(bullet_id)
    if snapshot == nil or snapshot.Position == nil then
        return nil
    end

    local hit_normal = nil
    local shot_direction = nil
    if snapshot.Velocity ~= nil and snapshot.Velocity:Length() > 0.000001 then
        shot_direction = snapshot.Velocity:Normalized()
        hit_normal = shot_direction * -1.0
    end

    return {
        BulletId = bullet_id,
        HitLocation = snapshot.Position,
        HitNormal = hit_normal,
        ShotDirection = shot_direction,
        TravelDistance = tonumber(snapshot.TravelDistance) or tonumber(snapshot.TraveledDistance) or 0.0,
        TraveledDistance = tonumber(snapshot.TraveledDistance) or tonumber(snapshot.TravelDistance) or 0.0
    }
end

local function consume_sniper_killcam_floor_hit_payload(bullet_id)
    if SniperKillCam == nil or SniperKillCam.ConsumeFloorHit == nil then
        return nil
    end

    bullet_id = tonumber(bullet_id) or 0
    if bullet_id == 0 then
        return nil
    end

    local snapshot = SniperKillCam.ConsumeFloorHit(bullet_id)
    if snapshot == nil or snapshot.Position == nil then
        return nil
    end

    return {
        BulletId = bullet_id,
        HitLocation = snapshot.Position,
        Snapshot = snapshot,
        TravelDistance = tonumber(snapshot.TravelDistance) or tonumber(snapshot.TraveledDistance) or 0.0,
        TraveledDistance = tonumber(snapshot.TraveledDistance) or tonumber(snapshot.TravelDistance) or 0.0
    }
end

local function check_sniper_killcam_floor_hit_payload(bullet_id)
    if SniperKillCam == nil or SniperKillCam.CheckFloorHit == nil then
        return consume_sniper_killcam_floor_hit_payload(bullet_id)
    end

    bullet_id = tonumber(bullet_id) or 0
    if bullet_id == 0 then
        return nil
    end

    local snapshot = SniperKillCam.CheckFloorHit(bullet_id)
    if snapshot == nil or snapshot.Position == nil then
        return nil
    end

    return {
        BulletId = bullet_id,
        HitLocation = snapshot.Position,
        Snapshot = snapshot,
        TravelDistance = tonumber(snapshot.TravelDistance) or tonumber(snapshot.TraveledDistance) or 0.0,
        TraveledDistance = tonumber(snapshot.TraveledDistance) or tonumber(snapshot.TravelDistance) or 0.0
    }
end

local function merge_tables(base, override)
    local result = {}
    if type(base) == "table" then
        for key, value in pairs(base) do
            result[key] = value
        end
    end
    if type(override) == "table" then
        for key, value in pairs(override) do
            result[key] = value
        end
    end
    return result
end

local function play_sfx(general, path, volume)
    if general ~= nil and general.PlaySFX ~= nil then
        general:PlaySFX(path, volume or 1.0)
        return
    end

    if AudioManager ~= nil and AudioManager.PlaySFX ~= nil then
        AudioManager.PlaySFX(path, volume or 1.0)
    end
end

local function play_sfx_handle(general, path, volume)
    if general ~= nil and general.PlaySFXHandle ~= nil then
        return general:PlaySFXHandle(path, volume or 1.0) or 0
    end

    if AudioManager ~= nil and AudioManager.PlaySFXHandle ~= nil then
        return AudioManager.PlaySFXHandle(path, volume or 1.0) or 0
    end

    play_sfx(general, path, volume)
    return 0
end

local function stop_sfx_handle(general, handle)
    handle = tonumber(handle) or 0
    if handle == 0 then
        return
    end

    if general ~= nil and general.FadeOutSFX ~= nil then
        general:FadeOutSFX(handle, 0.05)
        return
    end

    if AudioManager ~= nil and AudioManager.FadeOutSFX ~= nil then
        AudioManager.FadeOutSFX(handle, 0.05)
        return
    end

    if general ~= nil and general.StopSound ~= nil then
        general:StopSound(handle)
        return
    end

    if AudioManager ~= nil and AudioManager.StopSound ~= nil then
        AudioManager.StopSound(handle)
    end
end

local function stop_killcam_sfx_handles(general, handles)
    if type(handles) ~= "table" then
        return
    end

    for _, handle in ipairs(handles) do
        stop_sfx_handle(general, handle)
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

local function get_killcam_hit_location(hit)
    if hit == nil then
        return nil
    end
    return hit.HitLocation or hit.WorldHitLocation or hit.Position
end

local function play_impact_sfx(general, path, volume, location)
    if path == nil or path == "" then
        return 0
    end

    if location ~= nil and AudioManager ~= nil and AudioManager.PlaySFX3D ~= nil then
        return AudioManager.PlaySFX3D(path, location, volume or 1.0, 80.0, 3000.0) or 0
    end

    return play_sfx_handle(general, path, volume)
end

local function spawn_killcam_blood_actor(location)
    if location == nil or Particle == nil or Particle.SpawnEmitterAtLocation == nil then
        return nil
    end

    local actor = Particle.SpawnEmitterAtLocation(
        SNIPER_KILLCAM_BLOOD_PARTICLE_PATH,
        location,
        Vec3(0.0, 0.0, 0.0),
        SNIPER_KILLCAM_BLOOD_SCALE,
        true)
    if actor == nil then
        return nil
    end

    actor.Location = location
    actor.Rotation = Vec3(0.0, 0.0, 0.0)
    actor.Scale = SNIPER_KILLCAM_BLOOD_SCALE

    local component = nil
    if actor.GetParticleSystemComponent ~= nil then
        component = actor:GetParticleSystemComponent()
    end
    if component ~= nil then
        if component.SetTickWhenPaused ~= nil then
            component:SetTickWhenPaused(true)
        end
        if component.Deactivate ~= nil then
            component:Deactivate()
        end
        if component.ResetParticles ~= nil then
            component:ResetParticles()
        end
        if component.Activate ~= nil then
            component:Activate(true)
        end
    end

    return actor
end

local function queue_killcam_blood_effects(current, hit)
    local location = get_killcam_hit_location(hit)
    if current == nil or location == nil then
        return false
    end

    current.killcam_blood_effects = current.killcam_blood_effects or {}
    local queued = false
    for index, delay in ipairs(SNIPER_KILLCAM_BLOOD_BURST_DELAYS) do
        local effect = {
            location = location,
            delay = tonumber(delay) or 0.0,
            remaining = SNIPER_KILLCAM_BLOOD_LIFETIME,
            actor = nil,
            spawned = false
        }
        if effect.delay <= 0.0 then
            effect.actor = spawn_killcam_blood_actor(location)
            effect.spawned = effect.actor ~= nil
        end
        if effect.delay > 0.0 or effect.spawned == true then
            current.killcam_blood_effects[#current.killcam_blood_effects + 1] = effect
            queued = true
        end
    end

    return queued
end

local function tick_killcam_blood_effects(current, dt)
    if current == nil or type(current.killcam_blood_effects) ~= "table" then
        return
    end

    dt = tonumber(dt) or 0.0
    for index = #current.killcam_blood_effects, 1, -1 do
        local effect = current.killcam_blood_effects[index]
        if effect.spawned ~= true then
            effect.delay = (tonumber(effect.delay) or 0.0) - dt
            if effect.delay <= 0.0 then
                effect.actor = spawn_killcam_blood_actor(effect.location)
                effect.spawned = effect.actor ~= nil
                if effect.spawned ~= true then
                    table.remove(current.killcam_blood_effects, index)
                end
            end
        else
            effect.remaining = (tonumber(effect.remaining) or 0.0) - dt
            if effect.remaining <= 0.0 then
                if is_valid_object(effect.actor) and effect.actor.Destroy ~= nil then
                    effect.actor:Destroy()
                end
                table.remove(current.killcam_blood_effects, index)
            end
        end
    end
end

local function clear_killcam_blood_effects(current)
    if current == nil or type(current.killcam_blood_effects) ~= "table" then
        return
    end

    for _, effect in ipairs(current.killcam_blood_effects) do
        if is_valid_object(effect.actor) and effect.actor.Destroy ~= nil then
            effect.actor:Destroy()
        end
    end
    current.killcam_blood_effects = nil
end

local function camera_fade_out(duration)
    if CameraManager ~= nil and CameraManager.FadeOut ~= nil then
        CameraManager.FadeOut(duration)
    end
end

local function camera_fade_in(duration)
    if CameraManager ~= nil and CameraManager.FadeIn ~= nil then
        CameraManager.FadeIn(duration)
    end
end

local function was_confirm_pressed()
    if Input ~= nil and Input.WasConfirmPressed ~= nil then
        local ok, value = pcall(function()
            return Input.WasConfirmPressed()
        end)
        if ok then
            return value == true
        end
    end

    return Input ~= nil and Input.GetKeyDown ~= nil and Input.GetKeyDown("Space")
end

local function get_time_dilation()
    if Time ~= nil and Time.GetTimeDilation ~= nil then
        return Time.GetTimeDilation()
    end
    return 1.0
end

local function set_time_dilation(value)
    if Time ~= nil and Time.SetTimeDilation ~= nil then
        Time.SetTimeDilation(value)
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
        return
    end
    if paused == true then
        if Engine.PauseGame ~= nil then
            Engine.PauseGame()
        end
    elseif Engine.ResumeGame ~= nil then
        Engine.ResumeGame()
    end
end

local function get_sniper_pawn()
    if World == nil then
        return nil
    end

    local actor = nil
    if World.FindFirstActorByClass ~= nil then
        actor = World.FindFirstActorByClass("ASniperPawn")
        if actor == nil then
            actor = World.FindFirstActorByClass("SniperPawn")
        end
    end
    if actor == nil and World.FindActorByName ~= nil then
        actor = World.FindActorByName("ScopeTest_Player")
    end
    if actor == nil then
        return nil
    end

    if actor.AsSniperPawn ~= nil then
        local ok, casted = pcall(function()
            return actor:AsSniperPawn()
        end)
        if ok and casted ~= nil then
            return casted
        end
    end

    return actor
end

local function force_scope_released()
    local pawn = get_sniper_pawn()
    if pawn ~= nil and pawn.ForceScopeReleased ~= nil then
        pcall(function()
            pawn:ForceScopeReleased()
        end)
    end

    if CameraManager ~= nil then
        if CameraManager.SetScopeZoomEnabled ~= nil then
            CameraManager.SetScopeZoomEnabled(false)
        end
        if CameraManager.ClearScopeLens ~= nil then
            CameraManager.ClearScopeLens()
        end
    end
end

local function disable_sniper_killcam_shockwave()
    if SniperKillCam ~= nil and SniperKillCam.EnableShockWave ~= nil then
        SniperKillCam.EnableShockWave(false)
    end
    if CameraManager ~= nil and CameraManager.ClearShockWaves ~= nil then
        CameraManager.ClearShockWaves()
    end
end

local function get_raw_delta_time(fallback)
    if Time ~= nil and Time.RawDeltaTime ~= nil then
        local raw = tonumber(Time.RawDeltaTime()) or 0.0
        if raw > 0.0 then
            return raw
        end
    end
    return fallback or 0.0
end

local function sample_keyframes(frames, alpha)
    if type(frames) ~= "table" or #frames == 0 then
        return {}
    end

    alpha = clamp01(alpha)
    local first = frames[1]
    if alpha <= (tonumber(first.time) or 0.0) then
        local sample = {}
        for key, value in pairs(first) do
            if key ~= "time" then
                sample[key] = value
            end
        end
        return sample
    end

    for index = 1, #frames - 1 do
        local from = frames[index]
        local to = frames[index + 1]
        local from_time = tonumber(from.time) or 0.0
        local to_time = tonumber(to.time) or 1.0
        if alpha <= to_time then
            local range = to_time - from_time
            local local_alpha = range > 0.0001 and ((alpha - from_time) / range) or 1.0
            local eased = local_alpha
            local sample = {}
            for key, to_value in pairs(to) do
                if key ~= "time" then
                    local from_value = from[key]
                    if type(from_value) == "number" and type(to_value) == "number" then
                        sample[key] = lerp(from_value, to_value, eased)
                    else
                        sample[key] = to_value
                    end
                end
            end
            for key, from_value in pairs(from) do
                if key ~= "time" and sample[key] == nil then
                    sample[key] = from_value
                end
            end
            return sample
        end
    end

    local last = frames[#frames]
    local sample = {}
    for key, value in pairs(last) do
        if key ~= "time" then
            sample[key] = value
        end
    end
    return sample
end

local SNIPER_KILLCAM_PROFILES = {
    front_pass_tail = {
        flash_out_alpha = 0.30,
        flash_in_alpha = 0.306,
        flash_out_duration = 0.0,
        flash_in_duration = 0.0,
        bullet = {
            spinRevolutions = 64.0,
            spinPhase = 0.0,
            scale = 1.0
        },
        shockwave = {
            enabled = true,
            forwardOffset = -0.34,
            sideOffset = 0.0,
            upOffset = 0.0,
            radius = 0.110,
            startRadiusBoost = 0.16,
            width = 0.040,
            strength = 0.018,
            startStrengthBoost = 0.040,
            falloff = 1.25,
            directionalStretch = 4.4,
            decay = 4.6
        },
        rig_frames = {
            {
                time = 0.00,
                bAllowRailExtrapolation = 1.0,
                bClampAuthoredRailAlpha = 1.0,
                RailAlphaClampMin = -0.35,
                RailAlphaClampMax = 1.0,
                CameraRailAlphaOverride = 0.000,
                LookRailAlphaOverride = 0.000,
                ForwardOffset = 7.40,
                SideOffset = -3.55,
                UpOffset = 0.34,
                LookAhead = 0.00,
                LookSideOffset = 0.0,
                LookUpOffset = 0.0,
                bLookAtBulletVisual = 0.0,
                FOV = 0.72,
                Roll = 0.0,
                CameraLagSpeed = 14.0,
                LookLagSpeed = 54.0,
                CameraShakeAmplitude = 0.0015,
                CameraShakeFrequency = 7.5,
                DOFFocusRange = 1.4,
                DOFBlurRadius = 4.2,
                OrbitBlend = 0.0,
                BulletRailAlphaOverride = 0.0,
                BulletScaleMultiplier = 0.74,
                BulletSpinRevolutions = 72.0
            },
            {
                time = 0.30,
                CameraRailAlphaOverride = 0.000,
                LookRailAlphaOverride = 0.000,
                ForwardOffset = 7.10,
                SideOffset = -3.35,
                UpOffset = 0.28,
                LookAhead = 0.00,
                bLookAtBulletVisual = 0.0,
                FOV = 0.70,
                Roll = 0.0,
                CameraLagSpeed = 14.0,
                LookLagSpeed = 54.0,
                CameraShakeAmplitude = 0.0015,
                CameraShakeFrequency = 7.5,
                DOFFocusRange = 1.2,
                DOFBlurRadius = 4.8,
                BulletRailAlphaOverride = 0.120,
                BulletScaleMultiplier = 0.74,
                BulletSpinRevolutions = 72.0
            },
            {
                time = 0.44,
                CameraRailAlphaOverride = 0.260,
                LookRailAlphaOverride = 0.360,
                ForwardOffset = 3.05,
                SideOffset = -2.05,
                UpOffset = 0.82,
                LookAhead = -0.08,
                bLookAtBulletVisual = 1.0,
                FOV = 0.56,
                Roll = 0.0,
                CameraLagSpeed = 18.0,
                LookLagSpeed = 56.0,
                CameraShakeAmplitude = 0.0012,
                CameraShakeFrequency = 8.5,
                DOFFocusRange = 1.0,
                DOFBlurRadius = 5.0,
                BulletRailAlphaOverride = 0.440,
                BulletScaleMultiplier = 0.72,
                BulletSpinRevolutions = 58.0
            },
            {
                time = 0.60,
                CameraRailAlphaOverride = 0.740,
                LookRailAlphaOverride = 0.940,
                ForwardOffset = 1.10,
                SideOffset = -1.82,
                UpOffset = 0.50,
                LookAhead = -0.04,
                FOV = 0.58,
                Roll = 0.0,
                CameraLagSpeed = 26.0,
                LookLagSpeed = 58.0,
                CameraShakeAmplitude = 0.0010,
                CameraShakeFrequency = 9.5,
                DOFFocusRange = 0.65,
                DOFBlurRadius = 4.6,
                BulletRailAlphaOverride = 1.000,
                BulletScaleMultiplier = 0.70,
                BulletSpinRevolutions = 50.0
            },
            {
                time = 0.78,
                CameraRailAlphaOverride = 0.940,
                LookRailAlphaOverride = 1.000,
                ForwardOffset = 0.98,
                SideOffset = -1.34,
                UpOffset = 0.28,
                LookAhead = -0.08,
                FOV = 0.54,
                Roll = 0.0,
                CameraLagSpeed = 34.0,
                LookLagSpeed = 60.0,
                CameraShakeAmplitude = 0.0008,
                CameraShakeFrequency = 10.5,
                DOFFocusRange = 0.95,
                DOFBlurRadius = 3.8,
                BulletRailAlphaOverride = 1.000,
                BulletScaleMultiplier = 0.68,
                BulletSpinRevolutions = 44.0
            },
            {
                time = 1.00,
                CameraRailAlphaOverride = 1.000,
                LookRailAlphaOverride = 1.000,
                ForwardOffset = 1.08,
                SideOffset = -1.18,
                UpOffset = 0.16,
                LookAhead = -0.02,
                FOV = 0.52,
                Roll = 0.0,
                CameraLagSpeed = 40.0,
                LookLagSpeed = 64.0,
                CameraShakeAmplitude = 0.0005,
                CameraShakeFrequency = 11.0,
                DOFFocusRange = 1.2,
                DOFBlurRadius = 3.2,
                BulletRailAlphaOverride = 1.000,
                BulletScaleMultiplier = 0.66,
                BulletSpinRevolutions = 42.0
            }
        }
    },
    right_high_center_rush = {
        bullet = {
            spinRevolutions = 78.0,
            spinPhase = 0.0,
            scale = 1.0
        },
        shockwave = {
            enabled = true,
            forwardOffset = -0.42,
            sideOffset = 0.0,
            upOffset = 0.0,
            radius = 0.100,
            startRadiusBoost = 0.14,
            width = 0.034,
            strength = 0.016,
            startStrengthBoost = 0.034,
            falloff = 1.25,
            directionalStretch = 4.8,
            decay = 4.8
        },
        rig_frames = {
            {
                time = 0.00,
                bAllowRailExtrapolation = 1.0,
                bClampAuthoredRailAlpha = 1.0,
                RailAlphaClampMin = -0.20,
                RailAlphaClampMax = 1.0,
                CameraRailAlphaOverride = 0.000,
                LookRailAlphaOverride = 0.030,
                BulletRailAlphaOverride = 0.000,
                ForwardOffset = -0.18,
                SideOffset = 1.45,
                UpOffset = 1.02,
                LookAhead = 1.15,
                LookSideOffset = 1.70,
                LookUpOffset = 0.92,
                bLookAtBulletVisual = 0.0,
                FOV = 0.44,
                Roll = 0.0,
                CameraLagSpeed = 42.0,
                LookLagSpeed = 54.0,
                CameraShakeAmplitude = 0.0010,
                CameraShakeFrequency = 8.0,
                DOFFocusRange = 0.95,
                DOFBlurRadius = 3.4,
                OrbitBlend = 0.0,
                BulletScaleMultiplier = 0.0,
                BulletSpinRevolutions = 88.0
            },
            {
                time = 0.30,
                CameraRailAlphaOverride = 0.300,
                LookRailAlphaOverride = 0.330,
                BulletRailAlphaOverride = 0.300,
                ForwardOffset = -0.18,
                SideOffset = 1.36,
                UpOffset = 0.96,
                LookAhead = 0.72,
                LookSideOffset = 1.06,
                LookUpOffset = 0.58,
                bLookAtBulletVisual = 0.0,
                FOV = 0.46,
                CameraLagSpeed = 42.0,
                LookLagSpeed = 54.0,
                CameraShakeAmplitude = 0.0010,
                CameraShakeFrequency = 8.0,
                DOFFocusRange = 0.82,
                DOFBlurRadius = 3.9,
                BulletScaleMultiplier = 0.0,
                BulletSpinRevolutions = 88.0
            },
            {
                time = 0.50,
                CameraRailAlphaOverride = 0.620,
                LookRailAlphaOverride = 0.760,
                BulletRailAlphaOverride = 0.840,
                ForwardOffset = 0.92,
                SideOffset = 0.24,
                UpOffset = -0.06,
                LookAhead = 0.0,
                LookSideOffset = 0.0,
                LookUpOffset = 0.0,
                bLookAtBulletVisual = 1.0,
                FOV = 0.98,
                CameraLagSpeed = 54.0,
                LookLagSpeed = 64.0,
                CameraShakeAmplitude = 0.0009,
                CameraShakeFrequency = 8.5,
                DOFFocusRange = 0.46,
                DOFBlurRadius = 5.2,
                BulletScaleMultiplier = 0.76,
                BulletSpinRevolutions = 62.0
            },
            {
                time = 0.72,
                CameraRailAlphaOverride = 0.860,
                LookRailAlphaOverride = 1.000,
                BulletRailAlphaOverride = 1.000,
                ForwardOffset = 1.58,
                SideOffset = 0.16,
                UpOffset = -0.13,
                LookAhead = 0.0,
                LookSideOffset = 0.0,
                LookUpOffset = 0.0,
                bLookAtBulletVisual = 1.0,
                FOV = 0.86,
                CameraLagSpeed = 50.0,
                LookLagSpeed = 66.0,
                CameraShakeAmplitude = 0.0008,
                CameraShakeFrequency = 9.0,
                DOFFocusRange = 0.58,
                DOFBlurRadius = 4.5,
                BulletScaleMultiplier = 0.68,
                BulletSpinRevolutions = 54.0
            },
            {
                time = 0.90,
                CameraRailAlphaOverride = 0.980,
                LookRailAlphaOverride = 1.000,
                BulletRailAlphaOverride = 1.000,
                ForwardOffset = 2.28,
                SideOffset = 0.08,
                UpOffset = -0.20,
                LookAhead = 0.0,
                LookSideOffset = 0.0,
                LookUpOffset = 0.0,
                bLookAtBulletVisual = 1.0,
                FOV = 0.74,
                CameraLagSpeed = 46.0,
                LookLagSpeed = 68.0,
                CameraShakeAmplitude = 0.0006,
                CameraShakeFrequency = 9.5,
                DOFFocusRange = 0.72,
                DOFBlurRadius = 3.5,
                BulletScaleMultiplier = 0.58,
                BulletSpinRevolutions = 48.0
            },
            {
                time = 0.925,
                CameraRailAlphaOverride = 1.000,
                LookRailAlphaOverride = 1.000,
                BulletRailAlphaOverride = 1.000,
                ForwardOffset = 2.48,
                SideOffset = 0.08,
                UpOffset = -0.22,
                LookAhead = 0.0,
                LookSideOffset = 0.0,
                LookUpOffset = 0.0,
                bLookAtBulletVisual = 1.0,
                FOV = 0.66,
                CameraLagSpeed = 48.0,
                LookLagSpeed = 68.0,
                CameraShakeAmplitude = 0.0004,
                CameraShakeFrequency = 9.5,
                DOFFocusRange = 0.95,
                DOFBlurRadius = 2.6,
                BulletScaleMultiplier = 0.56,
                BulletSpinRevolutions = 46.0
            },
            {
                time = 1.00,
                CameraRailAlphaOverride = 1.000,
                LookRailAlphaOverride = 1.000,
                BulletRailAlphaOverride = 1.000,
                ForwardOffset = 2.72,
                SideOffset = 0.08,
                UpOffset = -0.24,
                LookAhead = 0.0,
                LookSideOffset = 0.0,
                LookUpOffset = 0.0,
                bLookAtBulletVisual = 1.0,
                FOV = 0.62,
                CameraLagSpeed = 48.0,
                LookLagSpeed = 68.0,
                CameraShakeAmplitude = 0.0002,
                CameraShakeFrequency = 9.5,
                DOFFocusRange = 1.1,
                DOFBlurRadius = 2.2,
                BulletScaleMultiplier = 0.54,
                BulletSpinRevolutions = 46.0
            }
        }
    },
    near_bullet_sway_drop = {
        bullet = {
            spinRevolutions = 54.0,
            spinPhase = 0.0,
            scale = 1.0
        },
        shockwave = {
            enabled = true,
            forwardOffset = -0.30,
            sideOffset = 0.0,
            upOffset = 0.0,
            radius = 0.092,
            startRadiusBoost = 0.12,
            width = 0.032,
            strength = 0.014,
            startStrengthBoost = 0.030,
            falloff = 1.30,
            directionalStretch = 4.4,
            decay = 5.0
        },
        rig_frames = {
            {
                time = 0.00,
                bAllowRailExtrapolation = 1.0,
                bClampAuthoredRailAlpha = 1.0,
                RailAlphaClampMin = -0.10,
                RailAlphaClampMax = 1.0,
                CameraRailAlphaOverride = 0.000,
                LookRailAlphaOverride = 0.020,
                BulletRailAlphaOverride = 0.020,
                ForwardOffset = 1.75,
                SideOffset = -0.42,
                UpOffset = 0.44,
                LookAhead = -0.02,
                LookSideOffset = 0.0,
                LookUpOffset = -0.42,
                bLookAtBulletVisual = 1.0,
                FOV = 0.76,
                Roll = 0.0,
                CameraLagSpeed = 34.0,
                LookLagSpeed = 70.0,
                CameraShakeAmplitude = 0.0018,
                CameraShakeFrequency = 12.0,
                DOFFocusRange = 0.70,
                DOFBlurRadius = 4.7,
                OrbitBlend = 0.0,
                BulletScaleMultiplier = 0.96,
                BulletSpinRevolutions = 30.0
            },
            {
                time = 0.12,
                CameraRailAlphaOverride = 0.060,
                LookRailAlphaOverride = 0.085,
                BulletRailAlphaOverride = 0.120,
                ForwardOffset = 1.36,
                SideOffset = 0.70,
                UpOffset = 0.26,
                LookAhead = -0.02,
                LookSideOffset = 0.0,
                LookUpOffset = -0.46,
                bLookAtBulletVisual = 1.0,
                FOV = 0.80,
                Roll = -1.6,
                CameraLagSpeed = 46.0,
                LookLagSpeed = 72.0,
                CameraShakeAmplitude = 0.0019,
                CameraShakeFrequency = 12.5,
                DOFFocusRange = 0.66,
                DOFBlurRadius = 5.0,
                BulletScaleMultiplier = 1.00,
                BulletSpinRevolutions = 38.0
            },
            {
                time = 0.24,
                CameraRailAlphaOverride = 0.180,
                LookRailAlphaOverride = 0.220,
                BulletRailAlphaOverride = 0.275,
                ForwardOffset = 1.24,
                SideOffset = -0.68,
                UpOffset = 0.15,
                LookAhead = -0.01,
                LookSideOffset = 0.0,
                LookUpOffset = -0.44,
                bLookAtBulletVisual = 1.0,
                FOV = 0.86,
                Roll = 1.4,
                CameraLagSpeed = 50.0,
                LookLagSpeed = 72.0,
                CameraShakeAmplitude = 0.0017,
                CameraShakeFrequency = 12.5,
                DOFFocusRange = 0.62,
                DOFBlurRadius = 5.0,
                BulletScaleMultiplier = 1.00,
                BulletSpinRevolutions = 40.0
            },
            {
                time = 0.40,
                CameraRailAlphaOverride = 0.360,
                LookRailAlphaOverride = 0.420,
                BulletRailAlphaOverride = 0.460,
                ForwardOffset = 1.12,
                SideOffset = 0.52,
                UpOffset = -0.04,
                LookAhead = 0.0,
                LookSideOffset = 0.0,
                LookUpOffset = -0.36,
                bLookAtBulletVisual = 1.0,
                FOV = 0.78,
                Roll = -1.0,
                CameraLagSpeed = 52.0,
                LookLagSpeed = 76.0,
                CameraShakeAmplitude = 0.0012,
                CameraShakeFrequency = 11.0,
                DOFFocusRange = 0.58,
                DOFBlurRadius = 4.8,
                BulletScaleMultiplier = 0.98,
                BulletSpinRevolutions = 42.0
            },
            {
                time = 0.62,
                CameraRailAlphaOverride = 0.720,
                LookRailAlphaOverride = 0.930,
                BulletRailAlphaOverride = 1.000,
                ForwardOffset = 1.18,
                SideOffset = -0.18,
                UpOffset = -0.22,
                LookAhead = 0.0,
                LookSideOffset = 0.0,
                LookUpOffset = -0.30,
                bLookAtBulletVisual = 1.0,
                FOV = 0.60,
                Roll = 0.6,
                CameraLagSpeed = 50.0,
                LookLagSpeed = 76.0,
                CameraShakeAmplitude = 0.0009,
                CameraShakeFrequency = 9.8,
                DOFFocusRange = 0.62,
                DOFBlurRadius = 4.3,
                BulletScaleMultiplier = 0.94,
                BulletSpinRevolutions = 34.0
            },
            {
                time = 0.80,
                CameraRailAlphaOverride = 0.900,
                LookRailAlphaOverride = 1.000,
                BulletRailAlphaOverride = 1.000,
                ForwardOffset = 1.04,
                SideOffset = 0.34,
                UpOffset = -0.18,
                LookAhead = 0.0,
                LookSideOffset = 0.0,
                LookUpOffset = -0.20,
                bLookAtBulletVisual = 1.0,
                FOV = 0.70,
                Roll = -0.4,
                CameraLagSpeed = 42.0,
                LookLagSpeed = 74.0,
                CameraShakeAmplitude = 0.0005,
                CameraShakeFrequency = 8.0,
                DOFFocusRange = 0.68,
                DOFBlurRadius = 3.7,
                BulletScaleMultiplier = 0.74,
                BulletSpinRevolutions = 36.0
            },
            {
                time = 0.90,
                CameraRailAlphaOverride = 0.980,
                LookRailAlphaOverride = 1.000,
                BulletRailAlphaOverride = 1.000,
                ForwardOffset = 1.24,
                SideOffset = 0.50,
                UpOffset = -0.12,
                LookAhead = 0.0,
                LookSideOffset = 0.0,
                LookUpOffset = -0.08,
                bLookAtBulletVisual = 1.0,
                FOV = 0.68,
                Roll = 0.0,
                CameraLagSpeed = 28.0,
                LookLagSpeed = 76.0,
                CameraShakeAmplitude = 0.00025,
                CameraShakeFrequency = 7.5,
                DOFFocusRange = 0.74,
                DOFBlurRadius = 3.2,
                BulletScaleMultiplier = 0.66,
                BulletSpinRevolutions = 38.0
            },
            {
                time = 1.00,
                CameraRailAlphaOverride = 1.000,
                LookRailAlphaOverride = 1.000,
                BulletRailAlphaOverride = 1.000,
                ForwardOffset = 1.46,
                SideOffset = 0.66,
                UpOffset = -0.06,
                LookAhead = 0.10,
                LookSideOffset = 0.0,
                LookUpOffset = -0.10,
                bLookAtBulletVisual = 1.0,
                FOV = 0.64,
                Roll = 0.0,
                CameraLagSpeed = 24.0,
                LookLagSpeed = 76.0,
                CameraShakeAmplitude = 0.0001,
                CameraShakeFrequency = 7.0,
                DOFFocusRange = 1.0,
                DOFBlurRadius = 2.4,
                BulletScaleMultiplier = 0.60,
                BulletSpinRevolutions = 40.0
            }
        }
    },
    side_close_orbit_impact = {
        bullet = {
            spinRevolutions = 104.0,
            spinPhase = 0.0,
            scale = 1.0
        },
        shockwave = {
            enabled = true,
            forwardOffset = -0.36,
            sideOffset = 0.0,
            upOffset = 0.0,
            radius = 0.096,
            startRadiusBoost = 0.13,
            width = 0.034,
            strength = 0.015,
            startStrengthBoost = 0.032,
            falloff = 1.28,
            directionalStretch = 4.5,
            decay = 4.9
        },
        rig_frames = {
            {
                time = 0.00,
                bAllowRailExtrapolation = 1.0,
                bClampAuthoredRailAlpha = 1.0,
                RailAlphaClampMin = -0.12,
                RailAlphaClampMax = 1.0,
                CameraRailAlphaOverride = 0.020,
                LookRailAlphaOverride = 0.060,
                BulletRailAlphaOverride = 0.020,
                ForwardOffset = -1.42,
                SideOffset = -1.30,
                UpOffset = 0.36,
                LookAhead = 0.24,
                LookSideOffset = -0.30,
                LookUpOffset = 0.05,
                bLookAtBulletVisual = 1.0,
                FOV = 0.58,
                Roll = 0.0,
                CameraLagSpeed = 42.0,
                LookLagSpeed = 58.0,
                DOFFocusRange = 0.72,
                DOFBlurRadius = 4.8,
                OrbitBlend = 0.0,
                OrbitYaw = -72.0,
                OrbitPitch = 10.0,
                OrbitRadius = 1.70,
                OrbitPivotForwardOffset = 0.0,
                OrbitPivotSideOffset = 0.0,
                OrbitPivotUpOffset = 0.0,
                BulletScaleMultiplier = 1.18
            },
            {
                time = 0.18,
                CameraRailAlphaOverride = 0.170,
                LookRailAlphaOverride = 0.210,
                BulletRailAlphaOverride = 0.180,
                ForwardOffset = -1.02,
                SideOffset = -1.00,
                UpOffset = 0.24,
                LookAhead = 0.14,
                LookSideOffset = -0.18,
                LookUpOffset = 0.02,
                bLookAtBulletVisual = 1.0,
                FOV = 0.52,
                CameraLagSpeed = 48.0,
                LookLagSpeed = 62.0,
                DOFFocusRange = 0.56,
                DOFBlurRadius = 5.0,
                OrbitBlend = 0.0,
                OrbitYaw = -64.0,
                OrbitPitch = 8.0,
                OrbitRadius = 1.42,
                OrbitPivotForwardOffset = 0.0,
                OrbitPivotSideOffset = 0.0,
                OrbitPivotUpOffset = 0.0,
                BulletScaleMultiplier = 1.12
            },
            {
                time = 0.30,
                CameraRailAlphaOverride = 0.300,
                LookRailAlphaOverride = 0.335,
                BulletRailAlphaOverride = 0.300,
                ForwardOffset = -0.66,
                SideOffset = -0.54,
                UpOffset = 0.12,
                LookAhead = 0.04,
                LookSideOffset = 0.0,
                LookUpOffset = 0.0,
                bLookAtBulletVisual = 1.0,
                FOV = 0.46,
                CameraLagSpeed = 56.0,
                LookLagSpeed = 70.0,
                DOFFocusRange = 0.40,
                DOFBlurRadius = 5.3,
                OrbitBlend = 0.0,
                OrbitYaw = -52.0,
                OrbitPitch = 5.0,
                OrbitRadius = 1.08,
                OrbitPivotForwardOffset = 0.0,
                OrbitPivotSideOffset = 0.0,
                OrbitPivotUpOffset = 0.0,
                BulletScaleMultiplier = 1.06
            },
            {
                time = 0.48,
                CameraRailAlphaOverride = 0.480,
                LookRailAlphaOverride = 0.510,
                BulletRailAlphaOverride = 0.480,
                ForwardOffset = -0.46,
                SideOffset = 0.18,
                UpOffset = 0.02,
                LookAhead = 0.0,
                LookSideOffset = 0.0,
                LookUpOffset = 0.0,
                bLookAtBulletVisual = 1.0,
                FOV = 0.44,
                CameraLagSpeed = 58.0,
                LookLagSpeed = 72.0,
                DOFFocusRange = 0.42,
                DOFBlurRadius = 4.8,
                OrbitBlend = 0.0,
                OrbitYaw = -32.0,
                OrbitPitch = 0.0,
                OrbitRadius = 0.95,
                OrbitPivotForwardOffset = 0.0,
                OrbitPivotSideOffset = 0.0,
                OrbitPivotUpOffset = 0.0,
                BulletScaleMultiplier = 1.0
            },
            {
                time = 0.68,
                CameraRailAlphaOverride = 0.760,
                LookRailAlphaOverride = 0.940,
                BulletRailAlphaOverride = 1.000,
                ForwardOffset = -0.42,
                SideOffset = 0.46,
                UpOffset = -0.04,
                LookAhead = 0.0,
                LookSideOffset = 0.0,
                LookUpOffset = 0.0,
                bLookAtBulletVisual = 1.0,
                FOV = 0.42,
                CameraLagSpeed = 58.0,
                LookLagSpeed = 72.0,
                DOFFocusRange = 0.48,
                DOFBlurRadius = 4.1,
                OrbitBlend = 0.0,
                OrbitYaw = -18.0,
                OrbitPitch = -3.0,
                OrbitRadius = 0.92,
                OrbitPivotForwardOffset = 0.0,
                OrbitPivotSideOffset = 0.0,
                OrbitPivotUpOffset = 0.0,
                BulletScaleMultiplier = 0.96
            },
            {
                time = 0.80,
                CameraRailAlphaOverride = 0.940,
                LookRailAlphaOverride = 1.000,
                BulletRailAlphaOverride = 1.000,
                ForwardOffset = -0.58,
                SideOffset = 0.52,
                UpOffset = -0.06,
                LookAhead = 0.0,
                LookSideOffset = 0.0,
                LookUpOffset = 0.0,
                bLookAtBulletVisual = 1.0,
                FOV = 0.56,
                CameraLagSpeed = 54.0,
                LookLagSpeed = 72.0,
                DOFFocusRange = 0.58,
                DOFBlurRadius = 3.6,
                OrbitBlend = 0.20,
                OrbitYaw = -5.0,
                OrbitPitch = -7.0,
                OrbitRadius = 1.92,
                OrbitPivotForwardOffset = 0.10,
                OrbitPivotSideOffset = 0.0,
                OrbitPivotUpOffset = 0.0,
                BulletScaleMultiplier = 0.76
            },
            {
                time = 0.90,
                CameraRailAlphaOverride = 1.000,
                LookRailAlphaOverride = 1.000,
                BulletRailAlphaOverride = 1.000,
                ForwardOffset = -0.74,
                SideOffset = 0.28,
                UpOffset = -0.10,
                LookAhead = 0.04,
                LookSideOffset = 0.0,
                LookUpOffset = 0.0,
                bLookAtBulletVisual = 1.0,
                FOV = 0.54,
                CameraLagSpeed = 48.0,
                LookLagSpeed = 72.0,
                DOFFocusRange = 0.78,
                DOFBlurRadius = 2.9,
                OrbitBlend = 0.72,
                OrbitYaw = 32.0,
                OrbitPitch = -11.0,
                OrbitRadius = 2.58,
                OrbitPivotForwardOffset = 0.18,
                OrbitPivotSideOffset = 0.0,
                OrbitPivotUpOffset = -0.02,
                BulletScaleMultiplier = 0.66
            },
            {
                time = 1.00,
                CameraRailAlphaOverride = 1.000,
                LookRailAlphaOverride = 1.000,
                BulletRailAlphaOverride = 1.000,
                ForwardOffset = -0.92,
                SideOffset = -0.18,
                UpOffset = -0.16,
                LookAhead = 0.10,
                LookSideOffset = 0.0,
                LookUpOffset = 0.0,
                bLookAtBulletVisual = 1.0,
                FOV = 0.52,
                CameraLagSpeed = 42.0,
                LookLagSpeed = 72.0,
                DOFFocusRange = 1.0,
                DOFBlurRadius = 2.2,
                OrbitBlend = 1.0,
                OrbitYaw = 72.0,
                OrbitPitch = -14.0,
                OrbitRadius = 3.18,
                OrbitPivotForwardOffset = 0.30,
                OrbitPivotSideOffset = 0.0,
                OrbitPivotUpOffset = -0.04,
                BulletScaleMultiplier = 0.58
            }
        }
    }
}

local SNIPER_KILLCAM_PROFILE_CYCLE = {
    "front_pass_tail",
    "right_high_center_rush",
    "near_bullet_sway_drop",
    "side_close_orbit_impact"
}

local next_sniper_killcam_profile_cycle_index = 1

local function take_next_sniper_killcam_profile_id()
    local profile_id = SNIPER_KILLCAM_PROFILE_CYCLE[next_sniper_killcam_profile_cycle_index]
    next_sniper_killcam_profile_cycle_index = next_sniper_killcam_profile_cycle_index + 1
    if next_sniper_killcam_profile_cycle_index > #SNIPER_KILLCAM_PROFILE_CYCLE then
        next_sniper_killcam_profile_cycle_index = 1
    end
    return profile_id or "front_pass_tail"
end

local function resolve_sniper_killcam_profile(payload)
    local profile_id = "front_pass_tail"
    if type(payload) == "table" and type(payload.profile) == "string" then
        profile_id = payload.profile
    end
    return SNIPER_KILLCAM_PROFILES[profile_id] or SNIPER_KILLCAM_PROFILES.front_pass_tail
end

local function apply_sniper_killcam_profile(current)
    if SniperKillCam == nil or SniperKillCam.SetRigScalars == nil then
        return
    end

    local profile = current.killcam_profile
    if type(profile) ~= "table" then
        return
    end

    local duration = tonumber(current.travel_duration) or tonumber(current.duration) or 0.0
    local alpha = duration > 0.0001 and ((tonumber(current.elapsed) or 0.0) / duration) or 0.0
    local rig = sample_keyframes(profile.rig_frames, alpha)
    rig = merge_tables(rig, current.rig_overrides)
    local clamped_alpha = clamp01(alpha)
    local authored_bullet_scale = tonumber(rig.BulletScaleMultiplier)
    if authored_bullet_scale ~= nil then
        local shrink_alpha = smoothstep(clamped_alpha * clamped_alpha)
        rig.BulletScaleMultiplier = lerp(
            authored_bullet_scale,
            SNIPER_KILLCAM_IMPACT_BULLET_SCALE,
            shrink_alpha)
    end

    local authored_fov = tonumber(rig.FOV)
    if authored_fov ~= nil then
        rig.FOV = authored_fov * lerp(1.0, 1.0 + SNIPER_KILLCAM_TRAVEL_FOV_BOOST, smoothstep(clamped_alpha))
    end

    local pre_impact_alpha = smoothstep((clamped_alpha - 0.88) / 0.12)
    local authored_bullet_forward_offset = tonumber(rig.BulletForwardOffset) or 0.0
    rig.BulletForwardOffset = authored_bullet_forward_offset
        + SNIPER_KILLCAM_PRE_IMPACT_FORWARD_DISTANCE * pre_impact_alpha
    if clamped_alpha >= 0.999 then
        current.impact_bullet_forward_offset = rig.BulletForwardOffset
    end
    current.last_killcam_rig = rig
    SniperKillCam.SetRigScalars(rig)
end

local function apply_sniper_killcam_post_impact(current)
    if SniperKillCam == nil or SniperKillCam.SetRigScalars == nil then
        return
    end

    local post_seconds = tonumber(current.post_impact_seconds) or 0.0
    if post_seconds <= 0.0001 then
        return
    end

    local impact_time = tonumber(current.impact_time) or 0.0
    local elapsed = tonumber(current.elapsed) or 0.0
    local alpha = clamp01((elapsed - impact_time) / post_seconds)
    if elapsed >= impact_time and current.shockwave_disabled_after_impact ~= true then
        current.shockwave_disabled_after_impact = true
        disable_sniper_killcam_shockwave()
    end
    if alpha <= 0.0 then
        return
    end

    local eased = smoothstep(alpha)
    local impact_bullet_forward_offset = tonumber(current.impact_bullet_forward_offset)
    if impact_bullet_forward_offset == nil and type(current.last_killcam_rig) == "table" then
        impact_bullet_forward_offset = tonumber(current.last_killcam_rig.BulletForwardOffset)
    end
    impact_bullet_forward_offset = impact_bullet_forward_offset or SNIPER_KILLCAM_PRE_IMPACT_FORWARD_DISTANCE
    SniperKillCam.SetRigScalars({
        CameraRailAlphaOverride = 1.0,
        LookRailAlphaOverride = 1.0,
        BulletRailAlphaOverride = 1.0,
        bEnableShockWave = 0.0,
        ShockWaveStrength = 0.0,
        ShockWaveStartStrengthBoost = 0.0,
        BulletForwardOffset = impact_bullet_forward_offset + SNIPER_KILLCAM_POST_IMPACT_FORWARD_DISTANCE * eased,
        BulletScaleMultiplier = SNIPER_KILLCAM_IMPACT_BULLET_SCALE
    })
end

function CutSceneManager.new(general)
    return setmetatable({
        general = general,
        registry = {},
        current = nil,
        killcam_blood_effects = {}
    }, CutSceneManager)
end

function CutSceneManager:Initialize()
    self:Register("sniper_killcam", {
        duration = SNIPER_KILLCAM_DURATION,
        skippable = true,
        on_begin = function(current)
            local payload = current.payload or {}
            local bullet_id = tonumber(payload.bullet_id) or 0
            local duration = tonumber(payload.duration) or current.duration or SNIPER_KILLCAM_DURATION
            local requested_travel_duration = tonumber(payload.travel_duration) or SNIPER_KILLCAM_TRAVEL_DURATION
            local travel_duration = math.max(0.001, math.min(duration, requested_travel_duration))
            local camera_mode = tonumber(payload.camera_mode) or 0
            clear_killcam_blood_effects(self)
            current.duration = duration
            current.travel_duration = travel_duration
            current.post_impact_seconds = math.max(0.0, duration - travel_duration)
            current.killcam_profile = resolve_sniper_killcam_profile(payload)
            current.rig_overrides = type(payload.rig) == "table" and payload.rig or nil
            current.slowdown_sfx_played = false
            current.bullet_cam_sfx_played = false
            current.impact_sfx_played = false
            current.impact_event_published = false
            current.forced_blood_played = false
            current.camera_flash_out_played = false
            current.camera_flash_in_played = false
            current.use_raw_delta_time = true
            current.previous_time_dilation = get_time_dilation()
            current.previous_world_paused = is_world_paused()
            current.shockwave_disabled_after_impact = false
            current.impact_time = math.max(0.0, travel_duration)
            current.impact_effect_time = tonumber(payload.impact_effect_time) or tonumber(payload.forced_blood_time) or SNIPER_KILLCAM_FORCED_BLOOD_DELAY
            current.forced_blood_time = current.impact_effect_time
            current.impact_sfx_time = tonumber(payload.impact_sfx_time) or current.impact_effect_time
            current.impact_sfx = payload.impact_sfx or SNIPER_KILLCAM_DAMAGED_SFX
            current.impact_sfx_volume = tonumber(payload.impact_sfx_volume) or SNIPER_KILLCAM_IMPACT_SFX_VOLUME
            current.killcam_hit = payload.hit
            current.impact_bullet_forward_offset = nil
            current.killcam_sfx_handles = {}
            force_scope_released()
            set_time_dilation(0.0)
            set_world_paused(true)
            if SniperKillCam ~= nil and SniperKillCam.Start ~= nil then
                if SniperKillCam.ConfigureBullet ~= nil then
                    SniperKillCam.ConfigureBullet(merge_tables(current.killcam_profile.bullet, payload.bullet))
                end
                if SniperKillCam.ConfigureShockWave ~= nil then
                    SniperKillCam.ConfigureShockWave(merge_tables(current.killcam_profile.shockwave, payload.shockwave))
                end
                apply_sniper_killcam_profile(current)
                local started = SniperKillCam.Start(bullet_id, travel_duration, camera_mode)
                current.director_started = started == true
            end
            if self.general ~= nil and self.general.Publish ~= nil then
                self.general:Publish("cutscene.presentation", {
                    active = true
                })
                self.general:Publish("cutscene.skip_prompt", {
                    visible = true,
                    text = "Press Space to Skip"
                })
            end
        end,
        on_tick = function(current, dt)
            if current.skipped == true then
                return
            end
            tick_killcam_blood_effects(self, dt)
            local floor_hit = consume_sniper_killcam_floor_hit_payload((current.payload or {}).bullet_id)
            if floor_hit ~= nil then
                current.floor_hit = floor_hit
                self:Stop("floor_hit")
                return
            end
            apply_sniper_killcam_profile(current)
            apply_sniper_killcam_post_impact(current)
            floor_hit = check_sniper_killcam_floor_hit_payload((current.payload or {}).bullet_id)
            if floor_hit ~= nil then
                current.floor_hit = floor_hit
                self:Stop("floor_hit")
                return
            end
            local flash_out_alpha = tonumber(current.killcam_profile.flash_out_alpha)
            local flash_in_alpha = tonumber(current.killcam_profile.flash_in_alpha)
            if flash_out_alpha ~= nil and flash_in_alpha ~= nil then
                local duration = tonumber(current.travel_duration) or tonumber(current.duration) or 0.0
                local alpha = duration > 0.0001 and ((tonumber(current.elapsed) or 0.0) / duration) or 0.0
                if current.camera_flash_out_played ~= true and alpha >= flash_out_alpha then
                    current.camera_flash_out_played = true
                    camera_fade_out(tonumber(current.killcam_profile.flash_out_duration) or SNIPER_KILLCAM_FLASH_OUT_DURATION)
                end
                if current.camera_flash_in_played ~= true and alpha >= flash_in_alpha then
                    current.camera_flash_in_played = true
                    camera_fade_in(tonumber(current.killcam_profile.flash_in_duration) or SNIPER_KILLCAM_FLASH_IN_DURATION)
                end
            end
            if current.slowdown_sfx_played ~= true and
                (tonumber(current.elapsed) or 0.0) >= SNIPER_KILLCAM_SLOWDOWN_SFX_DELAY then
                current.slowdown_sfx_played = true
                current.killcam_sfx_handles[#current.killcam_sfx_handles + 1] =
                    play_sfx_handle(self.general, SNIPER_KILLCAM_SLOWDOWN_SFX, 1.0)
            end
            if current.bullet_cam_sfx_played ~= true and
                (tonumber(current.elapsed) or 0.0) >= SNIPER_KILLCAM_BULLET_CAM_SFX_DELAY then
                current.bullet_cam_sfx_played = true
                current.killcam_sfx_handles[#current.killcam_sfx_handles + 1] =
                    play_sfx_handle(self.general, SNIPER_KILLCAM_BULLET_CAM_SFX, 1.0)
            end
            if current.forced_blood_played ~= true and
                (tonumber(current.elapsed) or 0.0) >= (tonumber(current.forced_blood_time) or SNIPER_KILLCAM_FORCED_BLOOD_DELAY) then
                current.forced_blood_played = true
                local payload = current.payload or {}
                current.killcam_hit = current.killcam_hit or payload.hit or resolve_sniper_killcam_hit_payload(payload.bullet_id)
                local direct_blood_spawned = queue_killcam_blood_effects(self, current.killcam_hit or payload.hit)
                if self.general ~= nil and self.general.Publish ~= nil then
                    self.general:Publish("sniper.killcam_impact", {
                        bullet_id = payload.bullet_id,
                        hit = current.killcam_hit or payload.hit,
                        elapsed = current.elapsed,
                        impact_time = current.forced_blood_time,
                        forced = true,
                        direct_blood_spawned = direct_blood_spawned,
                        cutscene = current.id
                    })
                end
            end
            if current.impact_sfx_played ~= true and
                (tonumber(current.elapsed) or 0.0) >= (tonumber(current.impact_sfx_time) or tonumber(current.forced_blood_time) or SNIPER_KILLCAM_FORCED_BLOOD_DELAY) then
                current.impact_sfx_played = true
                local payload = current.payload or {}
                current.killcam_hit = current.killcam_hit or payload.hit or resolve_sniper_killcam_hit_payload(payload.bullet_id)
                local location = get_killcam_hit_location(current.killcam_hit or payload.hit)
                current.killcam_sfx_handles[#current.killcam_sfx_handles + 1] =
                    play_impact_sfx(self.general, current.impact_sfx, current.impact_sfx_volume, location)
            end
            if current.impact_event_published ~= true and
                (tonumber(current.elapsed) or 0.0) >= (tonumber(current.impact_time) or 0.0) then
                current.impact_event_published = true
                if self.general ~= nil and self.general.Publish ~= nil then
                    local payload = current.payload or {}
                    self.general:Publish("sniper.killcam_impact", {
                        bullet_id = payload.bullet_id,
                        hit = current.killcam_hit or payload.hit,
                        elapsed = current.elapsed,
                        impact_time = current.impact_time,
                        cutscene = current.id
                    })
                end
            end
            if was_confirm_pressed() then
                current.skipped = true
                self:Stop("skipped")
            end
        end,
        on_end = function(current)
            if current.reason == "skipped" or current.reason == "floor_hit" then
                stop_killcam_sfx_handles(self.general, current.killcam_sfx_handles)
            end
            if current.reason ~= "finished" then
                clear_killcam_blood_effects(self)
            end
            current.killcam_sfx_handles = nil
            if SniperKillCam ~= nil and SniperKillCam.Stop ~= nil then
                SniperKillCam.Stop()
            end
            if SniperKillCam ~= nil and SniperKillCam.ClearPendingBullets ~= nil then
                SniperKillCam.ClearPendingBullets()
            end
            if current.camera_flash_out_played == true and current.camera_flash_in_played ~= true then
                camera_fade_in(0.05)
            end
            if current.previous_time_dilation ~= nil then
                set_time_dilation(current.previous_time_dilation)
                current.previous_time_dilation = nil
            end
            set_world_paused(current.previous_world_paused == true)
            current.previous_world_paused = nil
            disable_sniper_killcam_shockwave()
            force_scope_released()
            if self.general ~= nil and self.general.Publish ~= nil then
                self.general:Publish("cutscene.presentation", {
                    active = false
                })
                self.general:Publish("cutscene.skip_prompt", {
                    visible = false,
                    text = ""
                })
            end
        end
    })
end

function CutSceneManager:Shutdown()
    self:Stop("shutdown")
    clear_killcam_blood_effects(self)
end

function CutSceneManager:Register(id, definition)
    if type(id) ~= "string" or type(definition) ~= "table" then
        return false
    end

    self.registry[id] = definition
    return true
end

function CutSceneManager:Play(id, payload)
    local definition = self.registry[id]
    if definition == nil then
        print("[CutSceneManager] missing cutscene: " .. tostring(id))
        return false
    end

    self:Stop("replace")
    self.current = {
        id = id,
        definition = definition,
        payload = payload or {},
        elapsed = 0.0,
        duration = tonumber((payload or {}).duration) or tonumber(definition.duration) or 0.0
    }

    if definition.on_begin ~= nil then
        pcall(definition.on_begin, self.current)
    end
    self.general:Publish("cutscene.started", self.current)
    return true
end

function CutSceneManager:Stop(reason)
    if self.current == nil then
        return
    end

    local finished = self.current
    finished.reason = reason
    if finished.definition.on_end ~= nil then
        pcall(finished.definition.on_end, finished)
    end
    self.current = nil
    self.general:Publish("cutscene.stopped", finished)
end

function CutSceneManager:Tick(dt)
    if self.current == nil then
        tick_killcam_blood_effects(self, get_raw_delta_time(dt or 0.0))
        self:PollSniperKillCam()
        return
    end

    local current = self.current
    local step = dt or 0.0
    if current.use_raw_delta_time == true then
        step = get_raw_delta_time(step)
    end
    current.elapsed = current.elapsed + step
    if current.definition.on_tick ~= nil then
        pcall(current.definition.on_tick, current, step)
    end
    if self.current ~= current then
        return
    end

    if current.duration > 0.0 and current.elapsed >= current.duration then
        local next_state = current.definition.next_state
        self:Stop("finished")
        if next_state ~= nil then
            self.general:RequestState(next_state, { reason = "cutscene_finished", cutscene = current.id })
        end
    end
end

function CutSceneManager:PollSniperKillCam()
    if SniperKillCam == nil or SniperKillCam.ConsumePendingBulletId == nil then
        return
    end

    local bullet_id = SniperKillCam.ConsumePendingBulletId()
    if bullet_id == nil or bullet_id == 0 then
        return
    end

    local hit = resolve_sniper_killcam_hit_payload(bullet_id)
    local payload = {
        bullet_id = bullet_id,
        hit = hit,
        forced_blood_time = SNIPER_KILLCAM_FORCED_BLOOD_DELAY,
        impact_effect_time = SNIPER_KILLCAM_FORCED_BLOOD_DELAY,
        impact_sfx_time = SNIPER_KILLCAM_FORCED_BLOOD_DELAY,
        impact_sfx = SNIPER_KILLCAM_DAMAGED_SFX,
        impact_sfx_volume = SNIPER_KILLCAM_IMPACT_SFX_VOLUME,
        duration = SNIPER_KILLCAM_DURATION,
        travel_duration = SNIPER_KILLCAM_TRAVEL_DURATION,
        camera_mode = 0,
        profile = take_next_sniper_killcam_profile_id()
    }
    if self.general ~= nil and self.general.Publish ~= nil then
        self.general:Publish("sniper.killcam_triggered", payload)
    end
    self:Play("sniper_killcam", payload)
end

return CutSceneManager
