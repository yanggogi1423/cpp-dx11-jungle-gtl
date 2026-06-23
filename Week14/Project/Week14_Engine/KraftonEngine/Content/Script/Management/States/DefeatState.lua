local DefeatState = {}
DefeatState.__index = DefeatState

local SCENE_PATH = "Content/Scene/Defeat.Scene"
local RESULT_HUD = {
    name = "ResultHUD",
    path = "Content/UI/ResultHUD.rml",
    z_order = 40,
    mode = "Result",
    payload = {
        result = "Defeat",
        result_radio_only = true
    }
}

local FALLBACK_RESULT_SECONDS = 10.0
local DEFEAT_BGM_KEY = "DefeatBGM"
local DEFEAT_BGM_PATH = "BGM/Defeat.mp3"
local DEFEAT_BGM_VOLUME = 1.0

local function log(message)
    if Debug and Debug.Log then
        Debug.Log("[DefeatState] " .. message)
    else
        print("[DefeatState] " .. message)
    end
end

local function is_defeat_comment(id)
    return id == "Defeat1" or id == "Defeat2"
end

function DefeatState.new(general, state_id)
    return setmetatable({
        general = general,
        state_id = state_id,
        entered = false,
        elapsed = 0.0,
        result_hud_requested = false
    }, DefeatState)
end

function DefeatState:GetHUD()
    return RESULT_HUD
end

function DefeatState:GetScenePath()
    return SCENE_PATH
end

function DefeatState:Enter()
    self.entered = true
    self.elapsed = 0.0
    self.result_hud_requested = false

    if self.general ~= nil and self.general.UnsubscribeOwner ~= nil then
        self.general:UnsubscribeOwner(self)
    end
    if self.general ~= nil and self.general.Subscribe ~= nil then
        self.general:Subscribe("radio.comment_finished", self, function(payload)
            self:OnRadioCommentFinished(payload)
        end)
    end
end

function DefeatState:Exit()
    self.entered = false
    if self.general ~= nil and self.general.UnsubscribeOwner ~= nil then
        self.general:UnsubscribeOwner(self)
    end
end

function DefeatState:Tick(dt)
    if self.entered ~= true or self.result_hud_requested == true then
        return
    end

    self.elapsed = self.elapsed + math.max(0.0, tonumber(dt) or 0.0)
    if self.elapsed < FALLBACK_RESULT_SECONDS then
        return
    end

    local radio = self.general ~= nil and self.general.managers ~= nil and self.general.managers.Radio or nil
    local has_current = radio ~= nil and radio.current ~= nil
    local has_queue = radio ~= nil and radio.queue ~= nil and #radio.queue > 0
    if has_current or has_queue then
        return
    end

    log("radio finish event missed; showing result HUD by fallback")
    self:ShowResultHUD("defeat_radio_fallback")
end

function DefeatState:OnRadioCommentFinished(payload)
    if self.entered ~= true or self.result_hud_requested == true then
        return
    end

    local id = payload ~= nil and payload.id or nil
    if not is_defeat_comment(id) then
        return
    end

    self:ShowResultHUD("defeat_sequence_complete")
end

function DefeatState:ShowResultHUD(reason)
    if self.result_hud_requested == true then
        return
    end

    self.result_hud_requested = true
    self:PlayDefeatBGM()
    if self.general == nil or self.general.Publish == nil then
        return
    end

    self.general:Publish("scene.hud_requested", {
        state = self.state_id,
        hud = RESULT_HUD,
        payload = {
            result = "Defeat",
            result_radio_only = false
        },
        reason = reason or "defeat_sequence_complete"
    })
end

function DefeatState:PlayDefeatBGM()
    local audio = self.general ~= nil and self.general.managers ~= nil and self.general.managers.Audio or nil
    if audio ~= nil and audio.LoadBGMOnce ~= nil and audio.PlayBGM ~= nil then
        if audio:LoadBGMOnce(DEFEAT_BGM_KEY, DEFEAT_BGM_PATH) then
            audio:PlayBGM(DEFEAT_BGM_KEY, DEFEAT_BGM_VOLUME)
        else
            log("Defeat BGM load failed path=" .. DEFEAT_BGM_PATH)
        end
        return
    end

    if AudioManager ~= nil and AudioManager.Load ~= nil and AudioManager.PlayBGM ~= nil then
        if AudioManager.Load(DEFEAT_BGM_KEY, DEFEAT_BGM_PATH, true) == true then
            AudioManager.PlayBGM(DEFEAT_BGM_KEY, DEFEAT_BGM_VOLUME)
        else
            log("Defeat BGM load failed path=" .. DEFEAT_BGM_PATH)
        end
    end
end

return DefeatState
