require("CoroutineManager")

local EventBus = require("Management/EventBus")
local GameState = require("Management/GameState")
local InGameManager = require("Management/InGameManager")
local DataManager = require("Management/DataManager")
local AudioManager = require("Management/AudioManager")
local RadioManager = require("Management/RadioManager")
local EffectManager = require("Management/EffectManager")
local UIManager = require("Management/UIManager")
local CutSceneManager = require("Management/CutSceneManager")
local SceneManager = require("Management/SceneManager")

local GeneralManager = {}
GeneralManager.__index = GeneralManager
GeneralManager._instance = nil

local CORE_ORDER = {
    "Data",
    "Audio",
    "Radio",
    "Effect",
    "UI",
    "CutScene",
    "InGame",
    "Scene"
}

local SCENE_START_STATES = {
    ["main.scene"] = GameState.Main,
    ["preingame.scene"] = GameState.PreInGame,
    ["loading.scene"] = GameState.Loading,
    ["ingame.scene"] = GameState.InGame,
    ["combattest.scene"] = GameState.InGame,
    ["victory.scene"] = GameState.Victory,
    ["defeat.scene"] = GameState.Defeat1
}

local function log(message)
    if Debug and Debug.Log then
        Debug.Log("[GeneralManager] " .. message)
    else
        print("[GeneralManager] " .. message)
    end
end

local function get_scene_file_name()
    if Scene == nil or Scene.GetCurrentPath == nil then
        return nil
    end

    local current_path = tostring(Scene.GetCurrentPath())
    local normalized = string.lower(string.gsub(current_path, "\\", "/"))
    return string.match(normalized, "([^/]+)$")
end

local function get_initial_state_from_scene()
    local scene_file = get_scene_file_name()
    if scene_file == nil then
        return nil
    end
    return SCENE_START_STATES[scene_file]
end

function GeneralManager.Get()
    if GeneralManager._instance == nil then
        GeneralManager._instance = setmetatable({
            initialized = false,
            initializing = false,
            runtime_active = false,
            context = nil,
            event_bus = nil,
            managers = {},
            tick_order = { "Scene", "InGame", "Effect", "CutScene", "Radio", "UI", "Audio", "Data" }
        }, GeneralManager)
    end
    return GeneralManager._instance
end

function GeneralManager:_EnsureEventBus()
    if self.event_bus == nil then
        self.event_bus = EventBus.new()
    end
    return self.event_bus
end

function GeneralManager:_EnsureInitialized()
    if not self.initialized and not self.initializing and self.runtime_active then
        self:Initialize(self.context)
    end
end

function GeneralManager:Initialize(context)
    if self.initialized or self.initializing then
        return
    end

    if context ~= nil then
        self.context = context
    end
    if self.context == nil then
        return
    end

    self.initializing = true
    self:_EnsureEventBus()
    self.managers = {
        Data = DataManager.new(self),
        Audio = AudioManager.new(self),
        Radio = RadioManager.new(self),
        Effect = EffectManager.new(self),
        UI = UIManager.new(self),
        CutScene = CutSceneManager.new(self),
        InGame = InGameManager.new(self),
        Scene = SceneManager.new(self)
    }

    if self.context.start_state ~= nil then
        self.managers.Scene:SetStartState(self.context.start_state)
    end

    self.initialized = true
    for _, name in ipairs(CORE_ORDER) do
        local manager = self.managers[name]
        if manager ~= nil and manager.Initialize ~= nil then
            manager:Initialize()
        end
    end

    self.initializing = false
    self:Publish("general.initialized", { state = self:GetState() })
end

function GeneralManager:Shutdown()
    if not self.initialized and not self.initializing then
        return
    end

    self.initializing = false
    for i = #CORE_ORDER, 1, -1 do
        local manager = self.managers[CORE_ORDER[i]]
        if manager ~= nil and manager.Shutdown ~= nil then
            manager:Shutdown()
        end
    end

    if self.event_bus ~= nil then
        self.event_bus:Clear()
    end

    self.initialized = false
    self.managers = {}
    self.event_bus = nil
end

function GeneralManager.OnWorldReset()
    local instance = GeneralManager._instance
    if instance ~= nil then
        instance.runtime_active = false
        instance.initializing = false
        instance:Shutdown()
        instance.context = nil
    end

    GeneralManager._instance = nil
    _G.GameGeneralManager = nil
end

function GeneralManager:Tick(dt)
    if not self.initialized then
        if self.runtime_active and self.context ~= nil then
            self:Initialize(self.context)
        end
        if not self.initialized then
            return
        end
    end

    for _, name in ipairs(self.tick_order) do
        local manager = self.managers[name]
        if manager ~= nil and manager.Tick ~= nil then
            manager:Tick(dt or 0.0)
        end
    end
end

function GeneralManager:Subscribe(event_name, owner, callback)
    return self:_EnsureEventBus():Subscribe(event_name, owner, callback)
end

function GeneralManager:Unsubscribe(token)
    if self.event_bus == nil then
        return false
    end
    return self.event_bus:Unsubscribe(token)
end

function GeneralManager:UnsubscribeOwner(owner)
    if self.event_bus == nil then
        return 0
    end
    return self.event_bus:ClearOwner(owner)
end

function GeneralManager:Publish(event_name, payload)
    return self:_EnsureEventBus():Publish(event_name, payload)
end

function GeneralManager:RequestState(next_state, payload)
    self:_EnsureInitialized()
    return self.managers.Scene:RequestState(next_state, payload)
end

function GeneralManager:GetState()
    if self.managers.Scene == nil then
        return GameState.Intro
    end
    return self.managers.Scene:GetState()
end

function GeneralManager:SetNickname(nickname)
    self:_EnsureInitialized()
    self.managers.Data:SetNickname(nickname)
end

function GeneralManager:AddScore(delta)
    self:_EnsureInitialized()
    self.managers.Data:AddScore(delta)
end

function GeneralManager:SetScore(score)
    self:_EnsureInitialized()
    self.managers.Data:SetScore(score)
end

function GeneralManager:GetScore()
    self:_EnsureInitialized()
    return self.managers.Data:GetScore()
end

function GeneralManager:GetSettings()
    self:_EnsureInitialized()
    return self.managers.Data:GetSettings()
end

function GeneralManager:SetSetting(key, value)
    self:_EnsureInitialized()
    local result = self.managers.Data:SetSetting(key, value)
    if result then
        if key == "bgm_volume" then
            self.managers.Audio:SetBGMVolume(value)
        elseif key == "sfx_volume" then
            self.managers.Audio:SetSFXVolume(value)
        end
    end
    return result
end

function GeneralManager:CommitRun(result)
    self:_EnsureInitialized()
    self.managers.Data:CommitRun(result)
end

function GeneralManager:SetInGameMatchDuration(seconds)
    self:_EnsureInitialized()
    if self.managers.InGame ~= nil and self.managers.InGame.SetMatchDuration ~= nil then
        return self.managers.InGame:SetMatchDuration(seconds)
    end
    return false
end

function GeneralManager:RequestVictory(reason)
    self:_EnsureInitialized()
    if self.managers.InGame ~= nil and self.managers.InGame.RequestVictory ~= nil then
        return self.managers.InGame:RequestVictory(reason)
    end
    return false
end

function GeneralManager:RequestDefeat(reason)
    self:_EnsureInitialized()
    if self.managers.InGame ~= nil and self.managers.InGame.RequestDefeat ~= nil then
        return self.managers.InGame:RequestDefeat(reason)
    end
    return false
end

function GeneralManager:PlaySFX(path_or_key, volume)
    self:_EnsureInitialized()
    return self.managers.Audio:PlaySFX(path_or_key, volume)
end

function GeneralManager:PlaySFXHandle(path_or_key, volume)
    self:_EnsureInitialized()
    return self.managers.Audio:PlaySFXHandle(path_or_key, volume)
end

function GeneralManager:QueueRadioComment(id, payload)
    self:_EnsureInitialized()
    if self.managers.Radio ~= nil and self.managers.Radio.Queue ~= nil then
        return self.managers.Radio:Queue(id, payload)
    end
    return false
end

function GeneralManager:QueueOpeningRadioComment(payload)
    self:_EnsureInitialized()
    if self.managers.Radio ~= nil and self.managers.Radio.QueueOpening ~= nil then
        return self.managers.Radio:QueueOpening(payload)
    end
    return false
end

function GeneralManager:FadeInSFX(handle, duration, target_volume)
    self:_EnsureInitialized()
    return self.managers.Audio:FadeInSFX(handle, duration, target_volume)
end

function GeneralManager:FadeOutSFX(handle, duration)
    self:_EnsureInitialized()
    return self.managers.Audio:FadeOutSFX(handle, duration)
end

function GeneralManager:PlayBGM(name, volume)
    self:_EnsureInitialized()
    return self.managers.Audio:PlayBGM(name, volume)
end

function GeneralManager:FadeInBGM(duration, target_volume)
    self:_EnsureInitialized()
    return self.managers.Audio:FadeInBGM(duration, target_volume)
end

function GeneralManager:FadeOutBGM(duration)
    self:_EnsureInitialized()
    return self.managers.Audio:FadeOutBGM(duration)
end

function GeneralManager:FadeInSound(handle, duration, target_volume)
    self:_EnsureInitialized()
    return self.managers.Audio:FadeInSound(handle, duration, target_volume)
end

function GeneralManager:FadeOutSound(handle, duration)
    self:_EnsureInitialized()
    return self.managers.Audio:FadeOutSound(handle, duration)
end

function GeneralManager:RegisterSceneHUD(state, widget_name, path, z_order, mode)
    self:_EnsureInitialized()
    local hud = widget_name
    if type(widget_name) ~= "table" then
        hud = {
            name = widget_name,
            path = path,
            z_order = z_order or 0,
            mode = mode
        }
    end
    return self.managers.Scene:RegisterHUD(state, hud)
end

function GeneralManager:RegisterCutScene(id, definition)
    self:_EnsureInitialized()
    return self.managers.CutScene:Register(id, definition)
end

function GeneralManager:PlayCutScene(id, payload)
    self:_EnsureInitialized()
    return self.managers.CutScene:Play(id, payload)
end

GeneralManager.State = GameState

local RuntimeGeneralManager = GeneralManager.Get()

local function get_initial_state()
    if this ~= nil and this.GetInitialGameStateName ~= nil then
        local ok, state = pcall(function()
            return this:GetInitialGameStateName()
        end)
        if ok and GameState.IsValid(state) then
            log("initial state from component=" .. tostring(state))
            return state
        end
        log("component initial state unavailable value=" .. tostring(state))
    end

    local scene_state = get_initial_state_from_scene()
    if GameState.IsValid(scene_state) then
        log("initial state from scene=" .. tostring(scene_state))
        return scene_state
    end

    log("initial state unavailable; SceneManager will use its default")
    return nil
end

function BeginPlay()
    RuntimeGeneralManager.runtime_active = true
    RuntimeGeneralManager:Initialize({
        actor = obj,
        script_component = this,
        start_state = get_initial_state()
    })
end

function EndPlay()
    RuntimeGeneralManager.runtime_active = false
    RuntimeGeneralManager:Shutdown()
end

function OnOverlap(OtherActor)
    RuntimeGeneralManager:Publish("actor.overlap", { self = obj, other = OtherActor })
end

function Tick(dt)
    UpdateCoroutines(dt)
    RuntimeGeneralManager:Tick(dt)
end

_G.GameGeneralManager = RuntimeGeneralManager

return GeneralManager
