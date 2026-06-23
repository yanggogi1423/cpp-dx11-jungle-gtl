local AudioManager = {}
AudioManager.__index = AudioManager

local INGAME_BGM_KEY = "InGameBGM"
local INGAME_BGM_PATH = "BGM/InGame.mp3"
local PAUSE_BGM_KEY = "PauseBGM"
local PAUSE_BGM_PATH = "BGM/Pause.mp3"
local BGM_SWITCH_FADE_SECONDS = 1.0
local DEFAULT_BGM_VOLUME = 1.0

local function engine_audio()
    return rawget(_G, "AudioManager")
end

function AudioManager.new(general)
    return setmetatable({
        general = general,
        master_volume = 1.0,
        bgm_volume = 1.0,
        sfx_volume = 1.0,
        loaded_bgm = {},
        current_bgm_key = nil,
        pending_bgm_switch = nil,
        bgm_switch_serial = 0,
        desired_bgm = nil,
        ingame_active = false,
        pause_active = false,
        opening_active = false,
        cutscene_active = false
    }, AudioManager)
end

function AudioManager:Initialize()
    local data = self.general and self.general.managers and self.general.managers.Data
    if data ~= nil and data.GetSettings ~= nil then
        local settings = data:GetSettings()
        self.bgm_volume = tonumber(settings.bgm_volume) or self.bgm_volume
        self.sfx_volume = tonumber(settings.sfx_volume) or self.sfx_volume
    end
    self:SetBGMVolume(self.bgm_volume)
    self:SetSFXVolume(self.sfx_volume)

    if self.general ~= nil and self.general.Subscribe ~= nil then
        self.general:Subscribe("ingame.started", self, function()
            self.ingame_active = true
            self:RequestInGameBGM()
        end)
        self.general:Subscribe("ingame.stopped", self, function()
            self.ingame_active = false
            self.pause_active = false
            self.desired_bgm = nil
            self:FadeOutCurrentBGM(BGM_SWITCH_FADE_SECONDS)
        end)
        self.general:Subscribe("ingame.pause_changed", self, function(payload)
            self:HandlePauseChanged(payload)
        end)
        self.general:Subscribe("radio.opening_presentation", self, function(payload)
            self:HandleOpeningPresentation(payload)
        end)
        self.general:Subscribe("cutscene.started", self, function()
            self.cutscene_active = true
            self:SuppressBGM(BGM_SWITCH_FADE_SECONDS)
        end)
        self.general:Subscribe("cutscene.stopped", self, function()
            self.cutscene_active = false
            self:ResumeDesiredBGM()
        end)
    end
end

function AudioManager:Shutdown()
    if self.general ~= nil and self.general.UnsubscribeOwner ~= nil then
        self.general:UnsubscribeOwner(self)
    end
    self:StopAllLoops()
    self:StopBGM()
    self:StopAllSounds()
end

function AudioManager:SetMasterVolume(volume)
    self.master_volume = tonumber(volume) or self.master_volume
    local audio = engine_audio()
    if audio and audio.SetMasterVolume then
        audio.SetMasterVolume(self.master_volume)
    end
    self.general:Publish("audio.master_volume_changed", { volume = self.master_volume })
end

function AudioManager:SetBGMVolume(volume)
    self.bgm_volume = tonumber(volume) or self.bgm_volume
    if self.bgm_volume < 0.0 then
        self.bgm_volume = 0.0
    elseif self.bgm_volume > 1.0 then
        self.bgm_volume = 1.0
    end
    local audio = engine_audio()
    if audio and audio.SetBGMVolume then
        audio.SetBGMVolume(self.bgm_volume)
    end
    self.general:Publish("audio.bgm_volume_changed", { volume = self.bgm_volume })
end

function AudioManager:SetSFXVolume(volume)
    self.sfx_volume = tonumber(volume) or self.sfx_volume
    if self.sfx_volume < 0.0 then
        self.sfx_volume = 0.0
    elseif self.sfx_volume > 1.0 then
        self.sfx_volume = 1.0
    end
    local audio = engine_audio()
    if audio and audio.SetSFXVolume then
        audio.SetSFXVolume(self.sfx_volume)
    end
    self.general:Publish("audio.sfx_volume_changed", { volume = self.sfx_volume })
end

function AudioManager:Load(name, path, loop)
    local audio = engine_audio()
    if audio and audio.Load then
        return audio.Load(name, path, loop == true)
    end
    return nil
end

function AudioManager:LoadBGMOnce(name, path)
    if self.loaded_bgm[name] == true then
        return true
    end

    local loaded = self:Load(name, path, true) == true
    if loaded then
        self.loaded_bgm[name] = true
    end
    return loaded
end

function AudioManager:PlaySFX(path_or_key, volume)
    local audio = engine_audio()
    if audio and audio.PlaySFX then
        return audio.PlaySFX(path_or_key, volume or 1.0)
    end
    return nil
end

function AudioManager:PlaySFXHandle(path_or_key, volume)
    local audio = engine_audio()
    if audio and audio.PlaySFXHandle then
        return audio.PlaySFXHandle(path_or_key, volume or 1.0)
    end
    if audio and audio.PlaySFX then
        audio.PlaySFX(path_or_key, volume or 1.0)
    end
    return 0
end

function AudioManager:FadeInSFX(handle, duration, target_volume)
    local audio = engine_audio()
    if audio and audio.FadeInSFX then
        return audio.FadeInSFX(handle, duration or 0.0, target_volume or 1.0)
    end
    return false
end

function AudioManager:FadeOutSFX(handle, duration)
    local audio = engine_audio()
    if audio and audio.FadeOutSFX then
        return audio.FadeOutSFX(handle, duration or 0.0)
    end
    if audio and audio.StopSound then
        audio.StopSound(handle)
        return true
    end
    return false
end

function AudioManager:PlayBGM(name, volume)
    local audio = engine_audio()
    if audio and audio.PlayBGM then
        audio.PlayBGM(name, volume or self.bgm_volume)
        self.current_bgm_key = name
    end
end

function AudioManager:FadeInBGM(duration, target_volume)
    local audio = engine_audio()
    if audio and audio.FadeInBGM then
        return audio.FadeInBGM(duration or 0.0, target_volume or self.bgm_volume)
    end
    return false
end

function AudioManager:FadeOutBGM(duration)
    local audio = engine_audio()
    if audio and audio.FadeOutBGM then
        return audio.FadeOutBGM(duration or 0.0)
    end
    self:StopBGM()
    return false
end

function AudioManager:StopBGM()
    local audio = engine_audio()
    if audio and audio.StopBGM then
        audio.StopBGM()
    end
    self.current_bgm_key = nil
    self.pending_bgm_switch = nil
end

function AudioManager:FadeOutCurrentBGM(duration)
    self.bgm_switch_serial = self.bgm_switch_serial + 1
    self.pending_bgm_switch = nil
    if self.current_bgm_key ~= nil then
        self:FadeOutBGM(duration or BGM_SWITCH_FADE_SECONDS)
        self.current_bgm_key = nil
    end
end

function AudioManager:IsBGMSuppressed()
    return self.opening_active == true or self.cutscene_active == true
end

function AudioManager:SuppressBGM(duration)
    self.bgm_switch_serial = self.bgm_switch_serial + 1
    self.pending_bgm_switch = nil
    if self.current_bgm_key ~= nil then
        self:FadeOutBGM(duration or BGM_SWITCH_FADE_SECONDS)
        self.current_bgm_key = nil
    end
end

function AudioManager:ScheduleBGM(name, path, volume, fade_seconds)
    if name == nil or path == nil then
        return false
    end

    self.bgm_switch_serial = self.bgm_switch_serial + 1
    local serial = self.bgm_switch_serial
    local fade = tonumber(fade_seconds) or BGM_SWITCH_FADE_SECONDS

    if self.current_bgm_key == name and self.pending_bgm_switch == nil then
        return true
    end

    if self:IsBGMSuppressed() then
        self.pending_bgm_switch = nil
        self.current_bgm_key = nil
        return true
    end

    if self.current_bgm_key ~= nil then
        self:FadeOutBGM(fade)
    else
        fade = 0.0
    end

    self.pending_bgm_switch = {
        serial = serial,
        name = name,
        path = path,
        volume = volume or DEFAULT_BGM_VOLUME,
        remaining = fade
    }
    self.current_bgm_key = nil
    return true
end

function AudioManager:PlayInGameBGM()
    self.desired_bgm = {
        name = INGAME_BGM_KEY,
        path = INGAME_BGM_PATH,
        volume = DEFAULT_BGM_VOLUME
    }
    return self:ScheduleBGM(INGAME_BGM_KEY, INGAME_BGM_PATH, DEFAULT_BGM_VOLUME, BGM_SWITCH_FADE_SECONDS)
end

function AudioManager:PlayPauseBGM()
    self.desired_bgm = {
        name = PAUSE_BGM_KEY,
        path = PAUSE_BGM_PATH,
        volume = DEFAULT_BGM_VOLUME
    }
    return self:ScheduleBGM(PAUSE_BGM_KEY, PAUSE_BGM_PATH, DEFAULT_BGM_VOLUME, BGM_SWITCH_FADE_SECONDS)
end

function AudioManager:RequestInGameBGM()
    if not self.ingame_active then
        return false
    end
    return self:PlayInGameBGM()
end

function AudioManager:ResumeDesiredBGM()
    if self:IsBGMSuppressed() then
        return false
    end

    if self.pause_active == true then
        return self:PlayPauseBGM()
    end

    if self.ingame_active == true then
        return self:PlayInGameBGM()
    end

    if self.desired_bgm ~= nil then
        return self:ScheduleBGM(
            self.desired_bgm.name,
            self.desired_bgm.path,
            self.desired_bgm.volume,
            BGM_SWITCH_FADE_SECONDS)
    end
    return false
end

function AudioManager:HandlePauseChanged(payload)
    if payload == nil then
        return
    end

    if payload.paused == true then
        self.pause_active = true
        self:PlayPauseBGM()
        return
    end

    self.pause_active = false
    local reason = payload.reason
    if reason == "resume" or reason == "start" then
        self:RequestInGameBGM()
    else
        self.desired_bgm = nil
        self:FadeOutCurrentBGM(BGM_SWITCH_FADE_SECONDS)
    end
end

function AudioManager:HandleOpeningPresentation(payload)
    local active = payload ~= nil and payload.active == true
    if active == self.opening_active then
        return
    end

    self.opening_active = active
    if active then
        self:SuppressBGM(BGM_SWITCH_FADE_SECONDS)
    else
        self:ResumeDesiredBGM()
    end
end

function AudioManager:Tick(dt)
    if self.pending_bgm_switch == nil then
        return
    end

    local pending = self.pending_bgm_switch
    pending.remaining = (pending.remaining or 0.0) - (dt or 0.0)
    if pending.remaining > 0.0 then
        return
    end

    if pending.serial ~= self.bgm_switch_serial then
        return
    end

    self.pending_bgm_switch = nil
    if self:LoadBGMOnce(pending.name, pending.path) then
        self:PlayBGM(pending.name, pending.volume)
    end
end

function AudioManager:StopSound(handle)
    local audio = engine_audio()
    if audio and audio.StopSound then
        audio.StopSound(handle)
    end
end

function AudioManager:StopAllSounds()
    local audio = engine_audio()
    if audio and audio.StopAllSounds then
        audio.StopAllSounds()
    end
end

function AudioManager:FadeInSound(handle, duration, target_volume)
    local audio = engine_audio()
    if audio and audio.FadeInSound then
        return audio.FadeInSound(handle, duration or 0.0, target_volume or 1.0)
    end
    return false
end

function AudioManager:FadeOutSound(handle, duration)
    local audio = engine_audio()
    if audio and audio.FadeOutSound then
        return audio.FadeOutSound(handle, duration or 0.0)
    end
    self:StopSound(handle)
    return false
end

function AudioManager:PlayLoop(sound_name, loop_name, volume, pitch)
    local audio = engine_audio()
    if audio and audio.PlayLoop then
        audio.PlayLoop(sound_name, loop_name, volume or 1.0, pitch or 1.0)
    end
end

function AudioManager:StopLoop(loop_name)
    local audio = engine_audio()
    if audio and audio.StopLoop then
        audio.StopLoop(loop_name)
    end
end

function AudioManager:StopAllLoops()
    local audio = engine_audio()
    if audio and audio.StopAllLoops then
        audio.StopAllLoops()
    end
end

return AudioManager
