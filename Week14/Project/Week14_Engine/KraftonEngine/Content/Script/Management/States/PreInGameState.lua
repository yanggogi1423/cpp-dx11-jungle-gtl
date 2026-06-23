local GameState = require("Management/GameState")

local PreInGameState = {}
PreInGameState.__index = PreInGameState

local SCENE_PATH = "Content/Scene/PreInGame.Scene"
local SHEET_COUNT = 6
local START_DELAY = 1.0
local SHEET_DURATION = 0.62
local TRANSITION_DELAY_AFTER_APPROVED = 3.0
local SKIP_PROMPT_FADE_SECONDS = 0.35
local SHEET_VISUAL_SCALE = 1.5
local OPENING_SFX = "SFX/BreakingNewsOpening.mp3"
local SCRIPT_SFX = "SFX/Scripts/BreakingNews.wav"
local STAMP_SFX = "SFX/Stamp.mp3"
local SCRIPT_START_DELAY = 1.5
local NEWS_EXIT_FADE_SECONDS = 0.3
local APPROVAL_AUDIO_FADE_SECONDS = 1.0
local HUD = {
    name = "PreInGameHUD",
    path = "Content/UI/PreInGameHUD.rml",
    z_order = 10,
    mode = "PreInGame"
}

local SHEET_TARGETS = {
    { x = 576.0, y = 282.0, width = 720.0, height = 540.0, rotation = -8.0 },
    { x = 598.0, y = 268.0, width = 720.0, height = 540.0, rotation = 6.0 },
    { x = 580.0, y = 278.0, width = 720.0, height = 540.0, rotation = -4.0 },
    { x = 610.0, y = 272.0, width = 720.0, height = 540.0, rotation = 3.0 },
    { x = 592.0, y = 266.0, width = 720.0, height = 540.0, rotation = -2.0 },
    { x = 600.0, y = 270.0, width = 720.0, height = 540.0, rotation = 0.0 }
}

local NEWS_SUBTITLES = {
    { until_time = 1.0, text = "긴급 속보입니다." },
    { until_time = 5.0, text = "발칸반도가 결국 통제 불능의 파국으로 치닫고 있습니다!" },
    { until_time = 10.0, text = "코소보 전역에서 벌어진 무차별 유혈 충돌로 현장은 그야말로 피로 물든 생지옥으로 변했습니다." },
    { until_time = 21.0, text = "민간인 대량 학살과 난민 폭증으로 서방 정보국은 '제3차 대전급 전면전이 초읽기에 들어갔다'고 경고했습니다." },
    { until_time = 25.0, text = "외교적 해법은 완벽히 파탄 났고, 현장의 통제권은 상실되었습니다." },
    { until_time = 31.0, text = "결국 조금 전, 미국과 NATO 연합군이 최후의 수단으로 전격적인 군사 개입을 승인, 작전에 돌입했습니다!" }
}

local function log(message)
    if Debug and Debug.Log then
        Debug.Log("[PreInGameState] " .. message)
    else
        print("[PreInGameState] " .. message)
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
    value = clamp01(value)
    return value * value * (3.0 - 2.0 * value)
end

local function lerp(a, b, alpha)
    return a + (b - a) * alpha
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

function PreInGameState.new(general)
    return setmetatable({
        general = general,
        transition_requested = false,
        start_delay_elapsed = 0.0,
        current_sheet = 1,
        sheet_elapsed = 0.0,
        sequence_complete = false,
        approval_pending = false,
        approval_fade_elapsed = 0.0,
        approved_elapsed = 0.0,
        news_elapsed = 0.0,
        script_started = false,
        subtitle_index = 0,
        opening_handle = 0,
        script_handle = 0,
        handoff_requested = false
    }, PreInGameState)
end

function PreInGameState:GetHUD()
    return HUD
end

function PreInGameState:GetScenePath()
    return SCENE_PATH
end

function PreInGameState:Enter(payload)
    log("Enter reason=" .. tostring(payload and payload.reason) .. " current=" ..
        tostring(Scene ~= nil and Scene.GetCurrentPath ~= nil and Scene.GetCurrentPath() or ""))
    self.transition_requested = false
    self.start_delay_elapsed = 0.0
    self.current_sheet = 1
    self.sheet_elapsed = 0.0
    self.sequence_complete = false
    self.approval_pending = false
    self.approval_fade_elapsed = 0.0
    self.approved_elapsed = 0.0
    self.news_elapsed = 0.0
    self.script_started = false
    self.subtitle_index = 0
    self.opening_handle = 0
    self.script_handle = 0
    self.handoff_requested = false

    if is_current_scene(SCENE_PATH) then
        log("Scene already current; no transition requested")
    else
        log("Scene mismatch on enter; SceneManager owns transition target=" .. tostring(SCENE_PATH))
    end

    self:PublishReset()
    self.opening_handle = self:PlaySFX(OPENING_SFX, 1.0)
end

function PreInGameState:Exit()
    self.transition_requested = false
    self.start_delay_elapsed = 0.0
    self.sequence_complete = false
    self.approval_pending = false
    self.approval_fade_elapsed = 0.0
    self.approved_elapsed = 0.0
    self.news_elapsed = 0.0
    self.script_started = false
    self.subtitle_index = 0
    self.handoff_requested = false
    self:PublishSubtitle("")
    self:StopNewsAudio()
end

function PreInGameState:Tick(dt)
    dt = dt or 0.0
    if self.handoff_requested then
        return
    end
    if not self.approval_pending and not self.transition_requested then
        self:TickNews(dt)
    end

    if self.approval_pending then
        self.approval_fade_elapsed = self.approval_fade_elapsed + dt
        self:PublishSkipPromptAlpha(1.0 - (self.approval_fade_elapsed / SKIP_PROMPT_FADE_SECONDS))
        if self.approval_fade_elapsed >= APPROVAL_AUDIO_FADE_SECONDS then
            log("Approval audio fade completed; showing Approved")
            self.approval_pending = false
            self.transition_requested = true
            self.approved_elapsed = 0.0
            self:PublishSkipPromptAlpha(0.0)
            self:PlaySFX(STAMP_SFX, 1.0)
            self:PublishApproved()
        end
        return
    end

    if self.transition_requested then
        self.approved_elapsed = self.approved_elapsed + dt
        if self.approved_elapsed >= TRANSITION_DELAY_AFTER_APPROVED and self.general ~= nil and self.general.RequestState ~= nil then
            log("Approved delay completed; requesting Loading")
            self.transition_requested = false
            self.handoff_requested = true
            self.general:RequestState(GameState.Loading, {
                reason = "pre_ingame_skip",
                target_state = GameState.InGame,
                target_scene = "Content/Scene/InGame.Scene"
            })
        end
        return
    end

    if not self.sequence_complete then
        self.start_delay_elapsed = self.start_delay_elapsed + dt
        if self.start_delay_elapsed < START_DELAY then
            return
        end
        self:TickSheets(dt)
        return
    end

    if was_confirm_pressed() then
        log("Confirm accepted after sheet sequence; fading news audio before Approved")
        self.approval_pending = true
        self.approval_fade_elapsed = 0.0
        self.approved_elapsed = 0.0
        self:PublishSkipPromptAlpha(1.0)
        self:StopNewsAudio(APPROVAL_AUDIO_FADE_SECONDS)
    end
end

function PreInGameState:PlaySFX(path, volume)
    if AudioManager ~= nil and AudioManager.PlaySFXHandle ~= nil then
        return AudioManager.PlaySFXHandle(path, volume or 1.0)
    end

    if AudioManager ~= nil and AudioManager.PlaySFX ~= nil then
        AudioManager.PlaySFX(path, volume or 1.0)
        return 0
    end

    if self.general ~= nil and self.general.PlaySFX ~= nil then
        self.general:PlaySFX(path, volume or 1.0)
    end

    return 0
end

function PreInGameState:FadeOutAudioHandle(handle, duration)
    if type(handle) ~= "number" or handle <= 0 then
        return
    end

    if AudioManager ~= nil and AudioManager.FadeOutSFX ~= nil then
        AudioManager.FadeOutSFX(handle, duration or 0.0)
        return
    end

    if AudioManager ~= nil and AudioManager.FadeOutSound ~= nil then
        AudioManager.FadeOutSound(handle, duration or 0.0)
        return
    end

    if AudioManager ~= nil and AudioManager.StopSound ~= nil then
        AudioManager.StopSound(handle)
    end
end

function PreInGameState:StopNewsAudio(duration)
    duration = duration or NEWS_EXIT_FADE_SECONDS
    self:FadeOutAudioHandle(self.opening_handle, duration)
    self:FadeOutAudioHandle(self.script_handle, duration)
    self.opening_handle = 0
    self.script_handle = 0
end

function PreInGameState:TickNews(dt)
    self.news_elapsed = self.news_elapsed + dt

    if not self.script_started and self.news_elapsed >= SCRIPT_START_DELAY then
        self.script_started = true
        self.script_handle = self:PlaySFX(SCRIPT_SFX, 1.0)
    end

    if not self.script_started then
        return
    end

    local script_time = self.news_elapsed - SCRIPT_START_DELAY
    local next_index = 0
    local next_text = ""
    for index, subtitle in ipairs(NEWS_SUBTITLES) do
        if script_time <= subtitle.until_time then
            next_index = index
            next_text = subtitle.text
            break
        end
    end

    if next_index ~= self.subtitle_index then
        self.subtitle_index = next_index
        self:PublishSubtitle(next_text)
    end
end

function PreInGameState:TickSheets(dt)
    self.sheet_elapsed = self.sheet_elapsed + dt

    local alpha = smoothstep(self.sheet_elapsed / SHEET_DURATION)
    self:PublishSheet(self.current_sheet, alpha)

    if self.sheet_elapsed < SHEET_DURATION then
        return
    end

    self:PublishSheet(self.current_sheet, 1.0)
    if self.current_sheet >= SHEET_COUNT then
        self.sequence_complete = true
        log("Sheet sequence complete; confirm skip enabled")
        self:PublishReady()
        return
    end

    self.current_sheet = self.current_sheet + 1
    self.sheet_elapsed = 0.0
end

function PreInGameState:BuildSheetPayload(index, alpha)
    local target = SHEET_TARGETS[index]
    local start_y = -420.0
    local start_scale = 1.55 * SHEET_VISUAL_SCALE
    local start_rotation = target.rotation * 2.4

    return {
        index = index,
        element_id = "sheet" .. tostring(index),
        alpha = alpha,
        left = target.x,
        top = lerp(start_y, target.y, alpha),
        width = target.width,
        height = target.height,
        scale = lerp(start_scale, SHEET_VISUAL_SCALE, alpha),
        rotation = lerp(start_rotation, target.rotation, alpha)
    }
end

function PreInGameState:PublishReset()
    if self.general ~= nil and self.general.Publish ~= nil then
        self.general:Publish("preingame.reset", { sheet_count = SHEET_COUNT })
    end
end

function PreInGameState:PublishSheet(index, alpha)
    if self.general ~= nil and self.general.Publish ~= nil then
        self.general:Publish("preingame.sheet_update", self:BuildSheetPayload(index, alpha))
    end
end

function PreInGameState:PublishReady()
    if self.general ~= nil and self.general.Publish ~= nil then
        self.general:Publish("preingame.ready", { sheet_count = SHEET_COUNT })
    end
end

function PreInGameState:PublishApproved()
    if self.general ~= nil and self.general.Publish ~= nil then
        self.general:Publish("preingame.approved", {})
    end
end

function PreInGameState:PublishSkipPromptAlpha(alpha)
    if self.general ~= nil and self.general.Publish ~= nil then
        self.general:Publish("preingame.skip_prompt_alpha", { alpha = clamp01(alpha or 0.0) })
    end
end

function PreInGameState:PublishSubtitle(text)
    if self.general ~= nil and self.general.Publish ~= nil then
        self.general:Publish("preingame.subtitle", { text = text or "" })
    end
end

return PreInGameState
