local TopDownSupport = require("TopDownSupport")

local BattleCruiserWarpMVP = {}
BattleCruiserWarpMVP.__index = BattleCruiserWarpMVP

BattleCruiserWarpMVP.Properties = {
    MeshAssetPath = {
        Type = "String",
        Default = "Asset/Mesh/BattleCruiser/Battlecruiser.uasset",
        Category = "BattleCruiser"
    },
    AnimationAssetPath = {
        Type = "String",
        Default = "Asset/Animation/Battlecruiser_Armature_Armature_Walk_full.uasset",
        Category = "BattleCruiser"
    },
    MoveSpeed = {
        Type = "Float",
        Default = 8.0,
        Min = 0.0,
        Category = "Movement"
    },
    WarpMoveSpeed = {
        Type = "Float",
        Default = 22.0,
        Min = 0.0,
        Category = "Movement"
    },
    Acceleration = {
        Type = "Float",
        Default = 10.0,
        Min = 0.0,
        Category = "Movement"
    },
    Deceleration = {
        Type = "Float",
        Default = 7.0,
        Min = 0.0,
        Category = "Movement"
    },
    BrakeAcceleration = {
        Type = "Float",
        Default = 16.0,
        Min = 0.0,
        Category = "Movement"
    },
    ReverseSpeedScale = {
        Type = "Float",
        Default = 0.35,
        Min = 0.0,
        Category = "Movement"
    },
    TurnSpeed = {
        Type = "Float",
        Default = 95.0,
        Min = 0.0,
        Category = "Movement"
    },
    TurnAcceleration = {
        Type = "Float",
        Default = 320.0,
        Min = 0.0,
        Category = "Movement"
    },
    TurnDamping = {
        Type = "Float",
        Default = 260.0,
        Min = 0.0,
        Category = "Movement"
    },
    ActorYawOffset = {
        Type = "Float",
        Default = 0.0,
        Category = "Movement"
    },
    WarpDuration = {
        Type = "Float",
        Default = 0.30,
        Min = 0.05,
        Category = "Warp"
    },
    WarpExitDistance = {
        Type = "Float",
        Default = 360.0,
        Min = 10.0,
        Category = "Warp"
    },
    WarpExitAcceleration = {
        Type = "Float",
        Default = 1000.0,
        Min = 10.0,
        Category = "Warp"
    },
    WarpStarHoldDuration = {
        Type = "Float",
        Default = 3.8,
        Min = 0.0,
        Category = "Warp"
    },
    WarpStretchDuration = {
        Type = "Float",
        Default = 6.9,
        Min = 0.05,
        Category = "Warp"
    },
    WarpRevealDuration = {
        Type = "Float",
        Default = 0.8,
        Min = 0.05,
        Category = "Warp"
    },
    WarpStageX = {
        Type = "Float",
        Default = 1000.0,
        Category = "Warp"
    },
    WarpStageY = {
        Type = "Float",
        Default = 1000.0,
        Category = "Warp"
    },
    WarpStageZ = {
        Type = "Float",
        Default = 1000.0,
        Category = "Warp"
    },
    WarpFOV = {
        Type = "Float",
        Default = 96.0,
        Min = 1.0,
        Category = "Warp"
    },
    WarpIntroDistance = {
        Type = "Float",
        Default = 54.0,
        Min = 0.0,
        Category = "Warp"
    },
    WarpIntroDuration = {
        Type = "Float",
        Default = 0.95,
        Min = 0.05,
        Category = "Warp"
    },
    EngineParticleAssetPath = {
        Type = "String",
        Default = "Asset/Particle System/PS_BattleCruiser_Engine.uasset",
        Category = "VFX"
    },
    EngineParticlePrefabPath = {
        Type = "String",
        Default = "Asset/Prefab/BattleCruiserEngineVFX.prefab",
        Category = "VFX"
    },
    WarpStarParticleAssetPath = {
        Type = "String",
        Default = "Asset/Particle System/PS_BattleCruiser_WarpStar.uasset",
        Category = "VFX"
    },
    EngineBackOffset = {
        Type = "Float",
        Default = -2.2,
        Category = "VFX"
    },
    EngineSideOffset = {
        Type = "Float",
        Default = 0.55,
        Category = "VFX"
    },
    EngineUpOffset = {
        Type = "Float",
        Default = 0.05,
        Category = "VFX"
    },
    EngineNormalScale = {
        Type = "Float",
        Default = 0.32,
        Min = 0.0,
        Category = "VFX"
    },
    EngineWarpScale = {
        Type = "Float",
        Default = 1.15,
        Min = 0.0,
        Category = "VFX"
    },
    WarpStreakForwardOffset = {
        Type = "Float",
        Default = 100.0,
        Category = "VFX"
    },
    WarpTunnelRadius = {
        Type = "Float",
        Default = 17.0,
        Min = 0.0,
        Category = "VFX"
    },
    bAutoSetupBattleCruiser = {
        Type = "Bool",
        Default = true,
        Category = "BattleCruiser"
    },
    OperationalSFXPath = {
        Type = "String",
        Default = "Asset/Sound/BattleCruiserOperational.mp3",
        Category = "Sound"
    },
    WarpSFXPath = {
        Type = "String",
        Default = "Asset/Sound/Warp.mp3",
        Category = "Sound"
    },
    OperationalSFXDelay = {
        Type = "Float",
        Default = 1.0,
        Min = 0.0,
        Category = "Sound"
    },
    SFXVolume = {
        Type = "Float",
        Default = 1.0,
        Min = 0.0,
        Category = "Sound"
    }
}

local function get_prop(properties, key, fallback)
    local value = properties and properties[key]
    if value == nil then
        return fallback
    end
    return value
end

local function clamp(value, minValue, maxValue)
    return TopDownSupport.Clamp(value, minValue, maxValue)
end

local function move_towards(current, target, maxDelta)
    if current < target then
        return math.min(current + maxDelta, target)
    end

    if current > target then
        return math.max(current - maxDelta, target)
    end

    return target
end

local function smoothstep(t)
    t = clamp(t, 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)
end

local function lerp(a, b, t)
    return a + (b - a) * t
end

local function lerp_vector(a, b, t)
    return Vector(
        lerp(a.X, b.X, t),
        lerp(a.Y, b.Y, t),
        lerp(a.Z, b.Z, t)
    )
end

local function is_key_down(key)
    return TopDownSupport.IsKeyDown(key)
end

local function get_axis(positiveKeys, negativeKeys)
    local value = 0.0

    for _, key in ipairs(positiveKeys) do
        if is_key_down(key) then
            value = value + 1.0
            break
        end
    end

    for _, key in ipairs(negativeKeys) do
        if is_key_down(key) then
            value = value - 1.0
            break
        end
    end

    return clamp(value, -1.0, 1.0)
end

local function is_space_down()
    return is_key_down("Space") or is_key_down("SpaceBar") or is_key_down("VK_SPACE")
end

local function make_ship_input()
    return {
        Throttle = get_axis({ "W", "w", "Up", "UpArrow" }, { "S", "s", "Down", "DownArrow" }),
        Turn = get_axis({ "D", "d", "Right", "RightArrow" }, { "A", "a", "Left", "LeftArrow" })
    }
end

local function play_sfx(path, volume)
    if not path or path == "" then
        return 0
    end

    local audio = Engine and Engine.API and Engine.API.Audio
    if not audio or not audio.PlaySFX then
        return 0
    end

    local ok, handle = pcall(function()
        return audio.PlaySFX(path, volume or 1.0)
    end)

    if ok then
        return handle or 0
    end

    return 0
end

local function direction_from_yaw(yawDegrees)
    local yaw = math.rad(yawDegrees)
    return Vector(math.cos(yaw), math.sin(yaw), 0.0)
end

local function fract(value)
    return value - math.floor(value)
end

local function hash01(seed)
    return fract(math.sin(seed * 12.9898 + 78.233) * 43758.5453)
end

local function local_to_world(origin, yawDegrees, localOffset)
    local yaw = math.rad(yawDegrees)
    local cosYaw = math.cos(yaw)
    local sinYaw = math.sin(yaw)

    local x = origin.X + localOffset.X * cosYaw - localOffset.Y * sinYaw
    local y = origin.Y + localOffset.X * sinYaw + localOffset.Y * cosYaw
    local z = origin.Z + localOffset.Z

    return Vector(x, y, z)
end

local function set_actor_active(actor, active)
    if not actor then
        return
    end

    local component = nil
    if actor.GetParticleSystemComponent then
        local ok, result = pcall(function()
            return actor:GetParticleSystemComponent()
        end)
        if ok then
            component = result
        end
    end

    if component then
        pcall(function()
            component.OpacityMultiplier = active and 1.0 or 0.0
        end)
    end

    if component and component.SetActive then
        pcall(function()
            component:SetActive(active)
        end)
    end

    if component and component.RefreshTemplateRuntime then
        pcall(function()
            component:RefreshTemplateRuntime(true)
        end)
    end
end

local function set_particle_opacity(actor, opacity)
    if not actor then
        return
    end

    if actor.GetParticleSystemComponent then
        local ok, component = pcall(function()
            return actor:GetParticleSystemComponent()
        end)
        if ok and component then
            pcall(function()
                component.OpacityMultiplier = clamp(opacity or 0.0, 0.0, 1.0)
            end)
        end
    end
end

local function is_valid_actor(actor)
    if not actor then
        return false
    end

    local world = Engine and Engine.API and Engine.API.World
    if world and world.IsValidActor then
        local ok, valid = pcall(function()
            return world.IsValidActor(actor)
        end)
        if ok then
            return valid == true
        end
        return false
    end

    return true
end

local function get_player_controller()
    return TopDownSupport.GetPlayerController()
end

local function spawn_actor_by_type(typeName)
    local world = Engine and Engine.API and Engine.API.World
    if not world or not world.SpawnActor then
        return nil
    end

    local ok, actor = pcall(function()
        return world.SpawnActor(typeName)
    end)

    if ok then
        return actor
    end

    return nil
end

local function spawn_actor_from_prefab(path)
    local world = Engine and Engine.API and Engine.API.World
    if not world or not world.SpawnActorFromPrefab or not path or path == "" then
        return nil
    end

    local ok, actor = pcall(function()
        return world.SpawnActorFromPrefab(path)
    end)

    if ok then
        return actor
    end

    return nil
end

function BattleCruiserWarpMVP.new(scriptComponent, properties)
    local self = setmetatable({}, BattleCruiserWarpMVP)

    self.Component = scriptComponent
    self.Actor = nil
    self.Mesh = nil
    self.Properties = properties or {}

    if scriptComponent and scriptComponent.GetOwner then
        local ok, owner = pcall(function()
            return scriptComponent:GetOwner()
        end)
        if ok then
            self.Actor = owner
        end
    end

    self.bInitialized = false
    self.bSpaceWasDown = false
    self.WarpTime = 0.0
    self.bWarping = false
    self.bStartFadeReleased = false
    self.bExitFadeInStarted = false
    self.bExitFadeOutStarted = false
    self.bWarpStageEntered = false
    self.bWarpReturnStarted = false
    self.ElapsedTime = 0.0
    self.bOperationalSFXPlayed = false
    self.WarpSFXHandle = 0
    self.SavedLocation = nil
    self.SavedRotation = nil
    self.SavedHeadingYaw = 0.0
    self.SavedForwardSpeed = 0.0
    self.SavedTurnRate = 0.0
    self.SavedCameraRelativeLocation = nil
    self.WarpStageLocation = nil
    self.WarpBaseYaw = 0.0
    self.WarpCameraActor = nil
    self.WarpCameraComponent = nil
    self.WarpCameraCleanupActor = nil
    self.WarpCameraCleanupTime = 0.0
    self.bViewTargetBound = false
    self.ForwardSpeed = 0.0
    self.TurnRate = 0.0
    self.HeadingYaw = 0.0
    self.Effects = {}
    self.bEffectsInitialized = false

    return self
end

function BattleCruiserWarpMVP:Initialize()
    if self.bInitialized then
        return true
    end

    local owner = self.Actor or Actor or Owner
    if not owner then
        return false
    end

    self.Actor = owner
    self:InitializeMovementState()

    if get_prop(self.Properties, "bAutoSetupBattleCruiser", true) then
        self:SetupBattleCruiserMesh()
    else
        self.Mesh = TopDownSupport.FindSkeletalMesh(self.Actor)
    end

    self:PlayLoopingAnimation()
    self:UseActorCamera()
    self:InitializeVFX()

    self.bInitialized = true
    return true
end

function BattleCruiserWarpMVP:InitializeMovementState()
    local actorYawOffset = get_prop(self.Properties, "ActorYawOffset", 0.0)
    local rotation = self.Actor and self.Actor.Rotation or nil

    if rotation then
        self.HeadingYaw = TopDownSupport.NormalizeAngleDegrees((rotation.Z or 0.0) - actorYawOffset)
    else
        self.HeadingYaw = 0.0
    end
    self.InitialPlayerStartYaw = self.HeadingYaw

    self.ForwardSpeed = self.ForwardSpeed or 0.0
    self.TurnRate = self.TurnRate or 0.0
end

function BattleCruiserWarpMVP:SpawnParticleEffect(name, assetPath, prefabPath, initialActive)
    if (assetPath == nil or assetPath == "") and (prefabPath == nil or prefabPath == "") then
        return nil
    end

    local actor = spawn_actor_from_prefab(prefabPath)
        or spawn_actor_by_type("AParticleSystemActor")
        or spawn_actor_by_type("ParticleSystemActor")

    if not actor then
        return nil
    end

    if actor.SetParticleTemplateAssetPath and assetPath and assetPath ~= "" then
        pcall(function()
            actor:SetParticleTemplateAssetPath(assetPath)
        end)
    elseif actor.SetTemplateAssetPath and assetPath and assetPath ~= "" then
        pcall(function()
            actor:SetTemplateAssetPath(assetPath)
        end)
    end

    actor:AddTag("BattleCruiserVFX")
    actor:AddTag(name)
    set_actor_active(actor, initialActive == true)

    local entry = {
        Name = name,
        Actor = actor,
        Active = initialActive == true
    }

    table.insert(self.Effects, entry)
    return entry
end

function BattleCruiserWarpMVP:InitializeVFX()
    if self.bEffectsInitialized then
        return
    end

    local engineAsset = get_prop(self.Properties, "EngineParticleAssetPath", "Asset/Particle System/PS_BattleCruiser_Engine.uasset")
    local warpStarAsset = get_prop(self.Properties, "WarpStarParticleAssetPath", "Asset/Particle System/PS_BattleCruiser_WarpStar.uasset")
    local enginePrefab = get_prop(self.Properties, "EngineParticlePrefabPath", "Asset/Prefab/BattleCruiserEngineVFX.prefab")

    self.EngineLeftEffect = self:SpawnParticleEffect("EngineLeft", engineAsset, enginePrefab, true)
    self.EngineRightEffect = self:SpawnParticleEffect("EngineRight", engineAsset, enginePrefab, true)
    self.JumpFlashEffect = self:SpawnParticleEffect("JumpFlash", engineAsset, enginePrefab, false)

    self.WarpStarEffect = self:SpawnParticleEffect("WarpStarTunnel", warpStarAsset, "", false)
    self.WarpCoreEffects = {}

    self.bEffectsInitialized = true
end

function BattleCruiserWarpMVP:SetEffectTransformFromBase(entry, baseLocation, localOffset, scale, active, yawOverride)
    if not entry or not entry.Actor or not baseLocation then
        return
    end

    if not is_valid_actor(entry.Actor) then
        entry.Actor = nil
        entry.Active = false
        return
    end

    if not active and entry.Active == false then
        return
    end

    local yaw = yawOverride or ((self.bWarping and self.bWarpStageEntered and self.WarpBaseYaw) or self.HeadingYaw or 0.0)
    local appliedScale = active and scale or 0.001
    if active then
        entry.Actor.Location = local_to_world(baseLocation, yaw, localOffset)
        entry.Actor.Rotation = Vector(0.0, 0.0, yaw + get_prop(self.Properties, "ActorYawOffset", 0.0))
        entry.Actor.Scale = Vector(appliedScale, appliedScale, appliedScale)
    else
        entry.Actor.Scale = Vector(appliedScale, appliedScale, appliedScale)
    end

    if entry.Active ~= active then
        entry.Active = active
        set_actor_active(entry.Actor, active)
    end
end

function BattleCruiserWarpMVP:SetEffectTransformVectorScaleFromBase(entry, baseLocation, localOffset, scaleVector, active, yawOverride)
    if not entry or not entry.Actor or not baseLocation then
        return
    end

    if not is_valid_actor(entry.Actor) then
        entry.Actor = nil
        entry.Active = false
        return
    end

    if not active and entry.Active == false then
        return
    end

    local yaw = yawOverride or ((self.bWarping and self.bWarpStageEntered and self.WarpBaseYaw) or self.HeadingYaw or 0.0)
    if active then
        entry.Actor.Location = local_to_world(baseLocation, yaw, localOffset)
        entry.Actor.Rotation = Vector(0.0, 0.0, yaw + get_prop(self.Properties, "ActorYawOffset", 0.0))
        entry.Actor.Scale = scaleVector
    else
        entry.Actor.Scale = Vector(0.001, 0.001, 0.001)
    end

    if entry.Active ~= active then
        entry.Active = active
        set_actor_active(entry.Actor, active)
    end
end

function BattleCruiserWarpMVP:SetEffectTransform(entry, localOffset, scale, active)
    if not self.Actor then
        return
    end

    self:SetEffectTransformFromBase(entry, self.Actor.Location, localOffset, scale, active)
end

function BattleCruiserWarpMVP:GetWarpCameraWorldLocation()
    if self.WarpCameraActor and is_valid_actor(self.WarpCameraActor) then
        return self.WarpCameraActor.Location
    end

    if not self.Actor then
        return nil
    end

    local cameraRelativeLocation = self:GetCameraRelativeLocation()
        or self.SavedCameraRelativeLocation
        or Vector(0.0, 0.0, 0.0)
    return local_to_world(self.Actor.Location, self.HeadingYaw or 0.0, cameraRelativeLocation)
end

function BattleCruiserWarpMVP:GetWarpCameraVFXBase()
    if self.WarpCameraActor and is_valid_actor(self.WarpCameraActor) then
        local rotation = self.WarpCameraActor.Rotation
        return self.WarpCameraActor.Location, rotation and rotation.Z or (self.WarpBaseYaw or 0.0)
    end

    return self:GetWarpCameraWorldLocation(), self.WarpBaseYaw or self.HeadingYaw or 0.0
end

function BattleCruiserWarpMVP:UpdateVFX(dt)
    if not self.bEffectsInitialized then
        self:InitializeVFX()
    end

    if not self.Actor then
        return
    end

    local engineBack = get_prop(self.Properties, "EngineBackOffset", -2.65)
    local engineSide = get_prop(self.Properties, "EngineSideOffset", 0.55)
    local engineUp = get_prop(self.Properties, "EngineUpOffset", 0.05)

    local engineScale = self.bWarping
        and get_prop(self.Properties, "EngineWarpScale", 1.15)
        or get_prop(self.Properties, "EngineNormalScale", 0.32)
    self:SetEffectTransform(self.EngineLeftEffect, Vector(engineBack, -engineSide, engineUp), engineScale, true)
    self:SetEffectTransform(self.EngineRightEffect, Vector(engineBack, engineSide, engineUp), engineScale, true)

    local forwardOffset = get_prop(self.Properties, "WarpStreakForwardOffset", 2.0)
    local tunnelRadius = get_prop(self.Properties, "WarpTunnelRadius", 17.0)
    local warpActive = self.bWarping == true and self.bWarpStageEntered == true
    local warpBaseLocation, warpBaseYaw = self.Actor.Location, self.HeadingYaw or 0.0
    if warpActive then
        local cameraLocation, cameraYaw = self:GetWarpCameraVFXBase()
        warpBaseLocation = cameraLocation or self.Actor.Location
        warpBaseYaw = cameraYaw or self.WarpBaseYaw or 0.0
    end
    local holdDuration = get_prop(self.Properties, "WarpStarHoldDuration", 3.8)
    local stretchDuration = get_prop(self.Properties, "WarpStretchDuration", 6.9)
    local revealDuration = get_prop(self.Properties, "WarpRevealDuration", 0.8)
    local exitTravelDuration = math.max(get_prop(self.Properties, "WarpDuration", 0.30), 0.05)
    local warpVisualTime = math.max((self.WarpTime or 0.0) - 0.22, 0.0)
    local starEndTime = holdDuration
    local approachEndTime = starEndTime + stretchDuration
    local windupEndTime = approachEndTime + revealDuration
    local launchEndTime = windupEndTime + exitTravelDuration
    local starsActive = warpActive and warpVisualTime < (launchEndTime + 0.12)
    self:SetEffectTransformVectorScaleFromBase(self.WarpStarEffect, warpBaseLocation, Vector(forwardOffset, 0.0, 0.0), Vector(1.0, 1.0, 1.0), starsActive, warpBaseYaw)
    if self.WarpStarEffect then
        set_particle_opacity(self.WarpStarEffect.Actor, starsActive and 1.0 or 0.0)
    end

    if self.WarpCoreEffects then
        for _, entry in ipairs(self.WarpCoreEffects) do
            self:SetEffectTransformFromBase(entry, warpBaseLocation, Vector(0.0, 0.0, 0.0), 0.001, false, warpBaseYaw)
        end
    end

    local revealFlashTime = 0.22 + approachEndTime
    local exitFlashTime = 0.22 + launchEndTime
    local flashActive = warpActive and (
        math.abs((self.WarpTime or 0.0) - revealFlashTime) < 0.28 or
        math.abs((self.WarpTime or 0.0) - exitFlashTime) < 0.25)
    self:SetEffectTransformFromBase(self.JumpFlashEffect, warpBaseLocation, Vector(-1.0, 0.0, 0.45), self.bWarping and 0.65 or 0.8, flashActive, warpBaseYaw)
end

function BattleCruiserWarpMVP:HideWarpVFX()
    local baseLocation = self.Actor and self.Actor.Location or Vector(0.0, 0.0, 0.0)

    if self.WarpStarEffect then
        self:SetEffectTransformFromBase(self.WarpStarEffect, baseLocation, Vector(0.0, 0.0, 0.0), 0.001, false)
    end

    if self.WarpCoreEffects then
        for _, entry in ipairs(self.WarpCoreEffects) do
            self:SetEffectTransformFromBase(entry, baseLocation, Vector(0.0, 0.0, 0.0), 0.001, false)
        end
    end

    self:SetEffectTransformFromBase(self.JumpFlashEffect, baseLocation, Vector(0.0, 0.0, 0.0), 0.001, false)
end

function BattleCruiserWarpMVP:SetupBattleCruiserMesh()
    self.Mesh = TopDownSupport.FindSkeletalMesh(self.Actor)

    if not self.Mesh and self.Actor.AddComponent then
        local ok, component = pcall(function()
            return self.Actor:AddComponent("SkeletalMeshComponent", true)
        end)
        if ok then
            self.Mesh = component
        end
    end

    if not self.Mesh then
        return
    end

    local meshPath = get_prop(self.Properties, "MeshAssetPath", "Asset/Mesh/BattleCruiser/Battlecruiser.uasset")
    if meshPath ~= "" and self.Mesh.SetSkeletalMeshAssetPath then
        pcall(function()
            self.Mesh:SetSkeletalMeshAssetPath(meshPath)
        end)
    end
end

function BattleCruiserWarpMVP:PlayLoopingAnimation()
    if not self.Mesh then
        return
    end

    local animPath = get_prop(self.Properties, "AnimationAssetPath", "")
    if animPath ~= "" and self.Mesh.PlayAnimationAssetPath then
        pcall(function()
            self.Mesh:PlayAnimationAssetPath(animPath, true)
        end)
        return
    end

    if self.Mesh.Play then
        pcall(function()
            self.Mesh:Play(true)
        end)
    end
end

function BattleCruiserWarpMVP:BeginPlay()
    self:Initialize()
end

function BattleCruiserWarpMVP:UseActorCamera()
    local controller = get_player_controller()
    if not controller then
        return
    end

    if controller.SetViewTargetWithBlend and self.Actor then
        pcall(function()
            controller:SetViewTargetWithBlend(self.Actor, 0.0, 0)
        end)
        self.bViewTargetBound = true
    end

    if controller.SetInputModeGameOnly then
        pcall(function()
            controller:SetInputModeGameOnly()
        end)
    end
end

function BattleCruiserWarpMVP:GetViewCamera()
    local controller = get_player_controller()
    if not controller or not controller.GetViewTargetCamera then
        return nil
    end

    local ok, camera = pcall(function()
        return controller:GetViewTargetCamera()
    end)

    if ok then
        return camera
    end

    return nil
end

function BattleCruiserWarpMVP:SetCameraRelativeLocation(location)
    local camera = self:GetViewCamera()
    if not camera or not camera.SetRelativeLocation then
        return
    end

    pcall(function()
        camera:SetRelativeLocation(location)
    end)
end

function BattleCruiserWarpMVP:GetCameraRelativeLocation()
    local camera = self:GetViewCamera()
    if not camera or not camera.GetRelativeLocation then
        return nil
    end

    local ok, location = pcall(function()
        return camera:GetRelativeLocation()
    end)

    if ok then
        return location
    end

    return nil
end

function BattleCruiserWarpMVP:StartWarp()
    self.bWarping = true
    self.WarpTime = 0.0
    self.bStartFadeReleased = false
    self.bExitFadeInStarted = false
    self.bExitFadeOutStarted = false
    self.bWarpStageEntered = false
    self.bWarpReturnStarted = false

    self.SavedLocation = self.Actor and self.Actor.Location or nil
    self.SavedRotation = self.Actor and self.Actor.Rotation or nil
    self.SavedHeadingYaw = self.HeadingYaw or 0.0
    self.SavedForwardSpeed = self.ForwardSpeed or 0.0
    self.SavedTurnRate = self.TurnRate or 0.0
    self.SavedCameraRelativeLocation = self:GetCameraRelativeLocation()
    self.WarpStageLocation = Vector(
        get_prop(self.Properties, "WarpStageX", 1000.0),
        get_prop(self.Properties, "WarpStageY", 1000.0),
        get_prop(self.Properties, "WarpStageZ", 1000.0)
    )
    self.WarpBaseYaw = self.InitialPlayerStartYaw or self.HeadingYaw or 0.0
    self:EnsureWarpCameraActor()
    self:SetWarpCameraHeldAtStage(-get_prop(self.Properties, "WarpIntroDistance", 34.0), 0.0)
    self.WarpSFXHandle = play_sfx(
        get_prop(self.Properties, "WarpSFXPath", "Asset/Sound/Warp.mp3"),
        get_prop(self.Properties, "SFXVolume", 1.0))

    local controller = get_player_controller()
    if controller then
        if self.WarpCameraActor and controller.SetViewTargetWithBlend then
            pcall(function()
                controller:SetViewTargetWithBlend(self.WarpCameraActor, 0.0, 0)
            end)
            self.bViewTargetBound = true
        end
        pcall(function()
            controller:StartCameraFade(0.0, 1.0, 0.18, 1.0, 1.0, 1.0)
        end)
        pcall(function()
            controller:LerpCameraFOVDegrees(get_prop(self.Properties, "WarpFOV", 96.0), 0.35)
        end)
        pcall(function()
            controller:StartCameraLetterbox(2.35, 0.25)
        end)
    end
end

function BattleCruiserWarpMVP:EndWarp()
    self.bWarping = false
    self.WarpTime = 0.0
    self.bWarpStageEntered = false
    self.bWarpReturnStarted = false

    if self.Actor then
        if self.SavedLocation then
            self.Actor.Location = self.SavedLocation
        end
        if self.SavedRotation then
            self.Actor.Rotation = self.SavedRotation
        end
    end

    if self.SavedCameraRelativeLocation then
        self:UseActorCamera()
        self:SetCameraRelativeLocation(self.SavedCameraRelativeLocation)
    end

    self.HeadingYaw = self.SavedHeadingYaw or self.HeadingYaw or 0.0
    self.ForwardSpeed = self.SavedForwardSpeed or 0.0
    self.TurnRate = self.SavedTurnRate or 0.0
    self:HideWarpVFX()

    local controller = get_player_controller()
    if controller then
        if self.Actor and controller.SetViewTargetWithBlend then
            pcall(function()
                controller:SetViewTargetWithBlend(self.Actor, 0.0, 0)
            end)
            self.bViewTargetBound = true
        end
        pcall(function()
            controller:ResetCameraFOV(0.35)
        end)
        pcall(function()
            controller:StopCameraLetterbox(0.25)
        end)
        pcall(function()
            controller:ClearCameraVignette()
        end)
        pcall(function()
            controller:StartCameraFade(1.0, 0.0, 0.35, 1.0, 1.0, 1.0)
        end)
    end

    if self.WarpCameraActor and is_valid_actor(self.WarpCameraActor) then
        self.WarpCameraCleanupActor = self.WarpCameraActor
        self.WarpCameraCleanupTime = 0.45
    end
    self.WarpCameraActor = nil
    self.WarpCameraComponent = nil
end

function BattleCruiserWarpMVP:CleanupWarpCamera(dt)
    if not self.WarpCameraCleanupActor then
        return
    end

    self.WarpCameraCleanupTime = (self.WarpCameraCleanupTime or 0.0) - dt
    if self.WarpCameraCleanupTime > 0.0 then
        return
    end

    if is_valid_actor(self.WarpCameraCleanupActor) then
        pcall(function()
            self.WarpCameraCleanupActor:MarkPendingKill()
        end)
    end

    self.WarpCameraCleanupActor = nil
    self.WarpCameraCleanupTime = 0.0
end

function BattleCruiserWarpMVP:EnterWarpStage()
    if self.bWarpStageEntered or not self.Actor then
        return
    end

    self.bWarpStageEntered = true
    local yaw = self.WarpBaseYaw or self.HeadingYaw or 0.0
    local forward = direction_from_yaw(yaw)
    local introDistance = get_prop(self.Properties, "WarpIntroDistance", 34.0)
    self.Actor.Location = (self.WarpStageLocation or Vector(1000.0, 1000.0, 1000.0)) + forward * (-introDistance)
    self.Actor.Rotation = Vector(0.0, 0.0, (self.WarpBaseYaw or 0.0) + get_prop(self.Properties, "ActorYawOffset", 0.0))
    self.HeadingYaw = self.WarpBaseYaw or self.HeadingYaw or 0.0
    self.ForwardSpeed = get_prop(self.Properties, "WarpMoveSpeed", 22.0)
    self.TurnRate = 0.0

    self:SetWarpCameraHeldAtStage(-introDistance, 0.0)
end

function BattleCruiserWarpMVP:EnsureWarpCameraActor()
    if self.WarpCameraActor and is_valid_actor(self.WarpCameraActor) then
        return true
    end

    local actor = spawn_actor_by_type("AActor") or spawn_actor_by_type("Actor")
    if not actor then
        return false
    end

    actor:AddTag("BattleCruiserWarpCamera")
    local camera = nil
    if actor.AddComponent then
        local ok, component = pcall(function()
            return actor:AddComponent("CameraComponent", true)
        end)
        if ok then
            camera = component
        end
    end

    self.WarpCameraActor = actor
    self.WarpCameraComponent = camera
    return camera ~= nil
end

function BattleCruiserWarpMVP:SetWarpCameraHeldAtStage(shipPush, zOffset)
    if not self.WarpStageLocation then
        return
    end

    if not self:EnsureWarpCameraActor() then
        return
    end

    local yaw = self.WarpBaseYaw or self.HeadingYaw or 0.0
    local forward = direction_from_yaw(yaw)
    local introDistance = get_prop(self.Properties, "WarpIntroDistance", 54.0)
    local cameraDistance = math.max(8.0, introDistance - 20.0)
    local cameraHeight = zOffset or 0.0
    local stage = self.WarpStageLocation
    local cameraPush = -cameraDistance + 3.0
    local cameraLocation = stage + forward * cameraPush + Vector(0.0, 0.0, cameraHeight)

    self.WarpCameraActor.Location = cameraLocation
    self.WarpCameraActor.Rotation = Vector(0.0, 0.0, yaw)

    if self.WarpCameraComponent and self.WarpCameraComponent.look_at then
        local tiltDistance = 30.0
        local lookTarget = cameraLocation + forward * tiltDistance + Vector(0.0, 0.0, -tiltDistance * math.tan(math.rad(30.0)))
        pcall(function()
            self.WarpCameraComponent:look_at(lookTarget)
        end)
    end
end

function BattleCruiserWarpMVP:UpdateWarp(dt)
    if not self.bWarping then
        return
    end

    self.WarpTime = self.WarpTime + dt

    local holdDuration = get_prop(self.Properties, "WarpStarHoldDuration", 3.8)
    local stretchDuration = get_prop(self.Properties, "WarpStretchDuration", 6.9)
    local revealDuration = get_prop(self.Properties, "WarpRevealDuration", 0.8)
    local exitTravelDuration = math.max(get_prop(self.Properties, "WarpDuration", 0.30), 0.05)
    local enterFadeEnd = 0.22
    local holdStart = enterFadeEnd
    local holdEnd = holdStart + holdDuration
    local approachEnd = holdEnd + stretchDuration
    local windupEnd = approachEnd + revealDuration
    local exitEnd = windupEnd + exitTravelDuration
    local returnEnd = exitEnd + 0.35
    local controller = get_player_controller()

    if self.WarpTime >= enterFadeEnd then
        self:EnterWarpStage()
    end

    if controller and not self.bStartFadeReleased and self.WarpTime >= enterFadeEnd then
        self.bStartFadeReleased = true
        pcall(function()
            controller:StartCameraFade(1.0, 0.0, 0.35, 1.0, 1.0, 1.0)
        end)
        pcall(function()
            controller:ClearCameraVignette()
        end)
    end

    if self.bWarpStageEntered and self.Actor then
        local stage = self.WarpStageLocation or self.Actor.Location
        local yaw = self.WarpBaseYaw or self.HeadingYaw or 0.0
        local forward = direction_from_yaw(yaw)
        local right = Vector(-forward.Y, forward.X, 0.0)
        local up = Vector(0.0, 0.0, 1.0)
        local introDistance = get_prop(self.Properties, "WarpIntroDistance", 34.0)
        local approachStartPush = -introDistance
        local approachEndPush = -18.0
        local windupEndPush = -24.0
        if self.WarpTime < holdEnd then
            local t = clamp((self.WarpTime - holdStart) / math.max(holdDuration, 0.05), 0.0, 1.0)
            local push = approachStartPush
            self.Actor.Location = stage + forward * push
            self.Actor.Rotation = Vector(0.0, 0.0, yaw + get_prop(self.Properties, "ActorYawOffset", 0.0))
            self:SetWarpCameraHeldAtStage(push, 0.0)
        elseif self.WarpTime < approachEnd then
            local t = smoothstep((self.WarpTime - holdEnd) / math.max(stretchDuration, 0.05))
            local shipShake = math.sin(t * math.pi * 8.0) * (1.0 - t) * 0.75
            local side = math.sin(t * math.pi * 3.0) * 1.10 + shipShake * 0.34
            local vertical = math.sin(t * math.pi * 2.0 + 0.7) * 0.42 + math.cos(t * math.pi * 7.0) * (1.0 - t) * 0.18
            local push = lerp(approachStartPush, approachEndPush, t)
            self.Actor.Location = stage + forward * push + right * side + up * vertical
            self.Actor.Rotation = Vector(0.0, shipShake * 1.1, yaw + get_prop(self.Properties, "ActorYawOffset", 0.0) + math.sin(t * math.pi * 3.0) * 2.2 + shipShake * 1.4)
            self:SetWarpCameraHeldAtStage(push, 0.08 * t)
        elseif self.WarpTime < windupEnd then
            local t = smoothstep((self.WarpTime - approachEnd) / math.max(revealDuration, 0.05))
            local recoilPush = lerp(approachEndPush, windupEndPush, t)
            local recoilShake = math.sin(t * math.pi * 3.0) * 0.45
            self.Actor.Location = stage + forward * recoilPush + right * (recoilShake * 0.22) + up * (math.sin(t * math.pi) * 0.12)
            self.Actor.Rotation = Vector(0.0, -1.6 * t, yaw + get_prop(self.Properties, "ActorYawOffset", 0.0) - recoilShake)
            self:SetWarpCameraHeldAtStage(recoilPush, lerp(0.08, 0.02, t))
        elseif self.WarpTime < exitEnd then
            local rawT = clamp((self.WarpTime - windupEnd) / math.max(exitTravelDuration, 0.05), 0.0, 1.0)
            local t = smoothstep(rawT)
            local exitDistance = get_prop(self.Properties, "WarpExitDistance", 360.0)
            local exitAcceleration = get_prop(self.Properties, "WarpExitAcceleration", 1000.0)
            local launchT = t * t
            local exitPush = windupEndPush + math.min(exitDistance, exitAcceleration * launchT)
            self.Actor.Location = stage + forward * exitPush
            self.Actor.Rotation = Vector(0.0, math.sin(t * math.pi * 4.0) * (1.0 - t) * 0.35, yaw + get_prop(self.Properties, "ActorYawOffset", 0.0))
            self:SetWarpCameraHeldAtStage(windupEndPush, 0.02)
        end
    end

    if controller and not self.bExitFadeInStarted and self.WarpTime >= exitEnd - 0.16 then
        self.bExitFadeInStarted = true
        pcall(function()
            controller:StartCameraFade(0.0, 1.0, 0.16, 1.0, 1.0, 1.0)
        end)
    end

    if controller and not self.bExitFadeOutStarted and self.WarpTime >= exitEnd then
        self.bExitFadeOutStarted = true
    end

    if self.WarpTime >= returnEnd then
        self:EndWarp()
    end
end

function BattleCruiserWarpMVP:UpdateSFX(dt)
    self.ElapsedTime = (self.ElapsedTime or 0.0) + dt

    if self.bOperationalSFXPlayed then
        return
    end

    local delay = get_prop(self.Properties, "OperationalSFXDelay", 1.0)
    if self.ElapsedTime < delay then
        return
    end

    self.bOperationalSFXPlayed = true
    play_sfx(
        get_prop(self.Properties, "OperationalSFXPath", "Asset/Sound/BattleCruiserOperational.mp3"),
        get_prop(self.Properties, "SFXVolume", 1.0))
end

function BattleCruiserWarpMVP:UpdateMovement(dt)
    if not self.Actor then
        return
    end

    if self.bWarping then
        return
    end

    local input = make_ship_input()
    local maxForwardSpeed = self.bWarping
        and get_prop(self.Properties, "WarpMoveSpeed", 22.0)
        or get_prop(self.Properties, "MoveSpeed", 8.0)
    local maxReverseSpeed = maxForwardSpeed * get_prop(self.Properties, "ReverseSpeedScale", 0.35)

    local targetSpeed = 0.0
    if input.Throttle > 0.0 then
        targetSpeed = maxForwardSpeed * input.Throttle
    elseif input.Throttle < 0.0 then
        targetSpeed = maxReverseSpeed * input.Throttle
    end

    local accel = get_prop(self.Properties, "Acceleration", 10.0)
    if math.abs(targetSpeed) < math.abs(self.ForwardSpeed or 0.0) then
        accel = get_prop(self.Properties, "Deceleration", 7.0)
    end
    if input.Throttle < 0.0 and (self.ForwardSpeed or 0.0) > 0.0 then
        accel = get_prop(self.Properties, "BrakeAcceleration", 16.0)
    end

    self.ForwardSpeed = move_towards(self.ForwardSpeed or 0.0, targetSpeed, accel * dt)

    local speedRatio = 0.0
    if maxForwardSpeed > 0.001 then
        speedRatio = clamp(math.abs(self.ForwardSpeed) / maxForwardSpeed, 0.0, 1.0)
    end

    local lowSpeedSteerScale = 0.35 + 0.65 * speedRatio
    local targetTurnRate = input.Turn * get_prop(self.Properties, "TurnSpeed", 95.0) * lowSpeedSteerScale
    local turnAccel = targetTurnRate == 0.0
        and get_prop(self.Properties, "TurnDamping", 260.0)
        or get_prop(self.Properties, "TurnAcceleration", 320.0)

    self.TurnRate = move_towards(self.TurnRate or 0.0, targetTurnRate, turnAccel * dt)
    self.HeadingYaw = TopDownSupport.NormalizeAngleDegrees((self.HeadingYaw or 0.0) + self.TurnRate * dt)

    local forward = direction_from_yaw(self.HeadingYaw)
    local delta = forward * self.ForwardSpeed * dt

    if math.abs(self.ForwardSpeed) > 0.0001 then
        if self.Actor.Add_Actor_World_Offset then
            local ok = pcall(function()
                self.Actor:Add_Actor_World_Offset(delta)
            end)

            if not ok then
                self.Actor.Location = self.Actor.Location + delta
            end
        else
            self.Actor.Location = self.Actor.Location + delta
        end
    end

    local actorYaw = TopDownSupport.NormalizeAngleDegrees(self.HeadingYaw + get_prop(self.Properties, "ActorYawOffset", 0.0))
    pcall(function()
        self.Actor.Rotation = Vector(0.0, 0.0, actorYaw)
    end)
end

function BattleCruiserWarpMVP:Tick(deltaTime)
    if not self.bInitialized and not self:Initialize() then
        return
    end

    local dt = TopDownSupport.GetDeltaTime(deltaTime)
    self:UpdateSFX(dt)

    local spaceDown = is_space_down()
    if spaceDown and not self.bSpaceWasDown and not self.bWarping then
        self:StartWarp()
    end
    self.bSpaceWasDown = spaceDown

    self:UpdateWarp(dt)
    self:UpdateMovement(dt)
    self:UpdateVFX(dt)
    self:CleanupWarpCamera(dt)

    if not self.bViewTargetBound then
        self:UseActorCamera()
    end

    if self.Mesh and not self.Mesh:IsPlaying() then
        self:PlayLoopingAnimation()
    end
end

return BattleCruiserWarpMVP
