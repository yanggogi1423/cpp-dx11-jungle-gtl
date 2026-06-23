local GameState = require("Management/GameState")
local LoadingTips = require("Management/LoadingTips")

local UIManager = {}
UIManager.__index = UIManager

local MAIN_HUD_MODE = "Main"
local PRE_INGAME_HUD_MODE = "PreInGame"
local LOADING_HUD_MODE = "Loading"
local IN_GAME_HUD_MODE = "InGame"
local RESULT_HUD_MODE = "Result"
local DEFAULT_LOADING_TIP = LoadingTips[1] or "Tip: Hold your breath only when the shot really matters."
local MAIN_MENU_BUTTON_IDS = { "btnGameStart", "btnScoreBoard", "btnSettings", "btnCredits", "btnExit" }
local MAIN_MENU_BUTTON_TEXTS = {
    { button = "btnGameStart", label = "btnGameStartLabel", text = "Game Start" },
    { button = "btnScoreBoard", label = "btnScoreBoardLabel", text = "Score Board" },
    { button = "btnSettings", label = "btnSettingsLabel", text = "Settings" },
    { button = "btnCredits", label = "btnCreditsLabel", text = "Credits" },
    { button = "btnExit", label = "btnExitLabel", text = "Exit" }
}
local MAIN_MENU_BUTTON_LABEL_IDS = {
    "btnGameStartLabel",
    "btnScoreBoardLabel",
    "btnSettingsLabel",
    "btnCreditsLabel",
    "btnExitLabel"
}
local MAIN_MENU_FADE_IDS = {
    "mainMenu",
    "btnGameStart",
    "btnScoreBoard",
    "btnSettings",
    "btnCredits",
    "btnExit",
    "btnGameStartLabel",
    "btnScoreBoardLabel",
    "btnSettingsLabel",
    "btnCreditsLabel",
    "btnExitLabel"
}
local MAIN_MENU_BUTTON_TEXT_COLOR = { r = 226, g = 232, b = 235, a = 230 }
local MAIN_BUTTON_HOVER_SFX = "SFX/ButtonHovering.mp3"
local MAIN_BUTTON_CLICK_SFX = "SFX/ButtonClickMain.mp3"
local MAIN_GAME_START_SFX = "SFX/ButtonClickGameStart.mp3"
local LOADING_END_SFX = "SFX/LoadingEnd.mp3"
local BREATH_IN_SFX = "SFX/Breath/Breath-in.mp3"
local BREATH_OUT_SFX = "SFX/Breath/Breath-out.mp3"
local BREATH_RECOVER_SFX = "SFX/Breath/Breath-recover.mp3"
local BREATH_HEARTBEAT_KEY = "Breath_HeartBeat_Loop"
local BREATH_HEARTBEAT_SFX = "SFX/Breath/HeartBeat.mp3"
local BREATH_HEARTBEAT_LOOP = "breath_heartbeat_loop"
local BREATH_HEARTBEAT_DELAY = 2.0
local BREATH_SFX_VOLUME = 1.0
local BREATH_HEARTBEAT_VOLUME = 0.88
local POPUP_LAYER_ID = "popupLayer"
local POPUP_BACKDROP_ID = "popupBackdrop"
local POPUP_IDS = {
    Settings = "settingsPopup",
    ScoreBoard = "scorePopup",
    Credits = "creditsPopup"
}
local POPUP_BUTTON_IDS = {
    "btnCloseSettings",
    "btnCloseScore",
    "btnCloseCredits",
    "btnBgmDown",
    "btnBgmUp",
    "btnSfxDown",
    "btnSfxUp",
    "btnZoomMode",
    "btnMouseDown",
    "btnMouseUp",
    "btnGamepadDown",
    "btnGamepadUp"
}
local SCORE_ROW_COUNT = 8
local PAUSE_LAYER_ID = "pauseLayer"
local PAUSE_PANEL_IDS = {
    Menu = "pauseMenuPanel",
    Settings = "pauseSettingsPanel",
    Controls = "pauseControlsPanel"
}
local PAUSE_MENU_BUTTON_IDS = {
    "btnPauseResume",
    "btnPauseMain",
    "btnPauseSettings",
    "btnPauseControls"
}
local PAUSE_SETTING_BUTTON_IDS = {
    "btnPauseSettingsBack",
    "btnPauseBgmDown",
    "btnPauseBgmUp",
    "btnPauseSfxDown",
    "btnPauseSfxUp",
    "btnPauseZoomMode",
    "btnPauseMouseDown",
    "btnPauseMouseUp",
    "btnPauseGamepadDown",
    "btnPauseGamepadUp"
}
local PAUSE_CONTROL_BUTTON_IDS = {
    "btnPauseControlsBack"
}
local CUTSCENE_LETTERBOX_TOP_ID = "cutsceneLetterboxTop"
local CUTSCENE_LETTERBOX_BOTTOM_ID = "cutsceneLetterboxBottom"
local CUTSCENE_LETTERBOX_THICKNESS = 130.0
local CUTSCENE_LETTERBOX_SCREEN_HEIGHT = 1080.0
local CUTSCENE_LETTERBOX_ENTER_SPEED = 18.0
local CUTSCENE_LETTERBOX_EXIT_SPEED = 14.0
local SCOPE_DISTANCE_TRACE_METERS = 2000.0
local SCOPE_DISTANCE_HOLD_SECONDS = 0.20
local WIND_UI_MAX_CROSS_DISPLAY = 4.0
local WIND_UI_MAX_HEAD_DISPLAY = 4.0
local WIND_UI_SMOOTH_SPEED = 6.0
local WIND_UI_CALM_CROSS_THRESHOLD = 0.18
local WIND_UI_PULSE_DECAY_SPEED = 4.5
local WIND_UI_PULSE_DELTA_SCALE = 1.25
local WIND_UI_SIDE_SWITCH_PULSE = 0.92
local WIND_UI_STRENGTH_SWITCH_PULSE = 0.58
local SCOPE_WIND_BAR_MAX_WIDTH = 150.0
local SCOPE_WIND_BAR_CENTER_X = 150.0
local SCOPE_WIND_BAR_TIP_WIDTH = 8.0
local DEFAULT_CURSOR_IMAGE_PATH = "Content/Texture/Pointer_Main.png"
local DEFAULT_CURSOR_WIDTH = 64.0
local DEFAULT_CURSOR_HEIGHT = 64.0
local DEFAULT_CURSOR_HOTSPOT_X = 2.0
local DEFAULT_CURSOR_HOTSPOT_Y = 2.0
local COMBAT_AGENT_ROW_COUNT = 5
local COMBAT_AGENT_BAR_WIDTH = 210.0
local HIT_NOTIFY_CENTER_X = 820.0
local HIT_NOTIFY_CENTER_Y = 700.0
local HIT_NOTIFY_RIGHT_X = 1328.0
local HIT_NOTIFY_RIGHT_Y = 700.0
local HIT_NOTIFY_DURATION = 4.55
local HIT_NOTIFY_PENDING_LIMIT = 4
local HIT_NOTIFY_IMPACT_TIME = 1.08
local HIT_NOTIFY_IMPACT_SFX = "SFX/Alert.mp3"
local HIT_NOTIFY_ENEMY_TITLE_BG = "rgba(255, 209, 70, 245)"
local HIT_NOTIFY_ENEMY_TITLE_COLOR = "rgba(32, 27, 15, 255)"
local HIT_NOTIFY_ENEMY_SCORE_COLOR = "rgba(255, 220, 88, 255)"
local HIT_NOTIFY_FRIENDLY_TITLE_BG = "rgba(196, 38, 38, 245)"
local HIT_NOTIFY_FRIENDLY_TITLE_COLOR = "rgba(255, 255, 255, 255)"
local HIT_NOTIFY_FRIENDLY_SCORE_COLOR = "rgba(255, 92, 92, 255)"

local function log(message)
    if Debug and Debug.Log then
        Debug.Log("[UIManager] " .. message)
    else
        print("[UIManager] " .. message)
    end
end

local function call_widget(widget, method_name, ...)
    if widget ~= nil and widget[method_name] ~= nil then
        return widget[method_name](widget, ...)
    end
    return nil
end

local function clamp01(value)
    if value == nil then
        return 0.0
    end
    if value < 0.0 then
        return 0.0
    end
    if value > 1.0 then
        return 1.0
    end
    return value
end

local function get_confirm_prompt_text(action_text, fallback_text)
    if Input ~= nil and Input.GetConfirmPromptText ~= nil then
        local ok, value = pcall(function()
            return Input.GetConfirmPromptText(action_text or "")
        end)
        if ok and value ~= nil and value ~= "" then
            return tostring(value)
        end
    end

    return fallback_text or "Press Space"
end

local function utf8_text(...)
    return string.char(...)
end

local function lerp(a, b, t)
    t = clamp01(t)
    return (a or 0.0) + ((b or 0.0) - (a or 0.0)) * t
end

local function ease_out_cubic(t)
    t = clamp01(t)
    local inv = 1.0 - t
    return 1.0 - inv * inv * inv
end

local function ease_in_out_cubic(t)
    t = clamp01(t)
    if t < 0.5 then
        return 4.0 * t * t * t
    end
    local f = -2.0 * t + 2.0
    return 1.0 - (f * f * f) * 0.5
end

local function approach01(current, target, dt, speed)
    current = clamp01(current or 0.0)
    target = clamp01(target or 0.0)
    if math.abs(target - current) <= 0.001 then
        return target
    end
    if dt == nil or dt <= 0.0 then
        return current
    end

    local alpha = clamp01(1.0 - math.exp(-(speed or 1.0) * dt))
    local result = current + (target - current) * alpha
    if math.abs(target - result) <= 0.001 then
        return target
    end
    return clamp01(result)
end

local function exp_approach(current, target, dt, speed)
    current = tonumber(current) or 0.0
    target = tonumber(target) or 0.0
    if math.abs(target - current) <= 0.001 then
        return target
    end
    if dt == nil or dt <= 0.0 then
        return current
    end

    local alpha = clamp01(1.0 - math.exp(-(speed or 1.0) * dt))
    local result = current + (target - current) * alpha
    if math.abs(target - result) <= 0.001 then
        return target
    end
    return result
end

local function rgba255(color, alpha_scale)
    alpha_scale = clamp01(alpha_scale)
    local alpha = math.floor((color.a or 255) * alpha_scale + 0.5)
    return string.format("rgba(%d, %d, %d, %d)", color.r or 255, color.g or 255, color.b or 255, alpha)
end

local function random_int(min_value, max_value)
    if Random ~= nil and Random.RandomInt ~= nil then
        return Random.RandomInt(min_value, max_value)
    end
    return math.random(min_value, max_value)
end

local function select_loading_tip(payload)
    if payload ~= nil and payload.tip ~= nil and payload.tip ~= "" then
        return payload.tip
    end
    if type(LoadingTips) == "table" and #LoadingTips > 0 then
        local index = random_int(1, #LoadingTips)
        return LoadingTips[index] or DEFAULT_LOADING_TIP
    end
    return DEFAULT_LOADING_TIP
end

local function normalize_degrees(degrees)
    local result = degrees % 360.0
    if result < 0.0 then
        result = result + 360.0
    end
    return result
end

local function shortest_angle_delta(from_degrees, to_degrees)
    local delta = normalize_degrees(to_degrees - from_degrees)
    if delta > 180.0 then
        delta = delta - 360.0
    end
    return delta
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

local function read_vector_xy(vector)
    if vector == nil then
        return 0.0, 0.0
    end

    local x = tonumber(vector.X or vector.x or 0.0) or 0.0
    local y = tonumber(vector.Y or vector.y or 0.0) or 0.0
    return x, y
end

local function planar_length(x, y)
    return math.sqrt(x * x + y * y)
end

local function normalize_planar_xy(x, y)
    local length = planar_length(x, y)
    if length <= 0.0001 then
        return 0.0, 0.0, 0.0
    end

    return x / length, y / length, length
end

local function get_camera_planar_forward(camera)
    if camera == nil or camera.Forward == nil then
        return nil
    end

    local x, y = read_vector_xy(camera.Forward)
    local normalized_x, normalized_y, length = normalize_planar_xy(x, y)
    if length <= 0.0001 then
        return nil
    end

    return normalized_x, normalized_y
end

local function get_camera_planar_right(camera)
    if camera ~= nil and camera.Right ~= nil then
        local x, y = read_vector_xy(camera.Right)
        local normalized_x, normalized_y, length = normalize_planar_xy(x, y)
        if length > 0.0001 then
            return normalized_x, normalized_y
        end
    end

    local forward_x, forward_y = get_camera_planar_forward(camera)
    if forward_x == nil or forward_y == nil then
        return nil
    end

    return -forward_y, forward_x
end

local function classify_crosswind_side(value)
    local magnitude = math.abs(tonumber(value) or 0.0)
    if magnitude <= WIND_UI_CALM_CROSS_THRESHOLD then
        return "Center"
    end

    if value > 0.0 then
        return "Right"
    end
    return "Left"
end

local function classify_headwind_state(value)
    local magnitude = math.abs(tonumber(value) or 0.0)
    if magnitude <= 0.05 then
        return "Neutral"
    end

    if value > 0.0 then
        return "Head"
    end
    return "Tail"
end

local function classify_wind_strength(normalized_strength)
    local normalized = clamp01(normalized_strength or 0.0)
    if normalized < 0.10 then
        return "Calm"
    end
    if normalized < 0.35 then
        return "Light"
    end
    if normalized < 0.70 then
        return "Medium"
    end
    return "Strong"
end

local function get_wind_strength_color(strength_level)
    local strength = tostring(strength_level or "Calm")
    if strength == "Strong" then
        return "rgba(255, 120, 70, 240)"
    end
    if strength == "Medium" then
        return "rgba(255, 210, 90, 230)"
    end
    if strength == "Light" then
        return "rgba(80, 210, 220, 220)"
    end
    return "rgba(120, 145, 150, 140)"
end

local function compute_relative_wind_snapshot(wind_vector, camera)
    local result = {
        wind_degrees = 0.0,
        wind_mps = 0.0,
        wind_cross = 0.0,
        wind_head = 0.0,
        wind_cross_abs = 0.0,
        wind_head_abs = 0.0,
        wind_cross_normalized = 0.0,
        wind_head_normalized = 0.0,
        wind_side = "Center",
        wind_head_state = "Neutral",
        wind_strength_level = "Calm"
    }

    if wind_vector == nil then
        return result
    end

    local wind_x, wind_y = read_vector_xy(wind_vector)
    local planar_speed = planar_length(wind_x, wind_y)
    result.wind_mps = planar_speed
    if planar_speed > 0.0001 then
        result.wind_degrees = normalize_degrees(atan2_degrees(wind_y, wind_x))
    end

    local forward_x, forward_y = get_camera_planar_forward(camera)
    local right_x, right_y = get_camera_planar_right(camera)
    if forward_x == nil or forward_y == nil or right_x == nil or right_y == nil then
        return result
    end

    result.wind_cross = wind_x * right_x + wind_y * right_y
    result.wind_head = wind_x * forward_x + wind_y * forward_y
    result.wind_cross_abs = math.abs(result.wind_cross)
    result.wind_head_abs = math.abs(result.wind_head)
    result.wind_cross_normalized = clamp01(result.wind_cross_abs / WIND_UI_MAX_CROSS_DISPLAY)
    result.wind_head_normalized = clamp01(result.wind_head_abs / WIND_UI_MAX_HEAD_DISPLAY)
    result.wind_side = classify_crosswind_side(result.wind_cross)
    result.wind_head_state = classify_headwind_state(result.wind_head)
    result.wind_strength_level = classify_wind_strength(result.wind_cross_normalized)
    return result
end

local function smooth_heading(current_degrees, target_degrees, dt, speed)
    if current_degrees == nil then
        return target_degrees
    end

    local alpha = 1.0
    if dt ~= nil and dt > 0.0 then
        alpha = 1.0 - math.exp(-speed * dt)
    end
    return normalize_degrees(current_degrees + shortest_angle_delta(current_degrees, target_degrees) * clamp01(alpha))
end

local function enum_equals(value, expected)
    if value == nil or expected == nil then
        return false
    end

    local ok, result = pcall(function()
        return value == expected
    end)
    return ok and result == true
end

local function read_number_method(target, method_names)
    if target == nil then
        return nil
    end

    for _, method_name in ipairs(method_names) do
        if target[method_name] ~= nil then
            local ok, value = pcall(function()
                return target[method_name](target)
            end)
            if ok and value ~= nil then
                local number = tonumber(value)
                if number ~= nil then
                    return math.max(0, math.floor(number + 0.5))
                end
            end
        end
    end

    return nil
end

local function read_float_method(target, method_names)
    if target == nil then
        return nil
    end

    for _, method_name in ipairs(method_names) do
        if target[method_name] ~= nil then
            local ok, value = pcall(function()
                return target[method_name](target)
            end)
            if ok and value ~= nil then
                local number = tonumber(value)
                if number ~= nil then
                    return number
                end
            end
        end
    end

    return nil
end

local function read_bool_method(target, method_names)
    if target == nil then
        return false
    end

    for _, method_name in ipairs(method_names) do
        if target[method_name] ~= nil then
            local ok, value = pcall(function()
                return target[method_name](target)
            end)
            if ok then
                return value == true
            end
        end
    end

    return false
end

local function read_string_method(target, method_names)
    if target == nil then
        return nil
    end

    for _, method_name in ipairs(method_names) do
        if target[method_name] ~= nil then
            local ok, value = pcall(function()
                return target[method_name](target)
            end)
            if ok and value ~= nil then
                local text = tostring(value)
                if text ~= "" then
                    return text
                end
            end
        end
    end

    return nil
end

local function is_friendly_combat_team(team_tag)
    if team_tag == nil or team_tag == "" then
        return false
    end

    local team = string.lower(tostring(team_tag))
    return string.find(team, "ally", 1, true) ~= nil
        or string.find(team, "friendly", 1, true) ~= nil
        or string.find(team, "player", 1, true) ~= nil
end

function UIManager.new(general)
    return setmetatable({
        general = general,
        widgets = {},
        current_hud = nil,
        active_hud_mode = nil,
        main_start_pending = false,
        main_start_elapsed = 0.0,
        main_start_duration = 2.0,
        main_state_requested = false,
        scope_visible = false,
        scope_distance_last_valid_meters = nil,
        scope_distance_last_valid_time = -1000.0,
        compass_frame_count = 360,
        compass_last_frame = -1,
        compass_smooth_speed = 18.0,
        smoothed_heading_degrees = nil,
        scope_telemetry_last_update_time = nil,
        smoothed_scope_wind_cross = 0.0,
        smoothed_scope_wind_head = 0.0,
        scope_wind_pulse_alpha = 0.0,
        scope_wind_last_raw_cross = nil,
        scope_wind_last_side = nil,
        scope_wind_last_strength_level = nil,
        sniper_pawn = nil,
        breath_visible = false,
        breath_bar_width = 288.0,
        breath_last_width = -1.0,
        breath_hide_delay = 3.0,
        breath_hide_time_remaining = 0.0,
        breath_fade_out_duration = 0.3,
        breath_fade_out_time_remaining = 0.0,
        breath_fade_elements = { "breathPanel", "breathLabel", "breathBarTrack", "breathBarFill" },
        breath_warning_time = 0.0,
        breath_warning_style_key = "",
        breath_missing_pawn_warned = false,
        breath_sfx_active_prev = false,
        breath_sfx_recovering_prev = false,
        breath_sfx_active_time = 0.0,
        breath_heartbeat_loaded = false,
        breath_heartbeat_looping = false,
        weapon_last_name = nil,
        weapon_last_ammo_text = nil,
        weapon_last_ammo_type = nil,
        combat_agent_last_key = "",
        hit_notify = nil,
        pending_hit_notifications = {},
        radio_hud_suppressed = false,
        radio_blackout_alpha = 0.0,
        radio_subtitle_visible = false,
        radio_subtitle_text = "",
        cutscene_active = false,
        cutscene_letterbox_alpha = 0.0,
        cutscene_letterbox_target = 0.0,
        cutscene_letterbox_last_alpha = -1.0,
        active_popup = nil,
        pause_visible = false,
        pause_panel = "Menu",
        applied_mouse_sensitivity = nil,
        applied_gamepad_sensitivity = nil,
        result_submitted = false,
        result_last_input = "",
        result_radio_only = false
    }, UIManager)
end

function UIManager:Initialize()
    self.general:Subscribe("scene.hud_requested", self, function(payload)
        self:ApplySceneHUDRequest(payload)
    end)

    self.general:Subscribe("scene.exiting", self, function(payload)
        if payload ~= nil and payload.from == "InGame" then
            self:ResetInGameHUDRuntime(true)
        end
    end)

    self.general:Subscribe("loading.ready", self, function(payload)
        self:SetLoadingReady(payload)
    end)

    self.general:Subscribe("preingame.reset", self, function(payload)
        self:ResetPreInGameHUD(payload)
    end)

    self.general:Subscribe("preingame.sheet_update", self, function(payload)
        self:ApplyPreInGameSheet(payload)
    end)

    self.general:Subscribe("preingame.ready", self, function(payload)
        self:SetPreInGameReady(payload)
    end)

    self.general:Subscribe("preingame.approved", self, function(payload)
        self:ShowPreInGameApproved(payload)
    end)

    self.general:Subscribe("preingame.skip_prompt_alpha", self, function(payload)
        self:SetPreInGameSkipPromptAlpha(payload)
    end)

    self.general:Subscribe("preingame.subtitle", self, function(payload)
        self:SetPreInGameSubtitle(payload)
    end)

    self.general:Subscribe("ingame.pause_changed", self, function(payload)
        self:SetInGamePauseVisible(payload and payload.paused == true)
    end)

    self.general:Subscribe("ingame.started", self, function(payload)
        self:SetInGameTimer(payload)
    end)

    self.general:Subscribe("ingame.timer", self, function(payload)
        self:SetInGameTimer(payload)
    end)

    self.general:Subscribe("ingame.scope_telemetry", self, function(payload)
        self:SetScopeTelemetry(payload)
    end)

    self.general:Subscribe("ingame.sniper_hit_scored", self, function(payload)
        self:ShowHitNotification(payload)
    end)

    self.general:Subscribe("ingame.sniper_killed", self, function(payload)
        self:ShowHitNotification(payload)
    end)

    self.general:Subscribe("sniper.target_damaged", self, function(payload)
        if self:ShouldUseRawSniperHitFallback() then
            self:ShowHitNotification(payload)
        end
    end)

    self.general:Subscribe("sniper.target_killed", self, function(payload)
        if self:ShouldUseRawSniperHitFallback() then
            self:ShowHitNotification(payload)
        end
    end)

    self.general:Subscribe("cutscene.skip_prompt", self, function(payload)
        self:SetCutSceneSkipPrompt(payload)
    end)

    self.general:Subscribe("cutscene.presentation", self, function(payload)
        self:SetCutScenePresentation(payload)
    end)

    self.general:Subscribe("radio.subtitle", self, function(payload)
        self:SetRadioSubtitle(payload)
    end)

    self.general:Subscribe("radio.opening_presentation", self, function(payload)
        self:SetRadioOpeningPresentation(payload)
    end)
end

function UIManager:Shutdown()
    self.general:UnsubscribeOwner(self)
    self:Clear()
    self:ResetInGameHUDRuntime(true)
end

function UIManager:Tick(dt)
    if self.active_hud_mode == MAIN_HUD_MODE then
        self:TickMainHUD(dt or 0.0)
    elseif self.active_hud_mode == PRE_INGAME_HUD_MODE then
        self:TickPreInGameHUD(dt or 0.0)
    elseif self.active_hud_mode == LOADING_HUD_MODE then
        self:TickLoadingHUD(dt or 0.0)
    elseif self.active_hud_mode == IN_GAME_HUD_MODE then
        self:TickInGameHUD(dt or 0.0)
    elseif self.active_hud_mode == RESULT_HUD_MODE then
        self:TickResultHUD(dt or 0.0)
    end
end

function UIManager:CreateWidget(name, path, z_order)
    if UI == nil or UI.CreateWidget == nil then
        log("UI.CreateWidget is unavailable")
        return nil
    end

    local existing = self.widgets[name]
    if existing ~= nil then
        log("reuse widget name=" .. tostring(name) .. " path=" .. tostring(path))
        return existing
    end

    local widget = UI.CreateWidget(path)
    if widget == nil then
        log("failed to create widget: " .. tostring(path))
        return nil
    end

    if widget.AddToViewportZ ~= nil then
        widget:AddToViewportZ(z_order or 0)
    else
        widget:AddToViewport()
    end

    call_widget(widget, "SetWantsMouse", false)
    call_widget(widget, "SetWantsKeyboard", false)
    call_widget(widget, "SetWantsTextInput", false)
    call_widget(widget, "SetBlocksGameInput", false)
    call_widget(widget, "SetBlocksGameKeyboard", false)
    call_widget(widget, "SetBlocksGameMouseLook", false)

    self.widgets[name] = widget
    log("created widget name=" .. tostring(name) .. " path=" .. tostring(path))
    self.general:Publish("ui.widget_created", { name = name, path = path, widget = widget })
    return widget
end

function UIManager:GetWidget(name)
    return self.widgets[name]
end

function UIManager:RemoveWidget(name)
    local widget = self.widgets[name]
    if widget == nil then
        return false
    end

    call_widget(widget, "RemoveFromParent")
    self.widgets[name] = nil
    if self.current_hud ~= nil and self.current_hud.name == name then
        self.current_hud = nil
        self.active_hud_mode = nil
    end
    self.general:Publish("ui.widget_removed", { name = name })
    return true
end

function UIManager:Clear()
    local names = {}
    for name, _ in pairs(self.widgets) do
        table.insert(names, name)
    end

    for _, name in ipairs(names) do
        self:RemoveWidget(name)
    end

    self.current_hud = nil
    self.active_hud_mode = nil
    self.main_start_pending = false
    self.main_start_elapsed = 0.0
    self.main_state_requested = false
    self.active_popup = nil
    self.pause_visible = false
    self.pause_panel = "Menu"
    self.result_radio_only = false
end

function UIManager:ApplySceneHUDRequest(payload)
    local hud = payload and payload.hud
    if hud == nil then
        log("clear HUD request")
        self:Clear()
        return
    end

    local name = hud.name or "HUD"
    local path = hud.path
    log("HUD request state=" .. tostring(payload and payload.state) ..
        " name=" .. tostring(name) .. " path=" .. tostring(path) ..
        " mode=" .. tostring(hud.mode))
    if path == nil or path == "" then
        self:Clear()
        return
    end

    if self.current_hud ~= nil and self.current_hud.name ~= name then
        self:RemoveWidget(self.current_hud.name)
    end

    local widget = self:CreateWidget(name, path, hud.z_order or 0)
    if widget == nil then
        self.current_hud = nil
        self.active_hud_mode = nil
        return
    end

    self.current_hud = {
        name = name,
        path = path,
        widget = widget,
        mode = hud.mode
    }
    self.active_hud_mode = hud.mode
    self:ApplyDefaultCursorImage()

    if self.active_hud_mode == MAIN_HUD_MODE then
        self:ConfigureMainHUD(widget)
    end

    if self.active_hud_mode == PRE_INGAME_HUD_MODE then
        self:ConfigurePreInGameHUD(widget)
    end

    local hud_payload = nil
    if hud ~= nil and type(hud.payload) == "table" then
        hud_payload = {}
        for key, value in pairs(hud.payload) do
            hud_payload[key] = value
        end
    end
    if payload ~= nil and type(payload.payload) == "table" then
        hud_payload = hud_payload or {}
        for key, value in pairs(payload.payload) do
            hud_payload[key] = value
        end
    elseif payload ~= nil and payload.payload ~= nil then
        hud_payload = payload.payload
    end

    if hud ~= nil and hud.mode == RESULT_HUD_MODE and type(hud_payload) == "table" and hud_payload.result_radio_only == nil then
        local reason = payload and payload.reason or nil
        hud_payload.result_radio_only = reason ~= "victory_sequence_complete" and reason ~= "defeat_sequence_complete"
    end

    if self.active_hud_mode == LOADING_HUD_MODE then
        self:ConfigureLoadingHUD(widget, hud_payload)
    end

    if self.active_hud_mode == IN_GAME_HUD_MODE then
        self:ConfigureInGameHUD(widget)
        self:FlushPendingHitNotification()
    end

    if self.active_hud_mode == RESULT_HUD_MODE then
        self:ConfigureResultHUD(widget, hud_payload)
    end
end

function UIManager:ApplyDefaultCursorImage()
    if Input == nil or Input.SetCursorImage == nil then
        return false
    end

    local ok, result = pcall(function()
        return Input.SetCursorImage(
            DEFAULT_CURSOR_IMAGE_PATH,
            DEFAULT_CURSOR_WIDTH,
            DEFAULT_CURSOR_HEIGHT,
            DEFAULT_CURSOR_HOTSPOT_X,
            DEFAULT_CURSOR_HOTSPOT_Y)
    end)

    return ok and result == true
end

function UIManager:GetActiveHUDWidget()
    if self.current_hud == nil then
        return nil
    end
    return self.current_hud.widget
end

function UIManager:SetElementAlpha(widget, element_id, alpha)
    call_widget(widget, "SetAlpha", element_id, alpha)
end

function UIManager:SetElementVisible(widget, element_id, visible)
    call_widget(widget, "SetVisible", element_id, visible)
end

function UIManager:SetElementStyle(widget, element_id, property, value)
    call_widget(widget, "SetElementStyle", element_id, property, value)
end

function UIManager:RemoveElementStyle(widget, element_id, property)
    call_widget(widget, "RemoveElementStyle", element_id, property)
end

function UIManager:HasElement(widget, element_id)
    if widget == nil or widget.HasElement == nil then
        return false
    end
    local ok, result = pcall(function()
        return widget:HasElement(element_id)
    end)
    return ok and result == true
end

function UIManager:SetMainMenuTextVisible(widget, visible)
    if widget == nil then
        return
    end

    for _, item in ipairs(MAIN_MENU_BUTTON_TEXTS) do
        local text = visible and item.text or ""
        if self:HasElement(widget, item.label) then
            call_widget(widget, "SetText", item.label, text)
        else
            call_widget(widget, "SetText", item.button, text)
        end
    end
end

function UIManager:SetMainMenuAlpha(widget, alpha)
    alpha = clamp01(alpha)
    for _, element_id in ipairs(MAIN_MENU_FADE_IDS) do
        self:SetElementAlpha(widget, element_id, alpha)
    end
end

function UIManager:SetMainMenuVisible(widget, visible)
    self:SetElementVisible(widget, "mainMenu", visible)
    for _, element_id in ipairs(MAIN_MENU_BUTTON_IDS) do
        self:SetElementVisible(widget, element_id, visible)
    end
    for _, element_id in ipairs(MAIN_MENU_BUTTON_LABEL_IDS) do
        self:SetElementVisible(widget, element_id, visible)
    end
    self:SetMainMenuTextVisible(widget, visible)
end

function UIManager:SetElementImage(widget, element_id, path)
    if widget == nil then
        return
    end

    if widget.SetImage ~= nil then
        widget:SetImage(element_id, path)
    else
        call_widget(widget, "SetElementAttribute", element_id, "src", path)
    end
end

function UIManager:ConfigureMainHUD(widget)
    self.main_start_pending = false
    self.main_start_elapsed = 0.0
    self.main_state_requested = false

    call_widget(widget, "SetWantsMouse", true)
    call_widget(widget, "SetBlocksGameMouseLook", true)
    call_widget(widget, "SetBlocksGameInput", true)
    self:SetMainMenuVisible(widget, true)
    self:SetMainMenuTextVisible(widget, true)
    self:SetMainMenuAlpha(widget, 1.0)
    self:SetMainMenuButtonsEnabled(widget, true)
    self:HideAllPopups(widget)

    self:ConfigureMainButtonActions(widget)
    if widget ~= nil and widget.bind_click ~= nil then
        widget:bind_click("btnGameStart", function()
            self:BeginMainStartTransition()
        end)
    end
end

function UIManager:ConfigureMainButtonActions(widget)
    if widget == nil then
        return
    end

    for _, element_id in ipairs(MAIN_MENU_BUTTON_IDS) do
        local click_action = "MainButtonClick"
        if element_id == "btnGameStart" then
            click_action = "GameStart"
        elseif element_id == "btnScoreBoard" then
            click_action = "OpenScoreBoard"
        elseif element_id == "btnSettings" then
            click_action = "OpenSettings"
        elseif element_id == "btnCredits" then
            click_action = "OpenCredits"
        elseif element_id == "btnExit" then
            click_action = "ExitGame"
        end
        call_widget(widget, "SetActionEvent", element_id, click_action)
        call_widget(widget, "SetElementAttribute", element_id, "data-hover-action", "MainButtonHover")
    end

    call_widget(widget, "SetActionEvent", POPUP_BACKDROP_ID, "ModalBlock")
    for _, element_id in ipairs(POPUP_BUTTON_IDS) do
        call_widget(widget, "SetElementAttribute", element_id, "data-hover-action", "MainButtonHover")
    end
    call_widget(widget, "SetActionEvent", "btnCloseSettings", "ClosePopup")
    call_widget(widget, "SetActionEvent", "btnCloseScore", "ClosePopup")
    call_widget(widget, "SetActionEvent", "btnCloseCredits", "ClosePopup")
    call_widget(widget, "SetActionEvent", "btnBgmDown", "BgmDown")
    call_widget(widget, "SetActionEvent", "btnBgmUp", "BgmUp")
    call_widget(widget, "SetActionEvent", "btnSfxDown", "SfxDown")
    call_widget(widget, "SetActionEvent", "btnSfxUp", "SfxUp")
    call_widget(widget, "SetActionEvent", "btnZoomMode", "ToggleZoomMode")
    call_widget(widget, "SetActionEvent", "btnMouseDown", "MouseDown")
    call_widget(widget, "SetActionEvent", "btnMouseUp", "MouseUp")
    call_widget(widget, "SetActionEvent", "btnGamepadDown", "GamepadDown")
    call_widget(widget, "SetActionEvent", "btnGamepadUp", "GamepadUp")
end

function UIManager:PlayUISFX(path, volume)
    if self.general ~= nil and self.general.PlaySFX ~= nil then
        self.general:PlaySFX(path, volume or 1.0)
        return
    end

    if AudioManager ~= nil and AudioManager.PlaySFX ~= nil then
        AudioManager.PlaySFX(path, volume or 1.0)
    end
end

function UIManager:RequestApplicationExit()
    log("Exit clicked; requesting application shutdown")
    if Application ~= nil then
        if Application.Exit ~= nil then
            Application.Exit()
            return
        end
        if Application.QuitGame ~= nil then
            Application.QuitGame()
            return
        end
    end

    log("Exit request failed: Application.Exit unavailable")
end

function UIManager:PlayBreathSFX(path, volume)
    if self.cutscene_active == true then
        return
    end

    local audio = self:GetAudioManager()
    if audio ~= nil and audio.PlaySFX ~= nil then
        audio:PlaySFX(path, volume or BREATH_SFX_VOLUME)
        return
    end

    if self.general ~= nil and self.general.PlaySFX ~= nil then
        self.general:PlaySFX(path, volume or BREATH_SFX_VOLUME)
        return
    end

    if AudioManager ~= nil and AudioManager.PlaySFX ~= nil then
        AudioManager.PlaySFX(path, volume or BREATH_SFX_VOLUME)
    end
end

function UIManager:EnsureBreathHeartbeatLoaded()
    if self.breath_heartbeat_loaded == true then
        return true
    end

    local loaded = nil
    local audio = self:GetAudioManager()
    if audio ~= nil and audio.Load ~= nil then
        loaded = audio:Load(BREATH_HEARTBEAT_KEY, BREATH_HEARTBEAT_SFX, true)
    elseif AudioManager ~= nil and AudioManager.Load ~= nil then
        loaded = AudioManager.Load(BREATH_HEARTBEAT_KEY, BREATH_HEARTBEAT_SFX, true)
    end

    self.breath_heartbeat_loaded = loaded ~= false
    return self.breath_heartbeat_loaded
end

function UIManager:StartBreathHeartbeat()
    if self.cutscene_active == true then
        self:StopBreathHeartbeat()
        return
    end
    if self.breath_heartbeat_looping == true then
        return
    end
    if not self:EnsureBreathHeartbeatLoaded() then
        return
    end

    local audio = self:GetAudioManager()
    if audio ~= nil and audio.PlayLoop ~= nil then
        audio:PlayLoop(BREATH_HEARTBEAT_KEY, BREATH_HEARTBEAT_LOOP, BREATH_HEARTBEAT_VOLUME, 1.0)
        self.breath_heartbeat_looping = true
        return
    end

    if AudioManager ~= nil and AudioManager.PlayLoop ~= nil then
        AudioManager.PlayLoop(BREATH_HEARTBEAT_KEY, BREATH_HEARTBEAT_LOOP, BREATH_HEARTBEAT_VOLUME, 1.0)
        self.breath_heartbeat_looping = true
    end
end

function UIManager:StopBreathHeartbeat()
    if self.breath_heartbeat_looping ~= true then
        return
    end

    local audio = self:GetAudioManager()
    if audio ~= nil and audio.StopLoop ~= nil then
        audio:StopLoop(BREATH_HEARTBEAT_LOOP)
    elseif AudioManager ~= nil and AudioManager.StopLoop ~= nil then
        AudioManager.StopLoop(BREATH_HEARTBEAT_LOOP)
    end
    self.breath_heartbeat_looping = false
end

function UIManager:ResetBreathSFXState()
    self:StopBreathHeartbeat()
    self.breath_sfx_active_prev = false
    self.breath_sfx_recovering_prev = false
    self.breath_sfx_active_time = 0.0
end

function UIManager:PollMainActions(widget)
    if widget == nil or widget.PollActionEvents == nil then
        return
    end

    local ok, events = pcall(function()
        return widget:PollActionEvents()
    end)
    if not ok or events == nil then
        log("Main HUD action polling failed")
        return
    end

    local hover_played = false
    for _, action in ipairs(events) do
        if action == "GameStart" or action == "StartGame" then
            if self.active_popup == nil then
                self:BeginMainStartTransition()
            end
        elseif action == "MainButtonClick" then
            self:PlayUISFX(MAIN_BUTTON_CLICK_SFX, 1.0)
        elseif action == "ExitGame" then
            self:PlayUISFX(MAIN_BUTTON_CLICK_SFX, 1.0)
            self:RequestApplicationExit()
        elseif action == "OpenSettings" then
            self:OpenPopup("Settings")
        elseif action == "OpenScoreBoard" then
            self:OpenPopup("ScoreBoard")
        elseif action == "OpenCredits" then
            self:OpenPopup("Credits")
        elseif action == "ClosePopup" then
            self:ClosePopup()
        elseif action == "BgmDown" then
            self:AdjustSetting("bgm_volume", -0.1)
        elseif action == "BgmUp" then
            self:AdjustSetting("bgm_volume", 0.1)
        elseif action == "SfxDown" then
            self:AdjustSetting("sfx_volume", -0.1)
        elseif action == "SfxUp" then
            self:AdjustSetting("sfx_volume", 0.1)
        elseif action == "ToggleZoomMode" then
            self:ToggleZoomMode()
        elseif action == "MouseDown" then
            self:AdjustSetting("mouse_sensitivity", -0.1)
        elseif action == "MouseUp" then
            self:AdjustSetting("mouse_sensitivity", 0.1)
        elseif action == "GamepadDown" then
            self:AdjustSetting("gamepad_sensitivity", -0.1)
        elseif action == "GamepadUp" then
            self:AdjustSetting("gamepad_sensitivity", 0.1)
        elseif action == "MainButtonHover" and not hover_played then
            hover_played = true
            self:PlayUISFX(MAIN_BUTTON_HOVER_SFX, 0.8)
        end
    end
end

function UIManager:TickMainHUD(dt)
    local widget = self:GetActiveHUDWidget()
    if not self.main_start_pending then
        self:PollMainActions(widget)
    end

    if not self.main_start_pending then
        return
    end

    self.main_start_elapsed = self.main_start_elapsed + (dt or 0.0)
    local alpha = 1.0 - clamp01(self.main_start_elapsed / self.main_start_duration)
    if widget ~= nil then
        self:SetMainMenuAlpha(widget, alpha)
        if alpha <= 0.0 then
            self:SetMainMenuVisible(widget, false)
        end
    else
        log("Main fade tick has no active HUD widget")
    end

    if self.main_start_elapsed >= self.main_start_duration and not self.main_state_requested then
        self.main_state_requested = true
        log("Main fade completed; requesting PreInGame state")
        if self.general ~= nil and self.general.RequestState ~= nil then
            local ok = self.general:RequestState(GameState.PreInGame, { reason = "main_game_start" })
            log("PreInGame state request result=" .. tostring(ok))
        else
            log("PreInGame state request failed: GeneralManager.RequestState unavailable")
        end
    end
end

function UIManager:BeginMainStartTransition()
    if self.main_start_pending then
        return
    end

    log("Game Start clicked; fading main menu before PreInGame")
    self.main_start_pending = true
    self.main_start_elapsed = 0.0
    self.main_state_requested = false
    self:PlayUISFX(MAIN_GAME_START_SFX, 1.0)
    if self.general ~= nil and self.general.Publish ~= nil then
        self.general:Publish("main.game_start_requested", { reason = "button" })
    end

    local widget = self:GetActiveHUDWidget()
    if widget ~= nil then
        self:SetMainMenuButtonsEnabled(widget, false)
        log("Main menu fade started")
    else
        log("Game Start transition has no active HUD widget")
    end
end

function UIManager:SetMainMenuButtonsEnabled(widget, enabled)
    for _, element_id in ipairs(MAIN_MENU_BUTTON_IDS) do
        call_widget(widget, "SetElementEnabled", element_id, enabled)
        if enabled then
            call_widget(widget, "RemoveElementAttribute", element_id, "disabled")
            call_widget(widget, "SetElementAttribute", element_id, "data-hover-action", "MainButtonHover")
            self:RemoveElementStyle(widget, element_id, "transform")
        else
            call_widget(widget, "SetElementAttribute", element_id, "disabled", "true")
            call_widget(widget, "RemoveElementAttribute", element_id, "data-hover-action")
            self:SetElementStyle(widget, element_id, "transform", "scale(1.0)")
        end
    end
end

function UIManager:SetElementDisplay(widget, element_id, visible)
    self:SetElementVisible(widget, element_id, visible)
    self:SetElementStyle(widget, element_id, "display", visible and "block" or "none")
    self:SetElementAlpha(widget, element_id, visible and 1.0 or 0.0)
end

function UIManager:SetCutSceneSkipPrompt(payload)
    local widget = self:GetActiveHUDWidget()
    if widget == nil then
        return
    end

    local visible = payload ~= nil and payload.visible == true
    local text = get_confirm_prompt_text("Skip", "Press Space to Skip")
    if payload ~= nil and payload.text ~= nil and payload.text ~= "" then
        text = get_confirm_prompt_text(payload.action or "Skip", tostring(payload.text))
    end

    call_widget(widget, "SetText", "cutsceneSkipPrompt", visible and text or "")
    self:SetElementDisplay(widget, "cutsceneSkipPrompt", visible)
end

function UIManager:ApplyCutSceneLetterbox(widget, alpha)
    if widget == nil then
        return
    end

    alpha = clamp01(alpha)
    local visible = alpha > 0.001 or self.cutscene_letterbox_target > 0.001
    local top_y = -CUTSCENE_LETTERBOX_THICKNESS + CUTSCENE_LETTERBOX_THICKNESS * alpha
    local bottom_y = CUTSCENE_LETTERBOX_SCREEN_HEIGHT - CUTSCENE_LETTERBOX_THICKNESS * alpha

    self:SetElementVisible(widget, CUTSCENE_LETTERBOX_TOP_ID, visible)
    self:SetElementVisible(widget, CUTSCENE_LETTERBOX_BOTTOM_ID, visible)
    self:SetElementStyle(widget, CUTSCENE_LETTERBOX_TOP_ID, "display", visible and "block" or "none")
    self:SetElementStyle(widget, CUTSCENE_LETTERBOX_BOTTOM_ID, "display", visible and "block" or "none")
    self:SetElementStyle(widget, CUTSCENE_LETTERBOX_TOP_ID, "top", string.format("%.3fpx", top_y))
    self:SetElementStyle(widget, CUTSCENE_LETTERBOX_BOTTOM_ID, "top", string.format("%.3fpx", bottom_y))
    self:SetElementStyle(widget, CUTSCENE_LETTERBOX_TOP_ID, "height", string.format("%.3fpx", CUTSCENE_LETTERBOX_THICKNESS))
    self:SetElementStyle(widget, CUTSCENE_LETTERBOX_BOTTOM_ID, "height", string.format("%.3fpx", CUTSCENE_LETTERBOX_THICKNESS))
    self:SetElementStyle(widget, CUTSCENE_LETTERBOX_TOP_ID, "background-color", "rgba(0, 0, 0, 255)")
    self:SetElementStyle(widget, CUTSCENE_LETTERBOX_BOTTOM_ID, "background-color", "rgba(0, 0, 0, 255)")
    self:SetElementAlpha(widget, CUTSCENE_LETTERBOX_TOP_ID, visible and 1.0 or 0.0)
    self:SetElementAlpha(widget, CUTSCENE_LETTERBOX_BOTTOM_ID, visible and 1.0 or 0.0)
end

function UIManager:SetCutSceneLetterboxTarget(widget, active)
    self.cutscene_letterbox_target = active and 1.0 or 0.0
    if active then
        self:ApplyCutSceneLetterbox(widget, self.cutscene_letterbox_alpha)
    end
end

function UIManager:TickCutSceneLetterbox(widget, dt)
    if widget == nil then
        return
    end

    local speed = self.cutscene_letterbox_target > self.cutscene_letterbox_alpha and
        CUTSCENE_LETTERBOX_ENTER_SPEED or CUTSCENE_LETTERBOX_EXIT_SPEED
    self.cutscene_letterbox_alpha = approach01(
        self.cutscene_letterbox_alpha,
        self.cutscene_letterbox_target,
        dt,
        speed)

    if math.abs(self.cutscene_letterbox_alpha - self.cutscene_letterbox_last_alpha) > 0.0005 then
        self.cutscene_letterbox_last_alpha = self.cutscene_letterbox_alpha
        self:ApplyCutSceneLetterbox(widget, self.cutscene_letterbox_alpha)
    end
end

function UIManager:SetCutScenePresentation(payload)
    local active = payload ~= nil and payload.active == true
    self.cutscene_active = active
    if active then
        self:ResetBreathSFXState()
    end

    local widget = self:GetActiveHUDWidget()
    if widget == nil or self.active_hud_mode ~= IN_GAME_HUD_MODE then
        return
    end

    self:SetCutSceneLetterboxTarget(widget, active)
    if active then
        self:DeferActiveHitNotificationForCutScene(widget)
        self:SetInGameHUDSuppressed(widget, true)
        self:SetElementDisplay(widget, PAUSE_LAYER_ID, false)
    elseif not self.pause_visible then
        self:SetInGameHUDSuppressed(widget, false)
        self:FlushPendingHitNotification()
    end
end

function UIManager:SetRadioSubtitle(payload)
    local visible = payload ~= nil and payload.visible == true
    local text = payload ~= nil and payload.text ~= nil and tostring(payload.text) or ""
    self.radio_subtitle_visible = visible
    self.radio_subtitle_text = visible and text or ""

    local widget = self:GetActiveHUDWidget()
    if widget == nil then
        return
    end

    if self.active_hud_mode ~= IN_GAME_HUD_MODE and self.active_hud_mode ~= RESULT_HUD_MODE then
        return
    end

    self:ApplyRadioSubtitle(widget)
end

function UIManager:ApplyRadioSubtitle(widget)
    if widget == nil or not self:HasElement(widget, "radioSubtitlePanel") then
        return
    end

    local visible = self.radio_subtitle_visible == true
    local text = visible and (self.radio_subtitle_text or "") or ""
    call_widget(widget, "SetText", "radioSubtitleText", text)
    self:SetElementVisible(widget, "radioSubtitlePanel", visible)
    self:SetElementStyle(widget, "radioSubtitlePanel", "display", visible and "block" or "none")
    self:SetElementAlpha(widget, "radioSubtitlePanel", visible and 1.0 or 0.0)
end

function UIManager:SetRadioOpeningPresentation(payload)
    local alpha = payload ~= nil and tonumber(payload.blackout_alpha) or 0.0
    local skip_alpha = payload ~= nil and tonumber(payload.skip_prompt_alpha) or 0.0
    local active = payload ~= nil and payload.active == true
    local suppress = payload ~= nil and payload.hud_suppressed == true
    self.radio_blackout_alpha = clamp01(alpha)
    self.radio_hud_suppressed = suppress
    skip_alpha = clamp01(skip_alpha)

    local widget = self:GetActiveHUDWidget()
    if widget == nil or self.active_hud_mode ~= IN_GAME_HUD_MODE then
        return
    end

    local blackout_visible = active and self.radio_blackout_alpha > 0.001
    local skip_visible = active and skip_alpha > 0.001
    self:SetElementVisible(widget, "radioBlackout", blackout_visible)
    self:SetElementStyle(widget, "radioBlackout", "display", blackout_visible and "block" or "none")
    self:SetElementAlpha(widget, "radioBlackout", blackout_visible and self.radio_blackout_alpha or 0.0)
    self:SetElementVisible(widget, "radioOpeningSkipPrompt", skip_visible)
    self:SetElementStyle(widget, "radioOpeningSkipPrompt", "display", skip_visible and "block" or "none")
    call_widget(widget, "SetText", "radioOpeningSkipPrompt", get_confirm_prompt_text("Skip", "Press Space to Skip"))
    self:SetElementAlpha(widget, "radioOpeningSkipPrompt", skip_visible and skip_alpha or 0.0)

    if suppress then
        self:SetInGameHUDSuppressed(widget, true)
    elseif not self.cutscene_active and not self.pause_visible then
        self:SetInGameHUDSuppressed(widget, false)
        self:FlushPendingHitNotification()
    end
end

function UIManager:HideAllPopups(widget)
    widget = widget or self:GetActiveHUDWidget()
    if widget == nil then
        self.active_popup = nil
        return
    end

    self:SetElementDisplay(widget, POPUP_LAYER_ID, false)
    for _, popup_id in pairs(POPUP_IDS) do
        self:SetElementDisplay(widget, popup_id, false)
    end
    self.active_popup = nil
    self:SetMainMenuButtonsEnabled(widget, true)
end

function UIManager:OpenPopup(popup_name)
    local widget = self:GetActiveHUDWidget()
    local popup_id = POPUP_IDS[popup_name]
    if widget == nil or popup_id == nil or self.main_start_pending then
        return
    end

    self:PlayUISFX(MAIN_BUTTON_CLICK_SFX, 1.0)
    self:SetMainMenuButtonsEnabled(widget, false)
    self:SetElementDisplay(widget, POPUP_LAYER_ID, true)
    self:SetElementDisplay(widget, POPUP_BACKDROP_ID, true)

    for name, id in pairs(POPUP_IDS) do
        self:SetElementDisplay(widget, id, name == popup_name)
    end

    self.active_popup = popup_name
    if popup_name == "Settings" then
        self:RefreshSettingsPopup(widget)
    elseif popup_name == "ScoreBoard" then
        self:RefreshScoreBoardPopup(widget)
    end
end

function UIManager:ClosePopup()
    local widget = self:GetActiveHUDWidget()
    if widget == nil or self.active_popup == nil then
        return
    end

    self:PlayUISFX(MAIN_BUTTON_CLICK_SFX, 0.9)
    self:HideAllPopups(widget)
end

function UIManager:GetDataManager()
    if self.general ~= nil and self.general.managers ~= nil then
        return self.general.managers.Data
    end
    return nil
end

function UIManager:GetAudioManager()
    if self.general ~= nil and self.general.managers ~= nil then
        return self.general.managers.Audio
    end
    return nil
end

function UIManager:GetSettings()
    local data = self:GetDataManager()
    if data ~= nil and data.GetSettings ~= nil then
        return data:GetSettings()
    end
    return {
        bgm_volume = 1.0,
        sfx_volume = 1.0,
        zoom_toggle = false,
        mouse_sensitivity = 1.0,
        gamepad_sensitivity = 1.0
    }
end

function UIManager:ApplySniperInputSettings(force)
    local pawn = self:GetSniperPawn()
    if pawn == nil then
        return
    end

    local settings = self:GetSettings()
    local mouse_sensitivity = tonumber(settings.mouse_sensitivity) or 1.0
    local gamepad_sensitivity = tonumber(settings.gamepad_sensitivity) or 1.0
    local zoom_toggle = settings.zoom_toggle == true

    if force or self.applied_mouse_sensitivity ~= mouse_sensitivity then
        if pawn.SetMouseSensitivityMultiplier ~= nil then
            pawn:SetMouseSensitivityMultiplier(mouse_sensitivity)
        end
        self.applied_mouse_sensitivity = mouse_sensitivity
    end

    if force or self.applied_gamepad_sensitivity ~= gamepad_sensitivity then
        if pawn.SetGamepadLookSensitivityMultiplier ~= nil then
            pawn:SetGamepadLookSensitivityMultiplier(gamepad_sensitivity)
        end
        self.applied_gamepad_sensitivity = gamepad_sensitivity
    end

    if force or self.applied_zoom_toggle ~= zoom_toggle then
        if pawn.SetRightClickZoomToggleMode ~= nil then
            pawn:SetRightClickZoomToggleMode(zoom_toggle)
        end
        self.applied_zoom_toggle = zoom_toggle
    end
end

function UIManager:SetSetting(key, value)
    local data = self:GetDataManager()
    if data ~= nil and data.SetSetting ~= nil then
        data:SetSetting(key, value)
    end

    local audio = self:GetAudioManager()
    if audio ~= nil then
        if key == "bgm_volume" and audio.SetBGMVolume ~= nil then
            audio:SetBGMVolume(value)
        elseif key == "sfx_volume" and audio.SetSFXVolume ~= nil then
            audio:SetSFXVolume(value)
        end
    end

    if key == "mouse_sensitivity" or key == "gamepad_sensitivity" or key == "zoom_toggle" then
        self:ApplySniperInputSettings(true)
    end

    self:RefreshSettingsPopup()
end

function UIManager:AdjustSetting(key, delta)
    local settings = self:GetSettings()
    local current = tonumber(settings[key]) or 0.0
    local is_sensitivity = key == "mouse_sensitivity" or key == "gamepad_sensitivity"
    local min_value = is_sensitivity and 0.1 or 0.0
    local max_value = is_sensitivity and 5.0 or 1.0
    local next_value = current + delta
    if next_value < min_value then
        next_value = min_value
    elseif next_value > max_value then
        next_value = max_value
    end
    self:SetSetting(key, next_value)
    self:PlayUISFX(MAIN_BUTTON_CLICK_SFX, 0.65)
end

function UIManager:ToggleZoomMode()
    local settings = self:GetSettings()
    self:SetSetting("zoom_toggle", not (settings.zoom_toggle == true))
    self:PlayUISFX(MAIN_BUTTON_CLICK_SFX, 0.65)
end

function UIManager:RefreshSettingsPopup(widget)
    widget = widget or self:GetActiveHUDWidget()
    if widget == nil then
        return
    end

    local settings = self:GetSettings()
    call_widget(widget, "SetText", "bgmValue", string.format("%d%%", math.floor((settings.bgm_volume or 1.0) * 100.0 + 0.5)))
    call_widget(widget, "SetText", "sfxValue", string.format("%d%%", math.floor((settings.sfx_volume or 1.0) * 100.0 + 0.5)))
    call_widget(widget, "SetText", "mouseValue", string.format("%.2fx", settings.mouse_sensitivity or 1.0))
    call_widget(widget, "SetText", "gamepadValue", string.format("%.2fx", settings.gamepad_sensitivity or 1.0))
    call_widget(widget, "SetText", "zoomModeValue", settings.zoom_toggle and "Toggle" or "Hold")

    call_widget(widget, "SetText", "pauseBgmValue", string.format("%d%%", math.floor((settings.bgm_volume or 1.0) * 100.0 + 0.5)))
    call_widget(widget, "SetText", "pauseSfxValue", string.format("%d%%", math.floor((settings.sfx_volume or 1.0) * 100.0 + 0.5)))
    call_widget(widget, "SetText", "pauseMouseValue", string.format("%.2fx", settings.mouse_sensitivity or 1.0))
    call_widget(widget, "SetText", "pauseGamepadValue", string.format("%.2fx", settings.gamepad_sensitivity or 1.0))
    call_widget(widget, "SetText", "pauseZoomModeValue", settings.zoom_toggle and "Toggle" or "Hold")
end

function UIManager:NormalizeRunResult(result)
    local text = string.lower(tostring(result or "Unknown"))
    if text == "victory" or text == "win" or text == "success" then
        return "Victory"
    end
    if text == "defeat" or text == "lose" or text == "loss" or text == "fail" then
        return "Defeat"
    end
    return tostring(result or "Unknown")
end

function UIManager:RefreshScoreBoardPopup(widget)
    widget = widget or self:GetActiveHUDWidget()
    if widget == nil then
        return
    end

    local entries = {}
    local data = self:GetDataManager()
    if data ~= nil and data.GetScoreEntries ~= nil then
        entries = data:GetScoreEntries()
    end

    if #entries <= 0 then
        entries = {
            { nickname = "Player", result = "Defeat", score = 0 }
        }
    end

    for index = 1, SCORE_ROW_COUNT do
        local entry = entries[index]
        local visible = entry ~= nil
        self:SetElementVisible(widget, "scoreRow" .. tostring(index), visible)
        if visible then
            call_widget(widget, "SetText", "scoreRank" .. tostring(index), tostring(index))
            call_widget(widget, "SetText", "scoreName" .. tostring(index), tostring(entry.nickname or "Player"))
            call_widget(widget, "SetText", "scoreResult" .. tostring(index), self:NormalizeRunResult(entry.result))
            call_widget(widget, "SetText", "scoreValue" .. tostring(index), tostring(math.floor(tonumber(entry.score) or 0)))
        end
    end

    local thumb_height = #entries > SCORE_ROW_COUNT and 96 or 406
    self:SetElementStyle(widget, "scoreScrollThumb", "height", tostring(thumb_height) .. "px")
end

function UIManager:ConfigureResultHUD(widget, payload)
    self.result_submitted = false
    self.result_last_input = ""

    local radio_only = payload ~= nil and payload.result_radio_only == true
    self.result_radio_only = radio_only
    if radio_only then
        call_widget(widget, "SetWantsMouse", false)
        call_widget(widget, "SetWantsKeyboard", false)
        call_widget(widget, "SetWantsTextInput", false)
        call_widget(widget, "SetBlocksGameInput", false)
        call_widget(widget, "SetBlocksGameKeyboard", false)
        call_widget(widget, "SetBlocksGameMouseLook", false)
        self:SetElementAlpha(widget, "resultRoot", 1.0)
        self:SetElementStyle(widget, "resultRoot", "background-color", "rgba(0, 0, 0, 0)")
        self:SetElementStyle(widget, "resultDim", "background-color", "rgba(0, 0, 0, 0)")
        self:SetElementDisplay(widget, "resultDim", false)
        self:SetElementDisplay(widget, "resultEntryPanel", false)
        self:SetElementDisplay(widget, "resultScorePanel", false)
        self:ApplyRadioSubtitle(widget)
        return
    end

    call_widget(widget, "SetWantsMouse", true)
    call_widget(widget, "SetWantsKeyboard", true)
    call_widget(widget, "SetWantsTextInput", true)
    call_widget(widget, "SetBlocksGameInput", true)
    call_widget(widget, "SetBlocksGameKeyboard", true)
    call_widget(widget, "SetBlocksGameMouseLook", true)
    call_widget(widget, "SetActionEvent", "btnSubmitScore", "SubmitScore")
    call_widget(widget, "SetElementAttribute", "btnSubmitScore", "data-hover-action", "MainButtonHover")
    call_widget(widget, "SetActionEvent", "btnResultGoMain", "GoToMain")
    call_widget(widget, "SetElementAttribute", "btnResultGoMain", "data-hover-action", "MainButtonHover")
    self:SetElementStyle(widget, "resultRoot", "background-color", "rgba(0, 0, 0, 0)")
    self:SetElementStyle(widget, "resultDim", "background-color", "rgba(0, 0, 0, 76)")
    self:SetElementDisplay(widget, "resultDim", true)
    self:SetElementDisplay(widget, "resultEntryPanel", true)
    self:SetElementDisplay(widget, "resultScorePanel", true)
    self:ApplyRadioSubtitle(widget)

    if Input ~= nil then
        if Input.SetInputModeUIOnly ~= nil then
            Input.SetInputModeUIOnly()
        elseif Input.SetInputModeGameAndUI ~= nil then
            Input.SetInputModeGameAndUI()
        end
        if Input.SetCursorVisible ~= nil then
            Input.SetCursorVisible(true)
        end
        if Input.ReleaseMouseCapture ~= nil then
            Input.ReleaseMouseCapture()
        end
    end

    local data = self:GetDataManager()
    local default_result = payload and payload.result or "Victory"
    local temp = { result = default_result, score = 0 }
    if data ~= nil and data.GetTempRun ~= nil then
        temp = data:GetTempRun(default_result)
    end
    self.result_current = temp

    call_widget(widget, "SetText", "resultTitle", self:NormalizeRunResult(temp.result))
    call_widget(widget, "SetText", "resultScoreValue", tostring(math.floor(tonumber(temp.score) or 0)))

    local nickname = ""
    if data ~= nil and data.GetNickname ~= nil then
        nickname = tostring(data:GetNickname() or "")
    end
    if nickname == "" then
        nickname = "Player1"
    end
    nickname = self:SanitizeResultNickname(nickname)
    call_widget(widget, "SetValue", "nicknameInput", nickname)
    self:RefreshResultScoreBoard(widget)
    self:UpdateResultSubmitState(widget, nickname)

    self:SetElementAlpha(widget, "resultRoot", 0.0)
    call_widget(widget, "SetTransition", "resultRoot", "opacity", 0.6, "ease-out", 0.0)
    self:SetElementAlpha(widget, "resultRoot", 1.0)
    call_widget(widget, "FocusElement", "nicknameInput", true)
end

function UIManager:SanitizeResultNickname(value)
    local source = tostring(value or "")
    local result = ""
    for index = 1, string.len(source) do
        local char = string.sub(source, index, index)
        if string.match(char, "[A-Za-z0-9]") ~= nil then
            result = result .. char
            if string.len(result) >= 12 then
                break
            end
        end
    end
    return result
end

function UIManager:IsResultNicknameValid(value)
    local text = tostring(value or "")
    return string.len(text) >= 6 and string.len(text) <= 12 and string.match(text, "^[A-Za-z0-9]+$") ~= nil
end

function UIManager:UpdateResultSubmitState(widget, nickname)
    widget = widget or self:GetActiveHUDWidget()
    if widget == nil then
        return false
    end

    local valid = self:IsResultNicknameValid(nickname)
    call_widget(widget, "SetElementEnabled", "btnSubmitScore", valid and not self.result_submitted)
    call_widget(widget, "SetClass", "btnSubmitScore", "disabled", (not valid) or self.result_submitted)
    if self.result_submitted then
        call_widget(widget, "SetText", "nicknameError", "Saved to local scoreboard.")
        call_widget(widget, "SetText", "btnSubmitScoreLabel", "Submitted")
    elseif valid then
        call_widget(widget, "SetText", "nicknameError", "")
        call_widget(widget, "SetText", "btnSubmitScoreLabel", "Submit")
    else
        call_widget(widget, "SetText", "nicknameError", "Nickname must be 6-12 letters or numbers.")
        call_widget(widget, "SetText", "btnSubmitScoreLabel", "Submit")
    end
    return valid
end

function UIManager:RefreshResultScoreBoard(widget)
    widget = widget or self:GetActiveHUDWidget()
    if widget == nil then
        return
    end

    local entries = {}
    local data = self:GetDataManager()
    if data ~= nil and data.GetScoreEntries ~= nil then
        entries = data:GetScoreEntries()
    end
    if #entries <= 0 then
        entries = {
            { nickname = "Player", result = "Defeat", score = 0 }
        }
    end

    for index = 1, SCORE_ROW_COUNT do
        local entry = entries[index]
        local visible = entry ~= nil
        self:SetElementVisible(widget, "resultScoreRow" .. tostring(index), visible)
        if visible then
            call_widget(widget, "SetText", "resultScoreRank" .. tostring(index), tostring(index))
            call_widget(widget, "SetText", "resultScoreName" .. tostring(index), tostring(entry.nickname or "Player"))
            call_widget(widget, "SetText", "resultScoreResult" .. tostring(index), self:NormalizeRunResult(entry.result))
            call_widget(widget, "SetText", "resultScoreValue" .. tostring(index), tostring(math.floor(tonumber(entry.score) or 0)))
        end
    end

    local thumb_height = #entries > SCORE_ROW_COUNT and 96 or 386
    self:SetElementStyle(widget, "resultScoreScrollThumb", "height", tostring(thumb_height) .. "px")
end

function UIManager:SubmitResultScore(widget)
    widget = widget or self:GetActiveHUDWidget()
    if widget == nil or self.result_submitted then
        return
    end

    local nickname = self:SanitizeResultNickname(call_widget(widget, "GetValue", "nicknameInput") or "")
    call_widget(widget, "SetValue", "nicknameInput", nickname)
    if not self:UpdateResultSubmitState(widget, nickname) then
        return
    end

    local result = self.result_current or { result = "Victory", score = 0 }
    local data = self:GetDataManager()
    if data ~= nil and data.CommitRun ~= nil then
        data:CommitRun({
            nickname = nickname,
            result = self:NormalizeRunResult(result.result),
            state = self:NormalizeRunResult(result.result),
            score = tonumber(result.score) or 0
        })
    end

    self.result_submitted = true
    self:PlayUISFX(MAIN_BUTTON_CLICK_SFX, 0.9)
    self:RefreshResultScoreBoard(widget)
    self:UpdateResultSubmitState(widget, nickname)
end

function UIManager:PollResultActions(widget)
    if widget == nil or widget.PollActionEvents == nil then
        return
    end

    local ok, events = pcall(function()
        return widget:PollActionEvents()
    end)
    if not ok or events == nil then
        log("Result HUD action polling failed")
        return
    end

    local hover_played = false
    for _, action in ipairs(events) do
        if action == "SubmitScore" then
            self:SubmitResultScore(widget)
        elseif action == "GoToMain" then
            self:PlayUISFX(MAIN_BUTTON_CLICK_SFX, 1.0)
            if self.general ~= nil and self.general.RequestState ~= nil then
                self.general:RequestState(GameState.Main, { reason = "result_go_main" })
            end
        elseif action == "MainButtonHover" and not hover_played then
            hover_played = true
            self:PlayUISFX(MAIN_BUTTON_HOVER_SFX, 0.75)
        end
    end
end

function UIManager:TickResultHUD(dt)
    local widget = self:GetActiveHUDWidget()
    if widget == nil then
        return
    end

    if self.result_radio_only then
        return
    end

    self:PollResultActions(widget)

    local nickname = self:SanitizeResultNickname(call_widget(widget, "GetValue", "nicknameInput") or "")
    if nickname ~= self.result_last_input then
        self.result_last_input = nickname
        call_widget(widget, "SetValue", "nicknameInput", nickname)
        self:UpdateResultSubmitState(widget, nickname)
    end

    if not self.result_submitted and Input ~= nil and Input.GetKeyDown ~= nil and Input.GetKeyDown("Enter") then
        self:SubmitResultScore(widget)
    end
end

function UIManager:ConfigurePreInGameHUD(widget)
    call_widget(widget, "SetWantsMouse", false)
    call_widget(widget, "SetBlocksGameMouseLook", false)
    call_widget(widget, "SetBlocksGameInput", false)
    self:ResetPreInGameHUD({ sheet_count = 6 })
end

function UIManager:ResetPreInGameHUD(payload)
    if self.active_hud_mode ~= PRE_INGAME_HUD_MODE then
        return
    end

    local widget = self:GetActiveHUDWidget()
    if widget == nil then
        return
    end

    local count = payload and payload.sheet_count or 6
    for index = 1, count do
        local element_id = "sheet" .. tostring(index)
        self:SetElementVisible(widget, element_id, false)
        self:SetElementAlpha(widget, element_id, 0.0)
        self:SetElementStyle(widget, element_id, "left", "600px")
        self:SetElementStyle(widget, element_id, "top", "-420px")
        self:SetElementStyle(widget, element_id, "width", "720px")
        self:SetElementStyle(widget, element_id, "height", "540px")
        self:SetElementStyle(widget, element_id, "transform", "scale(2.325)")
    end

    self:SetElementVisible(widget, "approvedStamp", false)
    self:SetElementAlpha(widget, "approvedStamp", 0.0)
    self:SetElementVisible(widget, "newsSubtitle", false)
    self:SetElementAlpha(widget, "newsSubtitle", 0.0)
    call_widget(widget, "SetText", "newsSubtitle", "")
    self:SetElementVisible(widget, "skipPrompt", false)
    self:SetElementAlpha(widget, "skipPrompt", 0.0)
end

function UIManager:ApplyPreInGameSheet(payload)
    if self.active_hud_mode ~= PRE_INGAME_HUD_MODE or payload == nil or payload.element_id == nil then
        return
    end

    local widget = self:GetActiveHUDWidget()
    if widget == nil then
        return
    end

    local element_id = payload.element_id
    self:SetElementVisible(widget, element_id, true)
    self:SetElementAlpha(widget, element_id, clamp01(payload.alpha or 1.0))
    self:SetElementStyle(widget, element_id, "left", string.format("%.3fpx", payload.left or 600.0))
    self:SetElementStyle(widget, element_id, "top", string.format("%.3fpx", payload.top or 270.0))
    self:SetElementStyle(widget, element_id, "width", string.format("%.3fpx", payload.width or 720.0))
    self:SetElementStyle(widget, element_id, "height", string.format("%.3fpx", payload.height or 540.0))
    self:SetElementStyle(
        widget,
        element_id,
        "transform",
        string.format("rotate(%.3fdeg) scale(%.3f)", payload.rotation or 0.0, payload.scale or 1.0))
end

function UIManager:SetPreInGameReady(payload)
    if self.active_hud_mode ~= PRE_INGAME_HUD_MODE then
        return
    end

    local widget = self:GetActiveHUDWidget()
    if widget == nil then
        return
    end

    call_widget(widget, "SetText", "skipPrompt", get_confirm_prompt_text("Skip", "Press Space to Skip"))
    self:SetElementVisible(widget, "skipPrompt", true)
    self:SetElementAlpha(widget, "skipPrompt", 1.0)
end

function UIManager:SetPreInGameSubtitle(payload)
    if self.active_hud_mode ~= PRE_INGAME_HUD_MODE then
        return
    end

    local widget = self:GetActiveHUDWidget()
    if widget == nil then
        return
    end

    local text = payload and payload.text or ""
    local visible = text ~= ""
    local text_length = string.len(text)
    local font_size = 24
    if text_length > 260 then
        font_size = 17
    elseif text_length > 190 then
        font_size = 19
    elseif text_length > 130 then
        font_size = 21
    end
    call_widget(widget, "SetText", "newsSubtitle", text)
    self:SetElementStyle(widget, "newsSubtitle", "font-size", tostring(font_size) .. "px")
    self:SetElementVisible(widget, "newsSubtitle", visible)
    self:SetElementAlpha(widget, "newsSubtitle", visible and 1.0 or 0.0)
end

function UIManager:ShowPreInGameApproved(payload)
    if self.active_hud_mode ~= PRE_INGAME_HUD_MODE then
        return
    end

    local widget = self:GetActiveHUDWidget()
    if widget == nil then
        return
    end

    self:SetElementVisible(widget, "approvedStamp", true)
    self:SetElementAlpha(widget, "approvedStamp", 1.0)
    self:SetElementStyle(widget, "approvedStamp", "left", "540px")
    self:SetElementStyle(widget, "approvedStamp", "top", "453px")
    self:SetElementVisible(widget, "skipPrompt", true)
end

function UIManager:SetPreInGameSkipPromptAlpha(payload)
    if self.active_hud_mode ~= PRE_INGAME_HUD_MODE then
        return
    end

    local widget = self:GetActiveHUDWidget()
    if widget == nil then
        return
    end

    local alpha = clamp01(payload and payload.alpha or 0.0)
    self:SetElementVisible(widget, "skipPrompt", alpha > 0.001)
    self:SetElementAlpha(widget, "skipPrompt", alpha)
end

function UIManager:ConfigureLoadingHUD(widget, payload)
    payload = payload or {}
    call_widget(widget, "SetWantsMouse", false)
    call_widget(widget, "SetBlocksGameMouseLook", false)
    call_widget(widget, "SetBlocksGameInput", false)

    call_widget(widget, "SetText", "loadingTitle", "Loading")
    call_widget(widget, "SetText", "loadingTip", select_loading_tip(payload))
    call_widget(widget, "SetText", "pressPrompt", get_confirm_prompt_text("Play", "Press Space to Play"))
    self:SetElementAlpha(widget, "pressPrompt", 0.0)
    self:SetElementVisible(widget, "pressPrompt", false)
end

function UIManager:SetLoadingReady(payload)
    if self.active_hud_mode ~= LOADING_HUD_MODE then
        return
    end

    local widget = self:GetActiveHUDWidget()
    if widget == nil then
        return
    end

    if payload ~= nil and payload.tip ~= nil then
        call_widget(widget, "SetText", "loadingTip", payload.tip)
    end
    call_widget(widget, "SetText", "pressPrompt", get_confirm_prompt_text("Play", "Press Space to Play"))
    self:SetElementVisible(widget, "pressPrompt", true)
    self:SetElementAlpha(widget, "pressPrompt", 1.0)
    self:PlayUISFX(LOADING_END_SFX, 1.0)
end

function UIManager:TickPreInGameHUD(dt)
    local widget = self:GetActiveHUDWidget()
    if widget ~= nil then
        call_widget(widget, "SetText", "skipPrompt", get_confirm_prompt_text("Skip", "Press Space to Skip"))
    end
end

function UIManager:TickLoadingHUD(dt)
    local widget = self:GetActiveHUDWidget()
    if widget ~= nil then
        call_widget(widget, "SetText", "pressPrompt", get_confirm_prompt_text("Play", "Press Space to Play"))
    end
end

function UIManager:SetBreathGroupAlpha(widget, alpha)
    if widget == nil then
        return
    end

    for _, element_id in ipairs(self.breath_fade_elements) do
        self:SetElementAlpha(widget, element_id, alpha)
    end
end

function UIManager:FormatTimerSeconds(seconds)
    seconds = math.max(0, math.ceil(tonumber(seconds) or 0.0))
    local minutes = math.floor(seconds / 60)
    local rest = seconds % 60
    return string.format("%02d:%02d", minutes, rest)
end

function UIManager:SetInGameTimer(payload)
    payload = payload or {}
    local widget = payload.widget or self:GetActiveHUDWidget()
    if widget == nil then
        return
    end

    local remaining = payload.remaining_time
    if remaining == nil and payload.match_duration ~= nil and payload.elapsed_time ~= nil then
        remaining = (tonumber(payload.match_duration) or 0.0) - (tonumber(payload.elapsed_time) or 0.0)
    elseif remaining == nil and payload.match_duration ~= nil and payload.timer ~= nil then
        remaining = (tonumber(payload.match_duration) or 0.0) - (tonumber(payload.timer) or 0.0)
    elseif remaining == nil then
        remaining = 300.0
    end

    self:SetElementVisible(widget, "airSupportTimerPanel", true)
    self:SetElementAlpha(widget, "airSupportTimerPanel", 1.0)
    call_widget(widget, "SetText", "airSupportTimerValue", self:FormatTimerSeconds(remaining))
end

function UIManager:ConfigureInGameHUD(widget)
    self:ResetInGameHUDRuntime(true)
    self.pause_visible = false
    self.pause_panel = "Menu"

    call_widget(widget, "SetWantsMouse", false)
    call_widget(widget, "SetBlocksGameMouseLook", false)
    call_widget(widget, "SetBlocksGameInput", false)

    self:SetElementAlpha(widget, "scopeOverlay", 0.0)
    self:SetElementVisible(widget, "scopeOverlay", false)
    self:ConfigureScopeTelemetry(widget)
    self:SetElementAlpha(widget, "crosshairImage", 1.0)
    self:SetElementVisible(widget, "crosshairImage", true)
    self:SetElementStyle(widget, "airSupportTimerLabel", "font-family", "\"Nexon\"")
    self:SetElementStyle(widget, "airSupportTimerLabel", "font-weight", "bold")
    self:SetElementStyle(widget, "airSupportTimerValue", "font-family", "\"Nexon\"")
    self:SetElementStyle(widget, "airSupportTimerValue", "font-weight", "bold")
    call_widget(widget, "SetText", "airSupportTimerLabel", "&#54637;&#44277; &#51648;&#50896; &#46020;&#52265; &#50696;&#51221;")
    self:SetInGameTimer({ widget = widget, remaining_time = 300.0, match_duration = 300.0, elapsed_time = 0.0 })

    self:SetBreathGroupAlpha(widget, 0.0)
    self:SetElementVisible(widget, "breathPanel", false)
    self:SetElementStyle(widget, "breathLabel", "font-family", "\"Nexon\"")
    self:SetElementStyle(widget, "breathLabel", "font-weight", "400")
    self:SetElementStyle(widget, "breathLabel", "color", "rgba(255, 255, 255, 255)")
    call_widget(widget, "SetText", "breathLabel", "&#49704;&#52280;&#44592;")
    self:SetElementStyle(widget, "breathBarFill", "width", "0px")
    self:SetBreathWarning(widget, false, 0.0)
    call_widget(widget, "SetElementValue", "breathProgress", "0")

    self:SetElementVisible(widget, "weaponInfoPanel", true)
    self:SetElementAlpha(widget, "weaponInfoPanel", 1.0)
    self:SetElementStyle(widget, "weaponNameLabel", "font-family", "\"Nexon\"")
    self:SetElementStyle(widget, "weaponNameLabel", "font-weight", "bold")
    self:SetElementStyle(widget, "ammoCountLabel", "font-family", "\"Nexon\"")
    self:SetElementStyle(widget, "ammoCountLabel", "font-weight", "bold")
    self:SetElementStyle(widget, "ammoTypeLabel", "font-family", "\"Nexon\"")
    self:SetElementStyle(widget, "ammoTypeLabel", "font-weight", "bold")
    self:SetElementStyle(widget, "weaponNameLabel", "color", "rgba(255, 255, 255, 255)")
    self:SetElementStyle(widget, "ammoTypeLabel", "color", "rgba(255, 255, 255, 255)")
    self:UpdateWeaponHUD(true)

    self:SetElementVisible(widget, "hitNotifyPanel", false)
    self:SetElementStyle(widget, "hitNotifyPanel", "display", "none")
    self:SetElementAlpha(widget, "hitNotifyPanel", 0.0)
    self:SetElementAlpha(widget, "hitNotifyTitle", 0.0)
    self:SetElementAlpha(widget, "hitNotifyMeta", 0.0)
    self:SetElementStyle(widget, "hitNotifyTitle", "font-family", "\"Nexon\"")
    self:SetElementStyle(widget, "hitNotifyTitle", "font-weight", "bold")
    self:SetElementStyle(widget, "hitNotifyTitle", "border-radius", "0px")
    self:SetElementStyle(widget, "hitNotifyDistance", "font-family", "\"Nexon\"")
    self:SetElementStyle(widget, "hitNotifyDistance", "font-weight", "bold")
    self:SetElementStyle(widget, "hitNotifyScore", "font-family", "\"Nexon\"")
    self:SetElementStyle(widget, "hitNotifyScore", "font-weight", "bold")

    self:SetElementVisible(widget, "combatAgentPanel", false)
    self:SetElementAlpha(widget, "combatAgentPanel", 0.0)
    self:SetElementStyle(widget, "combatAgentTitle", "font-family", "\"Nexon\"")
    self:SetElementStyle(widget, "combatAgentTitle", "font-weight", "bold")
    for index = 1, COMBAT_AGENT_ROW_COUNT do
        self:SetElementVisible(widget, "combatAgentRow" .. tostring(index), false)
        self:SetElementAlpha(widget, "combatAgentRow" .. tostring(index), 0.0)
        self:SetElementStyle(widget, "combatAgentName" .. tostring(index), "font-family", "\"Nexon\"")
        self:SetElementStyle(widget, "combatAgentName" .. tostring(index), "font-weight", "bold")
        self:SetElementStyle(widget, "combatAgentHp" .. tostring(index), "font-family", "\"Nexon\"")
        self:SetElementStyle(widget, "combatAgentHp" .. tostring(index), "font-weight", "bold")
        self:SetElementStyle(widget, "combatAgentState" .. tostring(index), "font-family", "\"Nexon\"")
        self:SetElementStyle(widget, "combatAgentState" .. tostring(index), "font-weight", "bold")
    end

    self:SetCutSceneSkipPrompt({ visible = false })
    self:ApplyCutSceneLetterbox(widget, self.cutscene_letterbox_alpha)
    self:SetRadioSubtitle({ visible = false, text = "" })
    self:SetRadioOpeningPresentation({ active = false, blackout_alpha = 0.0, hud_suppressed = false })
    self:SetElementVisible(widget, "radioOpeningSkipPrompt", false)
    self:SetElementStyle(widget, "radioOpeningSkipPrompt", "display", "none")
    self:SetElementAlpha(widget, "radioOpeningSkipPrompt", 0.0)

    self:SetElementImage(widget, "compassImage", "Image/Hor-Compass/Window/Compass_Window_000.png")
    self:ConfigurePauseMenuActions(widget)
    self:ApplySniperInputSettings(true)
    self:SetInGamePauseVisible(false)
end

function UIManager:ConfigurePauseMenuActions(widget)
    if widget == nil then
        return
    end

    call_widget(widget, "SetActionEvent", "btnPauseResume", "PauseResume")
    call_widget(widget, "SetActionEvent", "btnPauseMain", "PauseGoMain")
    call_widget(widget, "SetActionEvent", "btnPauseSettings", "PauseOpenSettings")
    call_widget(widget, "SetActionEvent", "btnPauseControls", "PauseOpenControls")
    call_widget(widget, "SetActionEvent", "btnPauseSettingsBack", "PauseBackMenu")
    call_widget(widget, "SetActionEvent", "btnPauseControlsBack", "PauseBackMenu")
    call_widget(widget, "SetActionEvent", "btnPauseBgmDown", "BgmDown")
    call_widget(widget, "SetActionEvent", "btnPauseBgmUp", "BgmUp")
    call_widget(widget, "SetActionEvent", "btnPauseSfxDown", "SfxDown")
    call_widget(widget, "SetActionEvent", "btnPauseSfxUp", "SfxUp")
    call_widget(widget, "SetActionEvent", "btnPauseZoomMode", "ToggleZoomMode")
    call_widget(widget, "SetActionEvent", "btnPauseMouseDown", "MouseDown")
    call_widget(widget, "SetActionEvent", "btnPauseMouseUp", "MouseUp")
    call_widget(widget, "SetActionEvent", "btnPauseGamepadDown", "GamepadDown")
    call_widget(widget, "SetActionEvent", "btnPauseGamepadUp", "GamepadUp")

    for _, element_id in ipairs(PAUSE_MENU_BUTTON_IDS) do
        call_widget(widget, "SetElementAttribute", element_id, "data-hover-action", "MainButtonHover")
    end
    for _, element_id in ipairs(PAUSE_SETTING_BUTTON_IDS) do
        call_widget(widget, "SetElementAttribute", element_id, "data-hover-action", "MainButtonHover")
    end
    for _, element_id in ipairs(PAUSE_CONTROL_BUTTON_IDS) do
        call_widget(widget, "SetElementAttribute", element_id, "data-hover-action", "MainButtonHover")
    end
end

function UIManager:SetInGameHUDSuppressed(widget, suppressed)
    local visible = not suppressed
    self:SetElementVisible(widget, "compassImage", visible)
    self:SetElementVisible(widget, "CompassArrow", visible)
    self:SetElementVisible(widget, "crosshairImage", visible and not self.scope_visible)
    self:SetElementVisible(widget, "scopeOverlay", visible and self.scope_visible)
    self:SetElementVisible(widget, "airSupportTimerPanel", visible)
    self:SetElementVisible(widget, "breathPanel", visible and self.breath_visible)
    self:SetElementVisible(widget, "weaponInfoPanel", visible)
    self:SetElementVisible(widget, "hitNotifyPanel", visible and self.hit_notify ~= nil)
    self:SetElementVisible(widget, "combatAgentPanel", visible and self.combat_agent_last_key ~= "")
end

function UIManager:SetPausePanel(panel_name)
    self.pause_panel = panel_name or "Menu"
    local widget = self:GetActiveHUDWidget()
    if widget == nil then
        return
    end

    for name, element_id in pairs(PAUSE_PANEL_IDS) do
        self:SetElementDisplay(widget, element_id, name == self.pause_panel)
    end
    if self.pause_panel == "Settings" then
        self:RefreshSettingsPopup(widget)
    end
end

function UIManager:SetInGamePauseVisible(visible)
    if self.active_hud_mode ~= IN_GAME_HUD_MODE then
        return
    end

    local widget = self:GetActiveHUDWidget()
    if widget == nil then
        self.pause_visible = visible
        return
    end

    self.pause_visible = visible
    if visible then
        local pawn = self:GetSniperPawn()
        if pawn ~= nil and pawn.ForceScopeReleased ~= nil then
            pawn:ForceScopeReleased()
        end
        self:SetScopeHUDVisible(false)

        if Input ~= nil and Input.SetInputModeGameAndUI ~= nil then
            Input.SetInputModeGameAndUI()
        end
        if Input ~= nil and Input.SetCursorVisible ~= nil then
            Input.SetCursorVisible(true)
        end
        if Input ~= nil and Input.ReleaseMouseCapture ~= nil then
            Input.ReleaseMouseCapture()
        end
        call_widget(widget, "SetWantsMouse", true)
        call_widget(widget, "SetBlocksGameMouseLook", true)
        call_widget(widget, "SetBlocksGameInput", false)
        call_widget(widget, "SetBlocksGameKeyboard", false)
        self:SetElementDisplay(widget, PAUSE_LAYER_ID, true)
        self:SetInGameHUDSuppressed(widget, true)
        self:SetPausePanel(self.pause_panel or "Menu")
        self:RefreshSettingsPopup(widget)
    else
        if Input ~= nil and Input.SetInputModeGameOnly ~= nil then
            Input.SetInputModeGameOnly()
        end
        if Input ~= nil and Input.SetCursorVisible ~= nil then
            Input.SetCursorVisible(false)
        end
        if Input ~= nil and Input.SetMouseCaptured ~= nil then
            Input.SetMouseCaptured(true)
        end
        call_widget(widget, "SetWantsMouse", false)
        call_widget(widget, "SetBlocksGameMouseLook", false)
        call_widget(widget, "SetBlocksGameInput", false)
        call_widget(widget, "SetBlocksGameKeyboard", false)
        self.pause_panel = "Menu"
        self:SetElementDisplay(widget, PAUSE_LAYER_ID, false)
        for _, element_id in pairs(PAUSE_PANEL_IDS) do
            self:SetElementDisplay(widget, element_id, false)
        end
        self:SetInGameHUDSuppressed(widget, false)
        self:FlushPendingHitNotification()
    end
end

function UIManager:ConfigureScopeTelemetry(widget)
    if widget == nil then
        return
    end

    self:SetScopeTelemetry({
        widget = widget,
        distance_text = "-- m",
        wind_text = "000 deg  0.0 m/s",
        zoom_text = "4x"
    })
end

function UIManager:GetWorldTimeSeconds()
    if World ~= nil and World.GetTimeSeconds ~= nil then
        local ok, value = pcall(function()
            return World.GetTimeSeconds()
        end)
        if ok then
            local seconds = tonumber(value)
            if seconds ~= nil then
                return seconds
            end
        end
    end

    return 0.0
end

function UIManager:RememberScopeDistance(distance_meters)
    if type(distance_meters) ~= "number" or distance_meters < 0.0 then
        return
    end

    self.scope_distance_last_valid_meters = distance_meters
    self.scope_distance_last_valid_time = self:GetWorldTimeSeconds()
end

function UIManager:GetHeldScopeDistance()
    local last_distance = self.scope_distance_last_valid_meters
    if type(last_distance) ~= "number" then
        return nil
    end

    local elapsed = self:GetWorldTimeSeconds() - (self.scope_distance_last_valid_time or -1000.0)
    if elapsed <= SCOPE_DISTANCE_HOLD_SECONDS then
        return last_distance
    end

    return nil
end

function UIManager:GetScopeTelemetrySnapshot()
    local snapshot = {
        distance_text = "-- m",
        wind_text = "000 deg  0.0 m/s",
        wind_degrees = 0.0,
        wind_mps = 0.0,
        wind_cross = 0.0,
        wind_head = 0.0,
        wind_cross_abs = 0.0,
        wind_head_abs = 0.0,
        wind_cross_normalized = 0.0,
        wind_head_normalized = 0.0,
        wind_side = "Center",
        wind_head_state = "Neutral",
        wind_strength_level = "Calm",
        wind_pulse_alpha = 0.0,
        zoom_text = "4x",
        zoom_multiplier = 4.0,
        zoom_min = 4.0,
        zoom_max = 16.0
    }

    local aim_forward_x = nil
    local aim_forward_y = nil
    local scope_camera = nil
    local pawn = self:GetSniperPawn()
    if pawn ~= nil then
        local current_zoom = read_float_method(pawn, { "GetCurrentScopeZoomMagnification" })
        local min_zoom = read_float_method(pawn, { "GetMinScopeZoomMagnification" })
        local max_zoom = read_float_method(pawn, { "GetMaxScopeZoomMagnification" })

        if type(min_zoom) == "number" and min_zoom > 0.0 then
            snapshot.zoom_min = min_zoom
        end
        if type(max_zoom) == "number" and max_zoom > 0.0 then
            snapshot.zoom_max = max_zoom
        end
        if snapshot.zoom_max < snapshot.zoom_min then
            local temp = snapshot.zoom_min
            snapshot.zoom_min = snapshot.zoom_max
            snapshot.zoom_max = temp
        end

        if type(current_zoom) == "number" then
            snapshot.zoom_multiplier = current_zoom
        else
            snapshot.zoom_multiplier = snapshot.zoom_min
        end

        if World ~= nil and pawn.GetCamera ~= nil then
            local ok_camera, camera = pcall(function()
                return pawn:GetCamera()
            end)
            if ok_camera and camera ~= nil then
                scope_camera = camera
                local ok_trace, trace_result = pcall(function()
                    local trace_start = nil
                    if camera.GetLocation ~= nil then
                        trace_start = camera:GetLocation()
                    else
                        trace_start = camera.Location
                    end

                    local trace_direction = camera.Forward
                    if trace_start == nil or trace_direction == nil then
                        return nil
                    end

                    aim_forward_x = tonumber(trace_direction.X or trace_direction.x or 0.0)
                    aim_forward_y = tonumber(trace_direction.Y or trace_direction.y or 0.0)

                    local trace_end = trace_start + trace_direction * SCOPE_DISTANCE_TRACE_METERS
                    if World.LineTraceGameplay ~= nil then
                        return World.LineTraceGameplay(trace_start, trace_end, pawn)
                    end
                    if World.LineTrace ~= nil then
                        return World.LineTrace(trace_start, trace_end, pawn)
                    end
                    return nil
                end)

                if ok_trace and type(trace_result) == "table" and trace_result.Hit == true then
                    local hit_distance = tonumber(trace_result.Distance)
                    if hit_distance ~= nil and hit_distance >= 0.0 then
                        snapshot.distance_meters = hit_distance
                        self:RememberScopeDistance(hit_distance)
                    end
                end
            end
        end
    end

    if type(snapshot.distance_meters) ~= "number" then
        local held_distance = self:GetHeldScopeDistance()
        if type(held_distance) == "number" then
            snapshot.distance_meters = held_distance
        end
    end

    local wind_enabled = true
    if Engine ~= nil and Engine.GetBallisticWindEnabled ~= nil then
        local ok, value = pcall(function()
            return Engine.GetBallisticWindEnabled()
        end)
        if ok then
            wind_enabled = value == true
        end
    end

    local current_wind = nil
    if Engine ~= nil and Engine.GetCurrentBallisticWindAcceleration ~= nil then
        local ok, value = pcall(function()
            return Engine.GetCurrentBallisticWindAcceleration()
        end)
        if ok then
            current_wind = value
        end
    elseif Engine ~= nil and Engine.GetBallisticWindAcceleration ~= nil then
        local ok, value = pcall(function()
            return Engine.GetBallisticWindAcceleration()
        end)
        if ok then
            current_wind = value
        end
    end

    local relative_wind = nil
    if wind_enabled then
        relative_wind = compute_relative_wind_snapshot(current_wind, scope_camera)
    else
        relative_wind = compute_relative_wind_snapshot(nil, scope_camera)
    end

    local raw_wind_side = classify_crosswind_side(relative_wind.wind_cross)
    local raw_wind_strength_level = classify_wind_strength(relative_wind.wind_cross_normalized)

    local current_time = self:GetWorldTimeSeconds()
    local telemetry_dt = 0.0
    if type(self.scope_telemetry_last_update_time) == "number" then
        telemetry_dt = math.max(0.0, current_time - self.scope_telemetry_last_update_time)
    end
    self.scope_telemetry_last_update_time = current_time

    if telemetry_dt > 0.0 then
        self.scope_wind_pulse_alpha = exp_approach(
            self.scope_wind_pulse_alpha or 0.0,
            0.0,
            telemetry_dt,
            WIND_UI_PULSE_DECAY_SPEED)
    end

    local pulse_trigger = 0.0
    if type(self.scope_wind_last_raw_cross) == "number" then
        pulse_trigger = clamp01(
            math.abs(relative_wind.wind_cross - self.scope_wind_last_raw_cross) / WIND_UI_PULSE_DELTA_SCALE)
    end
    if self.scope_wind_last_side ~= nil and self.scope_wind_last_side ~= raw_wind_side then
        pulse_trigger = math.max(pulse_trigger, WIND_UI_SIDE_SWITCH_PULSE)
    end
    if self.scope_wind_last_strength_level ~= nil and self.scope_wind_last_strength_level ~= raw_wind_strength_level then
        pulse_trigger = math.max(pulse_trigger, WIND_UI_STRENGTH_SWITCH_PULSE)
    end
    if pulse_trigger > (self.scope_wind_pulse_alpha or 0.0) then
        self.scope_wind_pulse_alpha = pulse_trigger
    end

    self.scope_wind_last_raw_cross = relative_wind.wind_cross
    self.scope_wind_last_side = raw_wind_side
    self.scope_wind_last_strength_level = raw_wind_strength_level

    if telemetry_dt <= 0.0 then
        self.smoothed_scope_wind_cross = relative_wind.wind_cross
        self.smoothed_scope_wind_head = relative_wind.wind_head
    else
        self.smoothed_scope_wind_cross = exp_approach(
            self.smoothed_scope_wind_cross,
            relative_wind.wind_cross,
            telemetry_dt,
            WIND_UI_SMOOTH_SPEED)
        self.smoothed_scope_wind_head = exp_approach(
            self.smoothed_scope_wind_head,
            relative_wind.wind_head,
            telemetry_dt,
            WIND_UI_SMOOTH_SPEED)
    end

    snapshot.wind_degrees = relative_wind.wind_degrees
    snapshot.wind_mps = relative_wind.wind_mps
    snapshot.wind_cross = self.smoothed_scope_wind_cross
    snapshot.wind_head = self.smoothed_scope_wind_head
    snapshot.wind_cross_abs = math.abs(snapshot.wind_cross)
    snapshot.wind_head_abs = math.abs(snapshot.wind_head)
    snapshot.wind_cross_normalized = clamp01(snapshot.wind_cross_abs / WIND_UI_MAX_CROSS_DISPLAY)
    snapshot.wind_head_normalized = clamp01(snapshot.wind_head_abs / WIND_UI_MAX_HEAD_DISPLAY)
    snapshot.wind_side = classify_crosswind_side(snapshot.wind_cross)
    snapshot.wind_head_state = classify_headwind_state(snapshot.wind_head)
    snapshot.wind_strength_level = classify_wind_strength(snapshot.wind_cross_normalized)
    snapshot.wind_pulse_alpha = clamp01(self.scope_wind_pulse_alpha or 0.0)
    snapshot.wind_text = string.format(
        "%s %.2f",
        snapshot.wind_side,
        snapshot.wind_cross_abs)

    return snapshot
end

function UIManager:UpdateScopeWindBar(widget, payload)
    if widget == nil then
        return
    end

    local wind_side = tostring(payload and payload.wind_side or "Center")
    local wind_strength_level = tostring(payload and payload.wind_strength_level or "Calm")
    local wind_normalized = clamp01(payload and payload.wind_cross_normalized or 0.0)
    local wind_pulse_alpha = clamp01(payload and payload.wind_pulse_alpha or 0.0)
    local active_width = math.floor(SCOPE_WIND_BAR_MAX_WIDTH * wind_normalized + 0.5)
    local left_width = 0
    local right_width = 0
    local left_left = SCOPE_WIND_BAR_CENTER_X
    local right_left = SCOPE_WIND_BAR_CENTER_X
    local left_tip_width = 0
    local right_tip_width = 0
    local left_tip_left = SCOPE_WIND_BAR_CENTER_X
    local right_tip_left = SCOPE_WIND_BAR_CENTER_X

    if wind_side == "Left" then
        left_width = active_width
        left_left = SCOPE_WIND_BAR_CENTER_X - left_width
        if left_width > 0 then
            left_tip_width = math.min(SCOPE_WIND_BAR_TIP_WIDTH, left_width)
            left_tip_left = left_left
        end
    elseif wind_side == "Right" then
        right_width = active_width
        if right_width > 0 then
            right_tip_width = math.min(SCOPE_WIND_BAR_TIP_WIDTH, right_width)
            right_tip_left = right_left + right_width - right_tip_width
        end
    end

    local bar_color = get_wind_strength_color(wind_strength_level)
    self:SetElementStyle(widget, "scopeWindBarLeft", "left", string.format("%.3fpx", left_left))
    self:SetElementStyle(widget, "scopeWindBarLeft", "width", string.format("%dpx", left_width))
    self:SetElementStyle(widget, "scopeWindBarLeft", "background-color", bar_color)
    self:SetElementStyle(widget, "scopeWindBarRight", "left", string.format("%.3fpx", right_left))
    self:SetElementStyle(widget, "scopeWindBarRight", "width", string.format("%dpx", right_width))
    self:SetElementStyle(widget, "scopeWindBarRight", "background-color", bar_color)
    self:SetElementStyle(widget, "scopeWindBarLeftTip", "left", string.format("%.3fpx", left_tip_left))
    self:SetElementStyle(widget, "scopeWindBarLeftTip", "width", string.format("%dpx", left_tip_width))
    self:SetElementStyle(widget, "scopeWindBarLeftTip", "background-color", bar_color)
    self:SetElementStyle(widget, "scopeWindBarRightTip", "left", string.format("%.3fpx", right_tip_left))
    self:SetElementStyle(widget, "scopeWindBarRightTip", "width", string.format("%dpx", right_tip_width))
    self:SetElementStyle(widget, "scopeWindBarRightTip", "background-color", bar_color)
    self:SetElementStyle(widget, "scopeWindPulseGlow", "background-color", bar_color)
    self:SetElementStyle(
        widget,
        "scopeWindBarTrack",
        "border-color",
        wind_strength_level == "Calm" and "rgba(210, 232, 238, 72)" or "rgba(210, 232, 238, 120)")
    self:SetElementStyle(
        widget,
        "scopeWindBarTrack",
        "background-color",
        wind_strength_level == "Calm" and "rgba(90, 112, 122, 52)" or "rgba(90, 112, 122, 80)")
    self:SetElementStyle(
        widget,
        "scopeWindCenterLine",
        "background-color",
        wind_strength_level == "Calm" and "rgba(240, 248, 252, 150)" or "rgba(240, 248, 252, 220)")
    self:SetElementAlpha(widget, "scopeWindBarLeft", left_width > 0 and (0.76 + 0.24 * wind_pulse_alpha) or 0.0)
    self:SetElementAlpha(widget, "scopeWindBarRight", right_width > 0 and (0.76 + 0.24 * wind_pulse_alpha) or 0.0)
    self:SetElementAlpha(widget, "scopeWindBarLeftTip", left_tip_width > 0 and (0.88 + 0.12 * wind_pulse_alpha) or 0.0)
    self:SetElementAlpha(widget, "scopeWindBarRightTip", right_tip_width > 0 and (0.88 + 0.12 * wind_pulse_alpha) or 0.0)
    self:SetElementAlpha(
        widget,
        "scopeWindCenterLine",
        math.min(1.0, wind_strength_level == "Calm" and 0.58 or (0.72 + 0.22 * wind_pulse_alpha)))
    self:SetElementAlpha(
        widget,
        "scopeWindPulseGlow",
        wind_strength_level == "Calm" and 0.0 or math.min(0.72, 0.16 + 0.42 * wind_pulse_alpha))
    self:SetElementStyle(widget, "scopeWindValue", "display", "none")
    self:SetElementStyle(widget, "scopeWindSpeedValue", "display", "block")
    self:SetElementAlpha(widget, "scopeWindValue", 0.0)
    self:SetElementAlpha(widget, "scopeWindSpeedValue", 1.0)
end

function UIManager:SetScopeTelemetry(payload)
    payload = payload or {}
    local widget = payload.widget or self:GetActiveHUDWidget()
    if widget == nil then
        return
    end

    local distance_text = payload.distance_text or payload.distance or "-- m"
    local wind_text = payload.wind_text or payload.wind or "000 deg  0.0 m/s"
    local wind_direction_text = "000 deg"
    local wind_speed_text = "0.0 m/s"
    local zoom_text = payload.zoom_text or payload.zoom or "4x"
    if type(payload.distance_meters) == "number" then
        distance_text = string.format("%d m", math.floor(payload.distance_meters + 0.5))
    end
    if payload.wind_side ~= nil or payload.wind_strength_level ~= nil then
        local side = tostring(payload.wind_side or "Center")
        if side == "Left" then
            wind_direction_text = "LEFT"
        elseif side == "Right" then
            wind_direction_text = "RIGHT"
        else
            wind_direction_text = "CALM"
        end
        if type(payload.wind_mps) == "number" then
            wind_speed_text = string.format("%.1f m/s", payload.wind_mps)
        else
            wind_speed_text = tostring(payload.wind_strength_level or "Calm")
        end
        wind_text = wind_direction_text .. "  " .. wind_speed_text
    elseif type(payload.wind_degrees) == "number" and type(payload.wind_mps) == "number" then
        wind_direction_text = string.format("%03d deg", math.floor(payload.wind_degrees + 0.5) % 360)
        wind_speed_text = string.format("%.1f m/s", payload.wind_mps)
        wind_text = wind_direction_text .. "  " .. wind_speed_text
    else
        local parsed_direction, parsed_speed = tostring(wind_text):match("^%s*([%+%-]?%d+%s*deg)%s+([%+%-]?[%d%.]+%s*m/s)%s*$")
        if parsed_direction ~= nil and parsed_speed ~= nil then
            wind_direction_text = parsed_direction
            wind_speed_text = parsed_speed
        else
            wind_direction_text = tostring(wind_text)
            wind_speed_text = ""
        end
    end
    if type(payload.zoom_multiplier) == "number" then
        local clamped_zoom = payload.zoom_multiplier
        local min_zoom = type(payload.zoom_min) == "number" and payload.zoom_min or 4
        local max_zoom = type(payload.zoom_max) == "number" and payload.zoom_max or 16
        if max_zoom < min_zoom then
            local temp = min_zoom
            min_zoom = max_zoom
            max_zoom = temp
        end
        if clamped_zoom < min_zoom then
            clamped_zoom = min_zoom
        elseif clamped_zoom > max_zoom then
            clamped_zoom = max_zoom
        end
        zoom_text = string.format("%dx", math.floor(clamped_zoom + 0.5))
    end

    local telemetry_values = {
        scopeDistanceValue = tostring(distance_text),
        scopeWindValue = tostring(wind_direction_text),
        scopeWindSpeedValue = tostring(wind_speed_text),
        scopeZoomValue = tostring(zoom_text)
    }

    for element_id, text in pairs(telemetry_values) do
        self:SetElementStyle(widget, element_id, "font-family", "\"Nexon\"")
        self:SetElementStyle(widget, element_id, "font-weight", "bold")
        self:SetElementStyle(widget, element_id, "color", "rgba(255, 255, 255, 255)")
        call_widget(widget, "SetText", element_id, text)
    end

    self:UpdateScopeWindBar(widget, payload)
end

function UIManager:UpdateScopeTelemetryHUD(force)
    local widget = self:GetActiveHUDWidget()
    if widget == nil then
        return
    end

    if not force and not self.scope_visible then
        return
    end

    self:SetScopeTelemetry(self:GetScopeTelemetrySnapshot())
end

function UIManager:GetHitNotifyHitInfo(payload)
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

function UIManager:GetHitNotifyRegionAlias(hit, payload)
    local region_name = payload ~= nil and payload.hit_region_name or nil
    if (region_name == nil or region_name == "") and hit ~= nil then
        region_name = hit.HitRegionName
    end

    if hit ~= nil and SniperHitRegion ~= nil then
        if enum_equals(hit.HitRegion, SniperHitRegion.Head) then
            return utf8_text(235, 168, 184, 235, 166, 172)
        end
        if enum_equals(hit.HitRegion, SniperHitRegion.Torso) then
            return utf8_text(235, 170, 184, 237, 134, 181)
        end
        if enum_equals(hit.HitRegion, SniperHitRegion.Arm) then
            return utf8_text(237, 140, 148)
        end
        if enum_equals(hit.HitRegion, SniperHitRegion.Leg) then
            return utf8_text(235, 139, 164, 235, 166, 172)
        end
    end

    region_name = string.upper(tostring(region_name or ""))
    if region_name == "HEAD" then
        return utf8_text(235, 168, 184, 235, 166, 172)
    end
    if region_name == "TORSO" or region_name == "BODY" then
        return utf8_text(235, 170, 184, 237, 134, 181)
    end
    if region_name == "ARM" then
        return utf8_text(237, 140, 148)
    end
    if region_name == "LEG" then
        return utf8_text(235, 139, 164, 235, 166, 172)
    end
    return utf8_text(235, 182, 128, 236, 156, 132)
end

function UIManager:GetHitNotifyOutcomeAlias(hit)
    if hit ~= nil and SniperHitOutcome ~= nil then
        if enum_equals(hit.HitOutcome, SniperHitOutcome.Penetrated) then
            return utf8_text(234, 180, 128, 237, 134, 181)
        end
        if enum_equals(hit.HitOutcome, SniperHitOutcome.Blocked) then
            return utf8_text(235, 176, 169, 237, 131, 132)
        end
        if enum_equals(hit.HitOutcome, SniperHitOutcome.Ricochet) then
            return utf8_text(235, 143, 132, 237, 131, 132)
        end
    end
    return utf8_text(235, 170, 133, 236, 164, 145)
end

function UIManager:FormatHitNotifyDistance(hit)
    if hit == nil then
        return utf8_text(234, 177, 176, 235, 166, 172) .. " --m"
    end

    local distance = tonumber(hit.TravelDistance)
    if distance == nil then
        return utf8_text(234, 177, 176, 235, 166, 172) .. " --m"
    end

    if distance >= 10000.0 then
        distance = distance / 100.0
    end
    return string.format("%s %dm", utf8_text(234, 177, 176, 235, 166, 172), math.floor(distance + 0.5))
end

function UIManager:FormatHitNotifyScore(payload)
    local score = payload ~= nil and tonumber(payload.score_delta) or nil
    if score == nil and payload ~= nil then
        score = tonumber(payload.hit_score_value)
    end
    if score == nil then
        local hit = self:GetHitNotifyHitInfo(payload)
        score = hit ~= nil and tonumber(hit.HitScoreValue) or 0
    end

    score = tonumber(score) or 0
    if score >= 0 then
        score = math.floor(score + 0.5)
    else
        score = -math.floor(math.abs(score) + 0.5)
    end
    if score >= 0 then
        return string.format("+%d%s", score, utf8_text(236, 160, 144))
    end
    return string.format("%d%s", score, utf8_text(236, 160, 144))
end

function UIManager:IsHitNotificationBlocked()
    return self.active_hud_mode ~= IN_GAME_HUD_MODE or
        self.cutscene_active == true or
        self.radio_hud_suppressed == true or
        self.pause_visible == true
end

function UIManager:ShouldUseRawSniperHitFallback()
    local ingame = self.general ~= nil and self.general.managers ~= nil and self.general.managers.InGame or nil
    if ingame ~= nil and ingame.running == true then
        return false
    end
    return self.active_hud_mode == IN_GAME_HUD_MODE
end

function UIManager:IsKillHitNotification(payload)
    if payload == nil then
        return false
    end
    if payload.killed == true then
        return true
    end
    if type(payload.payload) == "table" and payload.payload.killed == true then
        return true
    end

    local hit = self:GetHitNotifyHitInfo(payload)
    return hit ~= nil and hit.bKilled == true
end

local function normalize_hit_notify_team_tag(value)
    if value == nil then
        return ""
    end
    local text = string.lower(tostring(value))
    text = string.gsub(text, "^%s+", "")
    text = string.gsub(text, "%s+$", "")
    return text
end

local HIT_NOTIFY_FRIENDLY_TAGS = {
    ally = true,
    friendly = true,
    player = true,
    bravo = true
}

local HIT_NOTIFY_ENEMY_TAGS = {
    enemy = true,
    hostile = true,
    opfor = true
}

local function hit_notify_read_team_tag(object)
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

local function hit_notify_object_has_team_tag(object, tag_map)
    if object == nil then
        return false
    end
    if object.GetTags ~= nil then
        local ok, tags = pcall(function()
            return object:GetTags()
        end)
        if ok and type(tags) == "table" then
            for _, tag in pairs(tags) do
                if tag_map[normalize_hit_notify_team_tag(tag)] == true then
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
    local team_tag = normalize_hit_notify_team_tag(hit_notify_read_team_tag(object))
    return team_tag ~= "" and tag_map[team_tag] == true
end

local function hit_notify_get_target(payload, hit)
    if payload == nil then
        return nil
    end
    if payload.target ~= nil then
        return payload.target
    end
    if type(payload.payload) == "table" and payload.payload.target ~= nil then
        return payload.payload.target
    end
    if hit ~= nil and hit.HitActor ~= nil then
        return hit.HitActor
    end
    return nil
end

function UIManager:IsFriendlyHitNotification(payload)
    if payload == nil then
        return false
    end
    local hit = self:GetHitNotifyHitInfo(payload)
    local target = hit_notify_get_target(payload, hit)
    if hit_notify_object_has_team_tag(target, HIT_NOTIFY_ENEMY_TAGS) then
        return false
    end
    if hit_notify_object_has_team_tag(target, HIT_NOTIFY_FRIENDLY_TAGS) then
        return true
    end

    if payload.friendly == true then
        return true
    end
    if type(payload.payload) == "table" and payload.payload.friendly == true then
        return true
    end

    return hit ~= nil and hit.bFriendlyTarget == true
end

function UIManager:QueueHitNotification(payload)
    if payload == nil then
        return
    end

    if self.pending_hit_notifications == nil then
        self.pending_hit_notifications = {}
    end

    if self:IsKillHitNotification(payload) then
        self.hit_notify = nil
        local widget = self:GetActiveHUDWidget()
        if widget ~= nil then
            self:SetElementAlpha(widget, "hitNotifyPanel", 0.0)
            self:SetElementAlpha(widget, "hitNotifyTitle", 0.0)
            self:SetElementAlpha(widget, "hitNotifyMeta", 0.0)
            self:SetElementVisible(widget, "hitNotifyPanel", false)
            self:SetElementStyle(widget, "hitNotifyPanel", "display", "none")
        end

        for i = #self.pending_hit_notifications, 1, -1 do
            local existing = self.pending_hit_notifications[i]
            if not self:IsKillHitNotification(existing) then
                table.remove(self.pending_hit_notifications, i)
            end
        end
    end

    table.insert(self.pending_hit_notifications, payload)
    while #self.pending_hit_notifications > HIT_NOTIFY_PENDING_LIMIT do
        table.remove(self.pending_hit_notifications, 1)
    end
end

function UIManager:DeferActiveHitNotificationForCutScene(widget)
    if self.hit_notify == nil or self.hit_notify.payload == nil then
        return
    end

    local payload = self.hit_notify.payload
    self.hit_notify = nil
    self:QueueHitNotification(payload)

    widget = widget or self:GetActiveHUDWidget()
    if widget ~= nil then
        self:SetElementAlpha(widget, "hitNotifyPanel", 0.0)
        self:SetElementAlpha(widget, "hitNotifyTitle", 0.0)
        self:SetElementAlpha(widget, "hitNotifyMeta", 0.0)
        self:SetElementVisible(widget, "hitNotifyPanel", false)
        self:SetElementStyle(widget, "hitNotifyPanel", "display", "none")
    end
end

function UIManager:FlushPendingHitNotification()
    if self:IsHitNotificationBlocked() then
        return
    end
    if self.hit_notify ~= nil then
        return
    end
    if self.pending_hit_notifications == nil or #self.pending_hit_notifications == 0 then
        return
    end

    local payload = table.remove(self.pending_hit_notifications, 1)
    self:RenderHitNotification(payload)
end

function UIManager:ShowHitNotification(payload)
    local widget = self:GetActiveHUDWidget()
    if widget == nil or self.active_hud_mode ~= IN_GAME_HUD_MODE then
        self:QueueHitNotification(payload)
        return
    end

    if self:IsHitNotificationBlocked() then
        self:QueueHitNotification(payload)
        return
    end

    self:RenderHitNotification(payload)
end

function UIManager:RenderHitNotification(payload)
    local widget = self:GetActiveHUDWidget()
    if widget == nil or self.active_hud_mode ~= IN_GAME_HUD_MODE then
        self:QueueHitNotification(payload)
        return
    end

    local hit = self:GetHitNotifyHitInfo(payload)
    local title = self:GetHitNotifyRegionAlias(hit, payload) .. " " .. self:GetHitNotifyOutcomeAlias(hit)
    local friendly = self:IsFriendlyHitNotification(payload)
    local killed = self:IsKillHitNotification(payload)
    local friendly_kill = friendly and killed
    if friendly_kill then
        title = utf8_text(236, 149, 132, 234, 181, 176, 32, 236, 130, 172, 236, 130, 180)
    elseif killed then
        title = title .. " " .. utf8_text(236, 178, 152, 236, 185, 152)
    end
    local distance_text = self:FormatHitNotifyDistance(hit)
    local score_text = self:FormatHitNotifyScore(payload)
    log("render hit notification title=" .. tostring(title) ..
        " distance=" .. tostring(distance_text) ..
        " score=" .. tostring(score_text))

    call_widget(widget, "SetText", "hitNotifyTitle", title)
    call_widget(widget, "SetText", "hitNotifyDistance", distance_text)
    call_widget(widget, "SetText", "hitNotifyScore", score_text)

    self.hit_notify = {
        time = 0.0,
        duration = HIT_NOTIFY_DURATION,
        payload = payload,
        impact_sfx_played = false
    }

    self:SetElementVisible(widget, "hitNotifyPanel", true)
    self:SetElementStyle(widget, "hitNotifyPanel", "display", "block")
    if friendly then
        self:SetElementStyle(widget, "hitNotifyTitle", "background-color", HIT_NOTIFY_FRIENDLY_TITLE_BG)
        self:SetElementStyle(widget, "hitNotifyTitle", "color", HIT_NOTIFY_FRIENDLY_TITLE_COLOR)
        self:SetElementStyle(widget, "hitNotifyScore", "color", HIT_NOTIFY_FRIENDLY_SCORE_COLOR)
    else
        self:SetElementStyle(widget, "hitNotifyTitle", "background-color", HIT_NOTIFY_ENEMY_TITLE_BG)
        self:SetElementStyle(widget, "hitNotifyTitle", "color", HIT_NOTIFY_ENEMY_TITLE_COLOR)
        self:SetElementStyle(widget, "hitNotifyScore", "color", HIT_NOTIFY_ENEMY_SCORE_COLOR)
    end
    self:SetElementAlpha(widget, "hitNotifyPanel", 1.0)
    self:SetElementAlpha(widget, "hitNotifyTitle", 1.0)
    self:SetElementVisible(widget, "hitNotifyMeta", true)
    self:ApplyHitNotificationState(widget, 0.0)
end

function UIManager:ApplyHitNotificationState(widget, time)
    if widget == nil then
        return
    end

    local alpha = 1.0
    local x = HIT_NOTIFY_CENTER_X
    local y = HIT_NOTIFY_CENTER_Y
    local scale = 1.0
    local title_alpha = 1.0
    local meta_alpha = 0.0

    if time < 0.18 then
        local t = ease_out_cubic(time / 0.18)
        scale = lerp(0.84, 1.16, t)
    elseif time < 0.36 then
        local t = ease_out_cubic((time - 0.18) / 0.18)
        scale = lerp(1.16, 0.96, t)
    elseif time < 0.54 then
        local t = ease_out_cubic((time - 0.36) / 0.18)
        scale = lerp(0.96, 1.0, t)
    elseif time < 1.08 then
        local t = ease_in_out_cubic((time - 0.54) / 0.54)
        x = lerp(HIT_NOTIFY_CENTER_X, HIT_NOTIFY_RIGHT_X, t)
        y = lerp(HIT_NOTIFY_CENTER_Y, HIT_NOTIFY_RIGHT_Y, t)
        scale = 1.0
        meta_alpha = clamp01((time - 0.86) / 0.22)
    elseif time < 4.08 then
        x = HIT_NOTIFY_RIGHT_X
        y = HIT_NOTIFY_RIGHT_Y
        scale = 1.0
        meta_alpha = 1.0
    else
        local t = clamp01((time - 4.08) / 0.45)
        x = HIT_NOTIFY_RIGHT_X
        y = HIT_NOTIFY_RIGHT_Y
        scale = 1.0
        alpha = 1.0 - t
        title_alpha = alpha
        meta_alpha = alpha
    end

    self:SetElementStyle(widget, "hitNotifyPanel", "left", string.format("%.3fpx", x))
    self:SetElementStyle(widget, "hitNotifyPanel", "top", string.format("%.3fpx", y))
    self:SetElementStyle(widget, "hitNotifyPanel", "transform", string.format("scale(%.3f)", scale))
    self:SetElementAlpha(widget, "hitNotifyPanel", alpha)
    self:SetElementAlpha(widget, "hitNotifyTitle", title_alpha)
    self:SetElementAlpha(widget, "hitNotifyMeta", meta_alpha)
end

function UIManager:TickHitNotification(dt)
    if self.hit_notify == nil then
        return
    end

    local widget = self:GetActiveHUDWidget()
    if widget == nil then
        self.hit_notify = nil
        return
    end

    self.hit_notify.time = (self.hit_notify.time or 0.0) + (dt or 0.0)
    if self.hit_notify.impact_sfx_played ~= true and
        self.hit_notify.time >= HIT_NOTIFY_IMPACT_TIME then
        self.hit_notify.impact_sfx_played = true
        self:PlayUISFX(HIT_NOTIFY_IMPACT_SFX, 1.0)
    end

    if self.hit_notify.time >= (self.hit_notify.duration or HIT_NOTIFY_DURATION) then
        self.hit_notify = nil
        self:SetElementAlpha(widget, "hitNotifyPanel", 0.0)
        self:SetElementAlpha(widget, "hitNotifyTitle", 0.0)
        self:SetElementAlpha(widget, "hitNotifyMeta", 0.0)
        self:SetElementVisible(widget, "hitNotifyPanel", false)
        self:SetElementStyle(widget, "hitNotifyPanel", "display", "none")
        return
    end

    self:ApplyHitNotificationState(widget, self.hit_notify.time)
end

function UIManager:ResetInGameHUDRuntime(clear_pawn)
    self.scope_visible = false
    self.scope_distance_last_valid_meters = nil
    self.scope_distance_last_valid_time = -1000.0
    self.compass_last_frame = -1
    self.smoothed_heading_degrees = nil
    self.scope_telemetry_last_update_time = nil
    self.smoothed_scope_wind_cross = 0.0
    self.smoothed_scope_wind_head = 0.0
    self.scope_wind_pulse_alpha = 0.0
    self.scope_wind_last_raw_cross = nil
    self.scope_wind_last_side = nil
    self.scope_wind_last_strength_level = nil
    self.breath_visible = false
    self.breath_last_width = -1.0
    self.breath_hide_time_remaining = 0.0
    self.breath_fade_out_time_remaining = 0.0
    self.breath_warning_time = 0.0
    self.breath_warning_style_key = ""
    self.breath_missing_pawn_warned = false
    self:ResetBreathSFXState()
    self.weapon_last_name = nil
    self.weapon_last_ammo_text = nil
    self.weapon_last_ammo_type = nil
    self.combat_agent_last_key = ""
    self.hit_notify = nil
    self.pending_hit_notifications = {}
    self.radio_hud_suppressed = false
    self.radio_blackout_alpha = 0.0
    self.radio_subtitle_visible = false
    self.radio_subtitle_text = ""
    self.cutscene_active = false
    self.cutscene_letterbox_alpha = 0.0
    self.cutscene_letterbox_target = 0.0
    self.cutscene_letterbox_last_alpha = -1.0
    self.applied_mouse_sensitivity = nil
    self.applied_gamepad_sensitivity = nil
    if clear_pawn then
        self.sniper_pawn = nil
    end
end

function UIManager:GetSniperPawn()
    if self.sniper_pawn ~= nil then
        local ok, is_valid = pcall(function()
            if self.sniper_pawn.IsValid ~= nil then
                return self.sniper_pawn:IsValid()
            end
            return true
        end)
        if ok and is_valid and (
            self.sniper_pawn.GetHoldBreathGaugeRatio ~= nil or
            self.sniper_pawn.GetHoldBreathGauge ~= nil or
            self.sniper_pawn.IsHoldBreathActive ~= nil
        ) then
            return self.sniper_pawn
        end
    end

    self.sniper_pawn = nil
    if World == nil then
        return nil
    end

    local actor = nil
    if World.FindFirstSniperPawn ~= nil then
        actor = World.FindFirstSniperPawn()
    end
    if actor == nil and World.FindFirstActorByClass ~= nil then
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
            self.sniper_pawn = casted
            return self.sniper_pawn
        end
    end

    if actor.GetHoldBreathGaugeRatio ~= nil or actor.GetHoldBreathGauge ~= nil or actor.IsHoldBreathActive ~= nil then
        self.sniper_pawn = actor
        return self.sniper_pawn
    end

    return nil
end

function UIManager:GetSniperWeaponComponent()
    local pawn = self:GetSniperPawn()
    if pawn == nil or pawn.GetSniperWeaponComponent == nil then
        return nil
    end

    local ok, weapon = pcall(function()
        return pawn:GetSniperWeaponComponent()
    end)
    if ok then
        return weapon
    end
    return nil
end

function UIManager:FormatAmmoTypeName(ammo_type)
    if SniperAmmoType ~= nil then
        if enum_equals(ammo_type, SniperAmmoType.AntiMaterial) then
            return "ANTI-MATERIAL"
        end
        if enum_equals(ammo_type, SniperAmmoType.Normal) then
            return "NORMAL"
        end
    end

    local numeric = tonumber(ammo_type)
    if numeric == 1 then
        return "ANTI-MATERIAL"
    end
    if numeric == 0 then
        return "NORMAL"
    end

    local text = string.lower(tostring(ammo_type or ""))
    if string.find(text, "anti", 1, true) ~= nil then
        return "ANTI-MATERIAL"
    end
    if string.find(text, "normal", 1, true) ~= nil then
        return "NORMAL"
    end
    return "NORMAL"
end

function UIManager:GetWeaponHUDSnapshot()
    local weapon = self:GetSniperWeaponComponent()
    local weapon_name = "SNIPER RIFLE"
    local ammo_type_name = "NORMAL"
    local current_ammo = nil

    if weapon ~= nil then
        weapon_name = read_string_method(weapon, {
            "GetWeaponDisplayName",
            "GetDisplayName",
            "GetCurrentWeaponName",
            "GetWeaponName"
        }) or weapon_name

        if weapon.GetCurrentAmmoType ~= nil then
            local ok, ammo_type = pcall(function()
                return weapon:GetCurrentAmmoType()
            end)
            if ok then
                ammo_type_name = self:FormatAmmoTypeName(ammo_type)
            end
        end

        current_ammo = read_number_method(weapon, {
            "GetCurrentAmmoCount",
            "GetRemainingAmmo",
            "GetAmmoInMagazine",
            "GetCurrentAmmo",
            "GetClipAmmo"
        })
    end

    local ammo_text = "00"
    if current_ammo ~= nil then
        ammo_text = string.format("%02d", current_ammo)
    end

    return {
        weapon_name = weapon_name,
        ammo_text = ammo_text,
        ammo_type_name = "AMMO TYPE  " .. ammo_type_name
    }
end

function UIManager:UpdateWeaponHUD(force)
    local widget = self:GetActiveHUDWidget()
    if widget == nil then
        return
    end

    local snapshot = self:GetWeaponHUDSnapshot()
    if force or snapshot.weapon_name ~= self.weapon_last_name then
        self.weapon_last_name = snapshot.weapon_name
        call_widget(widget, "SetText", "weaponNameLabel", snapshot.weapon_name)
    end

    if force or snapshot.ammo_text ~= self.weapon_last_ammo_text then
        self.weapon_last_ammo_text = snapshot.ammo_text
        call_widget(widget, "SetText", "ammoCountLabel", snapshot.ammo_text)
    end

    if force or snapshot.ammo_type_name ~= self.weapon_last_ammo_type then
        self.weapon_last_ammo_type = snapshot.ammo_type_name
        call_widget(widget, "SetText", "ammoTypeLabel", snapshot.ammo_type_name)
    end
end

function UIManager:GetCombatAgentHUDSnapshot()
    if Combat == nil or Combat.GetAgents == nil then
        return {}
    end

    local ok, agents = pcall(function()
        return Combat.GetAgents()
    end)
    if not ok or agents == nil then
        return {}
    end

    local result = {}
    for _, agent in ipairs(agents) do
        local alive = true
        if agent.IsAlive ~= nil then
            local alive_ok, alive_value = pcall(function()
                return agent:IsAlive()
            end)
            alive = alive_ok and alive_value == true
        end

        if alive then
            local team = read_string_method(agent, { "GetTeamTag" }) or ""
            if is_friendly_combat_team(team) then
                local health = read_float_method(agent, { "GetHealth" }) or 0.0
                local max_health = read_float_method(agent, { "GetMaxHealth" }) or 0.0
                local ratio = read_float_method(agent, { "GetHealthRatio" })
                if ratio == nil then
                    ratio = max_health > 0.0 and health / max_health or 0.0
                end

                result[#result + 1] = {
                    name = read_string_method(agent, { "GetDisplayName", "GetName" }) or "Ally",
                    state = read_string_method(agent, { "GetStateName" }) or "-",
                    health = math.max(0.0, health),
                    max_health = math.max(0.0, max_health),
                    ratio = clamp01(ratio)
                }
            end
        end

        if #result >= COMBAT_AGENT_ROW_COUNT then
            break
        end
    end

    return result
end

function UIManager:UpdateCombatAgentHUD(force)
    local widget = self:GetActiveHUDWidget()
    if widget == nil then
        return
    end

    local snapshot = self:GetCombatAgentHUDSnapshot()
    local key_parts = {}
    for index, entry in ipairs(snapshot) do
        key_parts[index] = string.format(
            "%s|%s|%d|%d",
            entry.name,
            entry.state,
            math.floor(entry.health + 0.5),
            math.floor(entry.max_health + 0.5))
    end
    local snapshot_key = table.concat(key_parts, ";")

    if not force and snapshot_key == self.combat_agent_last_key then
        return
    end
    self.combat_agent_last_key = snapshot_key

    local has_agents = #snapshot > 0
    self:SetElementVisible(widget, "combatAgentPanel", has_agents)
    self:SetElementAlpha(widget, "combatAgentPanel", has_agents and 1.0 or 0.0)

    for index = 1, COMBAT_AGENT_ROW_COUNT do
        local entry = snapshot[index]
        local row_id = "combatAgentRow" .. tostring(index)
        local row_visible = entry ~= nil
        self:SetElementVisible(widget, row_id, row_visible)
        self:SetElementAlpha(widget, row_id, row_visible and 1.0 or 0.0)

        if entry ~= nil then
            call_widget(widget, "SetText", "combatAgentName" .. tostring(index), entry.name)
            call_widget(
                widget,
                "SetText",
                "combatAgentHp" .. tostring(index),
                string.format("%03d / %03d", math.floor(entry.health + 0.5), math.floor(entry.max_health + 0.5)))
            call_widget(widget, "SetText", "combatAgentState" .. tostring(index), string.upper(entry.state))
            self:SetElementStyle(
                widget,
                "combatAgentBarFill" .. tostring(index),
                "width",
                string.format("%.3fpx", COMBAT_AGENT_BAR_WIDTH * entry.ratio))
        end
    end
end

function UIManager:IsRawHoldBreathRequested()
    if Input == nil or Input.GetKey == nil then
        return false
    end

    local scope_down = Input.GetKey("RightMouseButton")
    local shift_down =
        Input.GetKey("Shift") or
        Input.GetKey("LeftShift") or
        Input.GetKey("RightShift")
    return scope_down and shift_down
end

function UIManager:GetScopeVisibleFromInputOrPawn()
    local pawn = self:GetSniperPawn()
    if pawn ~= nil and pawn.IsReloading ~= nil and pawn:IsReloading() then
        return false
    end
    if pawn ~= nil and pawn.IsScoped ~= nil then
        return pawn:IsScoped()
    end

    return Input ~= nil and Input.GetKey ~= nil and Input.GetKey("RightMouseButton") == true
end

function UIManager:GetHoldBreathGaugeRatio(pawn)
    if pawn == nil then
        return 0.0
    end

    local ratio = read_float_method(pawn, { "GetHoldBreathGaugeRatio" })
    if ratio ~= nil then
        return clamp01(ratio)
    end

    local gauge = read_float_method(pawn, { "GetHoldBreathGauge" })
    local max_gauge = read_float_method(pawn, { "GetMaxHoldBreathGauge" })
    if gauge ~= nil and max_gauge ~= nil and max_gauge > 0.0 then
        return clamp01(gauge / max_gauge)
    end

    return 0.0
end

function UIManager:SetScopeHUDVisible(visible)
    local widget = self:GetActiveHUDWidget()
    if widget == nil or self.scope_visible == visible then
        return
    end

    self.scope_visible = visible
    if visible then
        self:SetElementVisible(widget, "scopeOverlay", true)
        self:SetElementAlpha(widget, "scopeOverlay", 1.0)
        self:SetElementAlpha(widget, "crosshairImage", 0.0)
        self:SetElementVisible(widget, "crosshairImage", false)
    else
        self.scope_distance_last_valid_meters = nil
        self.scope_distance_last_valid_time = -1000.0
        self:SetElementAlpha(widget, "scopeOverlay", 0.0)
        self:SetElementVisible(widget, "scopeOverlay", false)
        self:SetElementVisible(widget, "crosshairImage", true)
        self:SetElementAlpha(widget, "crosshairImage", 1.0)
    end
end

function UIManager:SetBreathBarRatio(widget, ratio)
    if widget == nil then
        return
    end

    ratio = clamp01(ratio)
    local width = math.floor(self.breath_bar_width * ratio + 0.5)
    if width ~= self.breath_last_width then
        self.breath_last_width = width
        self:SetElementStyle(widget, "breathBarFill", "width", string.format("%dpx", width))
    end
    call_widget(widget, "SetElementValue", "breathProgress", string.format("%.3f", ratio))
end

function UIManager:SetBreathWarning(widget, warning, dt)
    if widget == nil then
        return
    end

    local fill_color = "rgba(255, 255, 255, 255)"
    local track_background_color = "rgba(12, 18, 26, 220)"
    local track_color = "rgba(236, 242, 255, 175)"
    if warning then
        self.breath_warning_time = (self.breath_warning_time or 0.0) + (dt or 0.0)
        local pulse = 0.5 + 0.5 * math.sin(self.breath_warning_time * 13.0)
        local green = math.floor(34 + 92 * (1.0 - pulse))
        local blue = math.floor(30 + 58 * (1.0 - pulse))
        local track_alpha = math.floor(90 + 120 * pulse)
        fill_color = string.format("rgba(255, %d, %d, 255)", green, blue)
        track_background_color = string.format("rgba(120, %d, %d, %d)", math.floor(green * 0.35), math.floor(blue * 0.35), track_alpha)
        track_color = string.format("rgba(255, %d, %d, 230)", math.floor(green * 0.85), math.floor(blue * 0.85))
    else
        self.breath_warning_time = 0.0
    end

    local style_key = fill_color .. "|" .. track_background_color .. "|" .. track_color
    if self.breath_warning_style_key == style_key then
        return
    end

    self.breath_warning_style_key = style_key
    self:SetElementStyle(widget, "breathBarFill", "background-color", fill_color)
    self:SetElementStyle(widget, "breathBarTrack", "background-color", track_background_color)
    self:SetElementStyle(widget, "breathBarTrack", "border-color", track_color)
end

function UIManager:SetBreathHUDVisible(visible)
    local widget = self:GetActiveHUDWidget()
    if widget == nil then
        self.breath_visible = visible
        return
    end

    if visible then
        self.breath_visible = true
        self.breath_fade_out_time_remaining = 0.0
        self:SetElementVisible(widget, "breathPanel", true)
        self:SetBreathGroupAlpha(widget, 1.0)
    else
        if self.breath_visible then
            self.breath_visible = false
            self.breath_fade_out_time_remaining = self.breath_fade_out_duration
            return
        end

        self.breath_visible = false
        if self.breath_fade_out_time_remaining <= 0.0 then
            self:SetBreathWarning(widget, false, 0.0)
            self:SetBreathGroupAlpha(widget, 0.0)
            self:SetElementVisible(widget, "breathPanel", false)
        end
    end
end

function UIManager:UpdateBreathFade(dt)
    if self.breath_fade_out_time_remaining <= 0.0 then
        return
    end

    self.breath_fade_out_time_remaining = self.breath_fade_out_time_remaining - (dt or 0.0)
    local widget = self:GetActiveHUDWidget()
    if self.breath_fade_out_time_remaining <= 0.0 then
        self.breath_fade_out_time_remaining = 0.0
        if widget ~= nil and not self.breath_visible then
            self:SetBreathWarning(widget, false, 0.0)
            self:SetBreathGroupAlpha(widget, 0.0)
            self:SetElementVisible(widget, "breathPanel", false)
        end
        return
    end

    if widget ~= nil and not self.breath_visible then
        self:SetBreathGroupAlpha(widget, self.breath_fade_out_time_remaining / self.breath_fade_out_duration)
    end
end

function UIManager:UpdateBreathSFX(active, recovering, ratio, dt)
    if self.cutscene_active == true then
        self:ResetBreathSFXState()
        return
    end

    active = active == true
    recovering = recovering == true
    ratio = clamp01(ratio or 0.0)
    dt = math.max(0.0, tonumber(dt) or 0.0)

    if active then
        if self.breath_sfx_active_prev ~= true then
            self.breath_sfx_active_time = 0.0
            self:PlayBreathSFX(BREATH_IN_SFX, BREATH_SFX_VOLUME)
        else
            self.breath_sfx_active_time = (self.breath_sfx_active_time or 0.0) + dt
        end

        if self.breath_sfx_active_time >= BREATH_HEARTBEAT_DELAY then
            self:StartBreathHeartbeat()
        end
    elseif self.breath_sfx_active_prev == true then
        self:StopBreathHeartbeat()
        self.breath_sfx_active_time = 0.0
        if recovering or ratio <= 0.001 then
            self:PlayBreathSFX(BREATH_RECOVER_SFX, BREATH_SFX_VOLUME)
        else
            self:PlayBreathSFX(BREATH_OUT_SFX, BREATH_SFX_VOLUME)
        end
    else
        self.breath_sfx_active_time = 0.0
        self:StopBreathHeartbeat()
    end

    if recovering and self.breath_sfx_recovering_prev ~= true and self.breath_sfx_active_prev ~= true then
        self:PlayBreathSFX(BREATH_RECOVER_SFX, BREATH_SFX_VOLUME)
    end

    self.breath_sfx_active_prev = active
    self.breath_sfx_recovering_prev = recovering
end

function UIManager:UpdateBreathHUD(dt)
    local widget = self:GetActiveHUDWidget()
    if widget == nil then
        return
    end

    local pawn = self:GetSniperPawn()
    local requested = false
    local ratio = 0.0
    local warning = false
    if pawn ~= nil then
        local active = read_bool_method(pawn, { "IsHoldBreathActive" })
        local scoped = read_bool_method(pawn, { "IsScoped" })
        local held = read_bool_method(pawn, { "IsHoldBreathInputHeld" })
        if pawn.IsHoldBreathInputHeld == nil then
            held = self:IsRawHoldBreathRequested()
        end
        requested = active or (scoped and held)

        ratio = self:GetHoldBreathGaugeRatio(pawn)

        local recovering = read_bool_method(pawn, { "IsHoldBreathRecovering", "IsHoldBreathInForcedRecovery" })
        local release_required = read_bool_method(pawn, { "IsHoldBreathReleaseRequired", "IsHoldBreathReleasePending" })
        local exhausted = ratio <= 0.001
        warning = (requested and exhausted) or recovering or release_required
        if scoped and held and not active and ratio < 0.999 then
            warning = true
        end
        self:UpdateBreathSFX(active, recovering or (exhausted and not active and release_required), ratio, dt)
    elseif self:IsRawHoldBreathRequested() then
        requested = true
        ratio = 1.0
        self:UpdateBreathSFX(false, false, ratio, dt)
        if not self.breath_missing_pawn_warned then
            self.breath_missing_pawn_warned = true
            log("SniperPawn binding unavailable; showing fallback hold-breath HUD")
        end
    else
        self:UpdateBreathSFX(false, false, 0.0, dt)
    end

    if not requested then
        if self.breath_visible and self.breath_hide_time_remaining > 0.0 then
            self.breath_hide_time_remaining = self.breath_hide_time_remaining - (dt or 0.0)
            if self.breath_hide_time_remaining > 0.0 then
                self:SetBreathHUDVisible(true)
                self:SetBreathBarRatio(widget, ratio)
                self:SetBreathWarning(widget, warning, dt)
                self:UpdateBreathFade(dt)
                return
            end
        end

        self.breath_hide_time_remaining = 0.0
        self.breath_last_width = -1.0
        self:SetBreathWarning(widget, false, 0.0)
        self:SetBreathHUDVisible(false)
        self:UpdateBreathFade(dt)
        return
    end

    self.breath_hide_time_remaining = self.breath_hide_delay
    self:SetBreathHUDVisible(true)
    self:SetBreathBarRatio(widget, ratio)
    self:SetBreathWarning(widget, warning, dt)
end

function UIManager:GetHeadingSource()
    local pawn = self:GetSniperPawn()
    if pawn ~= nil then
        return pawn
    end

    if World ~= nil and World.FindActorByName ~= nil then
        local player = World.FindActorByName("ScopeTest_Player")
        if player ~= nil then
            return player
        end
    end

    if self.general ~= nil and self.general.context ~= nil then
        return self.general.context.actor
    end
    return obj
end

function UIManager:GetHeadingDegrees()
    local source = self:GetHeadingSource()
    if source == nil or source.Forward == nil then
        return 0.0
    end

    local forward = source.Forward
    local x = forward.X or forward.x or 1.0
    local y = forward.Y or forward.y or 0.0
    if math.abs(x) < 0.0001 and math.abs(y) < 0.0001 then
        return 0.0
    end

    return normalize_degrees(atan2_degrees(y, x))
end

function UIManager:UpdateCompass(dt)
    local widget = self:GetActiveHUDWidget()
    if widget == nil then
        return
    end

    self.smoothed_heading_degrees = smooth_heading(
        self.smoothed_heading_degrees,
        self:GetHeadingDegrees(),
        dt,
        self.compass_smooth_speed)

    local frame = math.floor(normalize_degrees(self.smoothed_heading_degrees) + 0.5) % self.compass_frame_count
    if frame ~= self.compass_last_frame then
        self.compass_last_frame = frame
        self:SetElementImage(widget, "compassImage", string.format("Image/Hor-Compass/Window/Compass_Window_%03d.png", frame))
    end
end

function UIManager:PollInGameActions(widget)
    if widget == nil or widget.PollActionEvents == nil then
        return
    end

    local ok, events = pcall(function()
        return widget:PollActionEvents()
    end)
    if not ok or events == nil then
        log("InGame HUD action polling failed")
        return
    end

    local hover_played = false
    for _, action in ipairs(events) do
        if action == "PauseResume" then
            self:PlayUISFX(MAIN_BUTTON_CLICK_SFX, 1.0)
            if self.general ~= nil then
                self.general:Publish("ingame.pause_resume_requested", { reason = "ui" })
            end
        elseif action == "PauseGoMain" then
            self:PlayUISFX(MAIN_BUTTON_CLICK_SFX, 1.0)
            if self.general ~= nil then
                self.general:Publish("ingame.pause_main_requested", { reason = "ui" })
            end
        elseif action == "PauseOpenSettings" then
            self:PlayUISFX(MAIN_BUTTON_CLICK_SFX, 0.9)
            self:SetPausePanel("Settings")
        elseif action == "PauseOpenControls" then
            self:PlayUISFX(MAIN_BUTTON_CLICK_SFX, 0.9)
            self:SetPausePanel("Controls")
        elseif action == "PauseBackMenu" then
            self:PlayUISFX(MAIN_BUTTON_CLICK_SFX, 0.8)
            self:SetPausePanel("Menu")
        elseif action == "BgmDown" then
            self:AdjustSetting("bgm_volume", -0.1)
        elseif action == "BgmUp" then
            self:AdjustSetting("bgm_volume", 0.1)
        elseif action == "SfxDown" then
            self:AdjustSetting("sfx_volume", -0.1)
        elseif action == "SfxUp" then
            self:AdjustSetting("sfx_volume", 0.1)
        elseif action == "ToggleZoomMode" then
            self:ToggleZoomMode()
        elseif action == "MouseDown" then
            self:AdjustSetting("mouse_sensitivity", -0.1)
        elseif action == "MouseUp" then
            self:AdjustSetting("mouse_sensitivity", 0.1)
        elseif action == "GamepadDown" then
            self:AdjustSetting("gamepad_sensitivity", -0.1)
        elseif action == "GamepadUp" then
            self:AdjustSetting("gamepad_sensitivity", 0.1)
        elseif action == "MainButtonHover" and not hover_played then
            hover_played = true
            self:PlayUISFX(MAIN_BUTTON_HOVER_SFX, 0.8)
        end
    end
end

function UIManager:TickInGameHUD(dt)
    local widget = self:GetActiveHUDWidget()
    self:PollInGameActions(widget)
    self:ApplySniperInputSettings(false)
    self:TickCutSceneLetterbox(widget, dt)
    if self.cutscene_active then
        if widget ~= nil then
            call_widget(widget, "SetText", "cutsceneSkipPrompt", get_confirm_prompt_text("Skip", "Press Space to Skip"))
            self:SetInGameHUDSuppressed(widget, true)
        end
        return
    end

    if self.radio_hud_suppressed then
        if widget ~= nil then
            self:SetInGameHUDSuppressed(widget, true)
        end
        return
    end

    if self.pause_visible then
        return
    end

    self:UpdateCompass(dt)
    self:SetScopeHUDVisible(self:GetScopeVisibleFromInputOrPawn())
    self:UpdateScopeTelemetryHUD(false)
    self:UpdateBreathHUD(dt)
    self:UpdateWeaponHUD(false)
    self:UpdateCombatAgentHUD(false)
    self:FlushPendingHitNotification()
    self:TickHitNotification(dt)
end

return UIManager
