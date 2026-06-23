local DataManager = {}
DataManager.__index = DataManager

local SAVE_PATH = "GameData/player_profile.json"

local function default_settings()
    return {
        bgm_volume = 1.0,
        sfx_volume = 1.0,
        zoom_toggle = false,
        zoom_mode_user_set = false,
        mouse_sensitivity = 1.0,
        gamepad_sensitivity = 1.0
    }
end

local function default_data()
    return {
        nickname = "",
        score = 0,
        high_score = 0,
        settings = default_settings(),
        runs = {}
    }
end

local function clamp(value, min_value, max_value)
    value = tonumber(value) or min_value
    if value < min_value then
        return min_value
    end
    if value > max_value then
        return max_value
    end
    return value
end

local function normalize_settings(settings)
    settings = type(settings) == "table" and settings or {}
    local defaults = default_settings()
    return {
        bgm_volume = clamp(settings.bgm_volume or defaults.bgm_volume, 0.0, 1.0),
        sfx_volume = clamp(settings.sfx_volume or defaults.sfx_volume, 0.0, 1.0),
        zoom_toggle = settings.zoom_mode_user_set == true and settings.zoom_toggle == true,
        zoom_mode_user_set = settings.zoom_mode_user_set == true,
        mouse_sensitivity = clamp(settings.mouse_sensitivity or defaults.mouse_sensitivity, 0.1, 5.0),
        gamepad_sensitivity = clamp(settings.gamepad_sensitivity or defaults.gamepad_sensitivity, 0.1, 5.0)
    }
end

function DataManager.new(general)
    return setmetatable({
        general = general,
        data = default_data()
    }, DataManager)
end

function DataManager:Initialize()
    self:Load()
end

function DataManager:Shutdown()
    self:Save()
end

function DataManager:Load()
    if Save ~= nil and Save.ReadJson ~= nil then
        local loaded = Save.ReadJson(SAVE_PATH)
        if type(loaded) == "table" then
            self.data = loaded
            self.data.nickname = self.data.nickname or ""
            self.data.score = tonumber(self.data.score) or 0
            self.data.high_score = tonumber(self.data.high_score) or 0
            self.data.settings = normalize_settings(self.data.settings)
            self.data.runs = self.data.runs or {}
            return true
        end
    end
    self.data = default_data()
    return false
end

function DataManager:Save()
    if Save ~= nil and Save.WriteJson ~= nil then
        return Save.WriteJson(SAVE_PATH, self.data)
    end
    return false
end

function DataManager:SetNickname(nickname)
    self.data.nickname = tostring(nickname or "")
    self:Save()
    self.general:Publish("data.nickname_changed", { nickname = self.data.nickname })
end

function DataManager:GetNickname()
    return self.data.nickname or ""
end

function DataManager:SetScore(score)
    self.data.score = math.max(0, math.floor(tonumber(score) or 0))
    if self.data.score > (self.data.high_score or 0) then
        self.data.high_score = self.data.score
    end
    self.general:Publish("data.score_changed", {
        score = self.data.score,
        high_score = self.data.high_score
    })
end

function DataManager:AddScore(delta)
    self:SetScore((self.data.score or 0) + (tonumber(delta) or 0))
end

function DataManager:GetScore()
    return self.data.score or 0
end

function DataManager:SetTempRun(result, score)
    self.data.temp_run = {
        nickname = "TEMP",
        result = tostring(result or "Unknown"),
        score = math.max(0, math.floor(tonumber(score) or self:GetScore()))
    }
    self:Save()
    self.general:Publish("data.temp_run_changed", self.data.temp_run)
end

function DataManager:GetTempRun(default_result)
    local temp = type(self.data.temp_run) == "table" and self.data.temp_run or {}
    if next(temp) == nil then
        for index = #(self.data.runs or {}), 1, -1 do
            local run = self.data.runs[index]
            if type(run) == "table" and tostring(run.nickname or "") == "TEMP" then
                temp = run
                break
            end
        end
    end
    return {
        result = tostring(temp.result or default_result or "Unknown"),
        score = math.max(0, math.floor(tonumber(temp.score) or self:GetScore()))
    }
end

function DataManager:ClearTempRun()
    self.data.temp_run = nil
    self:Save()
end

function DataManager:GetHighScore()
    return self.data.high_score or 0
end

function DataManager:GetSettings()
    self.data.settings = normalize_settings(self.data.settings)
    return self.data.settings
end

function DataManager:SetSetting(key, value)
    self.data.settings = normalize_settings(self.data.settings)
    if key == "bgm_volume" or key == "sfx_volume" then
        self.data.settings[key] = clamp(value, 0.0, 1.0)
    elseif key == "mouse_sensitivity" or key == "gamepad_sensitivity" then
        self.data.settings[key] = clamp(value, 0.1, 5.0)
    elseif key == "zoom_toggle" then
        self.data.settings[key] = value == true
        self.data.settings.zoom_mode_user_set = true
    else
        return false
    end

    self:Save()
    self.general:Publish("settings.changed", { key = key, value = self.data.settings[key], settings = self.data.settings })
    return true
end

function DataManager:GetScoreEntries()
    local entries = {}
    for _, run in ipairs(self.data.runs or {}) do
        table.insert(entries, {
            nickname = tostring(run.nickname or self.data.nickname or "Player"),
            result = tostring(run.result or run.state or "Unknown"),
            score = tonumber(run.score) or 0
        })
    end

    table.sort(entries, function(a, b)
        return (a.score or 0) > (b.score or 0)
    end)
    return entries
end

function DataManager:CommitRun(result)
    result = result or {}
    result.score = tonumber(result.score) or self:GetScore()
    result.state = result.state or "Unknown"
    result.result = result.result or result.state
    result.nickname = tostring(result.nickname or self.data.nickname or "Player")

    for index = #(self.data.runs or {}), 1, -1 do
        local run = self.data.runs[index]
        if type(run) == "table" and tostring(run.nickname or "") == "TEMP" then
            table.remove(self.data.runs, index)
        end
    end

    table.insert(self.data.runs, result)

    if result.score > (self.data.high_score or 0) then
        self.data.high_score = result.score
    end

    self.data.nickname = result.nickname
    self.data.temp_run = nil
    self:Save()
    self.general:Publish("data.run_committed", result)
end

return DataManager
